#pragma once

#include <afxwin.h>
#include <stdint.h>

class TargetWindow : public CWnd
{
public:
    static const wchar_t* GetClassName();

    TargetWindow();

    // |opacity| should be in [0.0f - 1.0f].
    void Paint(float opacity);

private:
    static LRESULT __stdcall MyWinProc(HWND hwnd, uint32_t message,
                                       WPARAM wParam, LPARAM lParam);

    void ApplyOpacity(int8_t* contrastBuffer, int bufferSize,
                      const int8_t* opacityBuffer);
    void BlendContrastBuffer(CBitmap* canvas, const BITMAP& canvasDetails,
                             const CBitmap& contrast,
                             const BITMAP& contrastDetails);
    void CalculateOpacity(int8_t* opacityBuffer, int bufferSize,
                          const int8_t* contrastBuffer,
                          COLORREF backgroundColor, COLORREF textColor);
    void PrepareContrastBuffer(int8_t* contrastBuffer,
                               const BITMAP& contrastDetails,
                               const int8_t* canvasBuffer,
                               const BITMAP& canvasDetails,
                               COLORREF backgroundColor, COLORREF textColor,
                               COLORREF* contrastBackground);
    void RenderClearTypeGlyph(CBitmap* canvas, CDC* dc, const BITMAP& details,
                              COLORREF backgroundColor, COLORREF textColor);

    float background_opacity_;
    bool copy_background_;
    bool perform_abs_;
    bool reverse_contrast_;
    bool use_background_opacity_;

    DECLARE_MESSAGE_MAP()
};