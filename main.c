#include <windows.h>
#include <stdio.h>
#include "resource.h"

#define VERSION "2.0.0"

#define TIMER_ID 1
#define MAGNIFIER_WIDTH 150
#define MAGNIFIER_HEIGHT 150
#define MAGNIFICATION_FACTOR 10

// Глобальные переменные
HWND hTextBoxCoordinateX, hTextBoxCoordinateY, hTextBoxColorRGB, hTextBoxColorHEX, hPanelColor, hBtnStartAndStop, hMagnifier;
HWND hLabelCoordinateX, hLabelCoordinateY, hLabelColorRGB, hLabelColorHEX, hBtnCopyCoordinates, hBtnCopyRGB, hBtnCopyHEX;
BOOL startAndStop = TRUE;
COLORREF lastColor = -1; // Изначально не определен
HHOOK mouseHook;
NOTIFYICONDATA nid; // Для работы с треем
HFONT hFont;
UINT_PTR timerId;

// Прототипы функций
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void UpdateColorInfo();
void CreateMagnifier();
void UpdateMagnifier(POINT pt);
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
void ShowAboutDialog(HWND);
void StopScanning(HWND);
void StartScanning(HWND);
void CopyTextToClipboard(HWND hWnd, const char* text);

// Точка входа
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "ColorPickerClass";
    wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 30)); // Темный фон
    RegisterClass(&wc);

    HWND hWnd = CreateWindow("ColorPickerClass", "Color Picker", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 237, 386, NULL, NULL, hInstance, NULL);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    CreateMagnifier(); // Создаем лупу

    // Устанавливаем глобальный хук мыши
    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, hInstance, 0);

    // Скрываем консольное окно
    HWND myConsole = GetConsoleWindow();
    ShowWindow(myConsole, 0);

    // Настройка трея
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    nid.uCallbackMessage = WM_USER + 1;
    strcpy(nid.szTip, "Color Picker");
    Shell_NotifyIcon(NIM_ADD, &nid);

	SetFocus(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Удаляем хук мыши перед выходом
    UnhookWindowsHookEx(mouseHook);
    Shell_NotifyIcon(NIM_DELETE, &nid);

    return 0;
}

