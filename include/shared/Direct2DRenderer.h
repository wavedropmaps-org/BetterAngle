#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>

class Direct2DRenderer {
public:
    Direct2DRenderer();
    ~Direct2DRenderer();

    bool Initialize(HWND hWnd);
    void Cleanup();

    void BeginDraw();
    HRESULT EndDraw();

    void Clear(D2D1_COLOR_F color);
    void DrawText(const std::wstring& text, float x, float y, float fontSize, D2D1_COLOR_F color, bool centered = false);
    void DrawRectangle(float x, float y, float w, float h, D2D1_COLOR_F color, float thickness = 1.0f, bool filled = false);

private:
    ID2D1Factory* m_pDirect2dFactory = nullptr;
    ID2D1HwndRenderTarget* m_pRenderTarget = nullptr;
    IDWriteFactory* m_pDWriteFactory = nullptr;
    IDWriteTextFormat* m_pTextFormat = nullptr;
    ID2D1SolidColorBrush* m_pBrush = nullptr;

    HRESULT CreateDeviceResources(HWND hWnd);
    void DiscardDeviceResources();
};
