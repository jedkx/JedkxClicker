#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <cstdlib>

// MinimalClicker deliberately uses only Windows' built-in Win32 APIs.
// It writes no settings, logs, registry values, or temporary files.

namespace {
constexpr UINT kHotkeyId = 1;
constexpr UINT_PTR kPickTimerId = 2;
constexpr int kAppIconId = 101;
constexpr int kIdStartStop = 100;
constexpr int kIdInterval = 101;
constexpr int kIdButton = 102;
constexpr int kIdClickType = 103;
constexpr int kIdRepeat = 104;
constexpr int kIdRepeatCount = 105;
constexpr int kIdCurrentPosition = 106;
constexpr int kIdFixedPosition = 107;
constexpr int kIdPick = 108;
constexpr int kIdHotkey = 109;
constexpr int kIdStatus = 110;

HWND g_window = nullptr;
HWND g_startStop = nullptr;
HWND g_interval = nullptr;
HWND g_button = nullptr;
HWND g_clickType = nullptr;
HWND g_repeat = nullptr;
HWND g_repeatCount = nullptr;
HWND g_currentPosition = nullptr;
HWND g_fixedPosition = nullptr;
HWND g_pick = nullptr;
HWND g_hotkey = nullptr;
HWND g_status = nullptr;

HANDLE g_stopEvent = nullptr;
HANDLE g_thread = nullptr;
bool g_running = false;
bool g_picking = false;
POINT g_fixedPoint{};
bool g_previousLeftDown = false;
UINT g_hotkeyVk = VK_F6;
HFONT g_titleFont = nullptr;
HFONT g_sectionFont = nullptr;
HFONT g_textFont = nullptr;
HBRUSH g_backgroundBrush = nullptr;
HBRUSH g_inputBrush = nullptr;
HBRUSH g_cardBrush = nullptr;

constexpr COLORREF kBackground = RGB(0, 0, 0);
constexpr COLORREF kCard = RGB(23, 23, 23);
constexpr COLORREF kInk = RGB(255, 255, 255);
constexpr COLORREF kMuted = RGB(168, 168, 168);
constexpr COLORREF kAccent = RGB(167, 139, 250);

void SetStatus(const wchar_t* text) {
    SetWindowTextW(g_status, text);
    InvalidateRect(g_window, nullptr, FALSE);
}

void SetControlsEnabled(BOOL enabled) {
    EnableWindow(g_interval, enabled);
    EnableWindow(g_button, enabled);
    EnableWindow(g_clickType, enabled);
    EnableWindow(g_repeat, enabled);
    EnableWindow(g_repeatCount, enabled && SendMessageW(g_repeat, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(g_currentPosition, enabled);
    EnableWindow(g_fixedPosition, enabled);
    EnableWindow(g_pick, enabled && SendMessageW(g_fixedPosition, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(g_hotkey, enabled);
}

void SendOneClick(int button, bool doubleClick, const POINT& point, bool useFixedPoint) {
    if (useFixedPoint) {
        SetCursorPos(point.x, point.y);
    }

    DWORD down = MOUSEEVENTF_LEFTDOWN;
    DWORD up = MOUSEEVENTF_LEFTUP;
    if (button == 1) { down = MOUSEEVENTF_RIGHTDOWN; up = MOUSEEVENTF_RIGHTUP; }
    if (button == 2) { down = MOUSEEVENTF_MIDDLEDOWN; up = MOUSEEVENTF_MIDDLEUP; }

    INPUT input[4]{};
    int count = doubleClick ? 4 : 2;
    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = down;
    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = up;
    if (doubleClick) {
        input[2].type = INPUT_MOUSE;
        input[2].mi.dwFlags = down;
        input[3].type = INPUT_MOUSE;
        input[3].mi.dwFlags = up;
    }
    SendInput(count, input, sizeof(INPUT));
}

struct ClickConfiguration {
    DWORD intervalMs;
    int button;
    bool doubleClick;
    bool useFixedPoint;
    bool unlimited;
    unsigned long long repeatCount;
    POINT fixedPoint;
};

DWORD WINAPI ClickThread(void* parameter) {
    const ClickConfiguration configuration = *static_cast<ClickConfiguration*>(parameter);
    HeapFree(GetProcessHeap(), 0, parameter);

    unsigned long long clicks = 0;
    while (WaitForSingleObject(g_stopEvent, configuration.intervalMs) == WAIT_TIMEOUT) {
        SendOneClick(configuration.button, configuration.doubleClick, configuration.fixedPoint, configuration.useFixedPoint);
        ++clicks;
        if (!configuration.unlimited && clicks >= configuration.repeatCount) {
            PostMessageW(g_window, WM_APP + 1, 0, 0);
            return 0;
        }
    }
    return 0;
}

void StopClicking() {
    if (!g_running) return;
    SetEvent(g_stopEvent);
    if (g_thread != nullptr) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    g_running = false;
    SetWindowTextW(g_startStop, L"Start clicking");
    SetControlsEnabled(TRUE);
    SetStatus(L"Ready");
}

unsigned long long ReadUnsigned(HWND control, unsigned long long fallback) {
    wchar_t text[32]{};
    GetWindowTextW(control, text, ARRAYSIZE(text));
    wchar_t* end = nullptr;
    const unsigned long long value = wcstoull(text, &end, 10);
    return end == text ? fallback : value;
}

void StartClicking() {
    const unsigned long long interval = ReadUnsigned(g_interval, 100);
    const bool unlimited = SendMessageW(g_repeat, BM_GETCHECK, 0, 0) != BST_CHECKED;
    const unsigned long long repeats = ReadUnsigned(g_repeatCount, 1);
    if (interval == 0 || interval > 86400000ULL || (!unlimited && repeats == 0)) {
        MessageBoxW(g_window, L"The interval must be at least 1 ms and the click count must be at least 1.", L"JedkxClicker", MB_ICONWARNING);
        return;
    }

    auto* configuration = static_cast<ClickConfiguration*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ClickConfiguration)));
    if (configuration == nullptr) {
        MessageBoxW(g_window, L"Unable to allocate memory.", L"JedkxClicker", MB_ICONERROR);
        return;
    }
    configuration->intervalMs = static_cast<DWORD>(interval);
    configuration->button = static_cast<int>(SendMessageW(g_button, CB_GETCURSEL, 0, 0));
    configuration->doubleClick = SendMessageW(g_clickType, CB_GETCURSEL, 0, 0) == 1;
    configuration->useFixedPoint = SendMessageW(g_fixedPosition, BM_GETCHECK, 0, 0) == BST_CHECKED;
    configuration->unlimited = unlimited;
    configuration->repeatCount = repeats;
    configuration->fixedPoint = g_fixedPoint;

    ResetEvent(g_stopEvent);
    g_thread = CreateThread(nullptr, 0, ClickThread, configuration, 0, nullptr);
    if (g_thread == nullptr) {
        HeapFree(GetProcessHeap(), 0, configuration);
        MessageBoxW(g_window, L"Unable to start the click worker.", L"JedkxClicker", MB_ICONERROR);
        return;
    }
    g_running = true;
    SetWindowTextW(g_startStop, L"Stop clicking");
    SetControlsEnabled(FALSE);
    SetStatus(L"Running — press the shortcut to stop.");
}

void ToggleClicking() {
    if (g_running) StopClicking(); else StartClicking();
}

void UpdatePositionControls() {
    const BOOL fixed = SendMessageW(g_fixedPosition, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_pick, fixed && !g_running);
}

void UpdateRepeatControls() {
    const BOOL exact = SendMessageW(g_repeat, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_repeatCount, exact && !g_running);
}

void RegisterSelectedHotkey() {
    UnregisterHotKey(g_window, kHotkeyId);
    const int selection = static_cast<int>(SendMessageW(g_hotkey, CB_GETCURSEL, 0, 0));
    g_hotkeyVk = VK_F6 + (selection < 0 ? 0 : selection);
    if (!RegisterHotKey(g_window, kHotkeyId, MOD_NOREPEAT, g_hotkeyVk)) {
        MessageBoxW(g_window, L"This shortcut is already in use by another application.", L"JedkxClicker", MB_ICONWARNING);
        SendMessageW(g_hotkey, CB_SETCURSEL, 0, 0);
        g_hotkeyVk = VK_F6;
        RegisterHotKey(g_window, kHotkeyId, MOD_NOREPEAT, g_hotkeyVk);
    }
}

void BeginPickPosition() {
    if (g_picking) return;
    g_picking = true;
    g_previousLeftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    SetStatus(L"Click the target point once. Press Esc to cancel.");
    SetTimer(g_window, kPickTimerId, 20, nullptr);
}

void PollPickPosition() {
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        g_picking = false;
        KillTimer(g_window, kPickTimerId);
        SetStatus(L"Ready");
        return;
    }
    const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (leftDown && !g_previousLeftDown) {
        GetCursorPos(&g_fixedPoint);
        wchar_t status[96]{};
        wsprintfW(status, L"Fixed point selected: %ld, %ld", g_fixedPoint.x, g_fixedPoint.y);
        SetStatus(status);
        g_picking = false;
        KillTimer(g_window, kPickTimerId);
    }
    g_previousLeftDown = leftDown;
}

HWND AddButton(HWND parent, const wchar_t* text, int id, int x, int y, int width, DWORD style = BS_PUSHBUTTON) {
    const int height = id == kIdStartStop ? 44 : 32;
    HWND button = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_textFont), TRUE);
    return button;
}

