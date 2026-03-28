#include <neuroflyer/ui/screens/hangar_screen.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuroflyer/ui/screens/create_genome_screen.h>
#include <neuroflyer/ui/screens/hangar/variant_net_editor_screen.h>
#include <neuroflyer/ui/screens/variant_viewer_screen.h>

#include <game-ui/components/highlight_list.h>
#include <neuroflyer/components/fitness_editor.h>
#include <neuroflyer/components/test_bench.h>
#include <neuroflyer/genome_manager.h>
#include <neuroflyer/paths.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>
#include <neuroflyer/snapshot_utils.h>

#include <neuralnet-ui/render_net_topology.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace neuroflyer {

// ==================== on_enter ====================

void HangarScreen::on_enter() {
    sub_view_ = SubView::GenomeList;
    show_delete_confirm_ = false;
}

// ==================== refresh_genomes ====================

void HangarScreen::refresh_genomes(AppState& state) {
    std::string genomes_dir = state.data_dir + "/genomes";
    genomes_ = list_genomes(genomes_dir);

    // Try to select the active genome
    if (!state.config.active_genome.empty()) {
        for (int i = 0; i < static_cast<int>(genomes_.size()); ++i) {
            if (genomes_[static_cast<std::size_t>(i)].name
                == state.config.active_genome) {
                selected_genome_idx_ = i;
                break;
            }
        }
    }
    selected_genome_idx_ = std::clamp(
        selected_genome_idx_, 0,
        std::max(0, static_cast<int>(genomes_.size()) - 1));

    // Load topology + ship design + timestamp for each genome
    preview_.genome_topologies.clear();
    preview_.genome_designs.clear();
    preview_.genome_timestamps.clear();
    for (const auto& g : genomes_) {
        std::string genome_path =
            state.data_dir + "/genomes/" + g.name + "/genome.bin";
        try {
            auto snap = load_snapshot(genome_path);
            preview_.genome_topologies.push_back(snap.topology);
            preview_.genome_designs.push_back(snap.ship_design);

            int64_t latest_ts = snap.created_timestamp;
            // Check variants for more recent timestamp
            try {
                auto variants = list_variants(
                    state.data_dir + "/genomes/" + g.name);
                for (const auto& v : variants) {
                    if (v.created_timestamp > latest_ts)
                        latest_ts = v.created_timestamp;
                }
            } catch (...) {}
            preview_.genome_timestamps.push_back(
                format_short_date(latest_ts));
        } catch (...) {
            preview_.genome_topologies.push_back({});
            preview_.genome_designs.push_back({});
            preview_.genome_timestamps.push_back("---");
        }
    }
    state.genomes_dirty = false;
}

// ==================== Genome list drawing ====================

