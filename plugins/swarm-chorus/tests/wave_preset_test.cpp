#include "../src/PresetIO.hpp"
using namespace roj;
static void f(const char* w){ float t[kWaveSize]; std::string e; bool ok=compileWave(w,t,&e); if(ok){ float lo=9,hi=-9; bool fin=true; for(float v:t){lo=std::min(lo,v);hi=std::max(hi,v); if(!std::isfinite(v))fin=false;} printf("OK   %-44s t[0]=%.3f t[64]=%.3f t[128]=%.3f t[192]=%.3f range %.2f..%.2f%s\n",w,t[0],t[64],t[128],t[192],lo,hi,fin?"":" NONFINITE"); } else printf("ERR  %-44s -> %s\n",w,e.c_str()); }
int main(){
  for(const char* w: {"f:sin(2*pi*x)","f:x","f:-x^2","f:2^3^2*x","f:1 - exp(-6*x)","f:floor(x*5)","f:tri(x)^2","f:max(sin(2*pi*x), 0)","f:clamp(4*x, 1, 2)","f:1/x","f:ln(x)","f:sqrt(x-0.5)","f:3","f:2e*x","f:1.5e2*x","f:SIN(2*PI*X)","f:mix(0,1,x)*sqr(2*x)",
       "f:sin(","f:foo(x)","f:x +","f:x y","f:min(x)","f:","f:((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((x))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))",
       "n:0,0;0.5,1","l:0,0;0.5,1","l:0,0;0.2,1;0.5,1;0.7,0","n:0.3,0.2","n:","n:abc","q:1,2","l:0.5,1;0,0;0.25,2;0.75,-1"}) f(w);
  // CSV round trip with wave text + legacy cw columns
  std::vector<UserPreset> a; UserPreset p; p.name="Wave A"; presetDefaults(p.v); p.wave="f:sin(2*pi*x) + 0.4*sin(6*pi*x)"; a.push_back(p);
  p.name="Wave B"; p.wave="l:0,0;0.2,1;0.5,1;0.7,0"; a.push_back(p); p.name="Bad wave"; p.wave="f:sin("; a.push_back(p);
  writePresets("/tmp/w.csv",a); std::vector<UserPreset> in; readPresets("/tmp/w.csv",in); for(auto&q:in) printf("  [%s] wave=[%s]\n",q.name.c_str(),q.wave.c_str());
  Library lib; ImportReport r; for(auto&q:in) lib.addImported(q,r); UserPreset d=in[0]; d.wave="n:0,0;0.5,1"; lib.addImported(d,r); lib.addImported(in[0],r);
  printf("added=%d renamed=%d skipped=%d:",r.added,r.renamed,r.skipped); for(auto&u:lib.user) printf(" [%s]",u.name.c_str()); puts("");
  FILE*fp=fopen("/tmp/old.csv","w"); fputs("preset;mix;cw1;cw2;cw3;cw4;cw5;cw6;cw7;cw8;cw9;cw10;cw11;cw12;cw13;cw14;cw15;cw16\r\nOld;55;0;0,5;1;1;1;1;0,5;0;0;0;0;0;0;0;0;0\r\n",fp); fclose(fp);
  std::vector<UserPreset> o; readPresets("/tmp/old.csv",o); printf("legacy: [%s] mix=%g wave=%s\n",o[0].name.c_str(),o[0].v[gMix],o[0].wave.c_str());
}