void AddComboItem(HWND combo, const wchar_t* item) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
}

void CreateInterface(HWND window) {
    g_interval = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"100", WS_CHILD | WS_VISIBLE | ES_NUMBER, 388, 170, 58, 34, window, reinterpret_cast<HMENU>(kIdInterval), nullptr, nullptr);

    g_button = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 292, 225, 186, 180, window, reinterpret_cast<HMENU>(kIdButton), nullptr, nullptr);
    AddComboItem(g_button, L"Sol"); AddComboItem(g_button, L"Sağ"); AddComboItem(g_button, L"Orta");
    SendMessageW(g_button, CB_SETCURSEL, 0, 0);

    g_clickType = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 292, 280, 186, 180, window, reinterpret_cast<HMENU>(kIdClickType), nullptr, nullptr);
    AddComboItem(g_clickType, L"Tek"); AddComboItem(g_clickType, L"Çift");
    SendMessageW(g_clickType, CB_SETCURSEL, 0, 0);

    g_hotkey = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 292, 335, 186, 180, window, reinterpret_cast<HMENU>(kIdHotkey), nullptr, nullptr);
    AddComboItem(g_hotkey, L"F6"); AddComboItem(g_hotkey, L"F7"); AddComboItem(g_hotkey, L"F8"); AddComboItem(g_hotkey, L"F9"); AddComboItem(g_hotkey, L"F10"); AddComboItem(g_hotkey, L"F11");
    SendMessageW(g_hotkey, CB_SETCURSEL, 0, 0);

    g_repeat = AddButton(window, L"Stop after a fixed number of clicks", kIdRepeat, 42, 433, 290, BS_AUTOCHECKBOX);
    g_repeatCount = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_DISABLED, 388, 429, 90, 34, window, reinterpret_cast<HMENU>(kIdRepeatCount), nullptr, nullptr);

    g_currentPosition = AddButton(window, L"Follow cursor", kIdCurrentPosition, 42, 482, 150, BS_AUTORADIOBUTTON | WS_GROUP);
    g_fixedPosition = AddButton(window, L"Fixed point", kIdFixedPosition, 194, 482, 120, BS_AUTORADIOBUTTON);
    g_pick = AddButton(window, L"Choose point", kIdPick, 348, 478, 130, BS_OWNERDRAW);
    SendMessageW(g_currentPosition, BM_SETCHECK, BST_CHECKED, 0);
    EnableWindow(g_pick, FALSE);

    g_startStop = AddButton(window, L"Start clicking", kIdStartStop, 42, 558, 436, BS_OWNERDRAW);
    g_status = CreateWindowExW(0, L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_CENTER, 42, 614, 436, 22, window, reinterpret_cast<HMENU>(kIdStatus), nullptr, nullptr);

    const HWND controls[] = { g_interval, g_button, g_clickType, g_repeatCount, g_currentPosition, g_fixedPosition, g_hotkey, g_status };
    for (HWND control : controls) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_textFont), TRUE);
        SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    }
}

