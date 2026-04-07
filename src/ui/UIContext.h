#pragma once

#include <d3d11.h>

struct HWND__;
typedef HWND__* HWND;

class UIContext {
public:
    bool init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* deviceContext);
    void shutdown();
    void beginFrame();
    void endFrame();
    void applyOscilloscopeTheme();
};
