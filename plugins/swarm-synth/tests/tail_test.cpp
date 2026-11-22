// Long decaying notes whose fundamental fades before the harmonics: counts notes that leave their pitch.
//   g++ -O2 -std=c++17 tests/tail_test.cpp -o tail_test && ./tail_test
#include "../../../shared/guitar-engine/Engine.hpp"
#include <random>
using namespace swsyn;
int main(){ const double sr=48000; std::mt19937 rng(5); std::uniform_real_distribution<double> u(-1,1);
 for (int mode=0; mode<2; ++mode) for (int env=0; env<2; ++env) { int bad=0, notes=0; double soundMs=0;
  for (double f : {82.41,110.0,146.83,196.0,246.94,329.63,440.0}) for (int var=0; var<3; ++var) {
   const int N=int(sr*3); std::vector<float> in(N), o(N);
   for (int i=0;i<N;++i){ double t=i/sr; double s=0;
     // the fundamental fades faster than the 2nd / 3rd harmonic; hum and noise underneath
     s += 1.0*std::exp(-t*(var==0?3.0:5.0))*std::sin(2*M_PI*f*t);
     s += 0.7*std::exp(-t*(var==2?1.2:2.0))*std::sin(2*M_PI*2.003*f*t+1);
     s += 0.5*std::exp(-t*2.5)*std::sin(2*M_PI*3.01*f*t+2);
     s += 0.3*std::exp(-t*6)*std::sin(2*M_PI*4.02*f*t);
     in[i]=float(0.3*std::min(1.0,t/0.0005)*s*(t>0.02?1:1) + 0.002*u(rng) + 0.003*std::sin(2*M_PI*50*t) + 0.002*std::sin(2*M_PI*150*t)); }
   float p[kInputCount]; presetDefaults(p); setVoices(p,1); p[gMode]=mode; p[gFltOn]=0; p[gEnvMode]=env; float outp[kOutputCount];
   Engine e; e.setSampleRate(sr); int off=0, snd=0;
   for (int ms=0; ms<3000; ++ms){ e.process(&in[ms*48],&in[ms*48],&o[ms*48],&o[ms*48],48,p,outp); double got=outp[0]; if (got<=0 || ms<40) continue; ++snd; if (std::fabs(12*std::log2(got/f))>0.7) ++off; }
   ++notes; soundMs+=snd; if (off>5) { ++bad; printf("  mode %d env %d note %.0f var %d: off pitch %d ms of %d\n",mode,env,f,var,off,snd);} }
  printf("mode %d, %s: %d of %d decaying notes went off pitch; average sounding time %.0f ms\n",mode,env?"ADSR":"follow",bad,notes,soundMs/notes); } }
