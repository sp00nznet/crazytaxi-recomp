/*
 * Crazy Taxi - static recompilation host.
 *
 * A 32-bit host on pcrecomp's runtime/native32 (the native bridge, callbacks
 * and machine lock; see its header). Every import goes to the real DLL --
 * Direct3D 8, DirectSound, DirectInput 8, winmm -- so what is
 * here is only what is specific to this game: where the image goes, the
 * command line, the calls that reach outside the game, muting, and the fault
 * report. Taken from the HotD2 host (hotd2-recomp src/runtime/host.c).
 *
 * Linked at /BASE:0x60000000 (CMakeLists.txt) so the guest's
 * image is free when main() maps the image.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native32.h"
#include "recomp_trace.h"

extern const uint32_t ct_entry_va;  /* recomp_dispatch.c */

#define IMAGE_BASE 0x00400000u
#define ARG(n) MEM32(g_esp + 4 + 4 * (n))

static DWORD g_watchdog_s;
static int   g_headless;
static int   g_fullscreen;   /* --fullscreen: no always-active window */

/* --shot SECONDS FILE: frame SECONDS*50 (the game runs at 50), read from the back
 * buffer in Present (hook_Present), as a .bmp. --shot can be given more than
 * once. A frame count is printed every five seconds. */
static volatile LONG g_frames;   /* frames presented; --shot and --key count from the first */
static DWORD g_shot_s[8];
static char  g_shot_file[8][MAX_PATH];
static int   g_shots, g_shot_next;
static volatile LONG g_shot_due;
static DWORD WINAPI shot_thread(LPVOID unused) {
    (void)unused;
    for (int i = 0; i < g_shots; i++) {
        while (g_frames < (LONG)g_shot_s[i] * 50) Sleep(10);
        InterlockedExchange(&g_shot_due, 1);
        while (g_shot_due) Sleep(10);
    }
    return 0;
}

/* Writes a locked X8R8G8B8 surface as a 24-bit .bmp. */
static void write_bmp(const char* path, const uint8_t* px, int pitch, int w, int h) {
    DWORD stride = ((DWORD)w * 3 + 3) & ~3u, size = stride * h;
    BITMAPINFOHEADER bh = { sizeof bh, w, h, 1, 24, BI_RGB };
    BITMAPFILEHEADER fh = { 0x4D42, sizeof fh + sizeof bh + size, 0, 0, sizeof fh + sizeof bh };
    uint8_t* row = calloc(1, stride);
    FILE* f = fopen(path, "wb");
    if (!f || !row) { free(row); if (f) fclose(f); return; }
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&bh, sizeof bh, 1, f);
    for (int y = h - 1; y >= 0; y--) {
        const uint8_t* src = px + (size_t)y * pitch;
        for (int x = 0; x < w; x++) memcpy(row + x * 3, src + x * 4, 3);
        fwrite(row, stride, 1, f);
    }
    fclose(f);
    free(row);
}

/* ---- the process is the host, the module is the guest ----------------------
 * GetModuleHandleA(NULL) and GetModuleFileNameA(NULL) would name the host;
 * the game finds its own folder through them. */
static char g_guest_exe[MAX_PATH], g_guest_cmdline[MAX_PATH + 512];

static void shim_GetModuleHandleA(void) {
    g_eax = ARG(0) ? (uint32_t)(uintptr_t)GetModuleHandleA((LPCSTR)(uintptr_t)ARG(0)) : IMAGE_BASE;
    g_esp += 4 + 1 * 4;
}

static void shim_GetModuleFileNameA(void) {
    uint32_t h = ARG(0), size = ARG(2);
    char* out = (char*)(uintptr_t)ARG(1);
    if (h == 0 || h == IMAGE_BASE) {
        uint32_t n = (uint32_t)strlen(g_guest_exe);
        if (size) {
            uint32_t k = n < size ? n : size - 1;
            memcpy(out, g_guest_exe, k);
            out[k] = 0;
            n = k;
        }
        g_eax = n;
    } else {
        g_eax = GetModuleFileNameA((HMODULE)(uintptr_t)h, out, size);
    }
    g_esp += 4 + 3 * 4;
}

static void shim_GetCommandLineA(void) {
    g_eax = (uint32_t)(uintptr_t)g_guest_cmdline;
    g_esp += 4;
}

/* A shim runs holding the machine, and a window call can wait on another
 * thread answering messages: run it with the machine let go. */
#define UNLOCKED(call) do { uint32_t esp_ = g_esp; mach_leave(); \
    call; DWORD err_ = GetLastError(); mach_enter(); \
    g_esp = esp_; SetLastError(err_); } while (0)

