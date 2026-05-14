#include "shared/Direct2DRenderer.h"
#include <algorithm>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

Direct2DRenderer::Direct2DRenderer() {}

Direct2DRenderer::~Direct2DRenderer() {
    Cleanup();
}

bool Direct2DRenderer::Initialize(HWND hWnd) {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pDirect2dFactory);
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(m_pDWriteFactory), reinterpret_cast<IUnknown**>(&m_pDWriteFactory));
    if (FAILED(hr)) return false;

    return SUCCEEDED(CreateDeviceResources(hWnd));
}

HRESULT Direct2DRenderer::CreateDeviceResources(HWND hWnd) {
    if (!m_pRenderTarget) {
        RECT rc;
        GetClientRect(hWnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

        HRESULT hr = m_pDirect2dFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            D2D1::HwndRenderTargetProperties(hWnd, size),
            &m_pRenderTarget
        );
        if (FAILED(hr)) return hr;

        hr = m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_pBrush);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

void Direct2DRenderer::DiscardDeviceResources() {
    if (m_pRenderTarget) {
        m_pRenderTarget->Release();
        m_pRenderTarget = nullptr;
    }
    if (m_pBrush) {
        m_pBrush->Release();
        m_pBrush = nullptr;
    }
}

void Direct2DRenderer::Cleanup() {
    DiscardDeviceResources();
    if (m_pDirect2dFactory) {
        m_pDirect2dFactory->Release();
        m_pDirect2dFactory = nullptr;
    }
    if (m_pDWriteFactory) {
        m_pDWriteFactory->Release();
        m_pDWriteFactory = nullptr;
    }
    if (m_pTextFormat) {
        m_pTextFormat->Release();
        m_pTextFormat = nullptr;
    }
}

void Direct2DRenderer::BeginDraw() {
    if (m_pRenderTarget) {
        m_pRenderTarget->BeginDraw();
    }
}

HRESULT Direct2DRenderer::EndDraw() {
    if (m_pRenderTarget) {
        HRESULT hr = m_pRenderTarget->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            DiscardDeviceResources();
            return hr;
        }
        return hr;
    }
    return S_OK;
}

void Direct2DRenderer::Clear(D2D1_COLOR_F color) {
    if (m_pRenderTarget) {
        m_pRenderTarget->Clear(color);
    }
}

void Direct2DRenderer::DrawText(const std::wstring& text, float x, float y, float fontSize, D2D1_COLOR_F color, bool centered) {
    if (!m_pRenderTarget || !m_pDWriteFactory) return;

    IDWriteTextFormat* pFormat = nullptr;
    HRESULT hr = m_pDWriteFactory->CreateTextFormat(
        L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"", &pFormat
    );

    if (SUCCEEDED(hr)) {
        if (centered) {
            pFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        m_pBrush->SetColor(color);
        D2D1_RECT_F layoutRect = D2D1::RectF(x, y, x + 800, y + fontSize + 10);
        m_pRenderTarget->DrawText(text.c_str(), (UINT32)text.length(), pFormat, layoutRect, m_pBrush);
        pFormat->Release();
    }
}

void Direct2DRenderer::DrawRectangle(float x, float y, float w, float h, D2D1_COLOR_F color, float thickness, bool filled) {
    if (!m_pRenderTarget) return;

    m_pBrush->SetColor(color);
    D2D1_RECT_F rect = D2D1::RectF(x, y, x + w, y + h);
    if (filled) {
        m_pRenderTarget->FillRectangle(rect, m_pBrush);
    } else {
        m_pRenderTarget->DrawRectangle(rect, m_pBrush, thickness);
    }
}
