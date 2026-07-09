#include "BetterText/BetterText.h"
#include "BetterTextDocument.h"

#include <iostream>
#include <string>
#include <windows.h>

namespace {

int g_failures = 0;

void Expect(bool condition, const char* message) {
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL: " << message << "\n";
    }
}

void PlainTextRoundTrip() {
    bettertext::Document document;
    document.SetPlainText(L"Hello\r\nWorld");
    Expect(document.PlainText() == L"Hello\nWorld", "plain text normalizes line endings");
    Expect(document.Paragraphs().size() == 2, "plain text creates paragraphs");
}

void JsonRoundTrip() {
    bettertext::Document document;
    document.SetPlainText(L"Alpha\nBeta");
    document.InsertImage(5, L"file:///image.png", L"Image", 40.0f, 30.0f);

    const std::wstring json = document.ToJson();
    Expect(json.find(L"file:///image.png") != std::wstring::npos, "json contains image URI");

    bettertext::Document loaded;
    std::wstring error;
    Expect(loaded.SetJson(json, &error), "json imports");
    Expect(loaded.PlainText() == document.PlainText(), "json round trip preserves flattened text");
    Expect(loaded.ToJson().find(L"\"type\":\"image\"") != std::wstring::npos, "json round trip preserves image run");
}

void HtmlRoundTrip() {
    bettertext::Document document;
    std::wstring error;
    Expect(document.SetHtml(L"<p>Hello<img src=\"file:///x.png\" alt=\"x\" width=\"10\" height=\"20\"></p><p>World</p>", &error), "html imports");
    Expect(document.PlainText() == std::wstring(L"Hello") + wchar_t(0xfffc) + L"\nWorld", "html preserves image and paragraph break");

    const std::wstring html = document.ToHtml();
    Expect(html.find(L"<img") != std::wstring::npos, "html exports image");

    bettertext::Document loaded;
    Expect(loaded.SetHtml(html, &error), "exported html imports");
    Expect(loaded.PlainText() == document.PlainText(), "html round trip preserves flattened text");
}

void EditPreservesImageRuns() {
    bettertext::Document document;
    document.SetPlainText(L"AB");
    document.InsertImage(1, L"file:///image.png", L"Image", 40.0f, 30.0f);
    document.InsertText(1, L"x");
    Expect(document.PlainText() == std::wstring(L"Ax") + wchar_t(0xfffc) + L"B", "insert text preserves image run");
    document.DeleteRange(1, 1);
    Expect(document.ToJson().find(L"file:///image.png") != std::wstring::npos, "delete text preserves image run");
}

void HiddenControlSmokeTest() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    Expect(BetterTextRegisterControl(instance), "register control");
    HWND hwnd = CreateWindowExW(
        0,
        BETTERTEXT_CLASS_NAME,
        L"",
        WS_OVERLAPPED,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        320,
        200,
        nullptr,
        nullptr,
        instance,
        nullptr);
    Expect(hwnd != nullptr, "create hidden control");
    if (!hwnd) {
        return;
    }
    Expect(BetterTextSetText(hwnd, L"Smoke"), "set text through public API");
    Expect(BetterTextGetTextLength(hwnd) == 5, "get text length through public API");
    wchar_t buffer[32] = {};
    BetterTextGetText(hwnd, buffer, 32);
    Expect(std::wstring(buffer) == L"Smoke", "get text through public API");
    DestroyWindow(hwnd);
}

} // namespace

int main() {
    PlainTextRoundTrip();
    JsonRoundTrip();
    HtmlRoundTrip();
    EditPreservesImageRuns();
    HiddenControlSmokeTest();

    if (g_failures != 0) {
        std::cerr << g_failures << " BetterText test(s) failed.\n";
        return 1;
    }
    std::cout << "BetterText tests passed.\n";
    return 0;
}
