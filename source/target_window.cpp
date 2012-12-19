#include "stdafx.h"

#include <functional>
#include <memory>

#include "target_window.h"

using std::function;
using std::unique_ptr;
using std::wstring;

const wchar_t* TargetWindow::GetClassName()
{
    static ATOM className = 0;
    if (!className) {
        WNDCLASSW winClass = {};
        winClass.lpszClassName = L"cleartype_alpha_blending.TargetWindow";
        winClass.lpfnWndProc = &TargetWindow::MyWinProc;
        className = RegisterClassW(&winClass);
    }

    return reinterpret_cast<const wchar_t*>(className);
}

TargetWindow::TargetWindow()
    : CWnd()
    , background_opacity_(1.0f)
    , copy_background_(false)
    , perform_abs_(true)
    , reverse_contrast_(true)
    , use_background_opacity_(false)
{
}

void TargetWindow::Paint(float opacity)
{
    assert(opacity >= 0.0f && opacity <= 1.0f);
    background_opacity_ = opacity;

    if (GetExStyle() & WS_EX_LAYERED) {
        auto autoReleaseDC = [this](CDC* dc) {
            this->ReleaseDC(dc);
        };
        unique_ptr<CDC, function<void (CDC*)>> dc(GetWindowDC(), autoReleaseDC);
        if (!dc)
            return;

        CRect bounds;
        GetClientRect(&bounds);

        CMemDC memDC(*dc, this);
        CBitmap canvas;
        if (!canvas.CreateCompatibleBitmap(&memDC.GetDC(), bounds.Width(),
                                           bounds.Height()))
            return;

        auto oldCanvas = memDC.GetDC().SelectObject(&canvas);
        auto selectBack = [oldCanvas](CDC* dc) {
            dc->SelectObject(oldCanvas);
        };
        unique_ptr<CDC, function<void (CDC*)>> autoSelectBack(&memDC.GetDC(),
                                                              selectBack);

        BITMAP details;
        if (canvas.GetBitmap(&details) != sizeof(details))
            return;

        COLORREF backgroundColor = RGB(236, 161, 0) |
            (static_cast<uint32_t>(opacity * 255) << 24);

        const int bufferSize = details.bmHeight * details.bmWidthBytes;
        unique_ptr<int8_t[]> buffer(new int8_t[bufferSize]);

        uint32_t* int32Pointer = reinterpret_cast<uint32_t*>(buffer.get());
        for (uint32_t* i = int32Pointer;
                i != int32Pointer + bufferSize / sizeof(*int32Pointer); ++i) {
            *i = backgroundColor;
        }

        canvas.SetBitmapBits(bufferSize, int32Pointer);
        RenderClearTypeGlyph(&canvas, &memDC.GetDC(), details, backgroundColor,
                             RGB(0, 0, 0));

        // Perform pre-multiplying.
        canvas.GetBitmapBits(bufferSize, int32Pointer);
        for (uint32_t* i = int32Pointer;
                i != int32Pointer + bufferSize / sizeof(*int32Pointer); ++i) {
            const uint32_t v = *i;
            const uint32_t a = v & 0xFF000000;
            const float o = static_cast<float>(a >> 24) / 255.0f;
            *i = RGB(GetRValue(v) * o, GetGValue(v) * o, GetBValue(v) * o) | a;
        }
        canvas.SetBitmapBits(bufferSize, int32Pointer);

        CSize size(bounds.Width(), bounds.Height());
        CPoint sourcePoint(0, 0);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(dc.get(), nullptr, &size, &memDC.GetDC(),
                            &sourcePoint, 0, &bf, ULW_ALPHA);
    }
}

LRESULT TargetWindow::MyWinProc(HWND hwnd, uint32_t message, WPARAM wParam,
                                LPARAM lParam)
{
    return ::DefWindowProc(hwnd, message, wParam, lParam);
}

void TargetWindow::ApplyOpacity(int8_t* contrastBuffer, int bufferSize,
                                const int8_t* opacityBuffer)
{
    uint32_t const* const opacityPointer =
        reinterpret_cast<uint32_t const* const>(opacityBuffer);
    uint32_t* const contrastPointer =
        reinterpret_cast<uint32_t*>(contrastBuffer);

    for (auto i = contrastPointer;
            i != contrastPointer + bufferSize / sizeof(*i); ++i) {
        const uint32_t& opacityRef = *(opacityPointer + (i - contrastPointer));
        *i = (*i & 0x00FFFFFF) | opacityRef;
    }
}

