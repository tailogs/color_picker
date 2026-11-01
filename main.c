// Color Picker — v0.1.0 (hardened + QoL improvements)
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <commctrl.h>
#include "resource.h"

#define VERSION "0.1.0"

#define TIMER_ID                1
#define ABOUT_TIMER_ID          2
#define TIMER_COPY_FEEDBACK     3

#define HOTKEY_TOGGLE_SCAN      1
#define HOTKEY_PICK             2

#define MAGNIFIER_WIDTH         150
#define MAGNIFIER_HEIGHT        150
#define MAGNIFICATION_FACTOR    10

#define WM_RESTORE_APP          (WM_USER + 2)
#define COLOR_WHEEL_SIZE        180

#define IDC_COLOR_WHEEL         1006
#define IDC_SLIDER_V            1007
#define IDC_SLIDER_R            1003
#define IDC_SLIDER_G            1004
#define IDC_SLIDER_B            1005

#define IDC_ABOUT_CLOSE         1001

/* Recent-color button IDs */
#define IDC_RECENT_BASE         100
#define IDC_RECENT_COUNT        8

/* Pick button ID */
#define IDC_PICK_BTN            5

/* Extra menu command (tray) */
#ifndef ID_TOGGLE_AOT
#define ID_TOGGLE_AOT           3001
#endif

/* ---------- Controls ---------- */
static HWND hTextBoxCoordinateX, hTextBoxCoordinateY, hTextBoxColorRGB, hTextBoxColorHEX;
static HWND hPanelColor, hBtnStartAndStop, hMagnifier, hBtnPick;
static HWND hLabelCoordinateX, hLabelCoordinateY, hLabelColorRGB, hLabelColorHEX;
static HWND hBtnCopyCoordinates, hBtnCopyRGB, hBtnCopyHEX;
static HWND hLabelR, hLabelG, hLabelB, hSliderR, hSliderG, hSliderB;
static HWND hColorWheel, hLabelV, hSliderV;
static HWND s_recentButtons[IDC_RECENT_COUNT];

/* ---------- State ---------- */
static BOOL       startAndStop = TRUE;
static BOOL       updatingSliders = FALSE;
static BOOL       isDragging = FALSE;
static BOOL       s_colorInitialized = FALSE;
static BOOL       s_wasScanningBeforeHide = FALSE;
static BOOL       s_alwaysOnTop = FALSE;
static BOOL       s_paused = FALSE;

static COLORREF   lastColor    = (COLORREF)-1;
static COLORREF   targetColor  = (COLORREF)-1;
static COLORREF   currentColor = (COLORREF)-1;
static COLORREF   s_recentColors[IDC_RECENT_COUNT];

static POINT      selectorPos       = { COLOR_WHEEL_SIZE / 2, COLOR_WHEEL_SIZE / 2 };
static POINT      targetSelectorPos = { COLOR_WHEEL_SIZE / 2, COLOR_WHEEL_SIZE / 2 };

/* ---------- GDI / system ---------- */
static HWND       g_hMainWnd = NULL;

static HFONT      hFont = NULL;
static HBRUSH     g_hbrBkgnd = NULL;
static HBRUSH     s_panelBrush = NULL;
static COLORREF   s_panelBrushColor = (COLORREF)-1;

static HHOOK      mouseHook = NULL;
static NOTIFYICONDATA nid;
static UINT_PTR   timerId = 0;
static HWND       hAboutWnd = NULL;
static int        hue = 0;

static HBITMAP    hbmColorWheelBuffer = NULL;
static HDC        hdcColorWheelBuffer = NULL;
static DWORD     *s_wheelBits = NULL;
static int        s_lastDrawnV = -1;

static HDC        hdcMem = NULL;
static HBITMAP    hbmMem = NULL;
static HDC        hdcMagnifier = NULL;
static LARGE_INTEGER lastUpdateTime;
static LARGE_INTEGER frequency;

static char       s_settingsPath[MAX_PATH] = { 0 };

/* Edit subclass for dblclick-copy */
static WNDPROC    s_editOldProcs[4] = { 0 };
static HWND       s_editHwnds[4]    = { 0 };
static int        s_editCount       = 0;

/* ---------- Forward decls ---------- */
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ColorWheelProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK AboutWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK EditSubProc(HWND, UINT, WPARAM, LPARAM);

static void UpdateColorInfo(void);
static void CreateMagnifier(void);
static void UpdateMagnifier(POINT pt);
static void ShowAboutDialog(HWND);
static void StopScanning(HWND);
static void StartScanning(HWND);
static void CopyTextToClipboard(HWND hWnd, const char* text);
static void UpdateColorFromSliders(void);
static void UpdateColorFromWheel(void);
static void RGBtoHSV(BYTE r, BYTE g, BYTE b, float* h, float* s, float* v);
static void HSVtoRGB(float h, float s, float v, BYTE* r, BYTE* g, BYTE* b);
static void RedrawWheelBuffer(int v);
static void AnimateSliderPosition(HWND hSlider, int targetPos);
static void AnimateSelectorPosition(POINT newPos);
static void CopyFeedback(HWND hWnd, HWND hBtn);
static void UpdateWindowTitle(HWND hWnd, COLORREF c);
static void PushRecentColor(COLORREF c);
static void ApplyRecentColor(int idx, HWND hWnd);
static void TogglePause(HWND hWnd);
static void ToggleAlwaysOnTop(HWND hWnd);
static void LoadSettings(HWND hWnd);
static void SaveSettings(HWND hWnd);

/* =================== INI =================== */
static void GetSettingsPath(char *buf, size_t size)
{
    char appdata[MAX_PATH] = { 0 };
    DWORD n = GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));

    if (n > 0 && n < sizeof(appdata))
    {
        /* %APPDATA%\ColorPicker\settings.ini */
        int written = snprintf(buf, size, "%s\\ColorPicker", appdata);
        if (written < 0 || (size_t)written >= size)
        {
            buf[0] = '\0';
            return;
        }
        if (GetFileAttributesA(buf) == INVALID_FILE_ATTRIBUTES)
            CreateDirectoryA(buf, NULL);

        {
            size_t cur = strlen(buf);
            const char *suffix = "\\settings.ini";
            size_t suffixLen = strlen(suffix);
            if (cur + suffixLen + 1 <= size)
            {
                memcpy(buf + cur, suffix, suffixLen + 1);
            }
            else
            {
                buf[0] = '\0';
            }
        }
        return;
    }

    /* Fallback: рядом с .exe — только если APPDATA недоступен */
    {
        char exePath[MAX_PATH] = { 0 };
        char *p;
        const char *suffix = "\\settings.ini";
        size_t dirLen, suffixLen;

        GetModuleFileNameA(NULL, exePath, (DWORD)sizeof(exePath) - 1);
        p = strrchr(exePath, '\\');
        if (p) *p = '\0';

        dirLen    = strlen(exePath);
        suffixLen = strlen(suffix);

        if (dirLen + suffixLen + 1 <= size)
        {
            memcpy(buf, exePath, dirLen);
            memcpy(buf + dirLen, suffix, suffixLen + 1);
        }
        else
        {
            lstrcpynA(buf, "settings.ini", (int)size);
        }
    }
}

