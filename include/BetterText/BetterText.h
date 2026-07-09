#pragma once

#include <stdint.h>
#include <windows.h>

#define BETTERTEXT_CLASS_NAME L"BetterText.Control"
#define BETTERTEXT_VERSION_MAJOR 0
#define BETTERTEXT_VERSION_MINOR 1
#define BETTERTEXT_VERSION_PATCH 0

#if defined(BETTERTEXT_SHARED)
#  if defined(BETTERTEXT_BUILDING)
#    define BETTERTEXT_API __declspec(dllexport)
#  else
#    define BETTERTEXT_API __declspec(dllimport)
#  endif
#else
#  define BETTERTEXT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BetterTextSelection {
    int64_t anchor;
    int64_t caret;
} BetterTextSelection;

typedef struct BetterTextTheme {
    uint32_t background_rgba;
    uint32_t foreground_rgba;
    uint32_t selection_rgba;
    uint32_t caret_rgba;
    uint32_t placeholder_rgba;
} BetterTextTheme;

typedef struct BetterTextTextStyle {
    const wchar_t* font_family;
    float font_size;
    uint32_t foreground_rgba;
    int32_t font_weight;
    BOOL italic;
    BOOL underline;
} BetterTextTextStyle;

BETTERTEXT_API BOOL BetterTextRegisterControl(HINSTANCE instance);

BETTERTEXT_API BOOL BetterTextSetText(HWND control, const wchar_t* text);
BETTERTEXT_API int BetterTextGetTextLength(HWND control);
BETTERTEXT_API int BetterTextGetText(HWND control, wchar_t* buffer, int buffer_length);

BETTERTEXT_API BOOL BetterTextSetDocumentJson(HWND control, const wchar_t* json);
BETTERTEXT_API int BetterTextGetDocumentJsonLength(HWND control);
BETTERTEXT_API int BetterTextGetDocumentJson(HWND control, wchar_t* buffer, int buffer_length);

BETTERTEXT_API BOOL BetterTextSetHtml(HWND control, const wchar_t* html);
BETTERTEXT_API int BetterTextGetHtmlLength(HWND control);
BETTERTEXT_API int BetterTextGetHtml(HWND control, wchar_t* buffer, int buffer_length);

BETTERTEXT_API BOOL BetterTextInsertText(HWND control, const wchar_t* text);
BETTERTEXT_API BOOL BetterTextInsertImageUri(
    HWND control,
    const wchar_t* uri,
    const wchar_t* alt_text,
    float display_width,
    float display_height);

BETTERTEXT_API BOOL BetterTextSetSelection(HWND control, int64_t anchor, int64_t caret);
BETTERTEXT_API BOOL BetterTextGetSelection(HWND control, BetterTextSelection* selection);

BETTERTEXT_API BOOL BetterTextUndo(HWND control);
BETTERTEXT_API BOOL BetterTextRedo(HWND control);
BETTERTEXT_API BOOL BetterTextCanUndo(HWND control);
BETTERTEXT_API BOOL BetterTextCanRedo(HWND control);

BETTERTEXT_API BOOL BetterTextSetReadOnly(HWND control, BOOL read_only);
BETTERTEXT_API BOOL BetterTextGetReadOnly(HWND control);

BETTERTEXT_API BOOL BetterTextSetTheme(HWND control, const BetterTextTheme* theme);
BETTERTEXT_API BOOL BetterTextGetTheme(HWND control, BetterTextTheme* theme);
BETTERTEXT_API BOOL BetterTextSetDefaultTextStyle(HWND control, const BetterTextTextStyle* style);
BETTERTEXT_API BOOL BetterTextGetDefaultTextStyle(HWND control, BetterTextTextStyle* style);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <dwrite.h>
#include <objidl.h>
#include <wincodec.h>

class IBetterTextImageProvider {
public:
    virtual ~IBetterTextImageProvider() = default;
    virtual void ResolveImageUri(
        HWND control,
        uint64_t request_id,
        const wchar_t* uri,
        float display_width,
        float display_height) = 0;
};

class IBetterTextClipboardAdapter {
public:
    virtual ~IBetterTextClipboardAdapter() = default;
    virtual bool MapClipboardImageToUri(
        HWND control,
        IDataObject* data_object,
        wchar_t* uri_buffer,
        uint32_t uri_buffer_length) = 0;
};

class IBetterTextFontProvider {
public:
    virtual ~IBetterTextFontProvider() = default;
    virtual HRESULT CreateFontCollection(
        IDWriteFactory* factory,
        IDWriteFontCollection** collection) = 0;
    virtual const wchar_t* EmojiFallbackFamily() const = 0;
};

extern "C" {
BETTERTEXT_API BOOL BetterTextSetImageProvider(HWND control, IBetterTextImageProvider* provider);
BETTERTEXT_API BOOL BetterTextNotifyImageResolved(
    HWND control,
    uint64_t request_id,
    const wchar_t* uri,
    IWICBitmapSource* bitmap,
    HRESULT status);
BETTERTEXT_API BOOL BetterTextSetClipboardAdapter(HWND control, IBetterTextClipboardAdapter* adapter);
BETTERTEXT_API BOOL BetterTextSetFontProvider(HWND control, IBetterTextFontProvider* provider);
}

#endif
