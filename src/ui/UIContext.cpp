#include "ui/UIContext.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

bool UIContext::init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    applyOscilloscopeTheme();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, deviceContext);

    return true;
}

void UIContext::shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void UIContext::beginFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void UIContext::endFrame() {
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void UIContext::applyOscilloscopeTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Dark oscilloscope theme
    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 4);

    // Very dark background (oscilloscope screen)
    colors[ImGuiCol_WindowBg]           = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg]            = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_PopupBg]            = ImVec4(0.10f, 0.10f, 0.12f, 0.95f);

    // Borders
    colors[ImGuiCol_Border]             = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame backgrounds (controls)
    colors[ImGuiCol_FrameBg]            = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);

    // Title bar
    colors[ImGuiCol_TitleBg]            = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.04f, 0.04f, 0.06f, 1.00f);

    // Menu bar
    colors[ImGuiCol_MenuBarBg]          = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.45f, 0.45f, 0.50f, 1.00f);

    // Buttons (accent: teal/cyan)
    colors[ImGuiCol_Button]             = ImVec4(0.15f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.20f, 0.40f, 0.45f, 1.00f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.10f, 0.50f, 0.55f, 1.00f);

    // Headers (used by collapsing headers, tree nodes, etc.)
    colors[ImGuiCol_Header]             = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.25f, 0.25f, 0.35f, 1.00f);

    // Separator
    colors[ImGuiCol_Separator]          = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);

    // Slider grab
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.30f, 0.55f, 0.60f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.35f, 0.65f, 0.70f, 1.00f);

    // Check mark
    colors[ImGuiCol_CheckMark]          = ImVec4(0.30f, 0.70f, 0.75f, 1.00f);

    // Tabs
    colors[ImGuiCol_Tab]               = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
    colors[ImGuiCol_TabHovered]        = ImVec4(0.20f, 0.35f, 0.40f, 1.00f);
    colors[ImGuiCol_TabSelected]       = ImVec4(0.15f, 0.30f, 0.35f, 1.00f);

    // Docking
    colors[ImGuiCol_DockingPreview]     = ImVec4(0.20f, 0.50f, 0.55f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]     = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);

    // Text
    colors[ImGuiCol_Text]              = ImVec4(0.85f, 0.85f, 0.88f, 1.00f);
    colors[ImGuiCol_TextDisabled]      = ImVec4(0.45f, 0.45f, 0.48f, 1.00f);
}