void TargetWindow::BlendContrastBuffer(CBitmap* canvas,
                                       const BITMAP& canvasDetails,
                                       const CBitmap& contrast,
                                       const BITMAP& contrastDetails)
{
    const int canvasBufferSize =
        canvasDetails.bmHeight * canvasDetails.bmWidthBytes;
    unique_ptr<int8_t[]> canvasBuffer(new int8_t[canvasBufferSize]);
    canvas->GetBitmapBits(canvasBufferSize, canvasBuffer.get());

    const int contrastBufferSize =
        contrastDetails.bmHeight * contrastDetails.bmWidthBytes;
    unique_ptr<int8_t[]> contrastBuffer(new int8_t[contrastBufferSize]);
    contrast.GetBitmapBits(contrastBufferSize, contrastBuffer.get());

    for (int i = 0; i != contrastDetails.bmHeight; ++i) {
        for (int j = 0; j != contrastDetails.bmWidth; ++j) {
            const uint32_t c = *(reinterpret_cast<uint32_t*>(
                contrastBuffer.get() + contrastDetails.bmWidthBytes * i) + j);

            const float contrastOpacity =
                static_cast<float>((c & 0xFF000000) >> 24) / 255.0f;

            uint32_t& r = *(reinterpret_cast<uint32_t*>(
                canvasBuffer.get() + canvasDetails.bmWidthBytes * i) + j);

            // Perform alpha blending.
            const float contrastRed = static_cast<float>(GetRValue(c));
            const float contrastGreen = static_cast<float>(GetGValue(c));
            const float contrastBlue = static_cast<float>(GetBValue(c));

            const float canvasRed = static_cast<float>(GetRValue(r));
            const float canvasGreen = static_cast<float>(GetGValue(r));
            const float canvasBlue = static_cast<float>(GetBValue(r));

            const uint32_t red = static_cast<uint32_t>(
                contrastOpacity * contrastRed +
                    (1.0f - contrastOpacity) * canvasRed);
            const uint32_t green = static_cast<uint32_t>(
                contrastOpacity * contrastGreen +
                    (1.0f - contrastOpacity) * canvasGreen);
            const uint32_t blue = static_cast<uint32_t>(
                contrastOpacity * contrastBlue +
                    (1.0f - contrastOpacity) * canvasBlue);
            float opacity = 1.0f;
            if ((c & 0xFF000000) != 0xFF000000) {
                const float canvasOpacity = static_cast<float>(
                    (r & 0xFF000000) >> 24) / 255.0f;
                opacity = contrastOpacity * contrastOpacity +
                    (1.0f - contrastOpacity) * canvasOpacity;
            }

            r = static_cast<uint32_t>(opacity * 255.0f) << 24 |
                RGB(red, green, blue);
        }
    }
    canvas->SetBitmapBits(canvasBufferSize, canvasBuffer.get());
}

void TargetWindow::CalculateOpacity(int8_t* opacityBuffer, int bufferSize,
                                    const int8_t* contrastBuffer,
                                    COLORREF backgroundColor,
                                    COLORREF textColor)
{
    uint32_t const* const contrastPointer =
        reinterpret_cast<uint32_t const* const>(contrastBuffer);
    uint32_t* const opacityPointer =
        reinterpret_cast<uint32_t*>(opacityBuffer);
    const uint32_t backgroundColorNoAlpha = backgroundColor & 0x00FFFFFF;

    for (auto i = contrastPointer;
            i != contrastPointer + bufferSize / sizeof(*i); ++i) {
        uint32_t& opacityRef = *(opacityPointer + (i - contrastPointer));

        uint32_t v = *i & 0x00FFFFFF;
        if (v == backgroundColorNoAlpha) {
            // Outside the region that the glyph rendered.
            opacityRef = 0x00000000;
        } else if (v == textColor) {
            // Opaque pixels of the glyph.
            opacityRef = 0xFF000000;
        } else {
            // Pixels with Opacity.

            // Recalculate the opacity according to the Apple ClearType opacity
            // equation.
            const float textRed = static_cast<float>(GetRValue(textColor));
            const float textGreen =
                static_cast<float>(GetGValue(textColor));
            const float textBlue = static_cast<float>(GetBValue(textColor));

            const float backgroundRed =
                static_cast<float>(GetRValue(backgroundColor));
            const float backgroundGreen =
                static_cast<float>(GetGValue(backgroundColor));
            const float backgroundBlue =
                static_cast<float>(GetBValue(backgroundColor));

            const float contrastRed = static_cast<float>(GetRValue(v));
            const float contrastGreen = static_cast<float>(GetGValue(v));
            const float contrastBlue = static_cast<float>(GetBValue(v));

            float red = GetRValue(textColor) == GetRValue(backgroundColor) ?
                1.0f :
                ((contrastRed - backgroundRed) / (textRed - backgroundRed));
            float green =
                GetGValue(textColor) == GetGValue(backgroundColor) ?
                    1.0f :
                    ((contrastGreen - backgroundGreen) /
                        (textGreen - backgroundGreen));
            float blue =
                GetBValue(textColor) == GetBValue(backgroundColor) ?
                    1.0f :
                    ((contrastBlue - backgroundBlue) /
                        (textBlue - backgroundBlue));

            if (perform_abs_) {
                red = abs(red);
                green = abs(green);
                blue = abs(blue);
            }

            red = red > 0.5f ? sqrt(red) : red * red;
            green = green > 0.5f ? sqrt(green) : green * green;
            blue = blue > 0.5f ? sqrt(blue) : blue * blue;

//                 red = red + (1.0f - red) * background_opacity_;
//                 green = green + (1.0f - green) * background_opacity_;
//                 blue = blue + (1.0f - blue) * background_opacity_;

            float opacity = 1.0f;//0.299f * red + 0.587f * green + 0.114f * blue;
            opacityRef = static_cast<uint32_t>(opacity * 255.0f) << 24;
        }
    }
}

