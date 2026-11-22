// Minimal VST3 load check: loads the module, queries the factory and lists its classes.
#include <windows.h>
#include <cstdio>
#include <cstdint>
struct PFactoryInfo { char vendor[64]; char url[256]; char email[128]; int32_t flags; };
struct PClassInfo { char cid[16]; int32_t cardinality; char category[32]; char name[64]; };
struct IPluginFactory;
struct Vtbl {
    int32_t (__stdcall *queryInterface)(IPluginFactory*, const char*, void**);
    uint32_t (__stdcall *addRef)(IPluginFactory*);
    uint32_t (__stdcall *release)(IPluginFactory*);
    int32_t (__stdcall *getFactoryInfo)(IPluginFactory*, PFactoryInfo*);
    int32_t (__stdcall *countClasses)(IPluginFactory*);
    int32_t (__stdcall *getClassInfo)(IPluginFactory*, int32_t, PClassInfo*);
};
struct IPluginFactory { Vtbl* v; };
int main(int argc, char** argv) {
    HMODULE m = LoadLibraryA(argv[1]);
    if (!m) { printf("LOAD FAILED %lu\n", GetLastError()); return 1; }
    typedef bool (*InitFn)(); if (InitFn init = (InitFn)GetProcAddress(m, "InitDll")) init();
    typedef IPluginFactory* (__stdcall *GetFactory)();
    GetFactory gf = (GetFactory)GetProcAddress(m, "GetPluginFactory");
    if (!gf) { printf("NO FACTORY EXPORT\n"); return 2; }
    IPluginFactory* f = gf();
    PFactoryInfo fi; f->v->getFactoryInfo(f, &fi);
    int n = f->v->countClasses(f);
    printf("vendor='%s' classes=%d\n", fi.vendor, n);
    for (int i = 0; i < n; ++i) { PClassInfo ci; f->v->getClassInfo(f, i, &ci); printf("  [%d] %s : %s\n", i, ci.category, ci.name); }
    return n > 0 ? 0 : 3;
}
