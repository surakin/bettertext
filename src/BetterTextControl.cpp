#include "BetterTextInternal.h"

#include <algorithm>
#include <imm.h>
#include <strsafe.h>
#include <windowsx.h>

namespace bettertext {
namespace {

constexpr float kPadding = 8.0f;

D2D1_COLOR_F Color(uint32_t rgba) {
    return D2D1::ColorF(
        static_cast<float>((rgba >> 24) & 0xff) / 255.0f,
        static_cast<float>((rgba >> 16) & 0xff) / 255.0f,
        static_cast<float>((rgba >> 8) & 0xff) / 255.0f,
        static_cast<float>(rgba & 0xff) / 255.0f);
}

int64_t SelectionStart(const ControlState& state) {
    return std::min(state.selection.anchor, state.selection.caret);
}

int64_t SelectionEnd(const ControlState& state) {
    return std::max(state.selection.anchor, state.selection.caret);
}

bool HasSelection(const ControlState& state) {
    return state.selection.anchor != state.selection.caret;
}

RECT ClientRect(HWND hwnd) {
    RECT rect{};
    GetClientRect(hwnd, &rect);
    return rect;
}

HRESULT EnsureFactories(ControlState* state) {
    if (!state->d2d_factory) {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, state->d2d_factory.GetAddressOf());
        if (FAILED(hr)) {
            return hr;
        }
    }
    if (!state->dwrite_factory) {
        HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(state->dwrite_factory.GetAddressOf()));
        if (FAILED(hr)) {
            return hr;
        }
    }
    return S_OK;
}

HRESULT EnsureRenderTarget(ControlState* state) {
    HRESULT hr = EnsureFactories(state);
    if (FAILED(hr)) {
        return hr;
    }
    if (!state->render_target) {
        RECT rect = ClientRect(state->hwnd);
        const D2D1_SIZE_U size = D2D1::SizeU(
            static_cast<UINT32>(std::max<LONG>(1, rect.right - rect.left)),
            static_cast<UINT32>(std::max<LONG>(1, rect.bottom - rect.top)));
        hr = state->d2d_factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(state->hwnd, size),
            state->render_target.GetAddressOf());
        if (FAILED(hr)) {
            return hr;
        }
    }

    if (!state->foreground_brush) {
        state->render_target->CreateSolidColorBrush(Color(state->theme.foreground_rgba), state->foreground_brush.GetAddressOf());
    }
    if (!state->selection_brush) {
        state->render_target->CreateSolidColorBrush(Color(state->theme.selection_rgba), state->selection_brush.GetAddressOf());
    }
    if (!state->caret_brush) {
        state->render_target->CreateSolidColorBrush(Color(state->theme.caret_rgba), state->caret_brush.GetAddressOf());
    }
    return S_OK;
}

HRESULT EnsureTextFormat(ControlState* state) {
    HRESULT hr = EnsureFactories(state);
    if (FAILED(hr)) {
        return hr;
    }
    if (state->text_format) {
        return S_OK;
    }

    Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
    IDWriteFontCollection* collection_raw = nullptr;
    if (state->font_provider &&
        SUCCEEDED(state->font_provider->CreateFontCollection(state->dwrite_factory.Get(), &collection_raw)) &&
        collection_raw) {
        collection.Attach(collection_raw);
    }

    hr = state->dwrite_factory->CreateTextFormat(
        state->default_style.font_family.c_str(),
        collection.Get(),
        static_cast<DWRITE_FONT_WEIGHT>(state->default_style.font_weight),
        state->default_style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        state->default_style.font_size,
        L"",
        state->text_format.GetAddressOf());
    if (SUCCEEDED(hr)) {
        state->text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        state->text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    return hr;
}

HRESULT CreateLayout(ControlState* state, IDWriteTextLayout** layout) {
    *layout = nullptr;
    HRESULT hr = EnsureTextFormat(state);
    if (FAILED(hr)) {
        return hr;
    }

    RECT rect = ClientRect(state->hwnd);
    const float width = std::max(1.0f, static_cast<float>(rect.right - rect.left) - (kPadding * 2.0f));
    const std::wstring text = state->document.PlainText();
    hr = state->dwrite_factory->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        state->text_format.Get(),
        width,
        100000.0f,
        layout);
    if (SUCCEEDED(hr) && state->default_style.underline && !text.empty()) {
        (*layout)->SetUnderline(TRUE, DWRITE_TEXT_RANGE{ 0, static_cast<UINT32>(text.size()) });
    }
    return hr;
}

