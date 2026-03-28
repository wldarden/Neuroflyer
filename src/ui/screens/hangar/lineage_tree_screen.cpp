#include <neuroflyer/ui/screens/lineage_tree_screen.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuroflyer/ui/screens/fly_session_screen.h>

#include <neuroflyer/components/lineage_graph.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/genome_manager.h>
#include <neuroflyer/snapshot_io.h>

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <iostream>

namespace neuroflyer {

// ==================== on_enter ====================

void LineageTreeScreen::on_enter() {
    // Force lineage rebuild on next draw
    last_genome_.clear();
}

// ==================== on_draw ====================

void LineageTreeScreen::on_draw(
    AppState& state, Renderer& /*renderer*/, UIManager& ui) {

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sw = display.x;
    const float sh = display.y;

    // Rebuild if genome changed
    std::string genome_dir = state.data_dir + "/genomes/" + state.active_genome;
    std::string genomes_dir = state.data_dir + "/genomes";
    if (state.lineage_dirty || lineage_state_.loaded_dir != genome_dir) {
        rebuild_lineage_graph(lineage_state_, genome_dir, genomes_dir);
        state.lineage_dirty = false;
    }

    // Escape = go back (skip if a modal is blocking input)
    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.90f);

    ImGui::Begin("##LineageTree", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.9f, 1.0f));
    char title[128];
    std::snprintf(title, sizeof(title), "%s — Lineage Tree",
        state.active_genome.c_str());
    float title_w = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((sw - title_w) * 0.5f);
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("%s", title);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Separator();

    float content_top = ImGui::GetCursorPosY();
    float footer_h = 50.0f;
    float content_h = sh - content_top - footer_h
                      - ImGui::GetStyle().WindowPadding.y;

    // Right panel width (for selected node details)
    float panel_w =
        (lineage_state_.selected_node >= 0) ? 250.0f : 0.0f;
    float graph_w = sw - ImGui::GetStyle().WindowPadding.x * 2.0f
                    - panel_w - (panel_w > 0 ? 10.0f : 0.0f);

    // ==================== LINEAGE GRAPH ====================
    [[maybe_unused]] auto graph_action =
        draw_lineage_graph(lineage_state_, genome_dir, graph_w, content_h);

    // ==================== SELECTED NODE PANEL ====================
    if (lineage_state_.selected_node >= 0) {
        ImGui::SameLine();
        ImGui::BeginChild("##LineageNodePanel",
            ImVec2(panel_w, content_h), true);

        auto& sel = lineage_state_.nodes[
            static_cast<std::size_t>(lineage_state_.selected_node)];

        // Node name
        ImGui::SetWindowFontScale(1.1f);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
            "%s", sel.name.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();

        // Node type
        if (sel.is_ancestor_genome) {
            ImGui::TextColored(
                ImVec4(0.63f, 0.39f, 0.71f, 1.0f), "Ancestor Genome");
        } else if (sel.is_child_genome) {
            ImGui::TextColored(
                ImVec4(0.39f, 0.71f, 0.47f, 1.0f), "Child Genome");
        } else if (sel.is_genome) {
            ImGui::TextColored(
                ImVec4(0.9f, 0.55f, 0.1f, 1.0f), "Genome (Root)");
        } else if (sel.is_mrca_stub) {
            ImGui::TextColored(
                ImVec4(0.6f, 0.6f, 0.3f, 1.0f), "Branch Point");
        } else {
            ImGui::TextColored(
                ImVec4(0.3f, 0.7f, 0.9f, 1.0f), "Variant");
        }

        ImGui::Text("Generation: %d", sel.generation);
        if (!sel.file.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "%s", sel.file.c_str());
        }

        // Children count
        ImGui::Text("Children: %zu", sel.children.size());

        ImGui::Dummy(ImVec2(0, 15));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 5));
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Actions");
        ImGui::Dummy(ImVec2(0, 3));

        float btn_w = panel_w - 20.0f;

        if (!sel.is_mrca_stub && !sel.is_genome) {
            if (ImGui::Button("Train from This", ImVec2(btn_w, 30))) {
                // Build variant path
                std::string var_path = sel.file.empty()
                    ? genome_dir + "/" + sel.name + ".bin"
                    : genome_dir + "/" + sel.file;
                try {
                    auto snap = load_snapshot(var_path);
                    EvolutionConfig evo_cfg;
                    evo_cfg.population_size =
                        state.config.population_size;
                    evo_cfg.elitism_count = state.config.elitism_count;
                    state.pending_population =
                        create_population_from_snapshot(
                            snap, state.config.population_size,
                            evo_cfg, state.rng);
                    state.training_parent_name = sel.name;
                    state.config.save(state.settings_path);
                    state.return_to_variant_view = true;
                    std::cout << "TrainFrom lineage: '" << sel.name
                              << "' (" << state.pending_population.size()
                              << " individuals)\n";
                    ui.push_screen(std::make_unique<FlySessionScreen>());
                } catch (const std::exception& e) {
                    std::cerr << "TrainFrom lineage failed: "
                              << e.what() << "\n";
                }
            }
            ImGui::Dummy(ImVec2(0, 3));
            if (ImGui::Button("View Neural Net", ImVec2(btn_w, 30))) {
                state.selected_variant = sel.name;
                // VariantViewer is below us on the stack — pop back to it
                ui.pop_screen();
            }
            ImGui::Dummy(ImVec2(0, 3));
            if (ImGui::Button("Promote to Genome", ImVec2(btn_w, 30))) {
                state.selected_variant = sel.name;
                // VariantViewer is below us on the stack — pop back to it
                ui.pop_screen();
            }
        }

        // Genome root can also be trained from
        if (sel.is_genome) {
            if (ImGui::Button("Train Fresh", ImVec2(btn_w, 30))) {
                try {
                    auto snap =
                        load_snapshot(genome_dir + "/genome.bin");
                    EvolutionConfig evo_cfg;
                    evo_cfg.population_size =
                        state.config.population_size;
                    evo_cfg.elitism_count = state.config.elitism_count;
                    state.pending_population =
                        create_population_from_snapshot(
                            snap, state.config.population_size,
                            evo_cfg, state.rng);
                    state.training_parent_name = snap.name;
                    state.config.save(state.settings_path);
                    state.return_to_variant_view = true;
                    std::cout << "TrainFresh from lineage: '"
                              << snap.name << "'\n";
                    ui.push_screen(std::make_unique<FlySessionScreen>());
                } catch (const std::exception& e) {
                    std::cerr << "TrainFresh from lineage failed: "
                              << e.what() << "\n";
                }
            }
        }

        ImGui::EndChild();
    }

    // ==================== FOOTER ====================
    ImGui::Separator();
    float pad = ImGui::GetStyle().WindowPadding.x;
    float footer_y = ImGui::GetCursorPosY();

    ImGui::SetCursorPos(ImVec2(pad, footer_y + 5.0f));
    if (ImGui::Button("Back to Variants", ImVec2(200, 35))) {
        ui.pop_screen();
    }

    ImGui::End();
}

} // namespace neuroflyer
