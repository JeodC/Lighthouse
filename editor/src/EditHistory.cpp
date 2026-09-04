#include "App.h"
#include "O2rImport.h"
#include "SetupNames.h"
#include "UiCommon.h"

#include "imgui.h"

#include <cstdio>
#include <string>

void App::EditShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        Undo();
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        Redo();
    } else if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        DeleteSelection();
    }
}

void App::ResetHistory() {
    mHistory.clear();
    mHistoryPos = -1;
    if (mSetup.loaded) {
        mHistory.push_back({ mSetup.props, mSetup.nodes, "Level loaded" });
        mHistoryPos = 0;
    }
}

// Called after the change, so every step in the list is a state the level was in.
void App::RecordEdit(const std::string& label) {
    if (mHistoryPos < 0) {
        ResetHistory();
        return;
    }
    mHistory.resize(mHistoryPos + 1);
    mHistory.push_back({ mSetup.props, mSetup.nodes, label });
    if (mHistory.size() > 64) {
        mHistory.erase(mHistory.begin());
    }
    mHistoryPos = (int)mHistory.size() - 1;
}

void App::ApplyHistory(int step) {
    if (step < 0 || step >= (int)mHistory.size()) {
        return;
    }
    mSetup.props = mHistory[step].props;
    mSetup.nodes = mHistory[step].nodes;
    mHistoryPos = step;
    mPropSel = -1;
    mScrollToSel = false;
    mStatus = "History: " + mHistory[step].label;
}

void App::Undo() {
    if (mHistoryPos <= 0) {
        mStatus = "Nothing to undo.";
        return;
    }
    ApplyHistory(mHistoryPos - 1);
}

void App::Redo() {
    if (mHistoryPos < 0 || mHistoryPos + 1 >= (int)mHistory.size()) {
        mStatus = "Nothing to redo.";
        return;
    }
    ApplyHistory(mHistoryPos + 1);
}

// Drops the record from the loaded scene only - the archive is never written.
void App::DeleteSelection() {
    const int propCount = (int)mSetup.props.size();
    if (mPropSel < 0 || mPropSel >= propCount + (int)mSetup.nodes.size()) {
        return;
    }
    char what[64];
    if (mPropSel < propCount) {
        std::snprintf(what, sizeof(what), "%s #%d", Lightbulb::PropKindName(mSetup.props[mPropSel].type), mPropSel);
        mSetup.props.erase(mSetup.props.begin() + mPropSel);
    } else {
        const int row = mPropSel - propCount;
        std::snprintf(what, sizeof(what), "%s #%d", Lightbulb::NodeKindName(mSetup.nodes[row]), row);
        mSetup.nodes.erase(mSetup.nodes.begin() + row);
    }
    mPropSel = -1;
    mScrollToSel = false;
    RecordEdit(std::string("Removed ") + what);
    mStatus = std::string("Removed ") + what + " from the loaded level. Ctrl-Z puts it back.";
}

void App::DrawHistory() {
    if (!mShowHistory) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(320, 300), ImGuiCond_Appearing);
    if (!ImGui::Begin("History", &mShowHistory)) {
        ImGui::End();
        return;
    }
    if (mHistory.empty()) {
        ImGui::TextWrapped("Select a level to start a history.");
        ImGui::End();
        return;
    }
    Lightbulb::ui::TextDisabledWrapped("Steps of the loaded level. Pick one to put the level back in that state.");
    ImGui::Separator();
    for (int step = 0; step < (int)mHistory.size(); ++step) {
        char label[96];
        std::snprintf(label, sizeof(label), "%d. %s##step%d", step, mHistory[step].label.c_str(), step);
        if (ImGui::Selectable(label, step == mHistoryPos)) {
            ApplyHistory(step);
        }
    }
    ImGui::End();
}
