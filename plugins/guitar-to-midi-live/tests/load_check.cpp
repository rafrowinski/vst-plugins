// Loads the built plugins the way a host would and prints what they report (Windows / Wine):
//   x86_64-w64-mingw32-g++-posix -static tests/load_check.cpp -o load_check.exe
//   wine load_check.exe "release/Guitar to MIDI Live.vst3" "release/Guitar to MIDI Live.dll" "release/Guitar to MIDI Live.clap"
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "travesty/base.h"
#include "travesty/component.h"

// ---- VST3: factory and classes
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

static int vst3(const char* path)
{
    HMODULE m = LoadLibraryA(path);
    if (!m) { printf("VST3 LOAD FAILED %lu\n", GetLastError()); return 1; }
    typedef bool (*InitFn)(); if (InitFn init = (InitFn)GetProcAddress(m, "InitDll")) init();
    typedef IPluginFactory* (__stdcall *GetFactory)();
    GetFactory gf = (GetFactory)GetProcAddress(m, "GetPluginFactory");
    if (!gf) { printf("VST3: no factory export\n"); return 2; }
    IPluginFactory* f = gf();
    PFactoryInfo fi; f->v->getFactoryInfo(f, &fi);
    const int n = f->v->countClasses(f);
    printf("VST3 vendor='%s' classes=%d\n", fi.vendor, n);
    int events = -1;
    for (int i = 0; i < n; ++i)
    {
        PClassInfo ci; f->v->getClassInfo(f, i, &ci);
        printf("  [%d] %s : %s\n", i, ci.category, ci.name);
        if (std::strcmp(ci.category, "Audio Module Class") != 0) continue;
        // create the processor and ask for its buses (vtable: unknown x3, plugin base x2, component x9)
        typedef int32_t (__stdcall *CreateFn)(IPluginFactory*, const char*, const char*, void**);
        CreateFn create = ((CreateFn*)f->v)[6];
        void** inst = nullptr;
        if (create(f, ci.cid, (const char*)v3_component_iid, (void**)&inst) != 0 || !inst) { printf("  could not create the component\n"); continue; }
        void** vt = *(void***)inst;
        typedef int32_t (__stdcall *InitFn3)(void*, void*);
        typedef int32_t (__stdcall *BusCountFn)(void*, int32_t, int32_t);
        ((InitFn3)vt[3])(inst, nullptr);
        BusCountFn busCount = (BusCountFn)vt[7];
        events = busCount(inst, 1, 1);
        printf("  buses: audio in %d, audio out %d, MIDI/event in %d, MIDI/event out %d\n",
               busCount(inst, 0, 0), busCount(inst, 0, 1), busCount(inst, 1, 0), events);
    }
    return (n > 0 && events > 0) ? 0 : 3;
}

// ---- VST2: entry point, AEffect, "can do" MIDI
struct AEffect;
typedef intptr_t (*HostCb)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
typedef intptr_t (*DispatchFn)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
struct AEffect {
    int32_t magic; DispatchFn dispatcher; void* process; void* setParameter; void* getParameter;
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t res1, res2; int32_t initialDelay, realQualities, offQualities; float ioRatio;
    void* object; void* user; int32_t uniqueID, version; void* processReplacing; void* processDoubleReplacing; char future[56];
};
static intptr_t hostCb(AEffect*, int32_t op, int32_t, intptr_t, void*, float) { return op == 1 ? 2400 : 0; }   // audioMasterVersion

static int vst2(const char* path)
{
    HMODULE m = LoadLibraryA(path);
    if (!m) { printf("VST2 LOAD FAILED %lu\n", GetLastError()); return 1; }
    typedef AEffect* (*Main)(HostCb);
    Main mainFn = (Main)GetProcAddress(m, "VSTPluginMain");
    if (!mainFn) mainFn = (Main)GetProcAddress(m, "main");
    if (!mainFn) { printf("VST2: no entry point\n"); return 2; }
    AEffect* e = mainFn(hostCb);
    if (!e || e->magic != 0x56737450) { printf("VST2: bad AEffect\n"); return 3; }
    e->dispatcher(e, 0, 0, 0, nullptr, 0.f);    // effOpen: the plugin instance is created here
    char name[256] = "", vendor[256] = "";
    e->dispatcher(e, 45, 0, 0, name, 0.f);    // effGetEffectName
    e->dispatcher(e, 47, 0, 0, vendor, 0.f);  // effGetVendorString
    const intptr_t sendMidi = e->dispatcher(e, 51, 0, 0, (void*)"sendVstMidiEvent", 0.f);   // effCanDo
    const intptr_t recvMidi = e->dispatcher(e, 51, 0, 0, (void*)"receiveVstMidiEvent", 0.f);
    printf("VST2 '%s' by '%s': %d parameters, %d in / %d out, editor %s, can send MIDI %s, can receive MIDI %s\n",
           name, vendor, e->numParams, e->numInputs, e->numOutputs, (e->flags & 1) ? "yes" : "no",
           sendMidi == 1 ? "yes" : "no", recvMidi == 1 ? "yes" : "no");
    e->dispatcher(e, 1, 0, 0, nullptr, 0.f);   // effClose
    return sendMidi == 1 ? 0 : 4;
}

// ---- CLAP: entry, factory, descriptor, note ports
struct clap_version { uint32_t major, minor, revision; };
struct clap_plugin_descriptor { clap_version v; const char* id; const char* name; const char* vendor; const char* url; const char* manual_url;
                                const char* support_url; const char* version; const char* description; const char* const* features; };
struct clap_plugin_factory { uint32_t (*get_plugin_count)(const clap_plugin_factory*); const clap_plugin_descriptor* (*get_plugin_descriptor)(const clap_plugin_factory*, uint32_t); void* create_plugin; };
struct clap_plugin_entry { clap_version v; bool (*init)(const char*); void (*deinit)(); const void* (*get_factory)(const char*); };

static int clap(const char* path)
{
    HMODULE m = LoadLibraryA(path);
    if (!m) { printf("CLAP LOAD FAILED %lu\n", GetLastError()); return 1; }
    const clap_plugin_entry* e = (const clap_plugin_entry*)GetProcAddress(m, "clap_entry");
    if (!e || !e->init(path)) { printf("CLAP: no entry\n"); return 2; }
    const clap_plugin_factory* f = (const clap_plugin_factory*)e->get_factory("clap.plugin-factory");
    if (!f) { printf("CLAP: no factory\n"); return 3; }
    const uint32_t n = f->get_plugin_count(f);
    for (uint32_t i = 0; i < n; ++i)
    {
        const clap_plugin_descriptor* d = f->get_plugin_descriptor(f, i);
        printf("CLAP '%s' (%s) version %s, features:", d->name, d->id, d->version);
        for (const char* const* ft = d->features; ft && *ft; ++ft) printf(" %s", *ft);
        printf("\n");
    }
    e->deinit();
    return n > 0 ? 0 : 4;
}

int main(int argc, char** argv)
{
    int bad = 0;
    for (int i = 1; i < argc; ++i)
    {
        const char* p = argv[i];
        const size_t l = strlen(p);
        if (l > 5 && !strcmp(p + l - 5, ".vst3")) bad += vst3(p) != 0;
        else if (l > 5 && !strcmp(p + l - 5, ".clap")) bad += clap(p) != 0;
        else bad += vst2(p) != 0;
    }
    printf(bad ? "%d FAILED\n" : "ALL LOADED\n", bad);
    return bad;
}
