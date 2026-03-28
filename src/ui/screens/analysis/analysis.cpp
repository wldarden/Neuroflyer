#include <neuroflyer/screens/analysis/analysis.h>

#include <neuroflyer/components/structural_heatmap.h>

#include <imgui.h>

namespace neuroflyer {

void draw_analysis(AnalysisState& analysis,
                   const std::vector<StructuralHistogram>& structural_history,
                   AppState& state,
                   Renderer& renderer,
                   bool as_tab) {
    (void)state;

    // Mark heatmap dirty if new data arrived
    if (structural_history.size() != analysis.last_history_size) {
        analysis.heatmap.dirty = true;
        analysis.last_history_size = structural_history.size();
    }

    // When used as a standalone screen, create a full-screen window.
    // When embedded as a tab, the container provides the window.
    if (!as_tab) {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(display, ImGuiCond_Always);
        ImGui::Begin("##AnalysisScreen", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    }

    // Sidebar
    constexpr float SIDEBAR_W = 200.0f;
    ImGui::BeginChild("##Sidebar", ImVec2(SIDEBAR_W, -1), true);

    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Analysis");
    ImGui::Separator();

    // Chart picker
    bool is_heatmap = (analysis.active_chart == AnalysisChart::StructuralHeatmap);
    if (ImGui::Selectable("Structural History", is_heatmap)) {
        analysis.active_chart = AnalysisChart::StructuralHeatmap;
    }

    ImGui::Separator();

    // Stats
    ImGui::Text("Generations: %zu", structural_history.size());

    if (!analysis.heatmap.cumulative.empty()) {
        // Find most common topology
        int best_count = 0;
        std::pair<int,int> best_topo = {0, 0};
        for (const auto& [key, count] : analysis.heatmap.cumulative) {
            if (count > best_count) {
                best_count = count;
                best_topo = key;
            }
        }
        ImGui::Text("Most common:");
        ImGui::Text("  %dL x %d nodes", best_topo.first, best_topo.second);
    }

    // Back button only in standalone screen mode
    if (!as_tab) {
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 40.0f);
        if (ImGui::Button("Back", ImVec2(-1, 30))) {
            analysis.wants_back = true;
        }
    }

    ImGui::EndChild();

    // Main chart area
    ImGui::SameLine();
    ImGui::BeginChild("##ChartArea", ImVec2(0, -1), false);

    switch (analysis.active_chart) {
    case AnalysisChart::StructuralHeatmap:
        draw_structural_heatmap(analysis.heatmap, structural_history,
                                renderer.renderer_);
        break;
    }

    ImGui::EndChild();

    if (!as_tab) {
        ImGui::End();
    }
}

} // namespace neuroflyer
