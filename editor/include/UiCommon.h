#pragma once

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Lightbulb {
namespace ui {
inline bool Contains(const std::vector<int>& v, int value) {
    return std::find(v.begin(), v.end(), value) != v.end();
}
inline void AddUnique(std::vector<int>& v, int value) {
    if (!Contains(v, value)) {
        v.push_back(value);
    }
}
inline bool ContainsNoCase(const char* haystack, const char* needle) {
    if (!needle || !needle[0]) {
        return true;
    }
    const size_t needleLen = std::strlen(needle);
    for (const char* at = haystack; *at; ++at) {
        size_t matched = 0;
        while (matched < needleLen && at[matched] &&
               std::tolower((unsigned char)at[matched]) == std::tolower((unsigned char)needle[matched])) {
            ++matched;
        }
        if (matched == needleLen) {
            return true;
        }
    }
    return false;
}

inline void TextDisabledWrapped(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

inline int AssetPicker(const char* idPrefix, const std::vector<std::string>& paths, char* filter, size_t filterSize,
                       int selected, const char* noun) {
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "search", filter, filterSize);
    ImGui::TextDisabled("%d %s", (int)paths.size(), noun);
    ImGui::Separator();
    int clicked = -1;
    for (int row = 0; row < (int)paths.size(); ++row) {
        const std::string& path = paths[row];
        const size_t slash = path.find_last_of('/');
        const char* shortName = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
        if (!ContainsNoCase(shortName, filter)) {
            continue;
        }
        char label[208];
        std::snprintf(label, sizeof(label), "%s##%s%d", shortName, idPrefix, row);
        if (ImGui::Selectable(label, row == selected)) {
            clicked = row;
        }
    }
    return clicked;
}

inline void VerticalSplitter(const char* id, float& width, float minWidth = 100.0f) {
    ImGui::SameLine();
    ImGui::Button(id, ImVec2(6.0f, -1.0f));
    if (ImGui::IsItemActive()) {
        width += ImGui::GetIO().MouseDelta.x;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (width < minWidth) {
        width = minWidth;
    }
    ImGui::SameLine();
}
} // namespace ui
} // namespace Lightbulb