void TargetWindow::PrepareContrastBuffer(int8_t* contrastBuffer,
                                         const BITMAP& contrastDetails,
                                         const int8_t* canvasBuffer,
                                         const BITMAP& canvasDetails,
                                         COLORREF backgroundColor,
                                         COLORREF textColor,
                                         COLORREF* contrastBackground)
{
    if (copy_background_) {
        *contrastBackground = backgroundColor & 0x00FFFFFF;

        if (!use_background_opacity_) {
            for (int i = 0; i != contrastDetails.bmHeight; ++i) {
                auto contrastAddr =
                    contrastBuffer + contrastDetails.bmWidthBytes * i;
                auto canvasAddr = canvasBuffer + canvasDetails.bmWidthBytes * i;
                memcpy(contrastAddr, canvasAddr, contrastDetails.bmWidthBytes);
            }
        } else {
            for (int i = 0; i != contrastDetails.bmHeight; ++i) {
                for (int j = 0; j != contrastDetails.bmWidth; ++j) {
                    auto contrastAddr =
                        contrastBuffer + contrastDetails.bmWidthBytes * i +
                            j * contrastDetails.bmBitsPixel / 8;

                    const float textRed =
                        static_cast<float>(GetRValue(textColor));
                    const float textGreen =
                        static_cast<float>(GetGValue(textColor));
                    const float textBlue =
                        static_cast<float>(GetBValue(textColor));

                    const float backgroundRed =
                        static_cast<float>(GetRValue(backgroundColor));
                    const float backgroundGreen =
                        static_cast<float>(GetGValue(backgroundColor));
                    const float backgroundBlue =
                        static_cast<float>(GetBValue(backgroundColor));

                    const float cr = static_cast<float>(GetRValue(textColor));
                    const float cg = static_cast<float>(GetGValue(textColor));
                    const float cb = static_cast<float>(GetBValue(textColor));
                    const uint32_t gray = static_cast<uint32_t>(
                        0.299f * cr + 0.587f * cg + 0.114f * cb);

                    const float opacity = static_cast<float>(
                        (backgroundColor & 0xFF000000) >> 24) / 255.0f;

                    const float red =
                        backgroundRed * opacity + (1.0f - opacity) * gray;
                    const float green =
                        backgroundGreen * opacity + (1.0f - opacity) * gray;
                    const float blue =
                        backgroundBlue * opacity + (1.0f - opacity) * gray;

                    *contrastBackground = RGB(static_cast<uint32_t>(red),
                                              static_cast<uint32_t>(green),
                                              static_cast<uint32_t>(blue));
                    *reinterpret_cast<uint32_t*>(contrastAddr) =
                        *contrastBackground;
                }
            }
        }
    } else { // |copy_background_| is false.
        const float cr = reverse_contrast_ ?
            static_cast<float>(255 - GetRValue(textColor)) :
            static_cast<float>(GetRValue(textColor));
        const float cg = reverse_contrast_ ?
            static_cast<float>(255 - GetGValue(textColor)) :
            static_cast<float>(GetGValue(textColor));
        const float cb = reverse_contrast_ ?
            static_cast<float>(255 - GetBValue(textColor)) :
            static_cast<float>(GetBValue(textColor));
        const uint32_t gray =
            static_cast<uint32_t>(0.299f * cr + 0.587f * cg + 0.114f * cb);
        *contrastBackground = RGB(gray, gray, gray);

        for (int i = 0; i != contrastDetails.bmHeight; ++i) {
            for (int j = 0; j != contrastDetails.bmWidth; ++j) {
                auto contrastAddr =
                    contrastBuffer + contrastDetails.bmWidthBytes * i +
                        j * contrastDetails.bmBitsPixel / 8;
                *reinterpret_cast<uint32_t*>(contrastAddr) =
                    *contrastBackground;
            }
        }
    }
}