// Обработчик сообщений
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        // Создаем меню
        HMENU hMenu = LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MYMENU));
        SetMenu(hWnd, hMenu);

        // Создаем шрифт
        hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        // Создаем элементы управления
        hLabelCoordinateX = CreateWindow("STATIC", "X:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 10, 20, 20, hWnd, NULL, NULL, NULL);
        hTextBoxCoordinateX = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_CENTER, 40, 10, 100, 20, hWnd, NULL, NULL, NULL);
        hLabelCoordinateY = CreateWindow("STATIC", "Y:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 40, 20, 20, hWnd, NULL, NULL, NULL);
        hTextBoxCoordinateY = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_CENTER, 40, 40, 100, 20, hWnd, NULL, NULL, NULL);
        hBtnCopyCoordinates = CreateWindow("BUTTON", "Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 150, 10, 70, 50, hWnd, (HMENU)2, NULL, NULL);

        hLabelColorRGB = CreateWindow("STATIC", "RGB:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 70, 40, 20, hWnd, NULL, NULL, NULL);
        hTextBoxColorRGB = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_CENTER, 60, 70, 160, 20, hWnd, NULL, NULL, NULL);
        hBtnCopyRGB = CreateWindow("BUTTON", "Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 100, 210, 20, hWnd, (HMENU)3, NULL, NULL);

        hLabelColorHEX = CreateWindow("STATIC", "HEX:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 130, 40, 20, hWnd, NULL, NULL, NULL);
        hTextBoxColorHEX = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_CENTER, 60, 130, 160, 20, hWnd, NULL, NULL, NULL);
        hBtnCopyHEX = CreateWindow("BUTTON", "Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 160, 210, 20, hWnd, (HMENU)4, NULL, NULL);

        hPanelColor = CreateWindowEx(WS_EX_CLIENTEDGE, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_SUNKEN, 10, 190, 210, 100, hWnd, NULL, NULL, NULL);
        hBtnStartAndStop = CreateWindow("BUTTON", "Start scanning flowers (Key -> P)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 300, 210, 30, hWnd, (HMENU)1, NULL, NULL);

        // Применяем шрифт и стиль к элементам управления
        SendMessage(hLabelCoordinateX, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hTextBoxCoordinateX, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hLabelCoordinateY, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hTextBoxCoordinateY, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnCopyCoordinates, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hLabelColorRGB, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hTextBoxColorRGB, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnCopyRGB, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hLabelColorHEX, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hTextBoxColorHEX, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnCopyHEX, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnStartAndStop, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Создаем таймер
        timerId = SetTimer(hWnd, TIMER_ID, 100, NULL);
    }
    break;

    case WM_TIMER:
        UpdateColorInfo();
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case 1: // ID кнопки Start/Stop
			if (startAndStop)
			{
				StopScanning(hWnd);
			}
			else
			{
				StartScanning(hWnd);
			}
            break;
        case 2: // ID кнопки Copy координат
        {
            char buffer[256];
            GetWindowText(hTextBoxCoordinateX, buffer, sizeof(buffer));
            strcat(buffer, ", ");
            GetWindowText(hTextBoxCoordinateY, buffer + strlen(buffer), sizeof(buffer) - strlen(buffer));
            CopyTextToClipboard(hWnd, buffer);
        }
        break;
        case 3: // ID кнопки Copy RGB
        {
            char buffer[256];
            GetWindowText(hTextBoxColorRGB, buffer, sizeof(buffer));
            CopyTextToClipboard(hWnd, buffer);
        }
        break;
        case 4: // ID кнопки Copy HEX
        {
            char buffer[256];
            GetWindowText(hTextBoxColorHEX, buffer, sizeof(buffer));
            CopyTextToClipboard(hWnd, buffer);
        }
        break;
        case ID_FILE_EXIT:
            Shell_NotifyIcon(NIM_DELETE, &nid); // Удаляем иконку трея
            PostQuitMessage(0); // Завершаем приложение
            break;
        case ID_HELP_ABOUT:
            ShowAboutDialog(hWnd);
            break;
        case ID_TRAY_SHOW:
            ShowWindow(hWnd, SW_RESTORE); // Восстанавливаем окно
            SetForegroundWindow(hWnd); // Активируем окно
            break;
        }
        break;

	case WM_USER + 1: // Сообщение от трея
		if (lParam == WM_RBUTTONUP)
		{
			HMENU hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING, ID_TRAY_SHOW, "Show");
			AppendMenu(hMenu, MF_STRING, ID_FILE_EXIT, "Exit");
			POINT pt;
			GetCursorPos(&pt);
			SetForegroundWindow(hWnd); // Устанавливаем фокус на главное окно
			TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
			DestroyMenu(hMenu);
		}
		else if (lParam == WM_LBUTTONDBLCLK)
		{
			ShowWindow(hWnd, SW_RESTORE); // Восстанавливаем окно
			SetForegroundWindow(hWnd); // Устанавливаем фокус на главное окно
		}
		break;

    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE); // Скрываем окно вместо закрытия
        StopScanning(hWnd); // Останавливаем сканирование при скрытии окна
        return 0;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid); // Удаляем иконку трея
        DeleteObject(hFont); // Удаляем шрифт
        PostQuitMessage(0);
        break;
	
	case WM_KEYDOWN:
        if (wParam == 0x50) {
			if (startAndStop)
			{
				StopScanning(hWnd);
			}
			else
			{
				StartScanning(hWnd);
				SetFocus(hWnd);
			}
        }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void UpdateColorInfo()
{
    POINT pt;
    GetCursorPos(&pt); // Получаем позицию курсора на экране

    // Обновляем информацию о цвете
    HDC hdcScreen = GetDC(NULL);
    COLORREF color = GetPixel(hdcScreen, pt.x, pt.y);
    ReleaseDC(NULL, hdcScreen);

    if (color != lastColor)
    {
        lastColor = color;

        BYTE r = GetRValue(color);
        BYTE g = GetGValue(color);
        BYTE b = GetBValue(color);

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "(%ld, %ld, %d)", (LONG)pt.x, (LONG)pt.y, r);
        SetWindowText(hTextBoxColorRGB, buffer);

        snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
        SetWindowText(hTextBoxColorHEX, buffer);

        HBRUSH hBrush = CreateSolidBrush(color);
        HDC hdcPanel = GetDC(hPanelColor);
        RECT rect;
        GetClientRect(hPanelColor, &rect);
        FillRect(hdcPanel, &rect, hBrush);
        ReleaseDC(hPanelColor, hdcPanel);
        DeleteObject(hBrush);
    }

    // Обновляем координаты курсора
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%ld", (LONG)pt.x);
    SetWindowText(hTextBoxCoordinateX, buffer);
    snprintf(buffer, sizeof(buffer), "%ld", (LONG)pt.y);
    SetWindowText(hTextBoxCoordinateY, buffer);
}

