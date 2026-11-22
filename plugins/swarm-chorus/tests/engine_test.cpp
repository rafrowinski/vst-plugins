#include "../src/Engine.hpp"
#include <chrono>
#include <cstdio>
using namespace roj;
int main(){
  const int sr=48000, N=sr*8;
  std::vector<float> in(N), oL(N), oR(N);
  // plucks every 1s
  for(int i=0;i<N;i++){ int k=i%sr; double t=k/(double)sr; double f=110*(1+ (i/sr)%4*0.25);
    in[i]=0.5*exp(-3*t)*(sin(2*M_PI*f*t)+0.5*sin(4*M_PI*f*t)+0.3*sin(6*M_PI*f*t)); }
  float outp[kOutputCount];
  for(int pr=0;pr<kPresetCount;pr++){
    float p[kInputCount]; kPresets[pr].fill(p);
    Engine e; e.setSampleRate(sr);
    int trig=0; float lastT=0; float peak=0; double rms=0; bool bad=false;
    auto t0=std::chrono::steady_clock::now();
    for(int o=0;o<N;o+=64){ e.process(&in[o],&in[o],&oL[o],&oR[o],64,p,120.f,outp); if(outp[kOutTrig-kInputCount]>0.5f&&lastT<0.5f)trig++; lastT=outp[kOutTrig-kInputCount]; }
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
    for(int i=0;i<N;i++){ if(!std::isfinite(oL[i])||!std::isfinite(oR[i]))bad=true; peak=std::max(peak,std::max(fabsf(oL[i]),fabsf(oR[i]))); rms+=oL[i]*oL[i];}
    printf("%-22s peak %.3f rms %.4f cpu %.2f%% trig %d %s\n",kPresets[pr].name,peak,sqrt(rms/N),ms/80.0,trig,bad?"NAN!":"ok");
  }
  // detune check: 1 kHz sine, 1 voice, +20ct, wet only
  { float p[kInputCount]; presetDefaults(p); setVoices(p,1); setAll(p,vDelayMin,10); setAll(p,vDelayMax,10); setAll(p,vDetune,20); p[gMix]=100;
    Engine e; e.setSampleRate(sr); for(int i=0;i<N;i++) in[i]=0.5*sin(2*M_PI*1000.0*i/sr);
    e.process(in.data(),in.data(),oL.data(),oR.data(),N,p,120.f,outp);
    int zc=0; for(int i=sr;i<N-1;i++) if(oL[i]<=0&&oL[i+1]>0) zc++;
    double f=zc/((N-1-sr)/(double)sr); printf("detune +20ct: %.2f Hz (expect %.2f)\n",f,1000*pow(2,20/1200.0)); }
  // unity check: mix 0
  { float p[kInputCount]; presetDefaults(p); p[gMix]=0; Engine e; e.setSampleRate(sr); e.process(in.data(),in.data(),oL.data(),oR.data(),N,p,120.f,outp);
    double d=0; for(int i=sr;i<N;i++) d=std::max(d,(double)fabs(oL[i]-in[i])); printf("dry passthrough err %.2g\n",d); }
  // stereo: left-only input must not leak into right when voices are centred (pan spread 0)
  { float p[kInputCount]; presetDefaults(p); p[gSpPan]=0; p[gMix]=100; Engine e; e.setSampleRate(sr); std::vector<float> z(N,0.f);
    for(int i=0;i<N;i++) in[i]=0.5*sin(2*M_PI*440.0*i/sr); e.process(in.data(),z.data(),oL.data(),oR.data(),N,p,120.f,outp);
    float pl=0,prr=0; for(int i=0;i<N;i++){pl=std::max(pl,fabsf(oL[i]));prr=std::max(prr,fabsf(oR[i]));} printf("stereo: L peak %.3f R peak %.3f\n",pl,prr); }
  // warmth: 6 kHz sine through 100%% warmth should be clearly attenuated
  for(float w: {0.f,100.f}){ float p[kInputCount]; presetDefaults(p); setVoices(p,1); p[gMix]=100; p[gWarmth]=w; Engine e; e.setSampleRate(sr);
    for(int i=0;i<N;i++) in[i]=0.3*sin(2*M_PI*6000.0*i/sr); e.process(in.data(),in.data(),oL.data(),oR.data(),N,p,120.f,outp);
    float pk=0; for(int i=sr;i<N;i++) pk=std::max(pk,fabsf(oL[i])); printf("warmth %.0f: 6k peak %.3f\n",w,pk); }
  // sync: 1/4 at 120 bpm = 2 Hz -> count LFO cycles in 8 s via delay-now crossings
  { float p[kInputCount]; presetDefaults(p); setVoices(p,1); p[gSync]=1; setAll(p,vDiv,8); Engine e; e.setSampleRate(sr); int cyc=0; float prev=0,mid=8.5f;
    for(int o=0;o<N;o+=64){ e.process(&in[o],&in[o],&oL[o],&oR[o],64,p,120.f,outp); if(prev<mid&&outp[0]>=mid)cyc++; prev=outp[0]; } printf("sync 1/4 @120: %d cycles in %ds (expect %d)\n",cyc,N/sr,2*N/sr); }
  // envelope + custom wave from a formula
  { float p[kInputCount]; presetDefaults(p); setAll(p,vShape,shCustom); p[gEnvDepth]=100; p[gEnvWah]=2; setAll(p,vFilter,100); Engine e; e.setSampleRate(sr);
    float tb[kWaveSize]; compileWave("f:sin(2*pi*x)^3",tb); e.setWave(tb);
    for(int i=0;i<N;i++) in[i]=(i<N/2)?0.5*sin(2*M_PI*220.0*i/sr):0; float e1=0,e2=0; bool bad=false;
    for(int o=0;o<N;o+=64){ e.process(&in[o],&in[o],&oL[o],&oR[o],64,p,120.f,outp); if(o==N/4)e1=outp[kOutEnv-kInputCount]; if(o>=N-64)e2=outp[kOutEnv-kInputCount]; }
    for(int i=0;i<N;i++) if(!std::isfinite(oL[i]))bad=true; printf("env playing %.2f, silent %.2f %s\n",e1,e2,bad?"NAN":"ok"); }
  // custom wave drives the delay: square-ish table -> delay must reach both Min and Max
  { float p[kInputCount]; presetDefaults(p); setVoices(p,1); setAll(p,vShape,shCustom); setAll(p,vRate,2); Engine e; e.setSampleRate(sr);
    float tb[kWaveSize]; compileWave("l:0,0;0.2,1;0.5,1;0.7,0",tb); e.setWave(tb); float lo=1e9,hi=0;
    for(int o=0;o<N;o+=64){ e.process(&in[o],&in[o],&oL[o],&oR[o],64,p,120.f,outp); if(o>sr){lo=std::min(lo,outp[0]);hi=std::max(hi,outp[0]);} }
    printf("custom wave delay range %.2f..%.2f ms (expect about 6..11)\n",lo,hi); }
  // extreme: everything max feedback, 6 voices, filters
  { float p[kInputCount]; presetDefaults(p); setVoices(p,6); setAll(p,vFeedback,95); setAll(p,vFilter,100); setAll(p,vReso,12); setAll(p,vDetune,50); setAll(p,vLevel,150);
    Engine e; e.setSampleRate(sr); for(int i=0;i<N;i++) in[i]=(i%sr<sr/2)?0.9*sin(2*M_PI*220.0*i/sr):0; e.process(in.data(),in.data(),oL.data(),oR.data(),N,p,120.f,outp);
    float pk=0; bool bad=false; for(int i=0;i<N;i++){ if(!std::isfinite(oL[i]))bad=true; pk=std::max(pk,fabsf(oL[i]));} printf("extreme peak %.2f %s\n",pk,bad?"NAN":"ok"); }
}