float LayoutHeight(ControlState* state) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(CreateLayout(state, layout.GetAddressOf())) || !layout) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) {
        return 0.0f;
    }
    return metrics.height + kPadding * 2.0f;
}

void UpdateScrollInfo(ControlState* state) {
    RECT rect = ClientRect(state->hwnd);
    const float content_height = LayoutHeight(state);
    const int page = std::max<LONG>(1, rect.bottom - rect.top);
    const int max_pos = std::max(0, static_cast<int>(content_height) - page);
    state->scroll_y = std::clamp(state->scroll_y, 0.0f, static_cast<float>(max_pos));

    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
    info.nMin = 0;
    info.nMax = std::max(page, static_cast<int>(content_height));
    info.nPage = static_cast<UINT>(page);
    info.nPos = static_cast<int>(state->scroll_y);
    SetScrollInfo(state->hwnd, SB_VERT, &info, TRUE);
}

void PaintGdiFallback(ControlState* state) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(state->hwnd, &ps);
    RECT rect = ClientRect(state->hwnd);
    HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &rect, background);
    DeleteObject(background);
    rect.left += static_cast<LONG>(kPadding);
    rect.top += static_cast<LONG>(kPadding - state->scroll_y);
    DrawTextW(dc, state->document.PlainText().c_str(), -1, &rect, DT_LEFT | DT_TOP | DT_WORDBREAK);
    EndPaint(state->hwnd, &ps);
}

void Paint(ControlState* state) {
    if (FAILED(EnsureRenderTarget(state))) {
        PaintGdiFallback(state);
        return;
    }

    PAINTSTRUCT ps{};
    BeginPaint(state->hwnd, &ps);

    state->render_target->BeginDraw();
    state->render_target->Clear(Color(state->theme.background_rgba));

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (SUCCEEDED(CreateLayout(state, layout.GetAddressOf())) && layout) {
        const D2D1_POINT_2F origin = D2D1::Point2F(kPadding, kPadding - state->scroll_y);

        if (HasSelection(*state)) {
            const UINT32 start = static_cast<UINT32>(SelectionStart(*state));
            const UINT32 length = static_cast<UINT32>(SelectionEnd(*state) - SelectionStart(*state));
            UINT32 actual = 0;
            HRESULT hr = layout->HitTestTextRange(start, length, origin.x, origin.y, nullptr, 0, &actual);
            if (hr == E_NOT_SUFFICIENT_BUFFER && actual > 0) {
                std::vector<DWRITE_HIT_TEST_METRICS> metrics(actual);
                if (SUCCEEDED(layout->HitTestTextRange(start, length, origin.x, origin.y, metrics.data(), actual, &actual))) {
                    for (UINT32 i = 0; i < actual; ++i) {
                        const auto& m = metrics[i];
                        state->render_target->FillRectangle(
                            D2D1::RectF(m.left, m.top, m.left + m.width, m.top + m.height),
                            state->selection_brush.Get());
                    }
                }
            }
        }

        state->render_target->DrawTextLayout(origin, layout.Get(), state->foreground_brush.Get());

        if (GetFocus() == state->hwnd) {
            const UINT32 caret = static_cast<UINT32>(std::clamp<int64_t>(
                state->selection.caret,
                0,
                static_cast<int64_t>(state->document.Length())));
            FLOAT x = 0.0f;
            FLOAT y = 0.0f;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (SUCCEEDED(layout->HitTestTextPosition(caret, FALSE, &x, &y, &metrics))) {
                const float left = origin.x + x;
                const float top = origin.y + y;
                state->render_target->DrawLine(
                    D2D1::Point2F(left, top),
                    D2D1::Point2F(left, top + metrics.height),
                    state->caret_brush.Get(),
                    1.0f);
            }
        }
    }

    HRESULT hr = state->render_target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ResetRenderResources(state);
    }
    EndPaint(state->hwnd, &ps);
}

int64_t HitTest(ControlState* state, float x, float y) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(CreateLayout(state, layout.GetAddressOf())) || !layout) {
        return state->selection.caret;
    }
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    const HRESULT hr = layout->HitTestPoint(
        x - kPadding,
        y - kPadding + state->scroll_y,
        &trailing,
        &inside,
        &metrics);
    if (FAILED(hr)) {
        return state->selection.caret;
    }
    int64_t position = static_cast<int64_t>(metrics.textPosition) + (trailing ? 1 : 0);
    return std::clamp<int64_t>(position, 0, static_cast<int64_t>(state->document.Length()));
}