HangarScreen::Action HangarScreen::draw_genome_list(
    AppState& state, Renderer& renderer, UIManager& /*ui*/) {
    Action action = Action::Stay;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sw = display.x;
    const float sh = display.y;

    // Handle refresh triggers
    if (state.genomes_dirty) {
        refresh_genomes(state);
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGui::Begin("##Hangar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.9f, 1.0f));
    const char* title = "HANGAR";
    float title_w = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((sw - title_w) * 0.5f);
    ImGui::Text("%s", title);
    ImGui::PopStyleColor();
    ImGui::Separator();

    float content_top = ImGui::GetCursorPosY();
    float footer_h = 50.0f;
    float content_h =
        sh - content_top - footer_h - ImGui::GetStyle().WindowPadding.y;
    float left_w = sw * 0.45f;

    // ==================== LEFT: Genome List ====================

    // Build rows for the highlight list
    std::vector<gameui::HighlightListRow> genome_rows;
    genome_rows.reserve(genomes_.size());
    for (std::size_t i = 0; i < genomes_.size(); ++i) {
        const auto& g = genomes_[i];
        std::string date_str = "---";
        if (i < preview_.genome_timestamps.size()) {
            date_str = preview_.genome_timestamps[i];
        }
        char detail[128];
        std::snprintf(detail, sizeof(detail), "%zu variant%s  |  Updated: %s",
                      g.variant_count, g.variant_count == 1 ? "" : "s",
                      date_str.c_str());
        genome_rows.push_back({g.name, {detail}});
    }

    gameui::HighlightListConfig list_cfg;
    list_cfg.row_height = 55.0f;
    list_cfg.width = left_w;
    list_cfg.height = content_h;
    list_cfg.show_create = true;
    list_cfg.create_label = "+ Create New Genome";

    auto list_result = gameui::draw_highlight_list(
        "##GenomeList", genome_rows, selected_genome_idx_, list_cfg);

    if (list_result.create_clicked) {
        action = Action::CreateGenome;
    }
    if (list_result.clicked >= 0) {
        // Single click: select the row (update highlight + preview)
        selected_genome_idx_ = list_result.clicked;
        state.config.active_genome =
            genomes_[static_cast<std::size_t>(list_result.clicked)].name;
    }
    if (list_result.double_clicked >= 0) {
        // Double click: navigate to variant viewer
        selected_genome_idx_ = list_result.double_clicked;
        state.config.active_genome =
            genomes_[static_cast<std::size_t>(list_result.double_clicked)].name;
        action = Action::SelectGenome;
    }
    preview_.hovered_genome_idx = list_result.hovered;

    // ==================== RIGHT: Neural Net Preview ====================
    ImGui::SameLine();
    ImGui::BeginChild("##NetPreviewPanel", ImVec2(0, content_h), true,
        ImGuiWindowFlags_NoScrollbar);

    // Record the screen rect for SDL rendering
    ImVec2 panel_pos = ImGui::GetCursorScreenPos();
    ImVec2 panel_avail = ImGui::GetContentRegionAvail();
    preview_.preview_x = static_cast<int>(panel_pos.x);
    preview_.preview_y = static_cast<int>(panel_pos.y);
    preview_.preview_w = static_cast<int>(panel_avail.x);
    preview_.preview_h = static_cast<int>(panel_avail.y);

    if (preview_.hovered_genome_idx >= 0) {
        auto idx = static_cast<std::size_t>(preview_.hovered_genome_idx);
        if (idx < genomes_.size()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Neural Net: %s", genomes_[idx].name.c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f),
            "Hover a genome to preview its network");
    }

    // Reserve space for SDL rendering
    ImGui::Dummy(panel_avail);

    // Render the hovered genome's net preview via SDL
    if (preview_.hovered_genome_idx >= 0
        && static_cast<std::size_t>(preview_.hovered_genome_idx)
            < preview_.genome_topologies.size()
        && preview_.preview_w > 0 && preview_.preview_h > 0) {
        const auto& topo = preview_.genome_topologies[
            static_cast<std::size_t>(preview_.hovered_genome_idx)];
        if (!topo.layers.empty()) {
            // Build input colors from ShipDesign if available
            std::vector<neuralnet_ui::NodeColor> input_colors;
            auto hidx =
                static_cast<std::size_t>(preview_.hovered_genome_idx);
            if (hidx < preview_.genome_designs.size()) {
                auto nf_colors = build_input_colors(preview_.genome_designs[hidx]);
                input_colors.reserve(nf_colors.size());
                for (const auto& c : nf_colors) {
                    input_colors.push_back({c.r, c.g, c.b});
                }
            }
            // Defer SDL rendering to after ImGui (so it draws on top)
            renderer.defer_topology({renderer.renderer_, &topo,
                preview_.preview_x, preview_.preview_y,
                preview_.preview_w, preview_.preview_h,
                std::move(input_colors)});
        }
    }

    ImGui::EndChild();

    // ==================== FOOTER ====================
    ImGui::Separator();
    float pad = ImGui::GetStyle().WindowPadding.x;
    float footer_y = ImGui::GetCursorPosY();

    ImGui::SetCursorPos(ImVec2(pad, footer_y + 5.0f));
    if (ImGui::Button("Back to Menu", ImVec2(200, 35))) {
        action = Action::Back;
        state.genomes_dirty = true;
    }

    // Delete Genome button (right-aligned in footer)
    if (!genomes_.empty()) {
        ImGui::SameLine(sw - 220.0f - pad);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Delete Genome", ImVec2(200, 35))) {
            show_delete_confirm_ = true;
        }
        ImGui::PopStyleColor(2);
    }

    // Delete Genome confirmation dialog
    if (show_delete_confirm_ && !genomes_.empty() &&
        selected_genome_idx_ >= 0 &&
        static_cast<std::size_t>(selected_genome_idx_) < genomes_.size()) {
        const auto& sel_genome =
            genomes_[static_cast<std::size_t>(selected_genome_idx_)];
        ImGui::SetNextWindowPos(
            ImVec2(sw * 0.5f - 200.0f, sh * 0.5f - 80.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);
        ImGui::Begin("##DeleteGenomeConfirm", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
            "Delete Genome");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 5));
        ImGui::TextWrapped(
            "Delete genome '%s' and all %zu variant%s? "
            "Child genomes will be relinked to the parent genome.",
            sel_genome.name.c_str(),
            sel_genome.variant_count,
            sel_genome.variant_count == 1 ? "" : "s");
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Confirm Delete", ImVec2(180, 30))) {
            action = Action::DeleteGenome;
            show_delete_confirm_ = false;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Cancel##delgenome", ImVec2(180, 30))) {
            show_delete_confirm_ = false;
        }
        ImGui::End();
    }

    ImGui::End();

    return action;
}

// ==================== on_draw ====================