static void LoadSettings(HWND hWnd)
{
    char buf[32];
    int i, x, y;

    GetSettingsPath(s_settingsPath, sizeof(s_settingsPath));

    s_recentColors[0] = RGB(255, 255, 255);
    s_recentColors[1] = RGB(  0,   0,   0);
    s_recentColors[2] = RGB(255,   0,   0);
    s_recentColors[3] = RGB(  0, 255,   0);
    s_recentColors[4] = RGB(  0,   0, 255);
    s_recentColors[5] = RGB(255, 255,   0);
    s_recentColors[6] = RGB(  0, 255, 255);
    s_recentColors[7] = RGB(255,   0, 255);

    for (i = 0; i < IDC_RECENT_COUNT; i++)
    {
        char key[16];
        unsigned int rgb;
        snprintf(key, sizeof(key), "Recent%d", i);
        GetPrivateProfileStringA("Colors", key, "FFFFFF", buf, sizeof(buf), s_settingsPath);
        rgb = (unsigned int)strtoul(buf, NULL, 16);
        s_recentColors[i] = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }

    x = GetPrivateProfileIntA("Window", "X", -1, s_settingsPath);
    y = GetPrivateProfileIntA("Window", "Y", -1, s_settingsPath);
    if (x != -1 && y != -1)
    {
        RECT rc = { x, y, x + 100, y + 100 };
        if (MonitorFromRect(&rc, MONITOR_DEFAULTTONULL))
            SetWindowPos(hWnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    s_alwaysOnTop = GetPrivateProfileIntA("Window", "AlwaysOnTop", 0, s_settingsPath);
    if (s_alwaysOnTop)
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    for (i = 0; i < IDC_RECENT_COUNT; i++)
        InvalidateRect(s_recentButtons[i], NULL, TRUE);
}

static void SaveSettings(HWND hWnd)
{
    RECT rc;
    char buf[32];
    int i;

    GetWindowRect(hWnd, &rc);
    snprintf(buf, sizeof(buf), "%d", (int)rc.left);
    WritePrivateProfileStringA("Window", "X", buf, s_settingsPath);
    snprintf(buf, sizeof(buf), "%d", (int)rc.top);
    WritePrivateProfileStringA("Window", "Y", buf, s_settingsPath);
    WritePrivateProfileStringA("Window", "AlwaysOnTop",
                               s_alwaysOnTop ? "1" : "0", s_settingsPath);

    for (i = 0; i < IDC_RECENT_COUNT; i++)
    {
        char key[16];
        snprintf(key, sizeof(key), "Recent%d", i);
        snprintf(buf, sizeof(buf), "%02X%02X%02X",
                 GetRValue(s_recentColors[i]),
                 GetGValue(s_recentColors[i]),
                 GetBValue(s_recentColors[i]));
        WritePrivateProfileStringA("Colors", key, buf, s_settingsPath);
    }
}

/* =================== Window title (throttled) =================== */
static void UpdateWindowTitle(HWND hWnd, COLORREF c)
{
    static COLORREF s_lastTitleColor = (COLORREF)0xFFFFFFFF;
    static BOOL     s_titleInit = FALSE;
    char buf[64];

    if (!hWnd) return;
    if (s_titleInit && c == s_lastTitleColor) return;
    s_titleInit = TRUE;
    s_lastTitleColor = c;

    if (c == (COLORREF)-1)
    {
        SetWindowTextA(hWnd, "Color Picker");
        return;
    }
    snprintf(buf, sizeof(buf), "#%02X%02X%02X - Color Picker",
             GetRValue(c), GetGValue(c), GetBValue(c));
    SetWindowTextA(hWnd, buf);
}

/* =================== Recent colors =================== */
static void PushRecentColor(COLORREF c)
{
    int i;
    if (c == (COLORREF)-1) return;
    if (s_recentColors[0] == c) return;
    for (i = IDC_RECENT_COUNT - 1; i > 0; i--)
        s_recentColors[i] = s_recentColors[i - 1];
    s_recentColors[0] = c;
    for (i = 0; i < IDC_RECENT_COUNT; i++)
        InvalidateRect(s_recentButtons[i], NULL, TRUE);
}

static void ApplyRecentColor(int idx, HWND hWnd)
{
    COLORREF c = s_recentColors[idx];
    BYTE r = GetRValue(c);
    BYTE g = GetGValue(c);
    BYTE b = GetBValue(c);
    float h, s, v;
    char buf[64];

    SendMessage(hSliderR, TBM_SETPOS, TRUE, r);
    SendMessage(hSliderG, TBM_SETPOS, TRUE, g);
    SendMessage(hSliderB, TBM_SETPOS, TRUE, b);

    lastColor = targetColor = currentColor = c;

    RGBtoHSV(r, g, b, &h, &s, &v);
    SendMessage(hSliderV, TBM_SETPOS, TRUE, (LPARAM)(v * 255));
    selectorPos.x = (LONG)(s * (COLOR_WHEEL_SIZE - 1));
    selectorPos.y = (LONG)((1.0f - (h / 360.0f)) * (COLOR_WHEEL_SIZE - 1));
    targetSelectorPos = selectorPos;

    snprintf(buf, sizeof(buf), "(%d, %d, %d)", r, g, b);
    SetWindowTextA(hTextBoxColorRGB, buf);
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    SetWindowTextA(hTextBoxColorHEX, buf);

    InvalidateRect(hPanelColor, NULL, TRUE);
    InvalidateRect(hColorWheel, NULL, FALSE);
    InvalidateRect(hSliderR, NULL, FALSE);
    InvalidateRect(hSliderG, NULL, FALSE);
    InvalidateRect(hSliderB, NULL, FALSE);
    InvalidateRect(hSliderV, NULL, FALSE);

    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    CopyTextToClipboard(hWnd, buf);

    UpdateWindowTitle(hWnd, c);

    if (!s_paused)
    {
        s_paused = TRUE;
        SetWindowTextA(hBtnPick, "Resume (F9)");
        InvalidateRect(hBtnPick, NULL, TRUE);
    }
}

/* =================== Pause / AOT =================== */
static void TogglePause(HWND hWnd)
{
    (void)hWnd;
    s_paused = !s_paused;
    if (s_paused)
    {
        PushRecentColor(lastColor);
        SetWindowTextA(hBtnPick, "Resume (F9)");
    }
    else
    {
        SetWindowTextA(hBtnPick, "Pick (F9)");
    }
    InvalidateRect(hBtnPick, NULL, TRUE);
}

static void ToggleAlwaysOnTop(HWND hWnd)
{
    s_alwaysOnTop = !s_alwaysOnTop;
    SetWindowPos(hWnd, s_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

/* =================== WinMain =================== */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASS wc = { 0 }, wcAbout = { 0 }, wcColorWheel = { 0 };
    HMENU    hMenu;
    HWND     hWnd;
    HANDLE   hMutex;
    MSG      msg;

    (void)hPrevInstance;
    (void)lpCmdLine;

    FreeConsole();

    {
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
        InitCommonControlsEx(&icc);
    }

    hMutex = CreateMutexA(NULL, TRUE, "ColorPickerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND hExisting = FindWindowA("ColorPickerClass", NULL);
        if (hExisting) PostMessage(hExisting, WM_RESTORE_APP, 0, 0);
        CloseHandle(hMutex);
        return 0;
    }

    g_hbrBkgnd = CreateSolidBrush(RGB(30, 30, 30));

    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "ColorPickerClass";
    wc.hbrBackground = g_hbrBkgnd;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    wcAbout.lpfnWndProc   = AboutWndProc;
    wcAbout.hInstance     = hInstance;
    wcAbout.lpszClassName = "AboutWindowClass";
    wcAbout.hbrBackground = g_hbrBkgnd;
    wcAbout.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wcAbout);

    wcColorWheel.lpfnWndProc   = ColorWheelProc;
    wcColorWheel.hInstance     = hInstance;
    wcColorWheel.lpszClassName = "ColorWheelClass";
    wcColorWheel.hbrBackground = NULL;
    RegisterClass(&wcColorWheel);

    hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_MYMENU));

    hWnd = CreateWindowA("ColorPickerClass", "Color Picker",
        WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX),
        CW_USEDEFAULT, CW_USEDEFAULT, 237, 750,
        NULL, hMenu, hInstance, NULL);

    g_hMainWnd = hWnd;

    LoadSettings(hWnd);

    {
        HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
        SendMessage(hWnd, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
        SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    CreateMagnifier();
    StartScanning(hWnd);
    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, hInstance, 0);

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hWnd;
    nid.uID              = 1;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.hIcon            = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    nid.uCallbackMessage = WM_USER + 1;
    lstrcpynA(nid.szTip, "Color Picker",
              sizeof(nid.szTip) / sizeof(nid.szTip[0]));
    Shell_NotifyIcon(NIM_ADD, &nid);

    RegisterHotKey(hWnd, HOTKEY_TOGGLE_SCAN, 0, VK_F8);
    RegisterHotKey(hWnd, HOTKEY_PICK,        0, VK_F9);

    SetFocus(hWnd);

    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == 'P' &&
            !(GetKeyState(VK_CONTROL) & 0x8000) &&
            !(GetKeyState(VK_MENU)    & 0x8000) &&
            !(GetKeyState(VK_SHIFT)   & 0x8000) &&
            GetForegroundWindow() == hWnd)
        {
            if (startAndStop) StopScanning(hWnd);
            else              StartScanning(hWnd);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterHotKey(hWnd, HOTKEY_TOGGLE_SCAN);
    UnregisterHotKey(hWnd, HOTKEY_PICK);
    if (mouseHook) { UnhookWindowsHookEx(mouseHook); mouseHook = NULL; }
    Shell_NotifyIcon(NIM_DELETE, &nid);
    CloseHandle(hMutex);
    if (g_hbrBkgnd) { DeleteObject(g_hbrBkgnd); g_hbrBkgnd = NULL; }

    return 0;
}