void DrawCard(HDC dc, const RECT& rect) {
    HBRUSH brush = CreateSolidBrush(kCard);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(56, 56, 56));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 14, 14);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawTextBlock(HDC dc, const wchar_t* text, RECT rect, HFONT font, COLORREF color, UINT flags) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rect, flags);
    SelectObject(dc, oldFont);
}

void PaintInterface(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, g_backgroundBrush);

    DrawTextBlock(dc, L"JedkxClicker", RECT{ 42, 24, 300, 54 }, g_titleFont, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    HBRUSH badge = CreateSolidBrush(g_running ? RGB(28, 72, 43) : RGB(69, 52, 112));
    HPEN badgePen = CreatePen(PS_SOLID, 1, g_running ? RGB(115, 201, 145) : kAccent);
    HGDIOBJ oldBrush = SelectObject(dc, badge);
    HGDIOBJ oldPen = SelectObject(dc, badgePen);
    RoundRect(dc, 387, 30, 478, 56, 13, 13);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
    DeleteObject(badge); DeleteObject(badgePen);
    DrawTextBlock(dc, g_running ? L"RUNNING" : L"READY", RECT{ 387, 30, 478, 56 }, g_sectionFont, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DrawCard(dc, RECT{ 20, 92, 500, 383 });
    DrawCard(dc, RECT{ 20, 399, 500, 530 });
    DrawTextBlock(dc, L"CLICK SETTINGS", RECT{ 42, 106, 300, 126 }, g_sectionFont, kAccent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Configure how each click is issued.", RECT{ 42, 126, 380, 146 }, g_textFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Click interval", RECT{ 42, 164, 250, 184 }, g_textFont, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Delay between clicks", RECT{ 42, 184, 250, 204 }, g_textFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"ms", RECT{ 452, 170, 478, 204 }, g_textFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Mouse button", RECT{ 42, 218, 260, 238 }, g_textFont, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Button to automate", RECT{ 42, 238, 260, 258 }, g_textFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Click type", RECT{ 42, 273, 260, 293 }, g_textFont, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Single or double click", RECT{ 42, 293, 260, 313 }, g_textFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Toggle shortcut", RECT{ 42, 328, 260, 348 }, g_textFont, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Start or stop from any app", RECT{ 42, 348, 260, 368 }, g_textFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    DrawTextBlock(dc, L"RUN BEHAVIOR", RECT{ 42, 413, 300, 433 }, g_sectionFont, kAccent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Clicks", RECT{ 388, 409, 478, 429 }, g_textFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Target location", RECT{ 42, 463, 260, 483 }, g_textFont, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Choose where clicks are sent", RECT{ 42, 503, 310, 523 }, g_textFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, L"Use the selected shortcut to start or stop clicking.", RECT{ 42, 642, 478, 662 }, g_textFont, kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    EndPaint(window, &paint);
}

void DrawButton(const DRAWITEMSTRUCT* item) {
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool startButton = item->CtlID == kIdStartStop;
    COLORREF fill = startButton ? (pressed ? RGB(124, 88, 215) : kAccent) : RGB(36, 36, 36);
    COLORREF border = startButton ? fill : RGB(94, 94, 94);
    COLORREF text = startButton ? RGB(255, 255, 255) : kInk;
    if (disabled) { fill = RGB(36, 36, 36); border = fill; text = RGB(115, 115, 115); }
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush);
    HGDIOBJ oldPen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right, item->rcItem.bottom, 10, 10);
    SelectObject(item->hDC, oldBrush); SelectObject(item->hDC, oldPen);
    DeleteObject(brush); DeleteObject(pen);
    wchar_t label[64]{};
    GetWindowTextW(item->hwndItem, label, ARRAYSIZE(label));
    DrawTextBlock(item->hDC, label, item->rcItem, g_textFont, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_window = window;
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        CreateInterface(window);
        RegisterSelectedHotkey();
        return 0;
    case WM_PAINT:
        PaintInterface(window);
        return 0;
    case WM_DRAWITEM:
        DrawButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam) == g_status ? kMuted : kInk);
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wParam), kInk);
        SetBkColor(reinterpret_cast<HDC>(wParam), kCard);
        return reinterpret_cast<LRESULT>(g_cardBrush);
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wParam), kInk);
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(36, 36, 36));
        return reinterpret_cast<LRESULT>(g_inputBrush);
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case kIdStartStop: ToggleClicking(); return 0;
            case kIdRepeat: UpdateRepeatControls(); return 0;
            case kIdCurrentPosition: UpdatePositionControls(); return 0;
            case kIdFixedPosition: UpdatePositionControls(); return 0;
            case kIdPick: BeginPickPosition(); return 0;
            }
        }
        if (LOWORD(wParam) == kIdHotkey && HIWORD(wParam) == CBN_SELCHANGE) {
            RegisterSelectedHotkey();
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (wParam == kHotkeyId && !g_picking) ToggleClicking();
        return 0;
    case WM_TIMER:
        if (wParam == kPickTimerId) PollPickPosition();
        return 0;
    case WM_APP + 1:
        StopClicking();
        return 0;
    case WM_CLOSE:
        StopClicking();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (g_picking) KillTimer(window, kPickTimerId);
        UnregisterHotKey(window, kHotkeyId);
        if (g_stopEvent != nullptr) CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    INITCOMMONCONTROLSEX commonControls{ sizeof(commonControls), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&commonControls);
    g_titleFont = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_sectionFont = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_textFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_backgroundBrush = CreateSolidBrush(kBackground);
    g_inputBrush = CreateSolidBrush(RGB(36, 36, 36));
    g_cardBrush = CreateSolidBrush(kCard);

    const wchar_t className[] = L"MinimalClickerWindow";
    WNDCLASSEXW windowClass{ sizeof(windowClass) };
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = g_backgroundBrush;
    windowClass.lpszClassName = className;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kAppIconId));
    windowClass.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(kAppIconId));
    RegisterClassExW(&windowClass);

    HWND window = CreateWindowExW(0, className, L"JedkxClicker", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 536, 710, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) return 1;
    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(window, 20, &darkMode, sizeof(darkMode));
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DeleteObject(g_titleFont);
    DeleteObject(g_sectionFont);
    DeleteObject(g_textFont);
    DeleteObject(g_inputBrush);
    DeleteObject(g_cardBrush);
    DeleteObject(g_backgroundBrush);
    return static_cast<int>(message.wParam);
}
