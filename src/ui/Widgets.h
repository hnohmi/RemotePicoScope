#pragma once

#include <imgui.h>
#include <algorithm>

// Wrappers around ImGui widgets that add mouse-wheel support when hovered.
// Rolling wheel up raises value / selects previous item.

namespace Widgets {

// SliderInt with mouse wheel: wheel up = +step, wheel down = -step
inline bool SliderInt(const char* label, int* v, int vMin, int vMax,
                      const char* format = "%d", int step = 1)
{
    bool changed = ImGui::SliderInt(label, v, vMin, vMax, format);
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            *v += (wheel > 0.0f) ? step : -step;
            *v = std::clamp(*v, vMin, vMax);
            changed = true;
        }
    }
    return changed;
}

// SliderFloat with mouse wheel: wheel adjusts by step
inline bool SliderFloat(const char* label, float* v, float vMin, float vMax,
                        const char* format = "%.3f", float step = 0.0f)
{
    bool changed = ImGui::SliderFloat(label, v, vMin, vMax, format);
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float s = (step > 0.0f) ? step : (vMax - vMin) * 0.02f;
            *v += (wheel > 0.0f) ? s : -s;
            *v = std::clamp(*v, vMin, vMax);
            changed = true;
        }
    }
    return changed;
}

// Combo with mouse wheel: wheel up = previous item, wheel down = next item
inline bool Combo(const char* label, int* currentItem,
                  const char* const items[], int itemsCount)
{
    bool changed = ImGui::Combo(label, currentItem, items, itemsCount);
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            *currentItem += (wheel > 0.0f) ? -1 : 1;
            *currentItem = std::clamp(*currentItem, 0, itemsCount - 1);
            changed = true;
        }
    }
    return changed;
}

} // namespace Widgets