/* =================== Edit subclass (dblclick copy) =================== */
static LRESULT CALLBACK EditSubProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    for (i = 0; i < s_editCount; i++)
        if (s_editHwnds[i] == hwnd) break;

    if (msg == WM_LBUTTONDBLCLK && i < s_editCount)
    {
        HWND parent = GetParent(hwnd);
        int id;
        if (hwnd == hTextBoxColorHEX) id = 4;
        else if (hwnd == hTextBoxColorRGB) id = 3;
        else id = 2;
        SendMessage(parent, WM_COMMAND, (WPARAM)id, 0);
        return 0;
    }
    return CallWindowProc(s_editOldProcs[i], hwnd, msg, wp, lp);
}

/* =================== WndProc =================== */
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        HMENU hMenu = LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MYMENU));
        SetMenu(hWnd, hMenu);

        hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        /* Coordinates */
        hLabelCoordinateX   = CreateWindowA("STATIC", "X:", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                            10, 10, 20, 20, hWnd, NULL, NULL, NULL);
        hTextBoxCoordinateX = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_CENTER | ES_READONLY,
            40, 10, 100, 20, hWnd, NULL, NULL, NULL);
        hLabelCoordinateY   = CreateWindowA("STATIC", "Y:", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                            10, 40, 20, 20, hWnd, NULL, NULL, NULL);
        hTextBoxCoordinateY = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_CENTER | ES_READONLY,
            40, 40, 100, 20, hWnd, NULL, NULL, NULL);
        hBtnCopyCoordinates = CreateWindowA("BUTTON", "Copy",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            150, 10, 70, 50, hWnd, (HMENU)2, NULL, NULL);

        /* RGB */
        hLabelColorRGB   = CreateWindowA("STATIC", "RGB:", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                         10, 70, 40, 20, hWnd, NULL, NULL, NULL);
        hTextBoxColorRGB = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_CENTER | ES_READONLY,
            60, 70, 160, 20, hWnd, NULL, NULL, NULL);
        hBtnCopyRGB      = CreateWindowA("BUTTON", "Copy",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            10, 100, 210, 20, hWnd, (HMENU)3, NULL, NULL);

        /* HEX */
        hLabelColorHEX   = CreateWindowA("STATIC", "HEX:", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                         10, 130, 40, 20, hWnd, NULL, NULL, NULL);
        hTextBoxColorHEX = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_CENTER | ES_READONLY,
            60, 130, 160, 20, hWnd, NULL, NULL, NULL);
        hBtnCopyHEX      = CreateWindowA("BUTTON", "Copy",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            10, 160, 210, 20, hWnd, (HMENU)4, NULL, NULL);

        /* Panel */
        hPanelColor = CreateWindowExA(WS_EX_CLIENTEDGE, "STATIC", "",
            WS_CHILD | WS_VISIBLE, 10, 190, 210, 70, hWnd, NULL, NULL, NULL);

        /* Recent colors */
        {
            int i;
            for (i = 0; i < IDC_RECENT_COUNT; i++)
            {
                s_recentButtons[i] = CreateWindowA("BUTTON", "",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    10 + i * 25, 265, 23, 23, hWnd,
                    (HMENU)(INT_PTR)(IDC_RECENT_BASE + i), NULL, NULL);
            }
        }

        /* RGB sliders */
        hLabelR  = CreateWindowA("STATIC", "R:", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                  10, 300, 20, 20, hWnd, NULL, NULL, NULL);
        hSliderR = CreateWindowExA(0, TRACKBAR_CLASS, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            40, 300, 180, 20, hWnd, (HMENU)IDC_SLIDER_R, NULL, NULL);
        SendMessage(hSliderR, TBM_SETRANGE, TRUE, MAKELONG(0, 255));
        SendMessage(hSliderR, TBM_SETPOS,   TRUE, 255);

        hLabelG  = CreateWindowA("STATIC", "G:", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                  10, 330, 20, 20, hWnd, NULL, NULL, NULL);
        hSliderG = CreateWindowExA(0, TRACKBAR_CLASS, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            40, 330, 180, 20, hWnd, (HMENU)IDC_SLIDER_G, NULL, NULL);
        SendMessage(hSliderG, TBM_SETRANGE, TRUE, MAKELONG(0, 255));
        SendMessage(hSliderG, TBM_SETPOS,   TRUE, 255);

        hLabelB  = CreateWindowA("STATIC", "B:", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                  10, 360, 20, 20, hWnd, NULL, NULL, NULL);
        hSliderB = CreateWindowExA(0, TRACKBAR_CLASS, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            40, 360, 180, 20, hWnd, (HMENU)IDC_SLIDER_B, NULL, NULL);
        SendMessage(hSliderB, TBM_SETRANGE, TRUE, MAKELONG(0, 255));
        SendMessage(hSliderB, TBM_SETPOS,   TRUE, 255);

        /* Color wheel */
        {
            RECT cr;
            GetClientRect(hWnd, &cr);
            hColorWheel = CreateWindowExA(WS_EX_CLIENTEDGE, "ColorWheelClass", "",
                WS_CHILD | WS_VISIBLE,
                (cr.right - COLOR_WHEEL_SIZE) / 2, 390,
                COLOR_WHEEL_SIZE, COLOR_WHEEL_SIZE,
                hWnd, (HMENU)IDC_COLOR_WHEEL, NULL, NULL);
        }

        /* V slider */
        hLabelV  = CreateWindowA("STATIC", "V:", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                  10, 390 + COLOR_WHEEL_SIZE + 10, 20, 20, hWnd, NULL, NULL, NULL);
        hSliderV = CreateWindowExA(0, TRACKBAR_CLASS, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            40, 390 + COLOR_WHEEL_SIZE + 10, 180, 20, hWnd, (HMENU)IDC_SLIDER_V, NULL, NULL);
        SendMessage(hSliderV, TBM_SETRANGE, TRUE, MAKELONG(0, 255));
        SendMessage(hSliderV, TBM_SETPOS,   TRUE, 255);

        /* Buttons */
        hBtnStartAndStop = CreateWindowA("BUTTON", "Stop scan (F8)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            10, 640, 100, 30, hWnd, (HMENU)1, NULL, NULL);
        hBtnPick = CreateWindowA("BUTTON", "Pick (F9)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            115, 640, 100, 30, hWnd, (HMENU)IDC_PICK_BTN, NULL, NULL);

        /* Font */
        {
            HWND all[] = {
                hLabelCoordinateX, hTextBoxCoordinateX, hLabelCoordinateY, hTextBoxCoordinateY,
                hBtnCopyCoordinates, hLabelColorRGB, hTextBoxColorRGB, hBtnCopyRGB,
                hLabelColorHEX, hTextBoxColorHEX, hBtnCopyHEX,
                hLabelR, hLabelG, hLabelB, hLabelV,
                hSliderR, hSliderG, hSliderB, hSliderV,
                hBtnStartAndStop, hBtnPick
            };
            int i;
            for (i = 0; i < (int)(sizeof(all) / sizeof(all[0])); i++)
                SendMessage(all[i], WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        /* Subclass read-only edits for dblclick copy */
        s_editHwnds[0] = hTextBoxColorHEX;
        s_editOldProcs[0] = (WNDPROC)SetWindowLongPtr(hTextBoxColorHEX, GWLP_WNDPROC, (LONG_PTR)EditSubProc);
        s_editHwnds[1] = hTextBoxColorRGB;
        s_editOldProcs[1] = (WNDPROC)SetWindowLongPtr(hTextBoxColorRGB, GWLP_WNDPROC, (LONG_PTR)EditSubProc);
        s_editHwnds[2] = hTextBoxCoordinateX;
        s_editOldProcs[2] = (WNDPROC)SetWindowLongPtr(hTextBoxCoordinateX, GWLP_WNDPROC, (LONG_PTR)EditSubProc);
        s_editHwnds[3] = hTextBoxCoordinateY;
        s_editOldProcs[3] = (WNDPROC)SetWindowLongPtr(hTextBoxCoordinateY, GWLP_WNDPROC, (LONG_PTR)EditSubProc);
        s_editCount = 4;

        timerId = SetTimer(hWnd, TIMER_ID, 16, NULL);
    }
    break;

    case WM_SIZE:
    {
        int width   = LOWORD(lParam);
        int height  = HIWORD(lParam);
        int centerX = (width - COLOR_WHEEL_SIZE) / 2;
        int buttonY = height - 30 - 10;
        int btnW    = (width - 25) / 2;

        SetWindowPos(hColorWheel, NULL, centerX, 390, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
        SetWindowPos(hBtnStartAndStop, NULL, 10, buttonY, btnW, 30, SWP_NOZORDER);
        SetWindowPos(hBtnPick, NULL, 15 + btnW, buttonY, btnW, 30, SWP_NOZORDER);
    }
    break;

    case WM_HOTKEY:
        if (wParam == HOTKEY_TOGGLE_SCAN)
        {
            if (startAndStop) StopScanning(hWnd);
            else              StartScanning(hWnd);
            return 0;
        }
        if (wParam == HOTKEY_PICK)
        {
            TogglePause(hWnd);
            return 0;
        }
        break;

    case WM_HSCROLL:
    {
        HWND hSlider = (HWND)lParam;
        if (hSlider == hSliderR || hSlider == hSliderG || hSlider == hSliderB)
        {
            updatingSliders = TRUE;
            UpdateColorFromSliders();
            updatingSliders = FALSE;
        }
        else if (hSlider == hSliderV)
        {
            updatingSliders = TRUE;
            UpdateColorFromWheel();
            updatingSliders = FALSE;
            InvalidateRect(hColorWheel, NULL, FALSE);
        }
    }
    break;

    case WM_TIMER:
        if (wParam == TIMER_ID)
        {
            if (!s_paused) UpdateColorInfo();

            if (currentColor != targetColor)
            {
                BYTE r_curr = GetRValue(currentColor);
                BYTE g_curr = GetGValue(currentColor);
                BYTE b_curr = GetBValue(currentColor);
                BYTE r_t = GetRValue(targetColor);
                BYTE g_t = GetGValue(targetColor);
                BYTE b_t = GetBValue(targetColor);

                r_curr += (r_t - r_curr) / 8;
                g_curr += (g_t - g_curr) / 8;
                b_curr += (b_t - b_curr) / 8;

                if (abs(r_curr - r_t) < 2 && abs(g_curr - g_t) < 2 && abs(b_curr - b_t) < 2)
                    currentColor = targetColor;
                else
                    currentColor = RGB(r_curr, g_curr, b_curr);

                InvalidateRect(hPanelColor, NULL, TRUE);
            }

            if (selectorPos.x != targetSelectorPos.x || selectorPos.y != targetSelectorPos.y)
            {
                selectorPos.x += (targetSelectorPos.x - selectorPos.x) / 8;
                selectorPos.y += (targetSelectorPos.y - selectorPos.y) / 8;
                if (abs(selectorPos.x - targetSelectorPos.x) < 2 &&
                    abs(selectorPos.y - targetSelectorPos.y) < 2)
                    selectorPos = targetSelectorPos;
                InvalidateRect(hColorWheel, NULL, FALSE);
            }

            if (!updatingSliders && !isDragging && !s_paused)
            {
                int currentR = (int)SendMessage(hSliderR, TBM_GETPOS, 0, 0);
                int currentG = (int)SendMessage(hSliderG, TBM_GETPOS, 0, 0);
                int currentB = (int)SendMessage(hSliderB, TBM_GETPOS, 0, 0);
                int currentV = (int)SendMessage(hSliderV, TBM_GETPOS, 0, 0);

                int targetR = GetRValue(targetColor);
                int targetG = GetGValue(targetColor);
                int targetB = GetBValue(targetColor);
                float h, s, v;
                int targetV;

                RGBtoHSV((BYTE)targetR, (BYTE)targetG, (BYTE)targetB, &h, &s, &v);
                targetV = (int)(v * 255);

                if (currentR != targetR) AnimateSliderPosition(hSliderR, targetR);
                if (currentG != targetG) AnimateSliderPosition(hSliderG, targetG);
                if (currentB != targetB) AnimateSliderPosition(hSliderB, targetB);
                if (currentV != targetV) AnimateSliderPosition(hSliderV, targetV);
            }
        }
        else if (wParam == TIMER_COPY_FEEDBACK)
        {
            KillTimer(hWnd, TIMER_COPY_FEEDBACK);
            SetWindowTextA(hBtnCopyCoordinates, "Copy");
            SetWindowTextA(hBtnCopyRGB,         "Copy");
            SetWindowTextA(hBtnCopyHEX,         "Copy");
            InvalidateRect(hBtnCopyCoordinates, NULL, TRUE);
            InvalidateRect(hBtnCopyRGB,         NULL, TRUE);
            InvalidateRect(hBtnCopyHEX,         NULL, TRUE);
            return 0;
        }
        break;

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        if (id >= IDC_RECENT_BASE && id < IDC_RECENT_BASE + IDC_RECENT_COUNT)
        {
            ApplyRecentColor(id - IDC_RECENT_BASE, hWnd);
            break;
        }

        switch (id)
        {
        case 1:
            if (startAndStop) StopScanning(hWnd);
            else              StartScanning(hWnd);
            break;

        case IDC_PICK_BTN:
            TogglePause(hWnd);
            break;

        case 2:
        {
            char x[32], y[32], buffer[128];
            GetWindowTextA(hTextBoxCoordinateX, x, sizeof(x));
            GetWindowTextA(hTextBoxCoordinateY, y, sizeof(y));
            snprintf(buffer, sizeof(buffer), "%s, %s", x, y);
            CopyTextToClipboard(hWnd, buffer);
            CopyFeedback(hWnd, hBtnCopyCoordinates);
        }
        break;

        case 3:
        {
            char buffer[64];
            GetWindowTextA(hTextBoxColorRGB, buffer, sizeof(buffer));
            CopyTextToClipboard(hWnd, buffer);
            CopyFeedback(hWnd, hBtnCopyRGB);
            if (lastColor != (COLORREF)-1) PushRecentColor(lastColor);
        }
        break;

        case 4:
        {
            char buffer[64];
            GetWindowTextA(hTextBoxColorHEX, buffer, sizeof(buffer));
            CopyTextToClipboard(hWnd, buffer);
            CopyFeedback(hWnd, hBtnCopyHEX);
            if (lastColor != (COLORREF)-1) PushRecentColor(lastColor);
        }
        break;

        case ID_TOGGLE_AOT:
            ToggleAlwaysOnTop(hWnd);
            break;

        case ID_FILE_EXIT:
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;

        case ID_HELP_ABOUT:
            ShowAboutDialog(hWnd);
            break;

        case ID_TRAY_SHOW:
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            if (s_wasScanningBeforeHide && !startAndStop)
            {
                StartScanning(hWnd);
                s_wasScanningBeforeHide = FALSE;
            }
            break;
        }
    }
    break;

    case WM_USER + 1:
        if (lParam == WM_RBUTTONUP)
        {
            HMENU hMenu = CreatePopupMenu();
            AppendMenuA(hMenu, MF_STRING, ID_TRAY_SHOW, "Show");
            AppendMenuA(hMenu,
                        MF_STRING | (s_alwaysOnTop ? MF_CHECKED : 0),
                        ID_TOGGLE_AOT, "Always on top");
            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hMenu, MF_STRING, ID_FILE_EXIT, "Exit");
            {
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hWnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
            }
            DestroyMenu(hMenu);
        }
        else if (lParam == WM_LBUTTONDBLCLK)
        {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            if (s_wasScanningBeforeHide && !startAndStop)
            {
                StartScanning(hWnd);
                s_wasScanningBeforeHide = FALSE;
            }
        }
        break;

    case WM_RESTORE_APP:
        ShowWindow(hWnd, SW_RESTORE);
        SetForegroundWindow(hWnd);
        if (s_wasScanningBeforeHide && !startAndStop)
        {
            StartScanning(hWnd);
            s_wasScanningBeforeHide = FALSE;
        }
        break;

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;

        if (dis->CtlID >= IDC_RECENT_BASE &&
            dis->CtlID < IDC_RECENT_BASE + IDC_RECENT_COUNT)
        {
            int idx = dis->CtlID - IDC_RECENT_BASE;
            HBRUSH  br = CreateSolidBrush(s_recentColors[idx]);
            HPEN    innerPen = NULL;
            HPEN    outerPen;
            HGDIOBJ oldPen, oldBrush;

            FillRect(dis->hDC, &dis->rcItem, br);
            DeleteObject(br);

            if (lastColor != (COLORREF)-1 && s_recentColors[idx] == lastColor)
            {
                innerPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                oldPen   = SelectObject(dis->hDC, innerPen);
                oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC,
                          dis->rcItem.left + 2, dis->rcItem.top + 2,
                          dis->rcItem.right - 2, dis->rcItem.bottom - 2);
                SelectObject(dis->hDC, oldPen);
                SelectObject(dis->hDC, oldBrush);
                DeleteObject(innerPen);
            }

            outerPen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
            oldPen   = SelectObject(dis->hDC, outerPen);
            oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                      dis->rcItem.right, dis->rcItem.bottom);
            SelectObject(dis->hDC, oldPen);
            SelectObject(dis->hDC, oldBrush);
            DeleteObject(outerPen);

            return TRUE;
        }

        if (dis->CtlType == ODT_BUTTON)
        {
            HBRUSH hBrush;
            char   text[128];

            if (dis->itemState & ODS_SELECTED)
                hBrush = CreateSolidBrush(RGB(100, 100, 100));
            else if (dis->itemState & ODS_HOTLIGHT)
                hBrush = CreateSolidBrush(RGB(80, 80, 80));
            else
                hBrush = CreateSolidBrush(RGB(50, 50, 50));

            FillRect(dis->hDC, &dis->rcItem, hBrush);
            DeleteObject(hBrush);

            SetTextColor(dis->hDC, RGB(255, 255, 255));
            SetBkMode(dis->hDC, TRANSPARENT);

            GetWindowTextA(dis->hwndItem, text, sizeof(text));
            DrawTextA(dis->hDC, text, -1, &dis->rcItem,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
    }
    break;

    case WM_CTLCOLORSTATIC:
    {
        HDC  hdc   = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;

        if (hCtrl == hPanelColor)
        {
            COLORREF desired = (currentColor == (COLORREF)-1) ? RGB(30, 30, 30) : currentColor;
            if (!s_panelBrush || s_panelBrushColor != desired)
            {
                if (s_panelBrush) DeleteObject(s_panelBrush);
                s_panelBrush = CreateSolidBrush(desired);
                s_panelBrushColor = desired;
            }
            return (INT_PTR)s_panelBrush;
        }

        SetBkColor(hdc, RGB(30, 30, 30));
        SetTextColor(hdc, RGB(255, 255, 255));
        return (INT_PTR)g_hbrBkgnd;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(30, 30, 30));
        SetTextColor(hdc, RGB(255, 255, 255));
        return (INT_PTR)g_hbrBkgnd;
    }

    case WM_KEYDOWN:
    {
        POINT pt;
        int moveStep = 1;
        BOOL handled = FALSE;

        GetCursorPos(&pt);

        switch (wParam)
        {
        case VK_LEFT:  SetCursorPos(pt.x - moveStep, pt.y); pt.x -= moveStep; handled = TRUE; break;
        case VK_RIGHT: SetCursorPos(pt.x + moveStep, pt.y); pt.x += moveStep; handled = TRUE; break;
        case VK_UP:    SetCursorPos(pt.x, pt.y - moveStep); pt.y -= moveStep; handled = TRUE; break;
        case VK_DOWN:  SetCursorPos(pt.x, pt.y + moveStep); pt.y += moveStep; handled = TRUE; break;
        case 'A':      SetCursorPos(pt.x - moveStep, pt.y); pt.x -= moveStep; handled = TRUE; break;
        case 'D':      SetCursorPos(pt.x + moveStep, pt.y); pt.x += moveStep; handled = TRUE; break;
        case 'W':      SetCursorPos(pt.x, pt.y - moveStep); pt.y -= moveStep; handled = TRUE; break;
        case 'S':      SetCursorPos(pt.x, pt.y + moveStep); pt.y += moveStep; handled = TRUE; break;
        }

        if (handled)
        {
            if (startAndStop) UpdateMagnifier(pt);
            return 0;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    case WM_CLOSE:
        s_wasScanningBeforeHide = startAndStop;
        ShowWindow(hWnd, SW_HIDE);
        StopScanning(hWnd);
        return 0;

    case WM_DESTROY:
        SaveSettings(hWnd);
        Shell_NotifyIcon(NIM_DELETE, &nid);
        if (hFont)              DeleteObject(hFont);
        if (hdcMem)             DeleteDC(hdcMem);
        if (hbmMem)             DeleteObject(hbmMem);
        if (hdcMagnifier)       ReleaseDC(hMagnifier, hdcMagnifier);
        if (hdcColorWheelBuffer)DeleteDC(hdcColorWheelBuffer);
        if (hbmColorWheelBuffer)DeleteObject(hbmColorWheelBuffer);
        if (s_panelBrush)     { DeleteObject(s_panelBrush); s_panelBrush = NULL; }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

/* =================== ColorWheelProc =================== */
LRESULT CALLBACK ColorWheelProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        if (!hdcColorWheelBuffer)
        {
            BITMAPINFO bmi = { 0 };
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = COLOR_WHEEL_SIZE;
            bmi.bmiHeader.biHeight      = -COLOR_WHEEL_SIZE;
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            hdcColorWheelBuffer = CreateCompatibleDC(hdc);
            hbmColorWheelBuffer = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS,
                                                   (void **)&s_wheelBits, NULL, 0);
            SelectObject(hdcColorWheelBuffer, hbmColorWheelBuffer);
        }

        {
            int curV = (int)SendMessage(hSliderV, TBM_GETPOS, 0, 0);
            if (s_lastDrawnV != curV)
            {
                RedrawWheelBuffer(curV);
                s_lastDrawnV = curV;
            }
        }

        BitBlt(hdc, 0, 0, COLOR_WHEEL_SIZE, COLOR_WHEEL_SIZE,
               hdcColorWheelBuffer, 0, 0, SRCCOPY);

        {
            int cx = selectorPos.x;
            int cy = selectorPos.y;
            BYTE r = 255 - GetRValue(lastColor);
            BYTE g = 255 - GetGValue(lastColor);
            BYTE b = 255 - GetBValue(lastColor);
            HPEN    hPen       = CreatePen(PS_SOLID, 2, RGB(r, g, b));
            HBRUSH  hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HGDIOBJ hOldPen    = SelectObject(hdc, hPen);
            HGDIOBJ hOldBrush  = SelectObject(hdc, hNullBrush);
            Ellipse(hdc, cx - 5, cy - 5, cx + 5, cy + 5);
            SelectObject(hdc, hOldPen);
            SelectObject(hdc, hOldBrush);
            DeleteObject(hPen);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        isDragging = TRUE;
        SetCapture(hWnd);
        AnimateSelectorPosition(pt);
        selectorPos = targetSelectorPos;
        updatingSliders = TRUE;
        UpdateColorFromWheel();
        updatingSliders = FALSE;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (isDragging)
        {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            AnimateSelectorPosition(pt);
            selectorPos = targetSelectorPos;
            updatingSliders = TRUE;
            UpdateColorFromWheel();
            updatingSliders = FALSE;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (isDragging)
        {
            isDragging = FALSE;
            ReleaseCapture();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

/* =================== RedrawWheelBuffer =================== */
static void RedrawWheelBuffer(int v)
{
    float vf = (float)v / 255.0f;
    DWORD *px = s_wheelBits;
    int x, y;
    if (!px) return;

    for (y = 0; y < COLOR_WHEEL_SIZE; y++)
    {
        float h = 360.0f * (1.0f - (float)y / (COLOR_WHEEL_SIZE - 1));
        for (x = 0; x < COLOR_WHEEL_SIZE; x++)
        {
            float s = (float)x / (COLOR_WHEEL_SIZE - 1);
            BYTE r, g, b;
            HSVtoRGB(h, s, vf, &r, &g, &b);
            px[y * COLOR_WHEEL_SIZE + x] =
                0xFF000000 | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
        }
    }
}

/* =================== UpdateColorInfo =================== */
static void UpdateColorInfo(void)
{
    POINT pt;
    HDC   hdcScreen;
    COLORREF color;
    const int colorThreshold = 5;
    BYTE r, g, b, lastR, lastG, lastB;
    BOOL changed;

    if (!startAndStop) return;

    GetCursorPos(&pt);

    hdcScreen = GetDC(NULL);
    color = GetPixel(hdcScreen, pt.x, pt.y);
    ReleaseDC(NULL, hdcScreen);

    r = GetRValue(color); g = GetGValue(color); b = GetBValue(color);
    lastR = GetRValue(lastColor); lastG = GetGValue(lastColor); lastB = GetBValue(lastColor);

    if (!s_colorInitialized)
    {
        changed = TRUE;
        s_colorInitialized = TRUE;
    }
    else
    {
        changed = (abs(r - lastR) > colorThreshold ||
                   abs(g - lastG) > colorThreshold ||
                   abs(b - lastB) > colorThreshold);
    }

    if (!isDragging && !updatingSliders && changed)
    {
        float h, s, v;
        POINT newSel;

        lastColor   = color;
        targetColor = color;

        AnimateSliderPosition(hSliderR, r);
        AnimateSliderPosition(hSliderG, g);
        AnimateSliderPosition(hSliderB, b);

        RGBtoHSV(r, g, b, &h, &s, &v);
        AnimateSliderPosition(hSliderV, (int)(v * 255));

        newSel.x = (LONG)(s * (COLOR_WHEEL_SIZE - 1));
        newSel.y = (LONG)((1.0f - (h / 360.0f)) * (COLOR_WHEEL_SIZE - 1));
        AnimateSelectorPosition(newSel);

        {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "(%d, %d, %d)", r, g, b);
            SetWindowTextA(hTextBoxColorRGB, buffer);
            snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
            SetWindowTextA(hTextBoxColorHEX, buffer);
        }

        UpdateWindowTitle(g_hMainWnd, color);
    }

    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%ld", (LONG)pt.x);
        SetWindowTextA(hTextBoxCoordinateX, buffer);
        snprintf(buffer, sizeof(buffer), "%ld", (LONG)pt.y);
        SetWindowTextA(hTextBoxCoordinateY, buffer);
    }
}

/* =================== UpdateColorFromSliders =================== */
static void UpdateColorFromSliders(void)
{
    BYTE r = (BYTE)SendMessage(hSliderR, TBM_GETPOS, 0, 0);
    BYTE g = (BYTE)SendMessage(hSliderG, TBM_GETPOS, 0, 0);
    BYTE b = (BYTE)SendMessage(hSliderB, TBM_GETPOS, 0, 0);
    float h, s, v;
    char buffer[64];

    lastColor = targetColor = currentColor = RGB(r, g, b);

    RGBtoHSV(r, g, b, &h, &s, &v);
    SendMessage(hSliderV, TBM_SETPOS, TRUE, (LPARAM)(v * 255));
    selectorPos.x = (LONG)(s * (COLOR_WHEEL_SIZE - 1));
    selectorPos.y = (LONG)((1.0f - (h / 360.0f)) * (COLOR_WHEEL_SIZE - 1));
    targetSelectorPos = selectorPos;

    snprintf(buffer, sizeof(buffer), "(%d, %d, %d)", r, g, b);
    SetWindowTextA(hTextBoxColorRGB, buffer);
    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
    SetWindowTextA(hTextBoxColorHEX, buffer);

    InvalidateRect(hPanelColor, NULL, TRUE);
    InvalidateRect(hColorWheel, NULL, FALSE);
    UpdateWindowTitle(g_hMainWnd, lastColor);
}

/* =================== UpdateColorFromWheel =================== */
static void UpdateColorFromWheel(void)
{
    float s = (float)targetSelectorPos.x / (COLOR_WHEEL_SIZE - 1);
    float h = 360.0f * (1.0f - (float)targetSelectorPos.y / (COLOR_WHEEL_SIZE - 1));
    float v = (float)SendMessage(hSliderV, TBM_GETPOS, 0, 0) / 255.0f;
    BYTE  r, g, b;
    char  buffer[64];

    selectorPos = targetSelectorPos;

    HSVtoRGB(h, s, v, &r, &g, &b);
    lastColor = targetColor = currentColor = RGB(r, g, b);

    AnimateSliderPosition(hSliderR, r);
    AnimateSliderPosition(hSliderG, g);
    AnimateSliderPosition(hSliderB, b);

    snprintf(buffer, sizeof(buffer), "(%d, %d, %d)", r, g, b);
    SetWindowTextA(hTextBoxColorRGB, buffer);
    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
    SetWindowTextA(hTextBoxColorHEX, buffer);

    InvalidateRect(hPanelColor, NULL, TRUE);
    InvalidateRect(hColorWheel, NULL, FALSE);
    UpdateWindowTitle(g_hMainWnd, lastColor);
}

/* =================== HSV <-> RGB =================== */
static void RGBtoHSV(BYTE r, BYTE g, BYTE b, float* h, float* s, float* v)
{
    float R = r / 255.0f, G = g / 255.0f, B = b / 255.0f;
    float cmax = max(max(R, G), B);
    float cmin = min(min(R, G), B);
    float delta = cmax - cmin;

    if (delta == 0)        *h = 0;
    else if (cmax == R)    *h = 60.0f * fmodf(((G - B) / delta), 6);
    else if (cmax == G)    *h = 60.0f * (((B - R) / delta) + 2);
    else                   *h = 60.0f * (((R - G) / delta) + 4);

    if (*h < 0) *h += 360;
    *s = (cmax == 0) ? 0 : delta / cmax;
    *v = cmax;
}

static void HSVtoRGB(float h, float s, float v, BYTE* r, BYTE* g, BYTE* b)
{
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    float r1, g1, b1;

    if      (h <  60) { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else              { r1 = c; g1 = 0; b1 = x; }

    *r = (BYTE)((r1 + m) * 255);
    *g = (BYTE)((g1 + m) * 255);
    *b = (BYTE)((b1 + m) * 255);
}

/* =================== Magnifier =================== */
static void CreateMagnifier(void)
{
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc   = DefWindowProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = "MagnifierClass";
    RegisterClass(&wc);

    hMagnifier = CreateWindowExA(
        WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "MagnifierClass", "Magnifier",
        WS_POPUP,
        0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    SetWindowPos(hMagnifier, HWND_TOPMOST, 0, 0,
                 MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT,
                 SWP_NOMOVE | SWP_NOACTIVATE);

    {
        HDC hdcScreen = GetDC(NULL);
        hdcMem        = CreateCompatibleDC(hdcScreen);
        hbmMem        = CreateCompatibleBitmap(hdcScreen, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT);
        hdcMagnifier  = GetDC(hMagnifier);
        ReleaseDC(NULL, hdcScreen);
    }

    SetStretchBltMode(hdcMagnifier, HALFTONE);
    SetBrushOrgEx(hdcMagnifier, 0, 0, NULL);

    QueryPerformanceFrequency(&frequency);
    lastUpdateTime.QuadPart = 0;
}

static void UpdateMagnifier(POINT pt)
{
    LARGE_INTEGER currentTime;
    double elapsedMs;
    int    srcSize = MAGNIFIER_WIDTH / MAGNIFICATION_FACTOR;
    int    sourceX, sourceY, mx, my;
    HMONITOR hMon;
    MONITORINFO mi;
    HDC hdcScreen;
    HGDIOBJ hOldBmp;
    COLORREF borderColor;
    HPEN hPen, hBorderPen;
    HGDIOBJ hOldPen;
    HBRUSH hNullBrush;

    QueryPerformanceCounter(&currentTime);
    elapsedMs = (double)(currentTime.QuadPart - lastUpdateTime.QuadPart) * 1000.0 / frequency.QuadPart;
    if (elapsedMs < 16.67) return;
    lastUpdateTime = currentTime;

    if (!hdcMem || !hbmMem || !hdcMagnifier) return;

    hdcScreen = GetDC(NULL);
    hOldBmp   = SelectObject(hdcMem, hbmMem);

    sourceX = pt.x - srcSize / 2;
    sourceY = pt.y - srcSize / 2;

    BitBlt(hdcMem, 0, 0, srcSize, srcSize, hdcScreen, sourceX, sourceY, SRCCOPY);
    StretchBlt(hdcMagnifier, 0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT,
               hdcMem, 0, 0, srcSize, srcSize, SRCCOPY);

    borderColor = RGB(255 - GetRValue(lastColor),
                      255 - GetGValue(lastColor),
                      255 - GetBValue(lastColor));

    hPen    = CreatePen(PS_SOLID, 1, borderColor);
    hOldPen = SelectObject(hdcMagnifier, hPen);
    MoveToEx(hdcMagnifier, MAGNIFIER_WIDTH / 2 - 5, MAGNIFIER_HEIGHT / 2, NULL);
    LineTo  (hdcMagnifier, MAGNIFIER_WIDTH / 2 + 5, MAGNIFIER_HEIGHT / 2);
    MoveToEx(hdcMagnifier, MAGNIFIER_WIDTH / 2, MAGNIFIER_HEIGHT / 2 - 5, NULL);
    LineTo  (hdcMagnifier, MAGNIFIER_WIDTH / 2, MAGNIFIER_HEIGHT / 2 + 5);

    hBorderPen = CreatePen(PS_SOLID, 2, borderColor);
    hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    SelectObject(hdcMagnifier, hBorderPen);
    SelectObject(hdcMagnifier, hNullBrush);
    Rectangle(hdcMagnifier, 0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT);

    SelectObject(hdcMagnifier, hOldPen);
    DeleteObject(hPen);
    DeleteObject(hBorderPen);

    SelectObject(hdcMem, hOldBmp);
    ReleaseDC(NULL, hdcScreen);

    hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(hMon, &mi);

    mx = pt.x + 10;
    my = pt.y + 10;
    if (mx + MAGNIFIER_WIDTH  > mi.rcMonitor.right)  mx = pt.x - MAGNIFIER_WIDTH  - 10;
    if (my + MAGNIFIER_HEIGHT > mi.rcMonitor.bottom) my = pt.y - MAGNIFIER_HEIGHT - 10;

    SetWindowPos(hMagnifier, HWND_TOPMOST, mx, my,
                 MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT,
                 SWP_NOACTIVATE | SWP_NOSIZE);
}

/* =================== Hook =================== */
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    static POINT lastPt = { -1, -1 };

    if (nCode >= 0 && wParam == WM_MOUSEMOVE && startAndStop)
    {
        MSLLHOOKSTRUCT* hs = (MSLLHOOKSTRUCT*)lParam;
        POINT pt = hs->pt;
        if (abs(pt.x - lastPt.x) >= 2 || abs(pt.y - lastPt.y) >= 2)
        {
            UpdateMagnifier(pt);
            lastPt = pt;
        }
    }
    return CallNextHookEx(mouseHook, nCode, wParam, lParam);
}

/* =================== About =================== */
static void ShowAboutDialog(HWND hwnd)
{
    if (hAboutWnd)
    {
        SetForegroundWindow(hAboutWnd);
        return;
    }

    hAboutWnd = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TOPMOST,
        "AboutWindowClass",
        "About Color Picker",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 260,
        hwnd, NULL, GetModuleHandle(NULL), NULL);

    SetLayeredWindowAttributes(hAboutWnd, 0, 240, LWA_ALPHA);

    {
        HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
        SendMessage(hAboutWnd, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
        SendMessage(hAboutWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }

    ShowWindow(hAboutWnd, SW_SHOW);
    UpdateWindow(hAboutWnd);
}

/* =================== Scanning =================== */
static void StopScanning(HWND hWnd)
{
    KillTimer(hWnd, timerId);
    timerId = 0;

    if (mouseHook)
    {
        UnhookWindowsHookEx(mouseHook);
        mouseHook = NULL;
    }

    SetWindowTextA(hBtnStartAndStop, "Start scan (F8)");
    ShowWindow(hMagnifier, SW_HIDE);
    InvalidateRect(hBtnStartAndStop, NULL, TRUE);
    startAndStop = FALSE;
    s_paused = FALSE;
    SetWindowTextA(hBtnPick, "Pick (F9)");
    InvalidateRect(hBtnPick, NULL, TRUE);
}

static void StartScanning(HWND hWnd)
{
    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(NULL), 0);
    if (!mouseHook)
    {
        MessageBoxA(hWnd, "Failed to install mouse hook!", "Error", MB_ICONERROR);
        return;
    }

    timerId = SetTimer(hWnd, TIMER_ID, 16, NULL);
    SetWindowTextA(hBtnStartAndStop, "Stop scan (F8)");
    ShowWindow(hMagnifier, SW_SHOW);
    InvalidateRect(hBtnStartAndStop, NULL, TRUE);
    startAndStop = TRUE;
    s_colorInitialized = FALSE;
    s_paused = FALSE;
    SetWindowTextA(hBtnPick, "Pick (F9)");
    InvalidateRect(hBtnPick, NULL, TRUE);
}

/* =================== Clipboard =================== */
static void CopyTextToClipboard(HWND hWnd, const char* text)
{
    HGLOBAL hMem;
    char   *memPtr;
    size_t  len;

    if (!OpenClipboard(hWnd)) return;
    EmptyClipboard();

    len  = strlen(text);
    hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
    if (!hMem) { CloseClipboard(); return; }

    memPtr = (char *)GlobalLock(hMem);
    if (!memPtr) { GlobalFree(hMem); CloseClipboard(); return; }

    memcpy(memPtr, text, len + 1);
    GlobalUnlock(hMem);

    if (!SetClipboardData(CF_TEXT, hMem))
        GlobalFree(hMem);

    CloseClipboard();
}

static void CopyFeedback(HWND hWnd, HWND hBtn)
{
    SetWindowTextA(hBtn, "Copied!");
    InvalidateRect(hBtn, NULL, TRUE);
    SetTimer(hWnd, TIMER_COPY_FEEDBACK, 1000, NULL);
}

/* =================== AboutWndProc (no link) =================== */
LRESULT CALLBACK AboutWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND  hIconStatic, hLabelTitle, hLabelVersion, hLabelNickname, hLabelHint, hBtnClose;
    static HFONT hTitleFont, hAboutFont, hButtonFont;

    switch (message)
    {
    case WM_CREATE:
    {
        RECT clientRect;
        int  width;
        GetClientRect(hWnd, &clientRect);
        width = clientRect.right - clientRect.left;

        hTitleFont = CreateFontA(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        hAboutFont = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        hButtonFont = CreateFontA(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        hIconStatic = CreateWindowA("STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ICON,
            width / 2 - 24, 15, 48, 48, hWnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hIconStatic, STM_SETICON,
                    (WPARAM)LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1)), 0);

        hLabelTitle = CreateWindowA("STATIC", "Color Picker",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 70, width, 30, hWnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hLabelTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

        hLabelVersion = CreateWindowA("STATIC", "Version " VERSION,
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 105, width, 25, hWnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hLabelVersion, WM_SETFONT, (WPARAM)hAboutFont, TRUE);

        hLabelNickname = CreateWindowA("STATIC", "Developed by Taillogs",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 130, width, 25, hWnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hLabelNickname, WM_SETFONT, (WPARAM)hAboutFont, TRUE);
        SetWindowLongPtr(hLabelNickname, GWLP_USERDATA, RGB(255, 255, 255));

        hLabelHint = CreateWindowA("STATIC",
            "Hotkeys: F8 = scan on/off    F9 = pick/resume",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 165, width, 20, hWnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hLabelHint, WM_SETFONT, (WPARAM)hAboutFont, TRUE);

        hBtnClose = CreateWindowA("BUTTON", "Close",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            width / 2 - 50, 200, 100, 35,
            hWnd, (HMENU)IDC_ABOUT_CLOSE, GetModuleHandle(NULL), NULL);
        SendMessage(hBtnClose, WM_SETFONT, (WPARAM)hButtonFont, TRUE);

        SetTimer(hWnd, ABOUT_TIMER_ID, 50, NULL);
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rect;
        TRIVERTEX v[2];
        GRADIENT_RECT gr;

        GetClientRect(hWnd, &rect);

        v[0].x = rect.left;   v[0].y = rect.top;
        v[0].Red = 0x1E00; v[0].Green = 0x1E00; v[0].Blue = 0x1E00; v[0].Alpha = 0xFF00;
        v[1].x = rect.right;  v[1].y = rect.bottom;
        v[1].Red = 0x3C00; v[1].Green = 0x3C00; v[1].Blue = 0x3C00; v[1].Alpha = 0xFF00;
        gr.UpperLeft = 0; gr.LowerRight = 1;
        GradientFill(hdc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);

        EndPaint(hWnd, &ps);
    }
    break;

    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        if (wParam == ABOUT_TIMER_ID)
        {
            float s = 1.0f, v = 1.0f;
            float c, x, m, r1, g1, b1;
            int   r, g, b;

            hue = (hue + 5) % 360;
            c = v * s;
            x = c * (1 - fabsf(fmodf(hue / 60.0f, 2) - 1));
            m = v - c;

            if      (hue <  60) { r1 = c; g1 = x; b1 = 0; }
            else if (hue < 120) { r1 = x; g1 = c; b1 = 0; }
            else if (hue < 180) { r1 = 0; g1 = c; b1 = x; }
            else if (hue < 240) { r1 = 0; g1 = x; b1 = c; }
            else if (hue < 300) { r1 = x; g1 = 0; b1 = c; }
            else                { r1 = c; g1 = 0; b1 = x; }

            r = (int)((r1 + m) * 255);
            g = (int)((g1 + m) * 255);
            b = (int)((b1 + m) * 255);

            SetWindowLongPtr(hLabelNickname, GWLP_USERDATA, RGB(r, g, b));
            InvalidateRect(hLabelNickname, NULL, TRUE);
        }
        break;

    case WM_CTLCOLORSTATIC:
    {
        HDC  hdc   = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;

        SetBkMode(hdc, TRANSPARENT);
        if (hCtrl == hLabelNickname)
            SetTextColor(hdc, (COLORREF)GetWindowLongPtr(hCtrl, GWLP_USERDATA));
        else
            SetTextColor(hdc, RGB(255, 255, 255));
        return (INT_PTR)GetStockObject(NULL_BRUSH);
    }

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_ABOUT_CLOSE)
        {
            HBRUSH  hBrush;
            HPEN    hPen;
            HGDIOBJ hOldPen, hOldBrush;

            if (dis->itemState & ODS_SELECTED)
                hBrush = CreateSolidBrush(RGB(80, 80, 80));
            else if (dis->itemState & ODS_HOTLIGHT)
                hBrush = CreateSolidBrush(RGB(60, 60, 60));
            else
                hBrush = CreateSolidBrush(RGB(50, 50, 50));

            hPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
            hOldPen   = SelectObject(dis->hDC, hPen);
            hOldBrush = SelectObject(dis->hDC, hBrush);

            RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                      dis->rcItem.right, dis->rcItem.bottom, 10, 10);

            SetTextColor(dis->hDC, RGB(255, 255, 255));
            SetBkMode(dis->hDC, TRANSPARENT);
            DrawTextA(dis->hDC, "Close", -1, &dis->rcItem,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(dis->hDC, hOldPen);
            SelectObject(dis->hDC, hOldBrush);
            DeleteObject(hPen);
            DeleteObject(hBrush);
        }
    }
    break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_ABOUT_CLOSE)
            DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        KillTimer(hWnd, ABOUT_TIMER_ID);
        if (hTitleFont)  DeleteObject(hTitleFont);
        if (hAboutFont)  DeleteObject(hAboutFont);
        if (hButtonFont) DeleteObject(hButtonFont);
        SendMessage(hIconStatic, STM_SETICON, (WPARAM)NULL, 0);
        hAboutWnd = NULL;
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

/* =================== Animations =================== */
static void AnimateSliderPosition(HWND hSlider, int targetPos)
{
    int currentPos = (int)SendMessage(hSlider, TBM_GETPOS, 0, 0);
    int newPos = (abs(currentPos - targetPos) > 1)
                 ? currentPos + (targetPos - currentPos) / 5
                 : targetPos;

    SendMessage(hSlider, TBM_SETPOS, TRUE, (LPARAM)newPos);
    InvalidateRect(hSlider, NULL, FALSE);

    if (hSlider == hSliderV)
        InvalidateRect(hColorWheel, NULL, FALSE);
}

static void AnimateSelectorPosition(POINT newPos)
{
    targetSelectorPos.x = max(0, min(COLOR_WHEEL_SIZE - 1, newPos.x));
    targetSelectorPos.y = max(0, min(COLOR_WHEEL_SIZE - 1, newPos.y));

    if (abs(selectorPos.x - targetSelectorPos.x) < 2 &&
        abs(selectorPos.y - targetSelectorPos.y) < 2)
        selectorPos = targetSelectorPos;
}