std::wstring SelectedText(ControlState* state) {
    const std::wstring text = state->document.PlainText();
    const int64_t start = SelectionStart(*state);
    const int64_t end = SelectionEnd(*state);
    if (start < 0 || end < start || static_cast<size_t>(end) > text.size()) {
        return {};
    }
    return text.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
}

void CopySelectionToClipboard(ControlState* state) {
    const std::wstring selected = SelectedText(state);
    if (selected.empty() || !OpenClipboard(state->hwnd)) {
        return;
    }
    EmptyClipboard();
    const SIZE_T bytes = (selected.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* data = GlobalLock(memory);
        if (data) {
            memcpy(data, selected.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
            memory = nullptr;
        }
    }
    if (memory) {
        GlobalFree(memory);
    }
    CloseClipboard();
}

void PasteClipboardText(ControlState* state) {
    if (state->read_only || !OpenClipboard(state->hwnd)) {
        return;
    }
    HGLOBAL memory = GetClipboardData(CF_UNICODETEXT);
    if (memory) {
        const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(memory));
        if (text) {
            BetterTextInsertText(state->hwnd, text);
            GlobalUnlock(memory);
        }
    }
    CloseClipboard();
}

void DeleteSelectionOrRange(ControlState* state, bool backward) {
    if (state->read_only) {
        return;
    }
    int64_t start = SelectionStart(*state);
    int64_t end = SelectionEnd(*state);
    if (start == end) {
        if (backward) {
            if (start == 0) {
                return;
            }
            --start;
        } else {
            if (end >= static_cast<int64_t>(state->document.Length())) {
                return;
            }
            ++end;
        }
    }
    state->PushUndo();
    state->ClearRedo();
    state->document.DeleteRange(static_cast<size_t>(start), static_cast<size_t>(end - start));
    state->selection = { start, start };
    InvalidateBetterText(state);
}

void MoveCaret(ControlState* state, int64_t caret, bool extend) {
    caret = std::clamp<int64_t>(caret, 0, static_cast<int64_t>(state->document.Length()));
    if (extend) {
        state->selection.caret = caret;
    } else {
        state->selection = { caret, caret };
    }
    InvalidateBetterText(state);
}