static void shim_MessageBoxA(void) {
    const char* text = ARG(1) ? (const char*)(uintptr_t)ARG(1) : "";
    const char* cap = ARG(2) ? (const char*)(uintptr_t)ARG(2) : "";
    printf("[host] MessageBoxA \"%s\": %s\n", cap, text);
    if (g_headless) {
        uint32_t b = ARG(3) & MB_TYPEMASK;
        g_eax = (b == MB_YESNO || b == MB_YESNOCANCEL) ? IDNO
              : (b == MB_RETRYCANCEL || b == MB_OKCANCEL) ? IDCANCEL
              : b == MB_ABORTRETRYIGNORE ? IDABORT : IDOK;
    } else {
        HWND w = (HWND)(uintptr_t)ARG(0);
        UINT type = ARG(3);
        int r;
        UNLOCKED(r = MessageBoxA(w, text, cap, type));
        g_eax = r;
    }
    g_esp += 4 + 4 * 4;
}

static void shim_OutputDebugStringA(void) {
    if (ARG(0)) printf("[dbg] %s", (const char*)(uintptr_t)ARG(0));
    g_esp += 4 + 1 * 4;
}

/* The game ends itself with ExitProcess, which skips the host's way out. */
static void shim_ExitProcess(void) {
    UINT code = ARG(0);
    printf("[host] ExitProcess(%u) from sub_%08X\n", code, g_cur_func);
    recomp_trace_flush();
    ExitProcess(code);
}

/* ---- the CD ------------------------------------------------------------------
 * The game checks for its CD in a real CD-ROM drive: your disc, or your disc
 * image mounted. The host only finds that drive (CD_DRIVE=E, or the first
 * CD-ROM drive with CRAZY_TAXI_PC.EXE on it) and reads its files from the game folder. */
static char g_game_dir[MAX_PATH];
static char g_cd = 0;

static const char* cd_path(const char* in, char* out, size_t n) {
    if (in && g_cd && (in[0] & ~0x20) == g_cd && in[1] == ':') {
        const char* rest = in + 2;
        while (*rest == '\\' || *rest == '/') rest++;
        _snprintf(out, n - 1, "%s\\%s", g_game_dir, rest);
        out[n - 1] = 0;
        return out;
    }
    return in;
}

static void cd_pick(const char* game_dir) {
    char env[8], root[] = "?:\\", probe[MAX_PATH];
    lstrcpynA(g_game_dir, game_dir, MAX_PATH);
    if (GetEnvironmentVariableA("CD_DRIVE", env, sizeof env)) g_cd = (char)(env[0] & ~0x20);
    for (char c = 'D'; c <= 'Z' && !g_cd; c++) {
        root[0] = c;
        _snprintf(probe, sizeof probe, "%sCRAZY_TAXI_PC.EXE", root);
        if (GetDriveTypeA(root) == DRIVE_CDROM && GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES) g_cd = c;
    }
    if (g_cd) printf("  CD drive: %c: (files read from %s)\n", g_cd, g_game_dir);
    else printf("  no disc found: put the CD in the drive or mount your disc image (or set CD_DRIVE)\n");
}

/* The game's file opens, logged: the milestones a run is read by. */
static void shim_CreateFileA(void) {
    char buf[MAX_PATH * 2];
    const char* name = cd_path((const char*)(uintptr_t)ARG(0), buf, sizeof buf);
    g_eax = (uint32_t)(uintptr_t)CreateFileA(name, ARG(1), ARG(2), (LPSECURITY_ATTRIBUTES)(uintptr_t)ARG(3),
                                             ARG(4), ARG(5), (HANDLE)(uintptr_t)ARG(6));
    printf("[host] open %s -> %s\n", name ? name : "(null)",
           g_eax == (uint32_t)(uintptr_t)INVALID_HANDLE_VALUE ? "missing" : "ok");
    g_esp += 4 + 7 * 4;
}

/* A C++ throw, logged: the object's type and first words (D3DX's carry an
 * HRESULT), then raised for real so the lifted handlers see it. */
