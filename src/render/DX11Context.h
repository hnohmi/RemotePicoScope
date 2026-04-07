#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class DX11Context {
public:
    bool init(HWND hwnd);
    void shutdown();
    void resize(UINT width, UINT height);
    void beginFrame(float clearColor[4]);
    bool endFrame(); // returns false if device lost

    ID3D11Device* device() const { return m_device.Get(); }
    ID3D11DeviceContext* deviceContext() const { return m_deviceContext.Get(); }
    IDXGISwapChain* swapChain() const { return m_swapChain.Get(); }

private:
    void createRenderTarget();
    void cleanupRenderTarget();

    ComPtr<ID3D11Device>            m_device;
    ComPtr<ID3D11DeviceContext>     m_deviceContext;
    ComPtr<IDXGISwapChain>          m_swapChain;
    ComPtr<ID3D11RenderTargetView>  m_renderTargetView;
    HWND                            m_hwnd = nullptr;
};