bool CtrlDown() {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

bool ShiftDown() {
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

LRESULT HandleKeyDown(ControlState* state, WPARAM key) {
    const bool ctrl = CtrlDown();
    const bool shift = ShiftDown();
    if (ctrl) {
        switch (key) {
        case L'A':
            state->selection = { 0, static_cast<int64_t>(state->document.Length()) };
            InvalidateBetterText(state);
            return 0;
        case L'C':
            CopySelectionToClipboard(state);
            return 0;
        case L'V':
            PasteClipboardText(state);
            return 0;
        case L'X':
            CopySelectionToClipboard(state);
            DeleteSelectionOrRange(state, false);
            return 0;
        case L'Z':
            BetterTextUndo(state->hwnd);
            return 0;
        case L'Y':
            BetterTextRedo(state->hwnd);
            return 0;
        default:
            break;
        }
    }

    switch (key) {
    case VK_LEFT:
        MoveCaret(state, state->selection.caret - 1, shift);
        return 0;
    case VK_RIGHT:
        MoveCaret(state, state->selection.caret + 1, shift);
        return 0;
    case VK_HOME:
        MoveCaret(state, 0, shift);
        return 0;
    case VK_END:
        MoveCaret(state, static_cast<int64_t>(state->document.Length()), shift);
        return 0;
    case VK_BACK:
        DeleteSelectionOrRange(state, true);
        return 0;
    case VK_DELETE:
        DeleteSelectionOrRange(state, false);
        return 0;
    default:
        return DefWindowProcW(state->hwnd, WM_KEYDOWN, key, 0);
    }
}

LRESULT HandleChar(ControlState* state, WPARAM ch) {
    if (state->read_only || CtrlDown()) {
        return 0;
    }
    if (ch == L'\b' || ch == 0x1b) {
        return 0;
    }

    wchar_t buffer[3] = {};
    if (ch == L'\r') {
        buffer[0] = L'\n';
    } else if (ch == L'\t' || ch >= 0x20) {
        buffer[0] = static_cast<wchar_t>(ch);
    } else {
        return 0;
    }
    BetterTextInsertText(state->hwnd, buffer);
    UpdateScrollInfo(state);
    return 0;
}

LRESULT HandleImeComposition(ControlState* state, LPARAM lparam) {
    if (state->read_only || !(lparam & GCS_RESULTSTR)) {
        return 0;
    }
    HIMC context = ImmGetContext(state->hwnd);
    if (!context) {
        return 0;
    }
    const LONG bytes = ImmGetCompositionStringW(context, GCS_RESULTSTR, nullptr, 0);
    if (bytes > 0) {
        std::wstring text(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
        ImmGetCompositionStringW(context, GCS_RESULTSTR, text.data(), bytes);
        BetterTextInsertText(state->hwnd, text.c_str());
    }
    ImmReleaseContext(state->hwnd, context);
    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    ControlState* state = GetState(hwnd);

    switch (message) {
    case WM_NCCREATE: {
        auto* created = new ControlState();
        created->hwnd = hwnd;
        created->document.SetDefaultStyle(created->default_style);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
        return TRUE;
    }
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateBetterText(state);
        return 0;
    case WM_SIZE:
        if (state && state->render_target) {
            const UINT width = LOWORD(lparam);
            const UINT height = HIWORD(lparam);
            state->render_target->Resize(D2D1::SizeU(width, height));
        }
        if (state) {
            UpdateScrollInfo(state);
        }
        return 0;
    case WM_PAINT:
        if (state) {
            Paint(state);
            UpdateScrollInfo(state);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        return state ? HandleKeyDown(state, wparam) : 0;
    case WM_CHAR:
        return state ? HandleChar(state, wparam) : 0;
    case WM_IME_COMPOSITION:
        return state ? HandleImeComposition(state, lparam) : 0;
    case WM_LBUTTONDOWN:
        if (state) {
            SetFocus(hwnd);
            SetCapture(hwnd);
            state->dragging = true;
            const int64_t pos = HitTest(state, static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)));
            state->selection = { pos, pos };
            InvalidateBetterText(state);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state && state->dragging && (wparam & MK_LBUTTON)) {
            const int64_t pos = HitTest(state, static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)));
            state->selection.caret = pos;
            InvalidateBetterText(state);
        }
        return 0;
    case WM_LBUTTONUP:
        if (state) {
            state->dragging = false;
            ReleaseCapture();
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            state->scroll_y -= static_cast<float>(delta) / WHEEL_DELTA * 48.0f;
            UpdateScrollInfo(state);
            InvalidateBetterText(state);
        }
        return 0;
    case WM_VSCROLL:
        if (state) {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &info);
            int pos = info.nPos;
            switch (LOWORD(wparam)) {
            case SB_LINEUP: pos -= 24; break;
            case SB_LINEDOWN: pos += 24; break;
            case SB_PAGEUP: pos -= static_cast<int>(info.nPage); break;
            case SB_PAGEDOWN: pos += static_cast<int>(info.nPage); break;
            case SB_THUMBTRACK: pos = info.nTrackPos; break;
            default: break;
            }
            state->scroll_y = static_cast<float>(std::clamp(pos, info.nMin, info.nMax));
            UpdateScrollInfo(state);
            InvalidateBetterText(state);
        }
        return 0;
    case WM_GETTEXT: {
        if (!state) {
            return 0;
        }
        int copied = 0;
        CopyStringToBuffer(state->document.PlainText(), reinterpret_cast<wchar_t*>(lparam), static_cast<int>(wparam), &copied);
        return copied;
    }
    case WM_GETTEXTLENGTH:
        return state ? state->document.PlainText().size() : 0;
    case WM_SETTEXT:
        return BetterTextSetText(hwnd, reinterpret_cast<const wchar_t*>(lparam));
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

} // namespace

ControlState* GetState(HWND hwnd) {
    return hwnd ? reinterpret_cast<ControlState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)) : nullptr;
}

BOOL RegisterBetterTextControl(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.cbWndExtra = 0;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.lpszClassName = BETTERTEXT_CLASS_NAME;

    ATOM atom = RegisterClassExW(&wc);
    if (atom) {
        return TRUE;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? TRUE : FALSE;
}

void ResetRenderResources(ControlState* state) {
    if (!state) {
        return;
    }
    state->caret_brush.Reset();
    state->selection_brush.Reset();
    state->foreground_brush.Reset();
    state->render_target.Reset();
}

} // namespace bettertext