static void shim_RaiseException(void) {
    DWORD code = ARG(0), flags = ARG(1), n = ARG(2);
    const ULONG_PTR* a = (const ULONG_PTR*)(uintptr_t)ARG(3);
    if (code == 0xE06D7363u && n >= 3) {
        const uint32_t* obj = (const uint32_t*)a[1];
        const uint32_t* ti = (const uint32_t*)a[2];
        const uint32_t* cta = ti ? (const uint32_t*)(uintptr_t)ti[3] : NULL;
        const uint32_t* ct = cta && cta[0] ? (const uint32_t*)(uintptr_t)cta[1] : NULL;
        const char* name = ct ? (const char*)(uintptr_t)(ct[1] + 8) : "?";
        printf("[throw] %s from sub_%08X: %08X %08X %08X %08X\n", name, g_cur_func,
               obj ? obj[0] : 0, obj ? obj[1] : 0, obj ? obj[2] : 0, obj ? obj[3] : 0);
        if (obj && !IsBadStringPtrA((LPCSTR)obj, 300) && ((const char*)obj)[0] >= ' ')
            printf("[throw]   \"%.300s\"\n", (const char*)obj);
        else if (obj && obj[1] > 0x400000 && obj[1] < 0x10000000 && !IsBadStringPtrA((LPCSTR)(uintptr_t)obj[1], 200))
            printf("[throw]   \"%s\"\n", (const char*)(uintptr_t)obj[1]);
        recomp_dump_trace("throw");
    }
    UNLOCKED(RaiseException(code, flags, n, a));
    g_esp += 4 + 4 * 4;
}

/* Fullscreen flips fail unless the game's window is in front; started from a
 * console it is not, so it is brought forward when shown. */
static void shim_ShowWindow(void) {
    HWND w = (HWND)(uintptr_t)ARG(0);
    int cmd = (int)ARG(1);
    printf("[host] ShowWindow %p %d from sub_%08X\n", (void*)w, cmd, g_cur_func);
    BOOL r;
    /* Windows refuses SetForegroundWindow to a process started in the
     * background; a tapped Alt lifts the lock (the usual workaround). */
    UNLOCKED(r = ShowWindow(w, cmd);
             if (cmd != SW_HIDE && !g_headless && g_fullscreen) {
                 keybd_event(VK_MENU, 0, 0, 0);
                 SetForegroundWindow(w);
                 keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
             });
    g_eax = r;
    g_esp += 4 + 2 * 4;
}

/* The game pauses (draws nothing) while its window is inactive. Windowed, it
 * is told it is always active, as HotD 1's online play does. */
static WNDPROC g_game_proc;
static HWND g_game_wnd;      /* the game's window, when windowed */
static LRESULT CALLBACK host_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ACTIVATEAPP: case WM_NCACTIVATE: w = TRUE; break;
    case WM_ACTIVATE: w = MAKEWPARAM(WA_ACTIVE, HIWORD(w)); break;
    case WM_KILLFOCUS: return 0;
    }
    return CallWindowProcA(g_game_proc, h, m, w, l);
}

static void shim_CreateWindowExA(void) {
    uint32_t a[12];
    for (int i = 0; i < 12; i++) a[i] = ARG(i);
    if (g_headless) a[3] &= ~WS_VISIBLE;
    printf("[host] CreateWindowExA \"%s\" style %08X at %d,%d %dx%d parent %08X\n",
           a[2] ? (const char*)(uintptr_t)a[2] : "", a[3], a[4], a[5], a[6], a[7], a[8]);
    HWND h;
    UNLOCKED(h = CreateWindowExA(a[0], (LPCSTR)(uintptr_t)a[1], (LPCSTR)(uintptr_t)a[2], a[3],
                                 a[4], a[5], a[6], a[7], (HWND)(uintptr_t)a[8],
                                 (HMENU)(uintptr_t)a[9], (HINSTANCE)(uintptr_t)a[10], (LPVOID)(uintptr_t)a[11]));
    DWORD err = h ? 0 : GetLastError();
    if (h && !a[8] && !g_game_proc && !g_fullscreen) {
        /* The game's procedure is guest code: Windows' call to it faults on
         * the non-executable page and native32 runs the lifted body. */
        g_game_proc = (WNDPROC)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)host_wndproc);
        g_game_wnd = h;
        PostMessageA(h, WM_ACTIVATEAPP, TRUE, 0);
    }
    SetLastError(err);
    g_eax = (uint32_t)(uintptr_t)h;
    g_esp += 4 + 12 * 4;
}

/* Before its main loop the game waits in GetMessage until its window is in
 * front (sub_0040A730), and minimizes it meanwhile. Windowed, it always is. */
static void shim_GetForegroundWindow(void) {
    g_eax = (uint32_t)(uintptr_t)(g_game_wnd ? g_game_wnd : GetForegroundWindow());
    g_esp += 4;
}

/* DirectInput checks the instance handle; the game passes its own image base,
 * which Windows never loaded. The host's stands in. */
static void* vt_hook(void** vt, int n, void* fn);

/* The game takes its devices foreground-only and retries Acquire until it
 * works (sub_00421CB0), which never happens while another window is in
 * front: devices are made background, non-exclusive.
 * IDirectInput8::CreateDevice is slot 3, the device's SetCooperativeLevel 13. */
