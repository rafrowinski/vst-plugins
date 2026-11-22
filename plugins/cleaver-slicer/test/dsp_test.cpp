#include "SlicerEngine.hpp"
#include <cstdio>
#include <vector>
using namespace cleaver;
static void wav(const char* fn, const std::vector<float>& l, const std::vector<float>& r, int sr){
    FILE* f=fopen(fn,"wb"); if(!f){printf("     (could not write %s)\n",fn); return;} uint32_t n=l.size(); uint32_t ds=n*4, rs=36+ds; uint16_t ch=2,bps=16,fmt=1,ba=4; uint32_t br=sr*4,sc=16;
    fwrite("RIFF",1,4,f);fwrite(&rs,4,1,f);fwrite("WAVEfmt ",1,8,f);fwrite(&sc,4,1,f);fwrite(&fmt,2,1,f);fwrite(&ch,2,1,f);fwrite(&sr,4,1,f);fwrite(&br,4,1,f);fwrite(&ba,2,1,f);fwrite(&bps,2,1,f);fwrite("data",1,4,f);fwrite(&ds,4,1,f);
    for(uint32_t i=0;i<n;++i){ int16_t s[2]={(int16_t)(std::fmax(-1.f,std::fmin(1.f,l[i]))*32767),(int16_t)(std::fmax(-1.f,std::fmin(1.f,r[i]))*32767)}; fwrite(s,2,2,f);} fclose(f);
}
int main(){
    int fails=0;
    for (int sr : {44100, 48000, 96000, 192000}) for (int pi=0; pi<kNumPresets; ++pi) for (int locked=0; locked<2; ++locked) {
        Engine e; e.setSampleRate(sr);
        const Preset& pr=kPresets[pi]; Pattern p; buildPresetPattern(pr,p); e.setPattern(p);
        // round trip of the text format
        Pattern q; if(!decodePattern(encodePattern(p).c_str(),q) || encodePattern(q)!=encodePattern(p)){printf("FAIL codec %s\n",pr.name);++fails;}
        e.setParam(pRate,pr.rate);e.setParam(pSteps,pr.steps);e.setParam(pAttack,pr.attack);e.setParam(pDuty,pr.duty);e.setParam(pRelease,pr.release);e.setParam(pSwing,pr.swing);e.setParam(pStereo,pr.stereo);e.setParam(pFiltMode,pr.filtMode);e.setParam(pReso,pr.reso);
        e.setParam(pSync, locked); e.setParam(pBpm,120);
        const int N=sr*4, B=256; std::vector<float> il(N),ir(N),ol(N),orr(N);
        for(int i=0;i<N;++i){ double t=(double)i/sr; float s=0.4f*std::sin(6.2831853*110*t)+0.25f*std::sin(6.2831853*220.5*t)+0.15f*std::sin(6.2831853*331*t); il[i]=ir[i]=s; }
        HostTime ht; ht.valid=locked; ht.playing=locked; ht.bpm=120;
        for(int o=0;o<N;o+=B){ int n=std::min(B,N-o); ht.ppq=(double)o/sr*2.0; e.process(&il[o],&ir[o],&ol[o],&orr[o],n,ht); }
        float peak=0, maxJump=0; double eOut=0,eIn=0; bool bad=false; float minEnv=1e9; 
        for(int i=0;i<N;++i){ if(!std::isfinite(ol[i])||!std::isfinite(orr[i])) bad=true; peak=std::fmax(peak,std::fmax(std::fabs(ol[i]),std::fabs(orr[i]))); if(i){maxJump=std::fmax(maxJump,std::fabs(ol[i]-ol[i-1])*(sr/48000.f));} eOut+=ol[i]*ol[i]+orr[i]*orr[i]; eIn+=2*il[i]*il[i]; }
        // quietest 5 ms window (the gate should actually close for patterns with rests)
        int w=sr/200; for(int o=sr; o+w<N; o+=w){ double s=0; for(int i=o;i<o+w;++i) s+=ol[i]*ol[i]+orr[i]*orr[i]; minEnv=std::fmin(minEnv,(float)std::sqrt(s/w)); }
        bool ok = !bad && peak<3.0f && eOut>0.001*eIn;
        if(!ok){++fails;}
        if(sr==48000 || !ok) printf("%s sr=%d lock=%d %-18s peak=%.2f maxJump=%.3f minRms=%.4f energy=%.2f\n", ok?"ok  ":"FAIL",sr,locked,pr.name,peak,maxJump,minEnv,eOut/eIn);
        if(sr==48000 && locked==1 && (pi==2||pi==19||pi==25)){ char fn[64]; snprintf(fn,64,"cleaver_p%d.wav",pi); wav(fn,ol,orr,sr);}        
    }
    // retrigger test: silence then burst, pattern must restart at step 0
    { Engine e; e.setSampleRate(48000); Pattern p; buildPresetPattern(kPresets[0],p); e.setPattern(p); e.setParam(pSync,0); e.setParam(pRetrig,1);
      std::vector<float> z(480,0.f), b(480), o1(480), o2(480); for(int i=0;i<480;++i) b[i]=0.5f*std::sin(i*0.05f); HostTime ht;
      for(int k=0;k<137;++k) e.process(z.data(),z.data(),o1.data(),o2.data(),480,ht);
      float before=e.getParam(oStep); e.process(b.data(),b.data(),o1.data(),o2.data(),480,ht); float after=e.getParam(oStep);
      printf("retrigger: step before=%g after=%g %s\n",before,after, after==0?"ok":"FAIL"); if(after!=0)++fails; }
    printf("fails=%d\n",fails); return fails;
}
