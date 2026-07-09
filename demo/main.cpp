#include "BetterText/BetterText.h"

#include <string>
#include <windows.h>

namespace {

constexpr int kEditorId = 1001;
constexpr int kInsertImageId = 40001;
constexpr int kJsonRoundTripId = 40002;
constexpr int kHtmlRoundTripId = 40003;
constexpr int kReadOnlyId = 40004;

HWND g_editor = nullptr;
bool g_read_only = false;

void PopulateEditor(HWND editor) {
    const wchar_t* intro =
        L"BetterText demo\r\n"
        L"\r\n"
        L"This is a native Win32 custom control using DirectWrite and Direct2D.\r\n"
        L"Try typing, selecting, copying, pasting, undoing, and using an IME.\r\n"
        L"Emoji fallback smoke test: \U0001f642 \U0001f680\r\n";
    BetterTextSetText(editor, intro);
    BetterTextSetSelection(editor, BetterTextGetTextLength(editor), BetterTextGetTextLength(editor));
    BetterTextInsertImageUri(editor, L"file:///C:/Images/bettertext-sample.png", L"Sample image URI", 120.0f, 90.0f);
}

HMENU CreateDemoMenu() {
    HMENU menu = CreateMenu();
    HMENU demo = CreatePopupMenu();
    AppendMenuW(demo, MF_STRING, kInsertImageId, L"Insert URI image");
    AppendMenuW(demo, MF_STRING, kJsonRoundTripId, L"JSON round trip");
    AppendMenuW(demo, MF_STRING, kHtmlRoundTripId, L"HTML round trip");
    AppendMenuW(demo, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(demo, MF_STRING, kReadOnlyId, L"Toggle read-only");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(demo), L"BetterText");
    return menu;
}

void RoundTripJson(HWND owner) {
    const int length = BetterTextGetDocumentJsonLength(g_editor);
    std::wstring json(static_cast<size_t>(length) + 1, L'\0');
    BetterTextGetDocumentJson(g_editor, json.data(), static_cast<int>(json.size()));
    json.resize(static_cast<size_t>(length));
    if (BetterTextSetDocumentJson(g_editor, json.c_str())) {
        MessageBoxW(owner, L"JSON exported and imported successfully.", L"BetterText", MB_OK);
    } else {
        MessageBoxW(owner, L"JSON round trip failed.", L"BetterText", MB_ICONERROR);
    }
}

void RoundTripHtml(HWND owner) {
    const int length = BetterTextGetHtmlLength(g_editor);
    std::wstring html(static_cast<size_t>(length) + 1, L'\0');
    BetterTextGetHtml(g_editor, html.data(), static_cast<int>(html.size()));
    html.resize(static_cast<size_t>(length));
    if (BetterTextSetHtml(g_editor, html.c_str())) {
        MessageBoxW(owner, L"HTML exported and imported successfully.", L"BetterText", MB_OK);
    } else {
        MessageBoxW(owner, L"HTML round trip failed.", L"BetterText", MB_ICONERROR);
    }
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        g_editor = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            BETTERTEXT_CLASS_NAME,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL,
            0,
            0,
            0,
            0,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditorId)),
            reinterpret_cast<LPCREATESTRUCTW>(lparam)->hInstance,
            nullptr);
        PopulateEditor(g_editor);
        return 0;
    case WM_SIZE:
        if (g_editor) {
            MoveWindow(g_editor, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case kInsertImageId:
            BetterTextInsertImageUri(g_editor, L"file:///C:/Images/inserted-from-menu.png", L"Inserted URI image", 120.0f, 90.0f);
            return 0;
        case kJsonRoundTripId:
            RoundTripJson(hwnd);
            return 0;
        case kHtmlRoundTripId:
            RoundTripHtml(hwnd);
            return 0;
        case kReadOnlyId:
            g_read_only = !g_read_only;
            BetterTextSetReadOnly(g_editor, g_read_only ? TRUE : FALSE);
            return 0;
        default:
            break;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    BetterTextRegisterControl(instance);

    const wchar_t* class_name = L"BetterTextDemo.Window";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = class_name;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        class_name,
        L"BetterText Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        640,
        nullptr,
        CreateDemoMenu(),
        instance,
        nullptr);

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