typedef HRESULT (WINAPI *coop_t)(void*, HWND, DWORD);
typedef HRESULT (WINAPI *di_create_dev_t)(void*, const GUID*, void**, void*);
static coop_t g_coop;
static di_create_dev_t g_di_create_dev;
static HRESULT WINAPI hook_SetCooperativeLevel(void* dev, HWND w, DWORD flags) {
    if (!g_fullscreen) flags = 0x8 | 0x2;   /* DISCL_BACKGROUND | DISCL_NONEXCLUSIVE */
    return g_coop(dev, w, flags);
}
/* --key S DIK MS: at S seconds of game time (frame S*50: loading
 * stalls the clock), the keyboard reads key DIK
 * (a DIK_ scan code) held for MS ms of game time, for scripted test runs.
 * The game reads the keyboard with GetDeviceState (slot 9), whose answer
 * the host ORs it into; every device of a DirectInput 8 shares one vtable,
 * so the keyboard is told apart by the device pointer. The game's keys (its
 * table at 0x006524F0): Enter start and pause, A decide, the arrows drive
 * (Up accelerates, Down brakes). */
typedef HRESULT (WINAPI *state_t)(void*, DWORD, void*);
static state_t g_get_state;
static void*   g_kbd;
static struct { DWORD at, ms; int dik; } g_keys[1024];
static int     g_nkeys;
static volatile uint8_t g_held[256];
static void key_set(int dik, int down) {
    g_held[dik] = down ? 0x80 : 0;
    printf("[key] %02X %s\n", dik, down ? "down" : "up");
}
static DWORD WINAPI key_thread(LPVOID unused) {
    (void)unused;
    for (DWORD now = 0; ; now = (DWORD)g_frames * 20) {
        int left = 0;
        for (int i = 0; i < g_nkeys; i++) {
            if (g_keys[i].dik < 0) continue;
            left = 1;
            if (now >= g_keys[i].at + g_keys[i].ms) { key_set(g_keys[i].dik, 0); g_keys[i].dik = -1; }
            else if (now >= g_keys[i].at && !g_held[g_keys[i].dik]) key_set(g_keys[i].dik, 1);
        }
        if (!left) return 0;
        Sleep(5);
    }
}
static HRESULT WINAPI hook_GetDeviceState(void* dev, DWORD cb, void* out) {
    HRESULT hr = g_get_state(dev, cb, out);
    if (dev == g_kbd && out) {
        if (hr != 0) memset(out, 0, cb), hr = 0;
        for (DWORD i = 0; i < cb && i < 256; i++) ((uint8_t*)out)[i] |= g_held[i];
    }
    return hr;
}
static HRESULT WINAPI hook_DI_CreateDevice(void* di, const GUID* g, void** out, void* outer) {
    static const GUID kbd = { 0x6f1d2b61, 0xd5a0, 0x11cf, { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
    HRESULT hr = g_di_create_dev(di, g, out, outer);
    printf("[dinput] CreateDevice %08lX -> %08lX%s\n", g->Data1, hr, !memcmp(g, &kbd, sizeof kbd) ? " (keyboard)" : "");
    if (hr == 0 && *out) {
        void** vt = **(void****)out;
        if (!memcmp(g, &kbd, sizeof kbd)) g_kbd = *out;
        if (vt[13] != (void*)hook_SetCooperativeLevel) g_coop = (coop_t)vt_hook(vt, 13, hook_SetCooperativeLevel);
        if (vt[9] != (void*)hook_GetDeviceState) g_get_state = (state_t)vt_hook(vt, 9, hook_GetDeviceState);
    }
    return hr;
}

static void shim_DirectInput8Create(void) {
    typedef HRESULT (WINAPI *create_t)(HINSTANCE, DWORD, const GUID*, void**, void*);
    static create_t create;
    if (!create) create = (create_t)GetProcAddress(LoadLibraryA("dinput8.dll"), "DirectInput8Create");
    uint32_t ver = ARG(1), iid = ARG(2), out = ARG(3), outer = ARG(4);
    HRESULT hr;
    UNLOCKED(hr = create(GetModuleHandleA(NULL), ver, (const GUID*)(uintptr_t)iid,
                         (void**)(uintptr_t)out, (void*)(uintptr_t)outer));
    if (hr == 0 && MEM32(out)) {
        void** vt = **(void****)(uintptr_t)out;
        if (vt[3] != (void*)hook_DI_CreateDevice) g_di_create_dev = (di_create_dev_t)vt_hook(vt, 3, hook_DI_CreateDevice);
    }
    g_eax = (uint32_t)hr;
    g_esp += 4 + 5 * 4;
}

/* ---- Direct3D 8 ---------------------------------------------------------------
 * The game asks for a fullscreen device. Unless --fullscreen it gets a
 * windowed one, the same size, in the desktop's format: IDirect3D8's
 * CreateDevice (vtable slot 15) is replaced in the real object. */
typedef struct {
    UINT w, h, fmt, count, ms, swap; HWND wnd; BOOL windowed, autods; UINT dsfmt, flags, refresh, interval;
} pp8_t;
typedef HRESULT (WINAPI *create_dev_t)(void*, UINT, UINT, HWND, DWORD, pp8_t*, void**);
typedef HRESULT (WINAPI *mode_t)(void*, UINT, UINT*);
typedef HRESULT (WINAPI *reset_t)(void*, pp8_t*);
static create_dev_t g_create_dev;
static reset_t g_reset;
static void* g_d3d;

/* Fullscreen parameters made windowed, in the desktop's format. */
static void windowed(pp8_t* pp, HWND focus) {
    if (g_shots) pp->flags |= 1;   /* D3DPRESENTFLAG_LOCKABLE_BACKBUFFER, for --shot */
    if (g_fullscreen || pp->windowed) return;
    UINT mode[4];   /* D3DDISPLAYMODE: w, h, refresh, format */
    ((mode_t)(*(void***)g_d3d)[8])(g_d3d, 0, mode);
    pp->windowed = TRUE;
    pp->fmt = mode[3];
    pp->refresh = 0;
    pp->interval = 0;
    pp->swap = 1;   /* D3DSWAPEFFECT_DISCARD */
    HWND w = pp->wnd ? pp->wnd : focus;
    RECT r = { 0, 0, (LONG)pp->w, (LONG)pp->h };
    SetWindowLongA(w, GWL_STYLE, WS_OVERLAPPEDWINDOW | (g_headless ? 0 : WS_VISIBLE));
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(w, HWND_NOTOPMOST, 40, 40, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED);
}

/* Replaces slot n of a real COM vtable; returns what was there. */
static void* vt_hook(void** vt, int n, void* fn) {
    DWORD old;
    void* was = vt[n];
    VirtualProtect(&vt[n], sizeof vt[n], PAGE_READWRITE, &old);
    vt[n] = fn;
    VirtualProtect(&vt[n], sizeof vt[n], old, &old);
    return was;
}

/* Present: counts frames, and takes a due --shot from the back buffer (made
 * lockable in windowed()). IDirect3DDevice8::GetBackBuffer is slot 16;
 * IDirect3DSurface8 GetDesc 8, LockRect 9, UnlockRect 10. */
typedef HRESULT (WINAPI *present_t)(void*, const RECT*, const RECT*, HWND, const void*);
static present_t g_present;
static HRESULT WINAPI hook_Present(void* dev, const RECT* src, const RECT* dst, HWND w, const void* dirty) {
    static DWORD next;
    InterlockedIncrement(&g_frames);
    if ((int)(GetTickCount() - next) >= 0) {
        if (next) printf("[d3d8] %ld frames\n", g_frames);
        next = GetTickCount() + 5000;
    }
    if (g_shot_due) {
        void* bb = NULL;
        typedef HRESULT (WINAPI *getbb_t)(void*, UINT, UINT, void**);
        if (((getbb_t)(*(void***)dev)[16])(dev, 0, 0, &bb) == 0 && bb) {
            void** svt = *(void***)bb;
            UINT desc[8];   /* D3DSURFACE_DESC: format, type, usage, pool, size, ms, w, h */
            struct { INT pitch; void* bits; } lr;
            ((HRESULT (WINAPI*)(void*, UINT*))svt[8])(bb, desc);
            if (((HRESULT (WINAPI*)(void*, void*, const RECT*, DWORD))svt[9])(bb, &lr, NULL, 0x10 /* READONLY */) == 0) {
                write_bmp(g_shot_file[g_shot_next], lr.bits, lr.pitch, desc[6], desc[7]);
                ((HRESULT (WINAPI*)(void*))svt[10])(bb);
                printf("[shot] frame %ld, %ux%u fmt %u -> %s\n", g_frames, desc[6], desc[7], desc[0],
                       g_shot_file[g_shot_next]);
            } else {
                printf("[shot] back buffer not lockable\n");
            }
            ((ULONG (WINAPI*)(void*))svt[2])(bb);
        }
        g_shot_next++;
        InterlockedExchange(&g_shot_due, 0);
    }
    return g_present(dev, src, dst, w, dirty);
}

static HWND g_focus;
static HRESULT WINAPI hook_Reset(void* dev, pp8_t* pp) {
    printf("[d3d8] Reset %ux%u fmt %u %s\n", pp->w, pp->h, pp->fmt, pp->windowed ? "windowed" : "fullscreen");
    windowed(pp, g_focus);
    HRESULT hr = g_reset(dev, pp);
    printf("[d3d8] Reset -> %08lX\n", hr);
    return hr;
}

static HRESULT WINAPI hook_CreateDevice(void* d3d, UINT adapter, UINT type, HWND focus, DWORD flags,
                                        pp8_t* pp, void** out) {
    printf("[d3d8] CreateDevice %ux%u fmt %u %s, swap %u, refresh %u, interval %08X, flags %08X\n",
           pp->w, pp->h, pp->fmt, pp->windowed ? "windowed" : "fullscreen", pp->swap,
           pp->refresh, pp->interval, flags);
    g_d3d = d3d, g_focus = focus;
    windowed(pp, focus);
    HRESULT hr = g_create_dev(d3d, adapter, type, focus, flags, pp, out);
    printf("[d3d8] CreateDevice -> %08lX\n", hr);
    if (hr == 0 && *out) {
        void** vt = **(void****)out;
        if (vt[14] != (void*)hook_Reset) {
            g_reset = (reset_t)vt_hook(vt, 14, hook_Reset);
            g_present = (present_t)vt_hook(vt, 15, hook_Present);
        }
    }
    return hr;
}

static void shim_Direct3DCreate8(void) {
    typedef void* (WINAPI *create_t)(UINT);
    static create_t create;
    if (!create) create = (create_t)GetProcAddress(LoadLibraryA("d3d8.dll"), "Direct3DCreate8");
    void* d3d = create(ARG(0));
    if (d3d) {
        void** vt = *(void***)d3d;
        DWORD old;
        if (vt[15] != (void*)hook_CreateDevice) {
            g_create_dev = (create_dev_t)vt[15];
            VirtualProtect(&vt[15], sizeof vt[15], PAGE_READWRITE, &old);
            vt[15] = (void*)hook_CreateDevice;
            VirtualProtect(&vt[15], sizeof vt[15], old, &old);
        }
    }
    g_eax = (uint32_t)(uintptr_t)d3d;
    g_esp += 4 + 1 * 4;
}

/* CloseWindow minimizes. The game does it to its window whenever another
 * window is in front (sub_0040A730); windowed, it never does. */
static void shim_CloseWindow(void) {
    static int once;
    if (!once++) printf("[host] CloseWindow from sub_%08X%s\n", g_cur_func, g_fullscreen ? "" : ": ignored");
    if (g_fullscreen) UNLOCKED(CloseWindow((HWND)(uintptr_t)ARG(0)));
    g_eax = TRUE;
    g_esp += 4 + 1 * 4;
}

static native32_shim_t g_shims[] = {
    { "CloseWindow", shim_CloseWindow },
    { "GetForegroundWindow", shim_GetForegroundWindow },
    { "Direct3DCreate8", shim_Direct3DCreate8 },
    { "ShowWindow", shim_ShowWindow },
    { "CreateWindowExA", shim_CreateWindowExA },
    { "DirectInput8Create", shim_DirectInput8Create },
    { "RaiseException", shim_RaiseException },
    { "CreateFileA", shim_CreateFileA },
    { "MessageBoxA", shim_MessageBoxA },
    { "OutputDebugStringA", shim_OutputDebugStringA },
    { "ExitProcess", shim_ExitProcess },
    { "GetModuleHandleA", shim_GetModuleHandleA },
    { "GetModuleFileNameA", shim_GetModuleFileNameA },
    { "GetCommandLineA", shim_GetCommandLineA },
};

/* ---- muted by default -------------------------------------------------------
 * Every run is silent until sound is being tested (--sound). */
static const GUID kCLSID_MMDeviceEnumerator =
    { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID kIID_IMMDeviceEnumerator =
    { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID kIID_IAudioSessionManager =
    { 0xBFA971F1, 0x4D5E, 0x40BB, { 0x93, 0x5E, 0x96, 0x70, 0x39, 0xBF, 0xBE, 0xE4 } };

/* 1: muted; 2: there is no audio device; 0: failed. */
static int mute_process(void) {
    IMMDeviceEnumerator* en = NULL;
    IMMDevice* dev = NULL;
    IAudioSessionManager* mgr = NULL;
    ISimpleAudioVolume* vol = NULL;
    HRESULT hr = CoCreateInstance(&kCLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                  &kIID_IMMDeviceEnumerator, (void**)&en);
    if (SUCCEEDED(hr)) hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev);
    if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) { IMMDeviceEnumerator_Release(en); return 2; }
    if (SUCCEEDED(hr)) hr = IMMDevice_Activate(dev, &kIID_IAudioSessionManager, CLSCTX_ALL, NULL, (void**)&mgr);
    if (SUCCEEDED(hr)) hr = IAudioSessionManager_GetSimpleAudioVolume(mgr, &GUID_NULL, FALSE, &vol);
    if (SUCCEEDED(hr)) hr = ISimpleAudioVolume_SetMute(vol, TRUE, NULL);
    if (vol) ISimpleAudioVolume_Release(vol);
    if (mgr) IAudioSessionManager_Release(mgr);
    if (dev) IMMDevice_Release(dev);
    if (en) IMMDeviceEnumerator_Release(en);
    return SUCCEEDED(hr);
}

/* ---- reports ---------------------------------------------------------------- */
recomp_func_t recomp_lookup_manual(uint32_t va) {
    (void)va;
    return NULL;
}

void recomp_not_lifted(uint32_t va) {
    fprintf(stderr, "\n[not-lifted] sub_%08X  (called from 0x%08X)\n", va, g_cur_func);
    recomp_dump_trace("not-lifted");
    native32_dump_icalls(8);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 2);
}

/* Added after native32's own handler, so callbacks are resolved first and only
 * real faults get here. */
static char g_crash_buf[4096];
static int g_crash_len;
static void crash_emit(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(g_crash_buf + g_crash_len, sizeof g_crash_buf - 1 - g_crash_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_crash_len += n;
}

static LONG CALLBACK crash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if ((r->ExceptionCode & 0xF0000000u) != 0xC0000000u) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&once, 1)) TerminateProcess(GetCurrentProcess(), 3);
    crash_emit("\n=== fault 0x%08lX at 0x%p, thread %lu ===\n", r->ExceptionCode,
               r->ExceptionAddress, GetCurrentThreadId());
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        ULONG_PTR op = r->ExceptionInformation[0];
        uint32_t at = (uint32_t)r->ExceptionInformation[1];
        crash_emit("  %s of 0x%08X%s\n", op == 0 ? "read" : op == 1 ? "write" : "execute", at,
                   native32_in_guest(at) ? " (inside the guest image)" : at < 0x10000 ? " (null/low)" : "");
    }
    crash_emit("  in lifted sub_%08X, last native call %s\n", g_cur_func, g_cur_import);
    crash_emit("  eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
               g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi);
    crash_emit("last indirect calls (newest first):\n");
    for (int i = 1; i <= 12 && i <= (int)g_icall_trace_idx; i++) {
        uint32_t k = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
        const char* nm = native32_name(g_icall_trace[k]);
        crash_emit("  0x%08X  from 0x%08X  %s\n", g_icall_trace[k], g_icall_from[k], nm ? nm : "");
    }
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), g_crash_buf, (DWORD)g_crash_len, &w, NULL);
    recomp_trace_flush();
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* --sample: the lifted function running, ten times a second, for a while. */
static int g_sample;
static DWORD WINAPI sampler(LPVOID unused) {
    (void)unused;
    for (int i = 0; i < 400; i++) {
        Sleep(100);
        printf("[sample] %5.1fs sub_%08X %s\n", i / 10.0, g_cur_func, g_cur_import ? g_cur_import : "");
    }
    return 0;
}