void HangarScreen::on_draw(AppState& state, Renderer& renderer, UIManager& ui) {
    // Escape = go back one level (skip if a modal is blocking input)
    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        switch (sub_view_) {
        case SubView::GenomeList:
            ui.pop_screen();
            return;
        case SubView::TestBench:
        case SubView::FitnessFunc:
            sub_view_ = SubView::GenomeList;
            return;
        }
    }

    switch (sub_view_) {
    case SubView::GenomeList: {
        auto action = draw_genome_list(state, renderer, ui);

        switch (action) {
        case Action::Back:
            state.config.save(state.settings_path);
            ui.pop_screen();
            break;

        case Action::CreateGenome:
            ui.push_screen(std::make_unique<CreateGenomeScreen>());
            break;

        case Action::SelectGenome:
            std::cout << "Selected genome: "
                      << state.config.active_genome << "\n";
            state.active_genome = state.config.active_genome;
            ui.push_screen(std::make_unique<VariantViewerScreen>());
            break;

        case Action::ViewNet: {
            if (!state.config.active_genome.empty()) {
                std::string genome_dir =
                    state.data_dir + "/genomes/"
                    + state.config.active_genome;
                std::string genome_path = genome_dir + "/genome.bin";
                try {
                    auto snap = load_snapshot(genome_path);
                    auto ind = snapshot_to_individual(snap);
                    auto net = ind.build_network();
                    ui.push_screen(std::make_unique<VariantNetEditorScreen>(
                        std::move(ind), std::move(net), snap.ship_design,
                        genome_path, snap.name));
                } catch (const std::exception& e) {
                    std::cerr << "Failed to load genome for viewer: "
                              << e.what() << "\n";
                }
            }
            break;
        }

        case Action::TestBench: {
            if (!state.config.active_genome.empty()) {
                std::string genome_dir =
                    state.data_dir + "/genomes/"
                    + state.config.active_genome;
                std::string genome_path = genome_dir + "/genome.bin";
                try {
                    auto snap = load_snapshot(genome_path);
                    auto ind = snapshot_to_individual(snap);

                    networks_.clear();
                    networks_.push_back(ind.build_network());

                    test_bench_state_ = {};
                    test_bench_state_.design = snap.ship_design;
                    test_bench_state_.variant_path = genome_path;
                    test_bench_state_.design_backup = snap.ship_design;
                    test_bench_state_.config_backup = state.config;

                    sub_view_ = SubView::TestBench;

                    std::cout << "Test bench loaded from "
                              << genome_path << "\n";
                } catch (const std::exception& e) {
                    std::cerr
                        << "Failed to load genome for test bench: "
                        << e.what() << "\n";
                }
            }
            break;
        }

        case Action::DeleteGenome: {
            if (!genomes_.empty() &&
                selected_genome_idx_ >= 0 &&
                static_cast<std::size_t>(selected_genome_idx_) < genomes_.size()) {
                const auto& sel_genome =
                    genomes_[static_cast<std::size_t>(selected_genome_idx_)];
                try {
                    std::string genomes_dir = state.data_dir + "/genomes";
                    delete_genome(genomes_dir, sel_genome.name);
                    std::cout << "Deleted genome: " << sel_genome.name << "\n";
                    if (state.config.active_genome == sel_genome.name) {
                        state.config.active_genome = "";
                        state.active_genome = "";
                    }
                    state.genomes_dirty = true;
                    state.lineage_dirty = true;
                    selected_genome_idx_ = 0;
                } catch (const std::exception& e) {
                    std::cerr << "Delete genome failed: " << e.what() << "\n";
                }
            }
            break;
        }

        case Action::FitnessFunc:
            sub_view_ = SubView::FitnessFunc;
            break;

        case Action::Stay:
            break;
        }
        break;
    }

    case SubView::TestBench: {
        draw_test_bench(
            test_bench_state_,
            networks_,
            state.config, renderer);

        if (test_bench_state_.wants_save) {
            // Save sensor design back to the genome's .bin file
            if (!test_bench_state_.variant_path.empty()) {
                try {
                    auto snap = load_snapshot(test_bench_state_.variant_path);
                    snap.ship_design = test_bench_state_.design;
                    save_snapshot(snap, test_bench_state_.variant_path);
                    std::cout << "Saved sensor config to: "
                              << test_bench_state_.variant_path << "\n";
                } catch (const std::exception& e) {
                    std::cerr << "Failed to save sensor config: "
                              << e.what() << "\n";
                }
            }
            sub_view_ = SubView::GenomeList;
            state.genomes_dirty = true;
        } else if (test_bench_state_.wants_cancel) {
            sub_view_ = SubView::GenomeList;
            state.genomes_dirty = true;
        }
        break;
    }

    case SubView::FitnessFunc: {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float sw_f = display.x;
        const float sh_f = display.y;

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(sw_f, sh_f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::Begin("##FitnessFunc", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        ImGui::PushStyleColor(
            ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));
        ImGui::Text("FITNESS FUNCTION");
        ImGui::PopStyleColor();
        ImGui::SameLine(sw_f - 150.0f);
        if (ImGui::Button("Back (Esc)", ImVec2(130, 25))) {
            state.config.save(state.settings_path);
            sub_view_ = SubView::GenomeList;
        }
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));

        draw_fitness_editor(state.config);

        ImGui::End();
        break;
    }
    }
}

} // namespace neuroflyer
