#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <commctrl.h>
#include "resource.h" // Убедитесь, что этот файл существует и содержит необходимые определения

#define VERSION "2.0.0"

#define TIMER_ID 1
#define MAGNIFIER_WIDTH 150
#define MAGNIFIER_HEIGHT 150
#define MAGNIFICATION_FACTOR 10
#define WM_RESTORE_APP (WM_USER + 2)
#define COLOR_WHEEL_SIZE 180 // Размер цветового круга
#define IDC_COLOR_WHEEL 1006
#define IDC_SLIDER_V 1007

// Slider control IDs
#define IDC_SLIDER_R 1003
#define IDC_SLIDER_G 1004
#define IDC_SLIDER_B 1005

// Глобальные переменные
HWND hTextBoxCoordinateX, hTextBoxCoordinateY, hTextBoxColorRGB, hTextBoxColorHEX, hPanelColor, hBtnStartAndStop, hMagnifier;
HWND hLabelCoordinateX, hLabelCoordinateY, hLabelColorRGB, hLabelColorHEX, hBtnCopyCoordinates, hBtnCopyRGB, hBtnCopyHEX;
HWND hLabelR, hLabelG, hLabelB, hSliderR, hSliderG, hSliderB, hColorWheel, hLabelV, hSliderV;
BOOL startAndStop = TRUE;
BOOL updatingSliders = FALSE; // Флаг для предотвращения зацикливания обновлений
COLORREF lastColor = (COLORREF)-1; // Исправлено: приведение -1 к COLORREF
COLORREF targetColor = (COLORREF)-1; // Исправлено: приведение -1 к COLORREF
COLORREF currentColor = (COLORREF)-1; // Исправлено: приведение -1 к COLORREF
HHOOK mouseHook;
NOTIFYICONDATA nid;
HFONT hFont;
UINT_PTR timerId;
HDC hdcMem = NULL;
HBITMAP hbmMem = NULL;
HDC hdcMagnifier = NULL;
LARGE_INTEGER lastUpdateTime;
LARGE_INTEGER frequency;

HWND hAboutWnd = NULL;
UINT_PTR aboutTimerId = 0;
int hue = 0;
#define IDC_ABOUT_CLOSE 1001
#define IDC_ABOUT_LINK 1002
#define ABOUT_TIMER_ID 2

// Переменные для цветового круга
static BOOL isDragging = FALSE;
static POINT selectorPos = { COLOR_WHEEL_SIZE / 2, COLOR_WHEEL_SIZE / 2 };
static POINT targetSelectorPos = { COLOR_WHEEL_SIZE / 2, COLOR_WHEEL_SIZE / 2 }; // Целевая позиция селектора
static HBITMAP hbmColorWheelBuffer = NULL; // Буфер для двойной буферизации
static HDC hdcColorWheelBuffer = NULL;     // Контекст устройства для буфера

// Прототипы функций
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ColorWheelProc(HWND, UINT, WPARAM, LPARAM);
void UpdateColorInfo();
void CreateMagnifier();
void UpdateMagnifier(POINT pt);
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
void ShowAboutDialog(HWND);
void StopScanning(HWND);
void StartScanning(HWND);
void CopyTextToClipboard(HWND hWnd, const char* text);
LRESULT CALLBACK AboutWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void UpdateColorFromSliders(void);
void UpdateColorFromWheel(void);
void RGBtoHSV(BYTE r, BYTE g, BYTE b, float* h, float* s, float* v);
void HSVtoRGB(float h, float s, float v, BYTE* r, BYTE* g, BYTE* b);
void DrawColorWheelContent(HDC hdc, int width, int height); // Отдельная функция для рисования содержимого круга
void AnimateColorTransition(COLORREF newColor); // Функция для запуска анимации цвета
void AnimateSliderPosition(HWND hSlider, int targetPos); // Функция для запуска анимации ползунка
void AnimateSelectorPosition(POINT newPos); // Функция для запуска анимации селектора

// Точка входа
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    FreeConsole();
    (void)hPrevInstance;
    (void)lpCmdLine;

    // Инициализация общих элементов управления
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    // Создаем мьютекс для проверки единственного экземпляра
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "ColorPickerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND hExistingWnd = FindWindowA("ColorPickerClass", NULL);
        if (hExistingWnd)
        {
            PostMessage(hExistingWnd, WM_RESTORE_APP, 0, 0);
        }
        CloseHandle(hMutex);
        return 0;
    }

    // Регистрация класса главного окна
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "ColorPickerClass";
    wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 30));
    RegisterClass(&wc);

    // Регистрация класса окна About
    WNDCLASS wcAbout = { 0 };
    wcAbout.lpfnWndProc = AboutWndProc;
    wcAbout.hInstance = hInstance;
    wcAbout.lpszClassName = "AboutWindowClass";
    wcAbout.hbrBackground = CreateSolidBrush(RGB(30, 30, 30));
    RegisterClass(&wcAbout);

    // Регистрация класса цветового круга
    WNDCLASS wcColorWheel = { 0 };
    wcColorWheel.lpfnWndProc = ColorWheelProc;
    wcColorWheel.hInstance = hInstance;
    wcColorWheel.lpszClassName = "ColorWheelClass";
    wcColorWheel.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // Будет перерисовываться полностью
    RegisterClass(&wcColorWheel);

    // Загружаем меню
    HMENU hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_MYMENU));

    // Увеличиваем высоту окна для размещения всех элементов
    HWND hWnd = CreateWindowA("ColorPickerClass", "Color Picker",
        WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX),
        CW_USEDEFAULT, CW_USEDEFAULT, 237, 750, // Увеличена высота окна
        NULL, hMenu, hInstance, NULL);

    // Устанавливаем иконку
    HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    CreateMagnifier();
    StartScanning(hWnd);
    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, hInstance, 0);
    HWND myConsole = GetConsoleWindow();
    ShowWindow(myConsole, 0);

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

    UnhookWindowsHookEx(mouseHook);
    Shell_NotifyIcon(NIM_DELETE, &nid);
    CloseHandle(hMutex);

    return 0;
}