static DWORD WINAPI watchdog(LPVOID unused) {
    (void)unused;
    Sleep(g_watchdog_s * 1000);
    fprintf(stderr, "\n[watchdog] %lu s: in sub_%08X, last native call %s, %u indirect calls\n",
            g_watchdog_s, g_cur_func, g_cur_import, g_icall_count);
    native32_dump_icalls(8);
    recomp_trace_flush();
    fflush(stdout);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 4);
    return 0;
}

/* MSVC's longjmp (run_lift.py PATCHES), from HotD2. jmp_buf: ebp, ebx, edi,
 * esi, esp at the setjmp call (on its return address), return address. The
 * lifted frame that owns that stack resumes at the landing pad after its
 * setjmp call, with setjmp "returning" val. A lifted call pushes no real
 * return address (RECOMP_RETADDR=0), so the pad is found by the owner. */
extern const uint32_t ct_setjmp_resume[][2];   /* recomp_dispatch.c */
void ct_longjmp(uint32_t jb, uint32_t val) {
    uint32_t esp = MEM32(jb + 0x10) + 4;          /* the return address popped */
    recomp_ehframe_t* f = recomp_eh_owner(esp);
    uint32_t resume = 0;
    int pads = 0;
    for (int i = 0; f && ct_setjmp_resume[i][0]; i++)
        if (ct_setjmp_resume[i][0] == f->func) resume = ct_setjmp_resume[i][1], pads++;
    if (!resume || pads > 1) {
        fprintf(stderr, "[longjmp] buffer 0x%08X (esp 0x%08X): %s\n", jb, esp,
                resume ? "its owner calls setjmp more than once" : "no lifted setjmp caller owns it");
        TerminateProcess(GetCurrentProcess(), 3);
    }
    printf("[longjmp] to sub_%08X at 0x%08X\n", f->func, resume);
    g_ebp = MEM32(jb), g_ebx = MEM32(jb + 4), g_edi = MEM32(jb + 8), g_esi = MEM32(jb + 0xC);
    g_esp = esp;
    g_eax = val ? val : 1;
    recomp_eh_resume(f, resume);
}