void TargetWindow::RenderClearTypeGlyph(CBitmap* canvas, CDC* dc,
                                        const BITMAP& details,
                                        COLORREF backgroundColor,
                                        COLORREF textColor)
{
    wstring fontName(L"Î¢ÈíÑÅºÚ");
    LOGFONTW fontDetails = { 0 };
    fontDetails.lfHeight = -12;
    fontDetails.lfQuality = CLEARTYPE_NATURAL_QUALITY;
    std::copy(fontName.begin(), fontName.end(), fontDetails.lfFaceName);
    CFont font;
    if (!font.CreateFontIndirectW(&fontDetails))
        return;

    const int width = 32;
    assert(width <= details.bmWidth);
    assert(width <= details.bmHeight);
    const CRect bounds(CPoint(0, 0), CSize(width, width));

    // A memory DC is needed to perform text rendering on the contrast buffer.
    CMemDC memDC(*dc, this);
    CBitmap contrast;
    if (!contrast.CreateCompatibleBitmap(&memDC.GetDC(), bounds.Width(),
                                         bounds.Height()))
        return;

    auto oldCanvas = memDC.GetDC().SelectObject(&contrast);
    auto selectOldCanvas = [oldCanvas](CDC* dc) {
        dc->SelectObject(oldCanvas);
    };
    unique_ptr<CDC, function<void (CDC*)>> autoSelectOldCanvas(&memDC.GetDC(),
                                                               selectOldCanvas);
    memDC.GetDC().SetTextColor(textColor);
    memDC.GetDC().SetBkMode(TRANSPARENT);

    // Before we call DrawText(), maybe we should fill the contrast buffer with
    // the content within the background canvas.
    BITMAP myDetails;
    if (contrast.GetBitmap(&myDetails) != sizeof(myDetails))
        return;

    const int canvasBufferSize = myDetails.bmHeight * details.bmWidthBytes;
    unique_ptr<int8_t[]> canvasBuffer(new int8_t[canvasBufferSize]);
    canvas->GetBitmapBits(canvasBufferSize, canvasBuffer.get());

    const int contrastBufferSize = myDetails.bmHeight * myDetails.bmWidthBytes;
    unique_ptr<int8_t[]> contrastBuffer(new int8_t[contrastBufferSize]);

    COLORREF contrastBackground;
    PrepareContrastBuffer(contrastBuffer.get(), myDetails, canvasBuffer.get(),
                          details, backgroundColor, textColor,
                          &contrastBackground);
    contrast.SetBitmapBits(contrastBufferSize, contrastBuffer.get());

    // Draw the glyph using Windows API.
    auto oldFont = memDC.GetDC().SelectObject(&font);
    auto selectOldFont = [oldFont](CDC* dc) {
        dc->SelectObject(oldFont);
    };
    unique_ptr<CDC, function<void (CDC*)>> autoSelectOldFont(&memDC.GetDC(),
                                                             selectOldFont);

    CRect textBounds(bounds);
    memDC.GetDC().DrawText(L"¹Ü", &textBounds, DT_NOPREFIX | DT_VCENTER);
    contrast.GetBitmapBits(contrastBufferSize, contrastBuffer.get());

    // A buffer to store the opacity values..
    const int opacityBufferSize = contrastBufferSize;
    unique_ptr<int8_t[]> opacityBuffer(new int8_t[opacityBufferSize]);
    CalculateOpacity(opacityBuffer.get(), opacityBufferSize,
                     contrastBuffer.get(), contrastBackground, textColor);

    // Copy the background into the contrast buffer and perform the actual
    // rendering.
    copy_background_ = true;
    PrepareContrastBuffer(contrastBuffer.get(), myDetails, canvasBuffer.get(),
                          details, backgroundColor, textColor,
                          &contrastBackground);
    contrast.SetBitmapBits(contrastBufferSize, contrastBuffer.get());
    memDC.GetDC().DrawText(L"¹Ü", &textBounds, DT_NOPREFIX | DT_VCENTER);
    contrast.GetBitmapBits(contrastBufferSize, contrastBuffer.get());

    ApplyOpacity(contrastBuffer.get(), contrastBufferSize, opacityBuffer.get());
    contrast.SetBitmapBits(contrastBufferSize, contrastBuffer.get());
//     uint32_t* const int32Pointer =
//         reinterpret_cast<uint32_t*>(contrastBuffer.get());
//     const uint32_t backgroundColorNoAlpha = backgroundColor & 0x00FFFFFF;
//     for (int i = 0; i != myDetails.bmHeight; ++i) {
//         for (int j = 0; j != myDetails.bmWidth; ++j) {
//             uint32_t& r = *(reinterpret_cast<uint32_t*>(
//                 contrastBuffer.get() + myDetails.bmWidthBytes * i) + j);
// 
//             uint32_t v = r & 0x00FFFFFF;
//             if (v == contrastBackground) {
//                 // Outside the region that the glyph rendered.
//                 r = v;
//             } else if (v == textColor) {
//                 // Opaque pixels of the glyph.
//                 r |= 0xFF000000;
//             } else {
//                 // Pixels with Opacity.
// 
//                 // Recalculate the opacity according to the Apple ClearType
//                 // opacity equation.
//                 const float textRed = static_cast<float>(GetRValue(textColor));
//                 const float textGreen =
//                     static_cast<float>(GetGValue(textColor));
//                 const float textBlue = static_cast<float>(GetBValue(textColor));
// 
//                 const float backgroundRed =
//                     static_cast<float>(GetRValue(backgroundColor));
//                 const float backgroundGreen =
//                     static_cast<float>(GetGValue(backgroundColor));
//                 const float backgroundBlue =
//                     static_cast<float>(GetBValue(backgroundColor));
// 
//                 const float contrastRed = static_cast<float>(GetRValue(v));
//                 const float contrastGreen = static_cast<float>(GetGValue(v));
//                 const float contrastBlue = static_cast<float>(GetBValue(v));
// 
//                 float reviseFactor =
//                     (textRed - contrastRed) * background_opacity_;
//                 reviseFactor = 0;
//                 float red = GetRValue(textColor) == GetRValue(backgroundColor) ?
//                     1.0f :
//                     ((contrastRed + reviseFactor - backgroundRed) /
//                         (textRed - backgroundRed));
// 
//                 reviseFactor =
//                     (textGreen - contrastGreen) * background_opacity_;
//                 reviseFactor = 0;
//                 float green =
//                     GetGValue(textColor) == GetGValue(backgroundColor) ?
//                         1.0f :
//                         ((contrastGreen + reviseFactor - backgroundGreen) /
//                             (textGreen - backgroundGreen));
// 
//                 reviseFactor = (textBlue - contrastBlue) * background_opacity_;
//                 reviseFactor = 0;
//                 float blue =
//                     GetBValue(textColor) == GetBValue(backgroundColor) ?
//                         1.0f :
//                         ((contrastBlue + reviseFactor - backgroundBlue) /
//                             (textBlue - backgroundBlue));
// 
//                 if (perform_abs_) {
//                     red = sqrt(abs(red));
//                     green = sqrt(abs(green));
//                     blue = sqrt(abs(blue));
// 
//                     red = red + (1.0f - red) * background_opacity_;
//                     green = green + (1.0f - green) * background_opacity_;
//                     blue = blue + (1.0f - blue) * background_opacity_;
//                 }
// 
//                 float opacity = (0.299f * red + 0.587f * green + 0.114f * blue);
//                 //opacity = min(1.0f, opacity);
//                 //opacity = 0.9f;
// 
//                 r = static_cast<uint32_t>(opacity * 255.0f) << 24 | v;
//             }
//         }
//     }

    // Render the contrast buffer into canvas.
    BlendContrastBuffer(canvas, details, contrast, myDetails);
}

BEGIN_MESSAGE_MAP(TargetWindow, CWnd)
    ON_WM_PAINT()
END_MESSAGE_MAP()