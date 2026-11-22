// Counts wrong octaves and pitch jumps on harsh plucked notes (weak fundamental, ringing previous note, hum).
//   g++ -O2 -std=c++17 tests/jump_test.cpp -o jump_test && ./jump_test 0 1 v     (mode, window, verbose)
#include "../../../shared/guitar-engine/Engine.hpp"
#include <random>
using namespace swsyn;
static std::mt19937 rng(3);
// harsher string model: harmonic levels vary per note (bridge pickup), fundamental can be weak, beating partials
static void pluck(std::vector<float>& out, int at, int len, double f, double sr, double amp, const double* ha, double decayMul)
{
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    for (int i = 0; i < len && at + i < int(out.size()); ++i)
    {
        const double t = i / sr; double s = 0.0;
        for (int h = 1; h <= 10; ++h) { const double fh = f * h * std::sqrt(1.0 + 0.0003 * h * h); if (fh > sr * 0.45) break;
            const double a = ha[h-1] * std::exp(-t * decayMul * (0.8 + 0.5 * h)) * (1.0 + 0.3*std::sin(2*M_PI*(1.5+h)*t)); s += a * std::sin(2 * M_PI * fh * t + 1.3 * h * h); }
        s += 0.8 * u(rng) * std::exp(-t / 0.003);
        out[at + i] += float(amp * 0.25 * std::min(1.0, t / 0.0005) * s);
    }
}
int main(int argc, char** argv){
  const double sr=48000; int mode = argc>1?atoi(argv[1]):0; float win = argc>2?atof(argv[2]):1.f; bool verbose = argc>3;
  static const double prof[4][10] = {
    {1,0.8,0.5,0.4,0.3,0.2,0.15,0.1,0.08,0.05},
    {0.5,1.5,1.0,0.6,0.5,0.3,0.2,0.2,0.1,0.1},     // strong 2nd/3rd
    {0.3,1.0,1.6,0.8,0.6,0.5,0.3,0.2,0.2,0.1},     // weak fundamental
    {1,0.3,0.9,0.2,0.5,0.1,0.3,0.1,0.1,0.05} };    // odd harmonics
  static const double notes[] = {82.41,98,110,123.47,146.83,164.81,196,220,246.94,293.66,329.63,392,440,523.25,659.25,783.99};
  int total=0, wrongStart=0, jumps=0, missing=0; 
  for (int pr=0; pr<4; ++pr) for (int ring=0; ring<2; ++ring) {
    const int noteLen=int(sr*0.35); const int nn=16; const int N=noteLen*nn+4800;
    std::vector<float> in(N,0.f), o(N);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    for (int i=0;i<N;++i) in[i] = float(0.0015*u(rng) + 0.002*std::sin(2*M_PI*50*i/sr)+0.001*std::sin(2*M_PI*100*i/sr));
    std::vector<int> order; for(int k=0;k<nn;++k) order.push_back((k*7)%nn);
    for (int k=0;k<nn;++k) pluck(in, 2400+k*noteLen, ring? noteLen*2 : noteLen-int(sr*0.03), notes[order[k]], sr, 0.7, prof[pr], ring?3.0:1.0);
    float p[kInputCount]; presetDefaults(p); setVoices(p,1); p[gMode]=mode; p[gFltOn]=0; p[gTrack]=win; float outp[kOutputCount];
    Engine e; e.setSampleRate(sr);
    for (int k=0;k<nn;++k) {
      double f = notes[order[k]]; int start=2400+k*noteLen; bool started=false; int badms=0, state=0; double first=0; int nj=0; double prev=0;
      for (int ms=0; ms<350; ++ms) { int oo=start+ms*48 - 2400 + 2400; if (ms==0 && k==0) e.process(&in[0],&in[0],&o[0],&o[0],2400,p,outp);
        e.process(&in[oo],&in[oo],&o[oo],&o[oo],48,p,outp); double got=outp[0];
        if (ms<3 || ms>300) continue;
        if (got<=0) continue;
        double st = 12*std::log2(got/f);
        if (mode==1 && ms<40) continue;
        if (!started && ms>=(mode?40:3)) { /* first sounding pitch of this note */ if (std::fabs(st)<0.7 || ms>100) { started=true; first=st; } else if (ms>3) { /* still old note? ignore until pending resolved */ } }
        if (started) { if (std::fabs(st)>0.7) ++badms; if (prev>0 && std::fabs(12*std::log2(got/prev))>5) ++nj; }
        prev=got;
      }
      ++total; if(!started) ++missing; else { if (std::fabs(first)>0.7) { ++wrongStart; if(verbose) printf("  START prof %d ring %d note %.0f: first %+.1f st\n",pr,ring,f,first);}  if (badms>3) { ++jumps; if(verbose) printf("  prof %d ring %d note %.0f: %d ms off pitch, %d jumps\n",pr,ring,f,badms,nj);} }
    }
  }
  printf("mode %d window %.1f: %d notes, never found %d, wrong at start %d, off-pitch >3ms later %d\n",mode,win,total,missing,wrongStart,jumps);
}