void CreateMagnifier()
{
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = DefWindowProc; // Используем дефолтный обработчик для лупы
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "MagnifierClass";
    RegisterClass(&wc);

    hMagnifier = CreateWindow("MagnifierClass", "Magnifier", WS_POPUP | WS_BORDER | WS_VISIBLE,
                              0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT, NULL, NULL, GetModuleHandle(NULL), NULL);
    SetWindowPos(hMagnifier, HWND_TOPMOST, 0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT, SWP_NOMOVE | SWP_NOACTIVATE);
}

void UpdateMagnifier(POINT pt)
{
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcScreen, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hbmMem);

    // Выбираем область вокруг курсора
    int sourceX = pt.x - (MAGNIFIER_WIDTH / 2 / MAGNIFICATION_FACTOR);
    int sourceY = pt.y - (MAGNIFIER_HEIGHT / 2 / MAGNIFICATION_FACTOR);

    // Захватываем область экрана в контексте устройства
    BitBlt(hdcMem, 0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT, hdcScreen, sourceX, sourceY, SRCCOPY);

    // Масштабируем изображение
    HDC hdcMagnifier = GetDC(hMagnifier);
    StretchBlt(hdcMagnifier, 0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT, hdcMem, 0, 0, MAGNIFIER_WIDTH / MAGNIFICATION_FACTOR, MAGNIFIER_HEIGHT / MAGNIFICATION_FACTOR, SRCCOPY);

    ReleaseDC(hMagnifier, hdcMagnifier);
    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    // Перемещаем лупу рядом с курсором
    SetWindowPos(hMagnifier, HWND_TOPMOST, pt.x + 10, pt.y + 10, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT, SWP_NOACTIVATE | SWP_NOSIZE);
}

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && wParam == WM_MOUSEMOVE && startAndStop)
    {
        MSLLHOOKSTRUCT* hookStruct = (MSLLHOOKSTRUCT*)lParam;
        POINT pt = hookStruct->pt;
        UpdateMagnifier(pt);
    }
    return CallNextHookEx(mouseHook, nCode, wParam, lParam);
}

void ShowAboutDialog(HWND hwnd)
{
    char message[256];
    sprintf(message, "Color Picker %s\nA color picker tool.\nDeveloper - Taillogs.", VERSION);
    MessageBox(hwnd, message, "About Color Picker", MB_OK | MB_ICONINFORMATION);
}

void StopScanning(HWND hWnd)
{
    KillTimer(hWnd, timerId); // Используем глобальный timerId

    // Удаляем глобальный хук мыши
    if (mouseHook != NULL)
    {
        UnhookWindowsHookEx(mouseHook);
        mouseHook = NULL;
    }

    SetWindowText(hBtnStartAndStop, "Start scanning flowers (Key -> P)");
    ShowWindow(hMagnifier, SW_HIDE);
    startAndStop = FALSE;
}

void StartScanning(HWND hWnd)
{
    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(NULL), 0);
    if (mouseHook == NULL) {
        MessageBox(hWnd, "Failed to install mouse hook!", "Error", MB_ICONERROR);
        return;
    }

    timerId = SetTimer(hWnd, TIMER_ID, 100, NULL);

    SetWindowText(hBtnStartAndStop, "Stop scanning flowers (Key -> P)");
    ShowWindow(hMagnifier, SW_SHOW);
    startAndStop = TRUE;
}

void CopyTextToClipboard(HWND hWnd, const char* text)
{
    if (OpenClipboard(hWnd))
    {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, strlen(text) + 1);
        if (hMem)
        {
            char* memPtr = (char*)GlobalLock(hMem);
            if (memPtr)
            {
                strcpy(memPtr, text);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
        }
        CloseClipboard();
    }
}