// Обработчик сообщений главного окна
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HBRUSH hbrBkgnd = NULL;

    switch (message)
    {
    case WM_CREATE:
    {
        HMENU hMenu = LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MYMENU));
        SetMenu(hWnd, hMenu);

        hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        hbrBkgnd = CreateSolidBrush(RGB(30, 30, 30));

        // Координаты
        hLabelCoordinateX = CreateWindowA("STATIC", "X:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 10, 20, 20, hWnd, NULL, NULL, NULL);
        hTextBoxCoordinateX = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_CENTER | ES_READONLY, 40, 10, 100, 20, hWnd, NULL, NULL, NULL);
        hLabelCoordinateY = CreateWindowA("STATIC", "Y:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 40, 20, 20, hWnd, NULL, NULL, NULL);
        hTextBoxCoordinateY = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_CENTER | ES_READONLY, 40, 40, 100, 20, hWnd, NULL, NULL, NULL);
        hBtnCopyCoordinates = CreateWindowA("BUTTON", "Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 150, 10, 70, 50, hWnd, (HMENU)2, NULL, NULL);

        // RGB
        hLabelColorRGB = CreateWindowA("STATIC", "RGB:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 70, 40, 20, hWnd, NULL, NULL, NULL);
        hTextBoxColorRGB = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_CENTER | ES_READONLY, 60, 70, 160, 20, hWnd, NULL, NULL, NULL);
        hBtnCopyRGB = CreateWindowA("BUTTON", "Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 10, 100, 210, 20, hWnd, (HMENU)3, NULL, NULL);

        // HEX
        hLabelColorHEX = CreateWindowA("STATIC", "HEX:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 130, 40, 20, hWnd, NULL, NULL, NULL);
        hTextBoxColorHEX = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_CENTER | ES_READONLY, 60, 130, 160, 20, hWnd, NULL, NULL, NULL);
        hBtnCopyHEX = CreateWindowA("BUTTON", "Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 10, 160, 210, 20, hWnd, (HMENU)4, NULL, NULL);

        // Панель цвета
        hPanelColor = CreateWindowExA(WS_EX_CLIENTEDGE, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_SUNKEN, 10, 190, 210, 100, hWnd, NULL, NULL, NULL);

        // Слайдеры RGB
        hLabelR = CreateWindowA("STATIC", "R:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 300, 20, 20, hWnd, NULL, NULL, NULL);
        hSliderR = CreateWindowExA(0, TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 40, 300, 180, 20, hWnd, (HMENU)IDC_SLIDER_R, NULL, NULL);
        SendMessage(hSliderR, TBM_SETRANGE, TRUE, MAKELONG(0, 255));
        SendMessage(hSliderR, TBM_SETPOS, TRUE, 255);

        hLabelG = CreateWindowA("STATIC", "G:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 330, 20, 20, hWnd, NULL, NULL, NULL);
        hSliderG = CreateWindowExA(0, TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 40, 330, 180, 20, hWnd, (HMENU)IDC_SLIDER_G, NULL, NULL);
        SendMessage(hSliderG, TBM_SETRANGE, TRUE, MAKELONG(0, 255));
        SendMessage(hSliderG, TBM_SETPOS, TRUE, 255);

        hLabelB = CreateWindowA("STATIC", "B:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 360, 20, 20, hWnd, NULL, NULL, NULL);
        hSliderB = CreateWindowExA(0, TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 40, 360, 180, 20, hWnd, (HMENU)IDC_SLIDER_B, NULL, NULL);
        SendMessage(hSliderB, TBM_SETRANGE, TRUE, MAKELONG(0, 255));
        SendMessage(hSliderB, TBM_SETPOS, TRUE, 255);

        // Цветовой круг (расположен по центру)
        // Вычисляем X-координату для центрирования
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int centerX = (clientRect.right - COLOR_WHEEL_SIZE) / 2;
        hColorWheel = CreateWindowExA(WS_EX_CLIENTEDGE, "ColorWheelClass", "", WS_CHILD | WS_VISIBLE, centerX, 390, COLOR_WHEEL_SIZE, COLOR_WHEEL_SIZE, hWnd, (HMENU)IDC_COLOR_WHEEL, NULL, NULL);

        // Слайдер яркости (V)
        hLabelV = CreateWindowA("STATIC", "V:", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 390 + COLOR_WHEEL_SIZE + 10, 20, 20, hWnd, NULL, NULL, NULL);
        hSliderV = CreateWindowExA(0, TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 40, 390 + COLOR_WHEEL_SIZE + 10, 180, 20, hWnd, (HMENU)IDC_SLIDER_V, NULL, NULL);
        SendMessage(hSliderV, TBM_SETRANGE, TRUE, MAKELONG(0, 255));
        SendMessage(hSliderV, TBM_SETPOS, TRUE, 255);

        // Кнопка Start/Stop (расположена внизу)
        hBtnStartAndStop = CreateWindowA("BUTTON", "Start scanning colors (Key -> P)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 10, 390 + COLOR_WHEEL_SIZE + 40 + 30, 210, 30, hWnd, (HMENU)1, NULL, NULL); // Добавлено смещение +30 для размещения внизу

        // Установка шрифта
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
        SendMessage(hLabelR, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hLabelG, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hLabelB, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hLabelV, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnStartAndStop, WM_SETFONT, (WPARAM)hFont, TRUE);

        timerId = SetTimer(hWnd, TIMER_ID, 16, NULL); // Таймер для обновления информации о цвете (~60 FPS)
    }
    break;

    case WM_SIZE: // Обработка изменения размера окна
    {
        int width = LOWORD(lParam);
        // Пересчитываем позицию цветового круга для центрирования
        int centerX = (width - COLOR_WHEEL_SIZE) / 2;
        SetWindowPos(hColorWheel, NULL, centerX, 390, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        // Пересчитываем позицию кнопки Start/Stop
        // Предполагаем, что высота окна достаточна
        int buttonY = HIWORD(lParam) - 30 - 10; // Высота кнопки 30, отступ от низа 10
        SetWindowPos(hBtnStartAndStop, NULL, 10, buttonY, width - 20, 30, SWP_NOZORDER);
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
			InvalidateRect(hColorWheel, NULL, TRUE); // Перерисовываем весь цветовой круг
		}
		break;
	}

    case WM_TIMER:
		if (wParam == TIMER_ID)
		{
			UpdateColorInfo();

			// Анимация цвета панели
			if (currentColor != targetColor)
			{
				BYTE r_curr = GetRValue(currentColor);
				BYTE g_curr = GetGValue(currentColor);
				BYTE b_curr = GetBValue(currentColor);

				BYTE r_target = GetRValue(targetColor);
				BYTE g_target = GetGValue(targetColor);
				BYTE b_target = GetBValue(targetColor);

				// Интерполяция цвета
				r_curr += (r_target - r_curr) / 8;
				g_curr += (g_target - g_curr) / 8;
				b_curr += (b_target - b_curr) / 8;

				if (abs(r_curr - r_target) < 2 && abs(g_curr - g_target) < 2 && abs(b_curr - b_target) < 2)
				{
					currentColor = targetColor;
				}
				else
				{
					currentColor = RGB(r_curr, g_curr, b_curr);
				}

				HBRUSH hBrush = CreateSolidBrush(currentColor);
				HDC hdcPanel = GetDC(hPanelColor);
				RECT rect;
				GetClientRect(hPanelColor, &rect);
				FillRect(hdcPanel, &rect, hBrush);
				ReleaseDC(hPanelColor, hdcPanel);
				DeleteObject(hBrush);
			}

			// Анимация селектора цветового круга
			if (selectorPos.x != targetSelectorPos.x || selectorPos.y != targetSelectorPos.y)
			{
				// Увеличен делитель для более плавной анимации
				selectorPos.x += (targetSelectorPos.x - selectorPos.x) / 8;
				selectorPos.y += (targetSelectorPos.y - selectorPos.y) / 8;

				if (abs(selectorPos.x - targetSelectorPos.x) < 2 && abs(selectorPos.y - targetSelectorPos.y) < 2)
				{
					selectorPos = targetSelectorPos;
				}
				InvalidateRect(hColorWheel, NULL, TRUE); // Перерисовываем весь цветовой круг
			}

			// Анимация ползунков (только если не взаимодействуем вручную)
			if (!updatingSliders && !isDragging)
			{
				int currentR = SendMessage(hSliderR, TBM_GETPOS, 0, 0);
				int currentG = SendMessage(hSliderG, TBM_GETPOS, 0, 0);
				int currentB = SendMessage(hSliderB, TBM_GETPOS, 0, 0);
                int currentV = SendMessage(hSliderV, TBM_GETPOS, 0, 0); // Получаем текущее значение V

				int targetR = GetRValue(targetColor);
				int targetG = GetGValue(targetColor);
				int targetB = GetBValue(targetColor);
                float h, s, v;
                RGBtoHSV(targetR, targetG, targetB, &h, &s, &v);
                int targetV = (int)(v * 255); // Целевое значение V

				if (currentR != targetR) AnimateSliderPosition(hSliderR, targetR);
				if (currentG != targetG) AnimateSliderPosition(hSliderG, targetG);
				if (currentB != targetB) AnimateSliderPosition(hSliderB, targetB);
                if (currentV != targetV) AnimateSliderPosition(hSliderV, targetV); // Анимация ползунка V
			}
		}
		break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case 1: // Кнопка Start/Stop
            if (startAndStop)
            {
                StopScanning(hWnd);
            }
            else
            {
                StartScanning(hWnd);
            }
            break;
        case 2: // Кнопка Copy Coordinates
        {
            char buffer[256];
            GetWindowTextA(hTextBoxCoordinateX, buffer, sizeof(buffer));
            strcat(buffer, ", ");
            GetWindowTextA(hTextBoxCoordinateY, buffer + strlen(buffer), sizeof(buffer) - strlen(buffer));
            CopyTextToClipboard(hWnd, buffer);
        }
        break;
        case 3: // Кнопка Copy RGB
        {
            char buffer[256];
            GetWindowTextA(hTextBoxColorRGB, buffer, sizeof(buffer));
            CopyTextToClipboard(hWnd, buffer);
        }
        break;
        case 4: // Кнопка Copy HEX
        {
            char buffer[256];
            GetWindowTextA(hTextBoxColorHEX, buffer, sizeof(buffer));
            CopyTextToClipboard(hWnd, buffer);
        }
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
            break;
        }
        break;

    case WM_USER + 1: // Сообщение от иконки в трее
        if (lParam == WM_RBUTTONUP)
        {
            HMENU hMenu = CreatePopupMenu();
            AppendMenuA(hMenu, MF_STRING, ID_TRAY_SHOW, "Show");
            AppendMenuA(hMenu, MF_STRING, ID_FILE_EXIT, "Exit");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
        }
        else if (lParam == WM_LBUTTONDBLCLK)
        {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
        }
        break;

    case WM_RESTORE_APP: // Сообщение для восстановления окна
        ShowWindow(hWnd, SW_RESTORE);
        SetForegroundWindow(hWnd);
        break;

    case WM_DRAWITEM: // Кастомная отрисовка кнопок
    {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType == ODT_BUTTON)
        {
            HBRUSH hBrush = CreateSolidBrush(RGB(50, 50, 50));
            if (dis->itemState & ODS_SELECTED)
            {
                hBrush = CreateSolidBrush(RGB(100, 100, 100));
            }
            else if (dis->itemState & ODS_HOTLIGHT)
            {
                hBrush = CreateSolidBrush(RGB(80, 80, 80));
            }
            FillRect(dis->hDC, &dis->rcItem, hBrush);
            DeleteObject(hBrush);

            SetTextColor(dis->hDC, RGB(255, 255, 255));
            SetBkMode(dis->hDC, TRANSPARENT);

            if (dis->hwndItem == hBtnCopyCoordinates)
            {
                DrawTextA(dis->hDC, "Copy", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            if (dis->hwndItem == hBtnCopyRGB)
            {
                DrawTextA(dis->hDC, "Copy", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            if (dis->hwndItem == hBtnCopyHEX)
            {
                DrawTextA(dis->hDC, "Copy", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            if (dis->hwndItem == hBtnStartAndStop)
            {
                DrawTextA(dis->hDC, startAndStop ? "Stop scanning colors (Key -> P)" : "Start scanning colors (Key -> P)", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }
    break;

    case WM_CTLCOLORSTATIC: // Кастомная отрисовка статических элементов
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(30, 30, 30));
        SetTextColor(hdc, RGB(255, 255, 255));
        return (INT_PTR)hbrBkgnd;
    }
    break;

    case WM_CTLCOLOREDIT: // Кастомная отрисовка полей ввода
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(30, 30, 30));
        SetTextColor(hdc, RGB(255, 255, 255));
        return (INT_PTR)hbrBkgnd;
    }
    break;

    case WM_CTLCOLORBTN: // Кастомная отрисовка кнопок (для слайдеров)
    {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == hSliderR || hCtrl == hSliderG || hCtrl == hSliderB || hCtrl == hSliderV)
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            return (INT_PTR)hbrBkgnd;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    break;

    case WM_MOUSEMOVE: // Обработка движения мыши для лупы
    {
        POINT pt;
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        ClientToScreen(hWnd, &pt);

        RECT rect;
        GetWindowRect(hWnd, &rect);

        if (PtInRect(&rect, pt)) // Если курсор находится над окном приложения
        {
            // If scanning is active, ensure the magnifier is visible
            if (startAndStop)
            {
                if (!IsWindowVisible(hMagnifier))
                {
                    ShowWindow(hMagnifier, SW_SHOW);
                }
            }
            else // If scanning is NOT active, hide the magnifier if it's over the app window
            {
                if (IsWindowVisible(hMagnifier))
                {
                    ShowWindow(hMagnifier, SW_HIDE);
                }
            }
        }
        else // Cursor is outside the application window
        {
            if (!IsWindowVisible(hMagnifier) && startAndStop)
            {
                ShowWindow(hMagnifier, SW_SHOW);
            }
        }
        if (startAndStop)
        {
            UpdateMagnifier(pt);
        }
    }
    break;

    case WM_LBUTTONDOWN:
    {
        // Обработка нажатия на кнопки (для визуального эффекта)
        HWND hwndButton = (HWND)lParam;
        if (GetDlgCtrlID(hwndButton) == 1 || GetDlgCtrlID(hwndButton) == 2 || GetDlgCtrlID(hwndButton) == 3 || GetDlgCtrlID(hwndButton) == 4)
        {
            SendMessage(hwndButton, BM_SETSTATE, TRUE, 0);
            InvalidateRect(hwndButton, NULL, TRUE);
        }
    }
    break;

    case WM_LBUTTONUP:
    {
        // Обработка отпускания кнопки (для визуального эффекта)
        HWND hwndButton = (HWND)lParam;
        if (GetDlgCtrlID(hwndButton) == 1 || GetDlgCtrlID(hwndButton) == 2 || GetDlgCtrlID(hwndButton) == 3 || GetDlgCtrlID(hwndButton) == 4)
        {
            SendMessage(hwndButton, BM_SETSTATE, FALSE, 0);
            InvalidateRect(hwndButton, NULL, TRUE);
        }
    }
    break;

    case WM_KEYDOWN: // Обработка нажатий клавиш для перемещения курсора и старт/стоп
    {
        POINT pt;
        GetCursorPos(&pt);

        int moveStep = 1;

        switch (wParam)
        {
        case VK_LEFT:
            SetCursorPos(pt.x - moveStep, pt.y);
            pt.x -= moveStep;
            break;
        case VK_RIGHT:
            SetCursorPos(pt.x + moveStep, pt.y);
            pt.x += moveStep;
            break;
        case VK_UP:
            SetCursorPos(pt.x, pt.y - moveStep);
            pt.y -= moveStep;
            break;
        case VK_DOWN:
            SetCursorPos(pt.x, pt.y + moveStep);
            pt.y += moveStep;
            break;
        case 0x57: // W
            SetCursorPos(pt.x, pt.y - moveStep);
            pt.y -= moveStep;
            break;
        case 0x41: // A
            SetCursorPos(pt.x - moveStep, pt.y);
            pt.x -= moveStep;
            break;
        case 0x53: // S
            SetCursorPos(pt.x, pt.y + moveStep);
            pt.y += moveStep;
            break;
        case 0x44: // D
            SetCursorPos(pt.x + moveStep, pt.y);
            pt.x += moveStep;
            break;
        case 0x50: // P
            if (startAndStop)
            {
                StopScanning(hWnd);
            }
            else
            {
                StartScanning(hWnd);
                SetFocus(hWnd); // Возвращаем фокус на главное окно
            }
            break;
        }

        if (startAndStop)
        {
            UpdateMagnifier(pt);
        }
    }
    break;

    case WM_CLOSE: // Обработка закрытия окна (сворачивание в трей)
        ShowWindow(hWnd, SW_HIDE);
        StopScanning(hWnd); // Останавливаем сканирование при сворачивании
        return 0;

    case WM_DESTROY: // Очистка ресурсов при уничтожении окна
        Shell_NotifyIcon(NIM_DELETE, &nid);
        DeleteObject(hFont);
        if (hbrBkgnd) DeleteObject(hbrBkgnd);
        if (hdcMem) DeleteDC(hdcMem);
        if (hbmMem) DeleteObject(hbmMem);
        if (hdcMagnifier) ReleaseDC(hMagnifier, hdcMagnifier);
        if (hdcColorWheelBuffer) DeleteDC(hdcColorWheelBuffer);
        if (hbmColorWheelBuffer) DeleteObject(hbmColorWheelBuffer);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Обработчик сообщений для цветового круга
LRESULT CALLBACK ColorWheelProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // Двойная буферизация для плавного рисования
        if (!hdcColorWheelBuffer)
        {
            // Создаем буфер только один раз
            hdcColorWheelBuffer = CreateCompatibleDC(hdc);
            hbmColorWheelBuffer = CreateCompatibleBitmap(hdc, COLOR_WHEEL_SIZE, COLOR_WHEEL_SIZE);
            SelectObject(hdcColorWheelBuffer, hbmColorWheelBuffer);
            DrawColorWheelContent(hdcColorWheelBuffer, COLOR_WHEEL_SIZE, COLOR_WHEEL_SIZE);
        }

        // Копируем содержимое буфера на экран
        BitBlt(hdc, 0, 0, COLOR_WHEEL_SIZE, COLOR_WHEEL_SIZE, hdcColorWheelBuffer, 0, 0, SRCCOPY);

        // Рисуем селектор поверх буфера
        int cx = selectorPos.x;
        int cy = selectorPos.y;
        BYTE r = 255 - GetRValue(lastColor);
        BYTE g = 255 - GetGValue(lastColor);
        BYTE b = 255 - GetBValue(lastColor);
        HPEN hPen = CreatePen(PS_SOLID, 2, RGB(r, g, b));
        HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HGDIOBJ hOldPen = SelectObject(hdc, hPen);
        HGDIOBJ hOldBrush = SelectObject(hdc, hNullBrush);
        Ellipse(hdc, cx - 5, cy - 5, cx + 5, cy + 5);
        SelectObject(hdc, hOldPen);
        SelectObject(hdc, hOldBrush);
        DeleteObject(hPen);

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        isDragging = TRUE;
        SetCapture(hWnd);
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        
        // Устанавливаем целевую позицию для анимации
        AnimateSelectorPosition(pt);

        updatingSliders = TRUE;
        UpdateColorFromWheel(); // Вызываем после установки targetSelectorPos
        updatingSliders = FALSE;
        InvalidateRect(hWnd, NULL, TRUE); // Принудительная перерисовка для немедленного обновления селектора
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (isDragging)
        {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            // Устанавливаем целевую позицию для анимации
            AnimateSelectorPosition(pt);

            updatingSliders = TRUE;
            UpdateColorFromWheel(); // Вызываем после установки targetSelectorPos
            updatingSliders = FALSE;
            InvalidateRect(hWnd, NULL, TRUE); // Принудительная перерисовка для немедленного обновления селектора
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (isDragging)
        {
            isDragging = FALSE;
            ReleaseCapture();
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // Предотвращаем стирание фона для двойной буферизации
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Функция для рисования содержимого цветового круга (без селектора)
void DrawColorWheelContent(HDC hdc, int width, int height)
{
    float v = (float)SendMessage(hSliderV, TBM_GETPOS, 0, 0) / 255.0f; // Берем текущее значение V из слайдера

    for (int x = 0; x < width; x++)
    {
        for (int y = 0; y < height; y++)
        {
            float s = (float)x / (width - 1);
            float h = 360.0f * (1.0f - (float)y / (height - 1));
            BYTE r, g, b;
            HSVtoRGB(h, s, v, &r, &g, &b);
            SetPixel(hdc, x, y, RGB(r, g, b));
        }
    }
}

void UpdateColorInfo()
{
    if (!startAndStop)
        return;

    POINT pt;
    GetCursorPos(&pt);

    HDC hdcScreen = GetDC(NULL);
    COLORREF color = GetPixel(hdcScreen, pt.x, pt.y);
    ReleaseDC(NULL, hdcScreen);

    const int colorThreshold = 5;
    BYTE r = GetRValue(color);
    BYTE g = GetGValue(color);
    BYTE b = GetBValue(color);
    BYTE lastR = GetRValue(lastColor);
    BYTE lastG = GetGValue(lastColor);
    BYTE lastB = GetBValue(lastColor);

    if (!isDragging && !updatingSliders && (abs(r - lastR) > colorThreshold || abs(g - lastG) > colorThreshold || abs(b - lastB) > colorThreshold))
    {
        lastColor = color;
        targetColor = color; // Устанавливаем целевой цвет для анимации

        // Запускаем анимацию ползунков
        AnimateSliderPosition(hSliderR, r);
        AnimateSliderPosition(hSliderG, g);
        AnimateSliderPosition(hSliderB, b);
        
        float h, s, v;
        RGBtoHSV(r, g, b, &h, &s, &v);
        // Анимация ползунка V
        AnimateSliderPosition(hSliderV, (int)(v * 255));

        // Обновляем позицию селектора на цветовом круге
        POINT newSelectorTarget = { (LONG)(s * (COLOR_WHEEL_SIZE - 1)), (LONG)((1.0f - (h / 360.0f)) * (COLOR_WHEEL_SIZE - 1)) };
        AnimateSelectorPosition(newSelectorTarget);

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "(%d, %d, %d)", r, g, b);
        SetWindowTextA(hTextBoxColorRGB, buffer);

        snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
        SetWindowTextA(hTextBoxColorHEX, buffer);
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%ld", (LONG)pt.x);
    SetWindowTextA(hTextBoxCoordinateX, buffer);
    snprintf(buffer, sizeof(buffer), "%ld", (LONG)pt.y);
    SetWindowTextA(hTextBoxCoordinateY, buffer);
}

void UpdateColorFromSliders(void)
{
    BYTE r = (BYTE)SendMessage(hSliderR, TBM_GETPOS, 0, 0);
    BYTE g = (BYTE)SendMessage(hSliderG, TBM_GETPOS, 0, 0);
    BYTE b = (BYTE)SendMessage(hSliderB, TBM_GETPOS, 0, 0);

    lastColor = RGB(r, g, b);
    targetColor = lastColor; // Обновляем целевой цвет

    // Обновляем позицию селектора напрямую
    float h, s, v;
    RGBtoHSV(r, g, b, &h, &s, &v);
    // Здесь мы устанавливаем позицию V напрямую, чтобы она соответствовала RGB
    SendMessage(hSliderV, TBM_SETPOS, TRUE, (LPARAM)(v * 255)); 
    selectorPos.x = (LONG)(s * (COLOR_WHEEL_SIZE - 1));
    selectorPos.y = (LONG)((1.0f - (h / 360.0f)) * (COLOR_WHEEL_SIZE - 1));
    targetSelectorPos = selectorPos; // Синхронизируем targetSelectorPos

    // Перерисовываем цветовой круг
    InvalidateRect(hColorWheel, NULL, TRUE);

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "(%d, %d, %d)", r, g, b);
    SetWindowTextA(hTextBoxColorRGB, buffer);

    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
    SetWindowTextA(hTextBoxColorHEX, buffer);

    HBRUSH hBrush = CreateSolidBrush(lastColor);
    HDC hdcPanel = GetDC(hPanelColor);
    RECT rect;
    GetClientRect(hPanelColor, &rect);
    FillRect(hdcPanel, &rect, hBrush);
    ReleaseDC(hPanelColor, hdcPanel);
    DeleteObject(hBrush);

    InvalidateRect(hSliderR, NULL, TRUE);
    UpdateWindow(hSliderR);
    InvalidateRect(hSliderG, NULL, TRUE);
    UpdateWindow(hSliderG);
    InvalidateRect(hSliderB, NULL, TRUE);
    UpdateWindow(hSliderB);
    InvalidateRect(hSliderV, NULL, TRUE); // Перерисовываем V ползунок
    UpdateWindow(hSliderV);
}

void UpdateColorFromWheel(void)
{
    // Используем targetSelectorPos для расчета цвета
    float s = (float)targetSelectorPos.x / (COLOR_WHEEL_SIZE - 1);
    float h = 360.0f * (1.0f - (float)targetSelectorPos.y / (COLOR_WHEEL_SIZE - 1));
    float v = (float)SendMessage(hSliderV, TBM_GETPOS, 0, 0) / 255.0f;

    // Обновляем selectorPos напрямую
    selectorPos = targetSelectorPos; // Мгновенно перемещаем селектор

    BYTE r, g, b;
    HSVtoRGB(h, s, v, &r, &g, &b);
    lastColor = RGB(r, g, b);
    targetColor = lastColor; // Обновляем целевой цвет

    // Запускаем анимацию ползунков
    AnimateSliderPosition(hSliderR, r);
    AnimateSliderPosition(hSliderG, g);
    AnimateSliderPosition(hSliderB, b);
    // Ползунок V уже обновлен пользователем, поэтому его не анимируем здесь.
    // Если бы он обновлялся программно, то AnimateSliderPosition была бы здесь.

    InvalidateRect(hSliderV, NULL, TRUE);
    UpdateWindow(hSliderV);

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "(%d, %d, %d)", r, g, b);
    SetWindowTextA(hTextBoxColorRGB, buffer);

    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
    SetWindowTextA(hTextBoxColorHEX, buffer);

    HBRUSH hBrush = CreateSolidBrush(lastColor);
    HDC hdcPanel = GetDC(hPanelColor);
    RECT rect;
    GetClientRect(hPanelColor, &rect);
    FillRect(hdcPanel, &rect, hBrush);
    ReleaseDC(hPanelColor, hdcPanel);
    DeleteObject(hBrush);

    // Перерисовываем цветовой круг
    InvalidateRect(hColorWheel, NULL, TRUE);
}

void RGBtoHSV(BYTE r, BYTE g, BYTE b, float* h, float* s, float* v)
{
    float R = r / 255.0f;
    float G = g / 255.0f;
    float B = b / 255.0f;

    float cmax = max(max(R, G), B);
    float cmin = min(min(R, G), B);
    float delta = cmax - cmin;

    // Hue
    if (delta == 0)
        *h = 0;
    else if (cmax == R)
        *h = 60.0f * fmod(((G - B) / delta), 6);
    else if (cmax == G)
        *h = 60.0f * (((B - R) / delta) + 2);
    else
        *h = 60.0f * (((R - G) / delta) + 4);

    if (*h < 0)
        *h += 360;

    // Saturation
    *s = (cmax == 0) ? 0 : delta / cmax;

    // Value
    *v = cmax;
}

void HSVtoRGB(float h, float s, float v, BYTE* r, BYTE* g, BYTE* b)
{
    float c = v * s;
    float x = c * (1 - fabs(fmod(h / 60.0f, 2) - 1));
    float m = v - c;
    float r1, g1, b1;

    if (h < 60) { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }

    *r = (BYTE)((r1 + m) * 255);
    *g = (BYTE)((g1 + m) * 255);
    *b = (BYTE)((b1 + m) * 255);
}

void CreateMagnifier()
{
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "MagnifierClass";
    RegisterClass(&wc);

    hMagnifier = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "MagnifierClass", "Magnifier",
        WS_POPUP,
        0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    SetWindowPos(hMagnifier, HWND_TOPMOST, 0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT, SWP_NOMOVE | SWP_NOACTIVATE);

    SetLayeredWindowAttributes(hMagnifier, 0, 255, LWA_ALPHA);

    HDC hdcScreen = GetDC(NULL);
    hdcMem = CreateCompatibleDC(hdcScreen);
    hbmMem = CreateCompatibleBitmap(hdcScreen, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT);
    hdcMagnifier = GetDC(hMagnifier);
    ReleaseDC(NULL, hdcScreen);

    SetStretchBltMode(hdcMagnifier, HALFTONE);
    SetBrushOrgEx(hdcMagnifier, 0, 0, NULL);

    QueryPerformanceFrequency(&frequency);
    lastUpdateTime.QuadPart = 0;
}

void UpdateMagnifier(POINT pt)
{
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    double elapsedMs = (double)(currentTime.QuadPart - lastUpdateTime.QuadPart) * 1000.0 / frequency.QuadPart;
    if (elapsedMs < 16.67) // Ограничиваем обновление до ~60 FPS
        return;
    lastUpdateTime = currentTime;

    if (hdcMem == NULL || hbmMem == NULL || hdcMagnifier == NULL) return;

    HDC hdcScreen = GetDC(NULL);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hbmMem);

    int sourceX = pt.x - (MAGNIFIER_WIDTH / 2 / MAGNIFICATION_FACTOR);
    int sourceY = pt.y - (MAGNIFIER_HEIGHT / 2 / MAGNIFICATION_FACTOR);

    BitBlt(hdcMem, 0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT, hdcScreen, sourceX, sourceY, SRCCOPY);

    StretchBlt(hdcMagnifier, 0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT, hdcMem, 0, 0,
        MAGNIFIER_WIDTH / MAGNIFICATION_FACTOR, MAGNIFIER_HEIGHT / MAGNIFICATION_FACTOR, SRCCOPY);

    int centerX = MAGNIFIER_WIDTH / 2;
    int centerY = MAGNIFIER_HEIGHT / 2;

    BYTE r = 255 - GetRValue(lastColor);
    BYTE g = 255 - GetGValue(lastColor);
    BYTE b = 255 - GetBValue(lastColor);
    COLORREF borderColor = RGB(r, g, b);

    HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
    HGDIOBJ hOldPen = SelectObject(hdcMagnifier, hPen);

    MoveToEx(hdcMagnifier, centerX - 5, centerY, NULL);
    LineTo(hdcMagnifier, centerX + 5, centerY);
    MoveToEx(hdcMagnifier, centerX, centerY - 5, NULL);
    LineTo(hdcMagnifier, centerX, centerY + 5);

    HPEN hBorderPen = CreatePen(PS_SOLID, 2, borderColor);
    HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    SelectObject(hdcMagnifier, hBorderPen);
    SelectObject(hdcMagnifier, hNullBrush);

    Rectangle(hdcMagnifier, 0, 0, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT);

    SelectObject(hdcMagnifier, hOldPen);
    DeleteObject(hPen);
    SelectObject(hdcMagnifier, hBorderPen);
    DeleteObject(hBorderPen);

    SelectObject(hdcMem, hOldBmp);
    ReleaseDC(NULL, hdcScreen);

    SetWindowPos(hMagnifier, HWND_TOPMOST, pt.x + 10, pt.y + 10, MAGNIFIER_WIDTH, MAGNIFIER_HEIGHT, SWP_NOACTIVATE | SWP_NOSIZE);
}

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    static POINT lastPt = { -1, -1 };
    if (nCode >= 0 && wParam == WM_MOUSEMOVE && startAndStop)
    {
        MSLLHOOKSTRUCT* hookStruct = (MSLLHOOKSTRUCT*)lParam;
        POINT pt = hookStruct->pt;

        // Обновляем лупу только если курсор значительно переместился
        if (abs(pt.x - lastPt.x) >= 2 || abs(pt.y - lastPt.y) >= 2)
        {
            UpdateMagnifier(pt);
            lastPt = pt;
        }
    }
    return CallNextHookEx(mouseHook, nCode, wParam, lParam);
}

void ShowAboutDialog(HWND hwnd)
{
    if (hAboutWnd != NULL)
    {
        SetForegroundWindow(hAboutWnd);
        return;
    }

    hAboutWnd = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TOPMOST,
        "AboutWindowClass",
        "About Color Picker",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 380,
        hwnd, NULL, GetModuleHandle(NULL), NULL);

    SetLayeredWindowAttributes(hAboutWnd, 0, 240, LWA_ALPHA);

    HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    SendMessage(hAboutWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    // Исправлено: WM_SMALL заменено на WM_SETICON
    SendMessage(hAboutWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon); 

    ShowWindow(hAboutWnd, SW_SHOW);
    UpdateWindow(hAboutWnd);
}

void StopScanning(HWND hWnd)
{
    KillTimer(hWnd, timerId);

    if (mouseHook != NULL)
    {
        UnhookWindowsHookEx(mouseHook);
        mouseHook = NULL;
    }

    SetWindowTextA(hBtnStartAndStop, "Start scanning colors (Key -> P)");
    ShowWindow(hMagnifier, SW_HIDE);
    startAndStop = FALSE;
}

void StartScanning(HWND hWnd)
{
    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(NULL), 0);
    if (mouseHook == NULL)
    {
        MessageBoxA(hWnd, "Failed to install mouse hook!", "Error", MB_ICONERROR);
        return;
    }

    timerId = SetTimer(hWnd, TIMER_ID, 16, NULL); // Возобновляем таймер (~60 FPS)

    SetWindowTextA(hBtnStartAndStop, "Stop scanning colors (Key -> P)");
    ShowWindow(hMagnifier, SW_SHOW);
    startAndStop = TRUE;
	
	if (!IsWindowVisible(hMagnifier)) {
        ShowWindow(hMagnifier, SW_SHOW);
    }
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

LRESULT CALLBACK AboutWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND hIconStatic, hLabelTitle, hLabelVersion, hLabelNickname, hLabelEmail, hLinkWebsite, hLinkBoosty, hLabelSubscribers, hBtnClose;
    static HFONT hTitleFont, hAboutFont, hButtonFont;
    static COLORREF linkColor = RGB(0, 120, 255);

    switch (message)
    {
    case WM_CREATE:
    {
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int width = clientRect.right - clientRect.left;

        hTitleFont = CreateFontA(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        hAboutFont = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        hButtonFont = CreateFontA(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        hIconStatic = CreateWindowA("STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ICON,
            width / 2 - 24, 20, 48, 48, hWnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hIconStatic, STM_SETICON, (WPARAM)LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1)), 0);

        hLabelTitle = CreateWindowA("STATIC", "Color Picker", WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 75, width, 30, hWnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hLabelTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

        hLabelVersion = CreateWindowA("STATIC", "Version " VERSION, WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 110, width, 25, hWnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hLabelVersion, WM_SETFONT, (WPARAM)hAboutFont, TRUE);

        hLabelNickname = CreateWindowA("STATIC", "Developed by Taillogs", WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 140, width, 25, hWnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hLabelNickname, WM_SETFONT, (WPARAM)hAboutFont, TRUE);

        hLabelEmail = CreateWindowA("STATIC", "Email: ttailogss@gmail.com", WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 170, width, 25, hWnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hLabelEmail, WM_SETFONT, (WPARAM)hAboutFont, TRUE);

        hLinkWebsite = CreateWindowA("STATIC", "https://tailogs.github.io/", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY,
            0, 200, width, 25, hWnd, (HMENU)IDC_ABOUT_LINK, GetModuleHandle(NULL), NULL);
        SendMessage(hLinkWebsite, WM_SETFONT, (WPARAM)hAboutFont, TRUE);
        SetClassLongPtr(hLinkWebsite, GCLP_HCURSOR, (LONG_PTR)LoadCursor(NULL, IDC_HAND));

        hLabelSubscribers = CreateWindowA("STATIC", "Made for subscribers", WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 230, width, 25, hWnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hLabelSubscribers, WM_SETFONT, (WPARAM)hAboutFont, TRUE);

        hLinkBoosty = CreateWindowA("STATIC", "https://boosty.to/tailogs", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY,
            0, 260, width, 25, hWnd, (HMENU)(IDC_ABOUT_LINK + 1), GetModuleHandle(NULL), NULL);
        SendMessage(hLinkBoosty, WM_SETFONT, (WPARAM)hAboutFont, TRUE);
        SetClassLongPtr(hLinkBoosty, GCLP_HCURSOR, (LONG_PTR)LoadCursor(NULL, IDC_HAND));

        hBtnClose = CreateWindowA("BUTTON", "Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            width / 2 - 50, 290, 100, 35, hWnd, (HMENU)IDC_ABOUT_CLOSE, GetModuleHandle(NULL), NULL);
        SendMessage(hBtnClose, WM_SETFONT, (WPARAM)hButtonFont, TRUE);

        aboutTimerId = SetTimer(hWnd, ABOUT_TIMER_ID, 50, NULL);
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rect;
        GetClientRect(hWnd, &rect);

        TRIVERTEX vertex[2];
        GRADIENT_RECT gRect;
        vertex[0].x = rect.left;
        vertex[0].y = rect.top;
        vertex[0].Red = 0x1E00;
        vertex[0].Green = 0x1E00;
        vertex[0].Blue = 0x1E00;
        vertex[0].Alpha = 0xFF00;
        vertex[1].x = rect.right;
        vertex[1].y = rect.bottom;
        vertex[1].Red = 0x3C00;
        vertex[1].Green = 0x3C00;
        vertex[1].Blue = 0x3C00;
        vertex[1].Alpha = 0xFF00;
        gRect.UpperLeft = 0;
        gRect.LowerRight = 1;
        GradientFill(hdc, vertex, 2, &gRect, 1, GRADIENT_FILL_RECT_V);

        EndPaint(hWnd, &ps);
    }
    break;

    case WM_MOUSEMOVE:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        HWND hCtrl = ChildWindowFromPoint(hWnd, pt);
        if (hCtrl == hLinkWebsite || hCtrl == hLinkBoosty)
        {
            if (linkColor != RGB(0, 180, 255))
            {
                linkColor = RGB(0, 180, 255);
                InvalidateRect(hLinkWebsite, NULL, TRUE);
                InvalidateRect(hLinkBoosty, NULL, TRUE);
            }
        }
        else if (linkColor != RGB(0, 120, 255))
        {
            linkColor = RGB(0, 120, 255);
            InvalidateRect(hLinkWebsite, NULL, TRUE);
            InvalidateRect(hLinkBoosty, NULL, TRUE);
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    break;

    case WM_TIMER:
        if (wParam == ABOUT_TIMER_ID)
        {
            hue = (hue + 5) % 360;
            float s = 1.0f, v = 1.0f;
            int r, g, b;

            float c = v * s;
            float x = c * (1 - fabs(fmod(hue / 60.0f, 2) - 1));
            float m = v - c;
            float r1, g1, b1;

            if (hue < 60) { r1 = c; g1 = x; b1 = 0; }
            else if (hue < 120) { r1 = x; g1 = c; b1 = 0; }
            else if (hue < 180) { r1 = 0; g1 = c; b1 = x; }
            else if (hue < 240) { r1 = 0; g1 = x; b1 = c; }
            else if (hue < 300) { r1 = x; g1 = 0; b1 = c; }
            else { r1 = c; g1 = 0; b1 = x; }

            r = (int)((r1 + m) * 255);
            g = (int)((g1 + m) * 255);
            b = (int)((b1 + m) * 255);

            InvalidateRect(hLabelNickname, NULL, TRUE);
            SetWindowLongPtr(hLabelNickname, GWLP_USERDATA, RGB(r, g, b));
        }
        break;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, (hCtrl == hLabelNickname) ? (COLORREF)GetWindowLongPtr(hCtrl, GWLP_USERDATA) : RGB(255, 255, 255));
        if (hCtrl == hLinkWebsite || hCtrl == hLinkBoosty)
            SetTextColor(hdc, linkColor);
        return (INT_PTR)GetStockObject(NULL_BRUSH);
    }
    break;

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_ABOUT_CLOSE)
        {
            HBRUSH hBrush;
            if (dis->itemState & ODS_SELECTED)
                hBrush = CreateSolidBrush(RGB(80, 80, 80));
            else if (dis->itemState & ODS_HOTLIGHT)
                hBrush = CreateSolidBrush(RGB(60, 60, 60));
            else
                hBrush = CreateSolidBrush(RGB(50, 50, 50));

            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
            HGDIOBJ hOldPen = SelectObject(dis->hDC, hPen);
            HGDIOBJ hOldBrush = SelectObject(dis->hDC, hBrush);

            RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, 10, 10);

            SetTextColor(dis->hDC, RGB(255, 255, 255));
            SetBkMode(dis->hDC, TRANSPARENT);
            DrawTextA(dis->hDC, "Close", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(dis->hDC, hOldPen);
            SelectObject(dis->hDC, hOldBrush);
            DeleteObject(hPen);
            DeleteObject(hBrush);
        }
    }
    break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_ABOUT_CLOSE:
            DestroyWindow(hWnd);
            break;
        case IDC_ABOUT_LINK:
            ShellExecuteA(NULL, "open", "https://tailogs.github.io/", NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDC_ABOUT_LINK + 1:
            ShellExecuteA(NULL, "open", "https://boosty.to/tailogs", NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        break;

    case WM_DESTROY:
        KillTimer(hWnd, aboutTimerId);
        DeleteObject(hTitleFont);
        DeleteObject(hAboutFont);
        DeleteObject(hButtonFont);
        SendMessage(hIconStatic, STM_SETICON, (WPARAM)NULL, 0);
        hAboutWnd = NULL;
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Новые функции для анимации

// Анимация цвета панели
void AnimateColorTransition(COLORREF newColor)
{
    targetColor = newColor;
    if (currentColor == (COLORREF)-1) // Исправлено: приведение -1 к COLORREF
    {
        currentColor = newColor;
        HBRUSH hBrush = CreateSolidBrush(currentColor);
        HDC hdcPanel = GetDC(hPanelColor);
        RECT rect;
        GetClientRect(hPanelColor, &rect);
        FillRect(hdcPanel, &rect, hBrush);
        ReleaseDC(hPanelColor, hdcPanel);
        DeleteObject(hBrush);
    }
}

// Анимация позиции ползунка
void AnimateSliderPosition(HWND hSlider, int targetPos)
{
    int currentPos = SendMessage(hSlider, TBM_GETPOS, 0, 0);
    if (abs(currentPos - targetPos) > 1) // Если разница больше 1, анимируем
    {
        int newPos = currentPos + (targetPos - currentPos) / 5; // Плавное перемещение
        SendMessage(hSlider, TBM_SETPOS, TRUE, (LPARAM)newPos);
        InvalidateRect(hSlider, NULL, TRUE);
        UpdateWindow(hSlider);
    }
    else // Иначе устанавливаем точное значение
    {
        SendMessage(hSlider, TBM_SETPOS, TRUE, (LPARAM)targetPos);
        InvalidateRect(hSlider, NULL, TRUE);
        UpdateWindow(hSlider);
    }
}

// Анимация позиции селектора цветового круга
void AnimateSelectorPosition(POINT newPos)
{
    targetSelectorPos.x = max(0, min(COLOR_WHEEL_SIZE - 1, newPos.x));
    targetSelectorPos.y = max(0, min(COLOR_WHEEL_SIZE - 1, newPos.y));

    // Если разница мала, сразу устанавливаем selectorPos
    if (abs(selectorPos.x - targetSelectorPos.x) < 2 && abs(selectorPos.y - targetSelectorPos.y) < 2)
    {
        selectorPos = targetSelectorPos;
        InvalidateRect(hColorWheel, NULL, TRUE);
    }
}
