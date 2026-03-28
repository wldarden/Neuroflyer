#include <neuroflyer/ui/screens/pause_config_screen.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/modals/input_modal.h>

#include <neuroflyer/components/pause_training.h>
#include <neuroflyer/components/pause_evolution.h>

#include <neuroflyer/config.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/screens/game/fly_session.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/snapshot_io.h>

#include <neuralnet-ui/render_net_topology.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace neuroflyer {

void PauseConfigScreen::on_draw(AppState& state, Renderer& renderer,
                                 UIManager& ui) {
    auto& fly_state = get_fly_session_state();
    auto& config = state.config;

    // Save config backup on screen entry
    if (!backup_saved_) {
        config_backup_ = config;
        evo_config_backup_ = fly_state.evo_config;
        evolvable_backup_ = fly_state.ship_design.evolvable;
        backup_saved_ = true;
    }

    auto& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;

    // Handle Escape key: same as Resume (skip if a modal is blocking input)
    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        backup_saved_ = false;
        ui.pop_screen();
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.9f);

    ImGui::Begin("##PauseConfig", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // ==================== HEADER ====================
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));
    float text_w = ImGui::CalcTextSize("PAUSED").x;
    ImGui::SetCursorPosX((sw - text_w) * 0.5f);
    ImGui::Text("PAUSED");
    ImGui::PopStyleColor();
    ImGui::Separator();

    // ==================== TAB BAR ====================
    if (ImGui::BeginTabBar("##PauseTabs")) {
        if (ImGui::BeginTabItem("Training")) {
            draw_pause_training(state, fly_state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Evolution")) {
            draw_pause_evolution(state, fly_state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Analysis")) {
            draw_analysis(analysis_state_, fly_state.structural_history,
                          state, renderer, /*as_tab=*/true);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Save Variants")) {
            draw_save_variants_tab(state, renderer, ui);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // ==================== FOOTER ====================
    ImGui::Separator();
    float footer_y = ImGui::GetCursorPosY();
    float btn_h = 35.0f;
    float pad = ImGui::GetStyle().WindowPadding.x;

    // Cancel -- left aligned
    ImGui::SetCursorPos(ImVec2(pad, footer_y + 5.0f));
    if (ImGui::Button("Cancel", ImVec2(120, btn_h))) {
        // Revert config from backup
        config = config_backup_;
        fly_state.evo_config = evo_config_backup_;
        fly_state.ship_design.evolvable = evolvable_backup_;
        backup_saved_ = false;
        ui.pop_screen();
    }

    // Headless run
    ImGui::SameLine(0.0f, 20.0f);
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("##hgens", &headless_gens_, 10, 50);
    headless_gens_ = std::max(1, headless_gens_);
    ImGui::SameLine();
    if (ImGui::Button("Run Headless", ImVec2(140, btn_h))) {
        fly_state.headless_remaining = headless_gens_;
        fly_state.headless_total = headless_gens_;
        fly_state.headless_stats.clear();
        fly_state.phase = FlySessionState::Phase::HeadlessRunning;
        backup_saved_ = false;
        ui.pop_screen();
    }

    // Resume + Apply -- right aligned
    float resume_w = 200.0f;
    float apply_w = 200.0f;
    float gap = 8.0f;
    float right_edge = sw - pad * 2.0f;
    ImGui::SetCursorPos(ImVec2(right_edge - resume_w - gap - apply_w, footer_y + 5.0f));
    if (ImGui::Button("Resume (Space/Esc)", ImVec2(resume_w, btn_h))) {
        backup_saved_ = false;
        ui.pop_screen();
    }
    ImGui::SameLine(0.0f, gap);
    if (ImGui::Button("Apply & Resume", ImVec2(apply_w, btn_h))) {
        fly_state.needs_reset = true;
        backup_saved_ = false;
        ui.pop_screen();
    }

    ImGui::End();
}

// ==================== SAVE VARIANTS TAB ====================

void PauseConfigScreen::draw_save_variants_tab(
        AppState& state, Renderer& renderer, UIManager& ui) {
    auto& fly = get_fly_session_state();

    // Rebuild sorted indices when population or generation changes
    if (fly.population.size() != last_pop_size_ ||
        fly.generation != last_generation_) {
        last_pop_size_ = fly.population.size();
        last_generation_ = fly.generation;
        sorted_indices_.resize(fly.population.size());
        for (std::size_t i = 0; i < sorted_indices_.size(); ++i)
            sorted_indices_[i] = i;

        // Sort by session score descending (sessions parallel population)
        std::sort(sorted_indices_.begin(), sorted_indices_.end(),
            [&](std::size_t a, std::size_t b) {
                float sa = a < fly.sessions.size() ? fly.sessions[a].score() : 0.0f;
                float sb = b < fly.sessions.size() ? fly.sessions[b].score() : 0.0f;
                return sa > sb;
            });
        selected_.assign(fly.population.size(), false);
        hovered_sorted_idx_ = -1;
    }

    if (fly.population.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
            "No population loaded.");
        return;
    }

    float avail_h = ImGui::GetContentRegionAvail().y - 45.0f;
    float avail_w = ImGui::GetContentRegionAvail().x;

    // Layout: list on left, preview on right
    float list_w = avail_w * 0.45f;
    float preview_w = avail_w - list_w - 10.0f;

    // ---- Left: Individual list ----
    ImGui::BeginChild("##IndividualList", ImVec2(list_w, avail_h), true);

    // Select All / Clear buttons
    {
        int sel_count = 0;
        for (bool s : selected_) if (s) ++sel_count;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d selected", sel_count);
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 1.0f), "%s", buf);
        ImGui::SameLine();
        if (ImGui::SmallButton("All")) {
            for (std::size_t i = 0; i < selected_.size(); ++i)
                selected_[i] = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            for (std::size_t i = 0; i < selected_.size(); ++i)
                selected_[i] = false;
        }
        ImGui::Separator();
    }

    hovered_sorted_idx_ = -1;

    for (std::size_t rank = 0; rank < sorted_indices_.size(); ++rank) {
        std::size_t pop_idx = sorted_indices_[rank];
        if (pop_idx >= fly.population.size()) continue;
        if (pop_idx >= fly.sessions.size()) continue;

        float score = fly.sessions[pop_idx].score();
        bool alive = fly.sessions[pop_idx].alive();
        bool is_elite = rank < fly.evo_config.elitism_count;

        // Build display name
        char name[64];
        if (is_elite) {
            std::snprintf(name, sizeof(name), "Gen %zu #%zu (Elite)",
                fly.generation, rank + 1);
        } else {
            std::snprintf(name, sizeof(name), "Gen %zu #%zu",
                fly.generation, rank + 1);
        }

        // Row with multi-select
        ImGui::PushID(static_cast<int>(rank));

        bool is_selected = rank < selected_.size() && selected_[rank];
        if (ImGui::Selectable("##sel", is_selected,
                ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 36))) {
            if (rank < selected_.size())
                selected_[rank] = !selected_[rank];
        }
        if (ImGui::IsItemHovered()) {
            hovered_sorted_idx_ = static_cast<int>(rank);
        }

        // Draw name and subtext on top of the selectable
        ImGui::SameLine(5.0f);
        ImGui::BeginGroup();
        if (is_elite) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", name);
        } else {
            ImGui::Text("%s", name);
        }
        // Subtext: score + alive/dead
        if (alive) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
                "%.0f pts  ALIVE", static_cast<double>(score));
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.3f, 0.3f, 1.0f),
                "%.0f pts  dead", static_cast<double>(score));
        }
        ImGui::EndGroup();

        ImGui::PopID();
    }

    ImGui::EndChild();

    // ---- Right: Hover preview ----
    ImGui::SameLine();
    ImGui::BeginChild("##PreviewPane", ImVec2(preview_w, avail_h), true);

    if (hovered_sorted_idx_ >= 0 &&
        static_cast<std::size_t>(hovered_sorted_idx_) < sorted_indices_.size()) {
        std::size_t pop_idx = sorted_indices_[static_cast<std::size_t>(hovered_sorted_idx_)];
        if (pop_idx < fly.population.size()) {
            const auto& ind = fly.population[pop_idx];
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 1.0f), "Network Preview");
            ImGui::Text("Layers: %zu  Inputs: %zu",
                ind.topology.layers.size(),
                ind.topology.input_size);
            ImGui::Separator();

            // Defer topology render into the preview area
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            float pw = preview_w - 20.0f;
            float ph = avail_h - ImGui::GetCursorPosY() - 10.0f;
            if (pw > 50.0f && ph > 50.0f) {
                int px = static_cast<int>(cursor.x);
                int py = static_cast<int>(cursor.y);
                renderer.defer_topology({renderer.renderer_, &ind.topology,
                    px, py, static_cast<int>(pw), static_cast<int>(ph), {}});
                // Reserve space so ImGui knows the area is used
                ImGui::Dummy(ImVec2(pw, ph));
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
            "Hover an individual to preview its network.");
    }

    ImGui::EndChild();

    // ---- Save Selected button ----
    {
        int sel_count = 0;
        for (bool s : selected_) if (s) ++sel_count;

        bool can_save = sel_count > 0 && !fly.active_genome_dir.empty();

        if (!can_save) {
            ImGui::BeginDisabled();
        }

        char btn_label[64];
        std::snprintf(btn_label, sizeof(btn_label), "Save %d Selected", sel_count);
        if (ImGui::Button(btn_label, ImVec2(180, 30))) {
            ui.push_modal(std::make_unique<InputModal>(
                "Save Variants",
                "Enter base name:",
                [this, &state](const std::string& base_name) {
                    auto& fs = get_fly_session_state();
                    int idx = 1;
                    for (std::size_t rank = 0; rank < sorted_indices_.size(); ++rank) {
                        if (rank >= selected_.size() || !selected_[rank]) continue;
                        std::size_t pop_idx = sorted_indices_[rank];
                        if (pop_idx >= fs.population.size()) continue;

                        const auto& ind = fs.population[pop_idx];

                        Snapshot snap;
                        char snap_name[128];
                        std::snprintf(snap_name, sizeof(snap_name),
                            "%s-%d", base_name.c_str(), idx);
                        snap.name = snap_name;
                        snap.generation = static_cast<uint32_t>(fs.generation);
                        snap.created_timestamp =
                            std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
                        snap.ship_design = fs.ship_design;
                        snap.topology = ind.topology;
                        snap.weights = ind.genome.flatten("layer_");

                        // Sync per-node activations from genome
                        for (std::size_t l = 0; l < snap.topology.layers.size(); ++l) {
                            std::string lp = "layer_" + std::to_string(l);
                            if (ind.genome.has_gene(lp + "_activations")) {
                                const auto& ag = ind.genome.gene(lp + "_activations");
                                auto& na = snap.topology.layers[l].node_activations;
                                na.resize(ag.values.size());
                                for (std::size_t n = 0; n < ag.values.size(); ++n) {
                                    int ai = std::clamp(
                                        static_cast<int>(std::round(ag.values[n])),
                                        0, neuralnet::ACTIVATION_COUNT - 1);
                                    na[n] = static_cast<neuralnet::Activation>(ai);
                                }
                            }
                        }

                        std::string path = fs.active_genome_dir + "/" +
                            snap_name + ".bin";
                        try {
                            save_snapshot(snap, path);
                        } catch (...) {
                            // Silently skip on error
                        }
                        ++idx;
                    }
                    state.variants_dirty = true;
                    state.lineage_dirty = true;
                },
                "gen" + std::to_string(fly.generation)));
        }

        if (!can_save) {
            ImGui::EndDisabled();
        }

        if (!save_error_.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                "%s", save_error_.c_str());
        }
    }
}

} // namespace neuroflyer