int main(int argc, char** argv) {
    const char* exe = "work\\CRAZY_TAXI_PC.EXE";
    const char* game = "game";
    char exe_full[MAX_PATH], game_full[MAX_PATH], guest_args[512] = "";
    int run = 0, sound = 0;
    setvbuf(stdout, NULL, _IONBF, 0);   /* a fault ends the run with TerminateProcess */
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--sound")) sound = 1;
        else if (!strcmp(argv[i], "--headless")) g_headless = 1;
        else if (!strcmp(argv[i], "--fullscreen")) g_fullscreen = 1;
        else if (!strcmp(argv[i], "--sample")) g_sample = 1;
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc && g_shots < 8) {
            g_shot_s[g_shots] = strtoul(argv[++i], NULL, 0);
            GetFullPathNameA(argv[++i], MAX_PATH, g_shot_file[g_shots++], NULL);
        }
        else if (!strcmp(argv[i], "--key") && i + 3 < argc && g_nkeys < 1024) {
            g_keys[g_nkeys].at = (DWORD)(atof(argv[++i]) * 1000);
            g_keys[g_nkeys].dik = (int)strtoul(argv[++i], NULL, 0) & 0xFF;
            g_keys[g_nkeys++].ms = strtoul(argv[++i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe = argv[++i];
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--args") && i + 1 < argc) _snprintf(guest_args, sizeof guest_args - 1, " %s", argv[++i]);
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) g_watchdog_s = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--callbacks")) native32_trace_callbacks = 1;
        else if (!strcmp(argv[i], "--native-trace")) native32_trace_native = 1;
        else {
            printf("usage: crazytaxi [--run] [--headless] [--sound] [--exe work\\CRAZY_TAXI_PC.EXE] [--game game]\n"
                   "             [--args \"<game switches>\"] [--watchdog S] [--callbacks] [--native-trace]\n");
            recomp_trace_help();
            return argv[i][1] == 'h' || argv[i][2] == 'h' ? 0 : 1;
        }
    }
    GetFullPathNameA(exe, MAX_PATH, exe_full, NULL);
    GetFullPathNameA(game, MAX_PATH, game_full, NULL);
    cd_pick(game_full);
    _snprintf(g_guest_exe, sizeof g_guest_exe - 1, "%s\\CRAZY_TAXI_PC.EXE", game_full);
    _snprintf(g_guest_cmdline, sizeof g_guest_cmdline - 1, "\"%s\"%s", g_guest_exe, guest_args);

    native32_init();
    AddVectoredExceptionHandler(0, crash);
    printf("Crazy Taxi recomp host\n  lifted functions in dispatch: %u\n", recomp_dispatch_count);

    uint32_t span = native32_map(exe_full, IMAGE_BASE);
    if (!span) { fprintf(stderr, "cannot map %s at 0x%08X\n", exe_full, IMAGE_BASE); return 1; }
    printf("  mapped %s: 0x%08X-0x%08X\n", exe, IMAGE_BASE, IMAGE_BASE + span);
    if (native32_bind(IMAGE_BASE, g_shims, (int)(sizeof g_shims / sizeof g_shims[0]))) return 1;
    printf("  guest command line %s\n", g_guest_cmdline);
    if (!run) {
        printf("\n(dry run: image mapped and bound; --run enters 0x%08X)\n", ct_entry_va);
        return 0;
    }
    /* The game opens its data relative to its working directory. */
    if (!SetCurrentDirectoryA(game_full)) { fprintf(stderr, "cannot enter %s\n", game_full); return 1; }
    CoInitialize(NULL);
    int m = sound ? 1 : mute_process();
    if (!m) {
        fprintf(stderr, "cannot mute the process audio session; pass --sound to run with sound\n");
        return 1;
    }
    printf("  audio: %s\n", sound ? "on (--sound)" : m == 2 ? "muted (no audio device)" : "muted (--sound to hear it)");
    if (g_watchdog_s) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    if (g_sample) CloseHandle(CreateThread(NULL, 0, sampler, NULL, 0, NULL));
    if (g_shots) CloseHandle(CreateThread(NULL, 0, shot_thread, NULL, 0, NULL));
    if (g_nkeys) CloseHandle(CreateThread(NULL, 0, key_thread, NULL, 0, NULL));
    printf("  entering 0x%08X\n\n", ct_entry_va);
    native32_call_guest(ct_entry_va, 0, NULL);
    printf("\nentry returned eax=%08X\n", g_eax);
    return (int)g_eax;
}

