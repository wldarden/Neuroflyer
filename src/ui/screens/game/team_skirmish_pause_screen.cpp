#include <neuroflyer/ui/screens/team_skirmish_pause_screen.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/ui_widget.h>
#include <neuroflyer/ui/modals/input_modal.h>

#include <neuroflyer/app_state.h>
#include <neuroflyer/genome_manager.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>

namespace neuroflyer {

TeamSkirmishPauseScreen::TeamSkirmishPauseScreen(
    std::vector<TeamPool> team_pools,
    std::size_t generation,
    ShipDesign ship_design,
    EvolutionConfig evo_config,
    std::function<void(const EvolutionConfig&)> on_resume)
    : team_pools_(std::move(team_pools))
    , generation_(generation)
    , ship_design_(std::move(ship_design))
    , evo_config_(std::move(evo_config))
    , on_resume_(std::move(on_resume)) {}

void TeamSkirmishPauseScreen::build_sorted_indices() {
    std::size_t num_teams = team_pools_.size();

    fighter_sorted_indices_.resize(num_teams);
    fighter_selected_.resize(num_teams);
    squad_sorted_indices_.resize(num_teams);
    squad_selected_.resize(num_teams);

    for (std::size_t t = 0; t < num_teams; ++t) {
        const auto& pool = team_pools_[t];

        // Fighter indices sorted by fighter_scores descending
        {
            auto& idx = fighter_sorted_indices_[t];
            idx.resize(pool.squad_population.size());
            for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
            std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
                float fa = a < pool.fighter_scores.size() ? pool.fighter_scores[a] : 0.0f;
                float fb = b < pool.fighter_scores.size() ? pool.fighter_scores[b] : 0.0f;
                return fa > fb;
            });
            fighter_selected_[t].assign(pool.squad_population.size(), false);
        }

        // Squad indices sorted by squad_scores descending
        {
            auto& idx = squad_sorted_indices_[t];
            idx.resize(pool.squad_population.size());
            for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
            std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
                float fa = a < pool.squad_scores.size() ? pool.squad_scores[a] : 0.0f;
                float fb = b < pool.squad_scores.size() ? pool.squad_scores[b] : 0.0f;
                return fa > fb;
            });
            squad_selected_[t].assign(pool.squad_population.size(), false);
        }
    }

    indices_built_ = true;
}

void TeamSkirmishPauseScreen::on_draw(AppState& state, Renderer& renderer,
                                       UIManager& ui) {
    if (!indices_built_) {
        build_sorted_indices();
    }

    // Escape / Space: resume
    if (!ui.input_blocked() && (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsKeyPressed(ImGuiKey_Space))) {
        on_resume_(evo_config_);
        ui.pop_screen();
        return;
    }

    float screen_w = static_cast<float>(renderer.screen_w());
    float screen_h = static_cast<float>(renderer.screen_h());

    ImGui::SetNextWindowPos(ImVec2(screen_w * 0.05f, screen_h * 0.05f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(screen_w * 0.9f, screen_h * 0.9f),
                             ImGuiCond_Always);
    ImGui::Begin("Team Skirmish \xe2\x80\x94 Paused", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse);

    // Tab bar
    if (ImGui::BeginTabBar("##TSPauseTabs")) {
        if (ImGui::BeginTabItem("Evolution")) {
            active_tab_ = Tab::Evolution;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Save Fighters")) {
            active_tab_ = Tab::SaveFighters;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Save Squad Leaders")) {
            active_tab_ = Tab::SaveSquadLeaders;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();

    float avail_h = ImGui::GetContentRegionAvail().y - 45.0f;

    // ==================== Evolution Tab ====================
    if (active_tab_ == Tab::Evolution) {
        ImGui::BeginChild("##TSEvoPane", ImVec2(0, avail_h), true);

        ui::section_header("Population");
        {
            int pop = static_cast<int>(evo_config_.population_size);
            if (ImGui::InputInt("Population Size", &pop, 10, 50)) {
                evo_config_.population_size =
                    static_cast<std::size_t>(std::max(10, pop));
            }
            int elite = static_cast<int>(evo_config_.elitism_count);
            if (ImGui::InputInt("Elitism Count", &elite, 1, 5)) {
                evo_config_.elitism_count =
                    static_cast<std::size_t>(std::max(1, elite));
            }
            int tourn = static_cast<int>(evo_config_.tournament_size);
            if (ImGui::InputInt("Tournament Size", &tourn, 1, 5)) {
                evo_config_.tournament_size =
                    static_cast<std::size_t>(std::max(2, tourn));
            }
        }
        ImGui::Dummy(ImVec2(0, 5));

        ui::section_header("Weight Mutations");
        {
            ImGui::SliderFloat("Mutation Rate",
                &evo_config_.weight_mutation_rate, 0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("Mutation Strength",
                &evo_config_.weight_mutation_strength, 0.01f, 2.0f, "%.3f");
        }
        ImGui::Dummy(ImVec2(0, 5));

        ui::section_header("Topology Mutations");
        {
            ImGui::SliderFloat("Add Node Chance",
                &evo_config_.add_node_chance, 0.0f, 0.1f, "%.4f");
            ImGui::SliderFloat("Remove Node Chance",
                &evo_config_.remove_node_chance, 0.0f, 0.1f, "%.4f");
            ImGui::SliderFloat("Add Layer Chance",
                &evo_config_.add_layer_chance, 0.0f, 0.05f, "%.4f");
            ImGui::SliderFloat("Remove Layer Chance",
                &evo_config_.remove_layer_chance, 0.0f, 0.05f, "%.4f");
        }

        ImGui::EndChild();

    // ==================== Save Fighters Tab ====================
    } else if (active_tab_ == Tab::SaveFighters) {
        // Team selector
        {
            ImGui::Text("Team:");
            ImGui::SameLine();
            std::vector<std::string> team_labels;
            std::vector<const char*> team_label_ptrs;
            for (std::size_t t = 0; t < team_pools_.size(); ++t) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "Team %zu", t + 1);
                team_labels.push_back(buf);
            }
            for (const auto& s : team_labels) team_label_ptrs.push_back(s.c_str());
            ImGui::SetNextItemWidth(200.0f);
            ImGui::Combo("##FiTeam", &selected_team_fighters_,
                team_label_ptrs.data(),
                static_cast<int>(team_label_ptrs.size()));
        }

        int t = std::clamp(selected_team_fighters_, 0,
            static_cast<int>(team_pools_.size()) - 1);
        const auto& pool = team_pools_[static_cast<std::size_t>(t)];
        auto& sorted = fighter_sorted_indices_[static_cast<std::size_t>(t)];
        auto& selected = fighter_selected_[static_cast<std::size_t>(t)];

        float avail_w = ImGui::GetContentRegionAvail().x;
        float list_w = avail_w * 0.6f;
        float info_w = avail_w - list_w - 10.0f;

        ImGui::BeginChild("##FiList", ImVec2(list_w, avail_h - 30.0f), true);

        {
            int sel_count = 0;
            for (bool s : selected) if (s) ++sel_count;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d selected", sel_count);
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 1.0f), "%s", buf);
            ImGui::SameLine();
            if (ImGui::SmallButton("All##fi")) {
                for (std::size_t j = 0; j < selected.size(); ++j) selected[j] = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##fi")) {
                for (std::size_t j = 0; j < selected.size(); ++j) selected[j] = false;
            }
            ImGui::Separator();
        }

        for (std::size_t rank = 0; rank < sorted.size(); ++rank) {
            std::size_t pop_idx = sorted[rank];
            if (pop_idx >= pool.squad_population.size()) continue;

            float fitness = (pop_idx < pool.fighter_scores.size())
                ? pool.fighter_scores[pop_idx] : 0.0f;
            bool is_elite = rank < evo_config_.elitism_count;

            char label[64];
            if (is_elite) {
                std::snprintf(label, sizeof(label), "Fighter #%zu (Elite)", rank + 1);
            } else {
                std::snprintf(label, sizeof(label), "Fighter #%zu", rank + 1);
            }

            ImGui::PushID(static_cast<int>(rank));

            bool is_sel = rank < selected.size() && selected[rank];
            if (ImGui::Selectable("##fisel", is_sel,
                    ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 36))) {
                if (rank < selected.size()) selected[rank] = !selected[rank];
            }

            ImGui::SameLine(5.0f);
            ImGui::BeginGroup();
            if (is_elite) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", label);
            } else {
                ImGui::Text("%s", label);
            }
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 1.0f),
                "Fitness: %d", static_cast<int>(fitness));
            ImGui::EndGroup();

            ImGui::PopID();
        }

        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##FiInfo", ImVec2(info_w, avail_h - 30.0f), true);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Team Skirmish");
        ImGui::Separator();
        ImGui::Text("Generation: %zu", generation_);
        ImGui::Text("Team: %d", t + 1);
        ImGui::Text("Pool size: %zu", pool.squad_population.size());

        bool genome_dir_ok = !pool.seed.fighter_genome_dir.empty();
        if (!genome_dir_ok) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                "No genome dir.\nSaving disabled.");
        }

        ImGui::EndChild();

    // ==================== Save Squad Leaders Tab ====================
    } else {
        // Team selector
        {
            ImGui::Text("Team:");
            ImGui::SameLine();
            std::vector<std::string> team_labels;
            std::vector<const char*> team_label_ptrs;
            for (std::size_t ti = 0; ti < team_pools_.size(); ++ti) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "Team %zu", ti + 1);
                team_labels.push_back(buf);
            }
            for (const auto& s : team_labels) team_label_ptrs.push_back(s.c_str());
            ImGui::SetNextItemWidth(200.0f);
            ImGui::Combo("##SqTeam", &selected_team_squads_,
                team_label_ptrs.data(),
                static_cast<int>(team_label_ptrs.size()));
        }

        int t = std::clamp(selected_team_squads_, 0,
            static_cast<int>(team_pools_.size()) - 1);
        const auto& pool = team_pools_[static_cast<std::size_t>(t)];
        auto& sorted = squad_sorted_indices_[static_cast<std::size_t>(t)];
        auto& selected = squad_selected_[static_cast<std::size_t>(t)];

        float avail_w = ImGui::GetContentRegionAvail().x;
        float list_w = avail_w * 0.6f;
        float info_w = avail_w - list_w - 10.0f;

        ImGui::BeginChild("##SqList", ImVec2(list_w, avail_h - 30.0f), true);

        {
            int sel_count = 0;
            for (bool s : selected) if (s) ++sel_count;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d selected", sel_count);
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 1.0f), "%s", buf);
            ImGui::SameLine();
            if (ImGui::SmallButton("All##sq")) {
                for (std::size_t j = 0; j < selected.size(); ++j) selected[j] = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##sq")) {
                for (std::size_t j = 0; j < selected.size(); ++j) selected[j] = false;
            }
            ImGui::Separator();
        }

        for (std::size_t rank = 0; rank < sorted.size(); ++rank) {
            std::size_t pop_idx = sorted[rank];
            if (pop_idx >= pool.squad_population.size()) continue;

            float fitness = (pop_idx < pool.squad_scores.size())
                ? pool.squad_scores[pop_idx] : 0.0f;
            bool is_elite = rank < evo_config_.elitism_count;

            char label[64];
            if (is_elite) {
                std::snprintf(label, sizeof(label), "Squad #%zu (Elite)", rank + 1);
            } else {
                std::snprintf(label, sizeof(label), "Squad #%zu", rank + 1);
            }

            ImGui::PushID(static_cast<int>(rank) + 10000);

            bool is_sel = rank < selected.size() && selected[rank];
            if (ImGui::Selectable("##sqsel", is_sel,
                    ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 36))) {
                if (rank < selected.size()) selected[rank] = !selected[rank];
            }

            ImGui::SameLine(5.0f);
            ImGui::BeginGroup();
            if (is_elite) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", label);
            } else {
                ImGui::Text("%s", label);
            }
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 1.0f),
                "Fitness: %d", static_cast<int>(fitness));
            ImGui::EndGroup();

            ImGui::PopID();
        }

        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##SqInfo", ImVec2(info_w, avail_h - 30.0f), true);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Team Skirmish");
        ImGui::Separator();
        ImGui::Text("Generation: %zu", generation_);
        ImGui::Text("Team: %d", t + 1);
        ImGui::Text("Pool size: %zu", pool.squad_population.size());

        bool genome_dir_ok = !pool.seed.squad_genome_dir.empty();
        if (!genome_dir_ok) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                "No genome dir.\nSaving disabled.");
        }

        ImGui::EndChild();
    }

    // ==================== Bottom bar ====================
    ImGui::Separator();

    if (active_tab_ == Tab::SaveFighters) {
        int t = std::clamp(selected_team_fighters_, 0,
            static_cast<int>(team_pools_.size()) - 1);
        const auto& pool = team_pools_[static_cast<std::size_t>(t)];
        const auto& selected = fighter_selected_[static_cast<std::size_t>(t)];
        const auto& sorted = fighter_sorted_indices_[static_cast<std::size_t>(t)];

        int sel_count = 0;
        for (bool s : selected) if (s) ++sel_count;
        bool can_save = sel_count > 0 && !pool.seed.fighter_genome_dir.empty();
        if (!can_save) ImGui::BeginDisabled();

        char btn_label[64];
        std::snprintf(btn_label, sizeof(btn_label), "Save %d Selected", sel_count);
        if (ImGui::Button(btn_label, ImVec2(180, 30))) {
            ui.push_modal(std::make_unique<InputModal>(
                "Save Fighter Variants",
                "Enter base name:",
                [this, t, &state, sorted, selected, pool](const std::string& base_name) {
                    int saved = 0;
                    int idx = 1;
                    auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    for (std::size_t rank = 0; rank < sorted.size(); ++rank) {
                        if (rank >= selected.size() || !selected[rank]) continue;
                        std::size_t pop_idx = sorted[rank];
                        if (pop_idx >= pool.squad_population.size()) continue;

                        const auto& team_ind = pool.squad_population[pop_idx];
                        const auto& fighter = team_ind.fighter_individual;

                        char t_str[8], i_str[8];
                        std::snprintf(t_str, sizeof(t_str), "%d", t + 1);
                        std::snprintf(i_str, sizeof(i_str), "%d", idx);

                        Snapshot snap;
                        snap.name = base_name + "-t" + t_str + "-" + i_str;
                        snap.generation = static_cast<uint32_t>(generation_);
                        snap.created_timestamp = now_ts;
                        snap.net_type = NetType::Fighter;
                        snap.ship_design = ship_design_;
                        snap.topology = fighter.topology;
                        snap.weights = fighter.genome.flatten("layer_");
                        sync_activations_from_genome(fighter.genome, snap.topology);

                        try {
                            save_squad_variant(pool.seed.fighter_genome_dir, snap);
                            ++saved;
                        } catch (const std::exception& e) {
                            std::cerr << "Failed to save fighter variant: "
                                      << e.what() << "\n";
                        }
                        ++idx;
                    }

                    std::cout << "Saved " << saved << " fighter variants\n";
                    state.variants_dirty = true;
                },
                "team-g" + std::to_string(generation_)));
        }

        if (!can_save) ImGui::EndDisabled();
        ImGui::SameLine();

    } else if (active_tab_ == Tab::SaveSquadLeaders) {
        int t = std::clamp(selected_team_squads_, 0,
            static_cast<int>(team_pools_.size()) - 1);
        const auto& pool = team_pools_[static_cast<std::size_t>(t)];
        const auto& selected = squad_selected_[static_cast<std::size_t>(t)];
        const auto& sorted = squad_sorted_indices_[static_cast<std::size_t>(t)];

        int sel_count = 0;
        for (bool s : selected) if (s) ++sel_count;
        bool can_save = sel_count > 0 && !pool.seed.squad_genome_dir.empty();
        if (!can_save) ImGui::BeginDisabled();

        char btn_label[64];
        std::snprintf(btn_label, sizeof(btn_label), "Save %d Selected", sel_count);
        if (ImGui::Button(btn_label, ImVec2(180, 30))) {
            ui.push_modal(std::make_unique<InputModal>(
                "Save Squad Leader Variants",
                "Enter base name:",
                [this, t, &state, sorted, selected, pool](const std::string& base_name) {
                    int saved = 0;
                    int idx = 1;
                    auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    for (std::size_t rank = 0; rank < sorted.size(); ++rank) {
                        if (rank >= selected.size() || !selected[rank]) continue;
                        std::size_t pop_idx = sorted[rank];
                        if (pop_idx >= pool.squad_population.size()) continue;

                        const auto& team_ind = pool.squad_population[pop_idx];
                        char t_str[8], i_str[8];
                        std::snprintf(t_str, sizeof(t_str), "%d", t + 1);
                        std::snprintf(i_str, sizeof(i_str), "%d", idx);

                        // Squad leader snapshot
                        {
                            Snapshot snap;
                            snap.name = base_name + "-t" + t_str + "-" + i_str;
                            snap.generation = static_cast<uint32_t>(generation_);
                            snap.created_timestamp = now_ts;
                            snap.net_type = NetType::SquadLeader;
                            snap.ship_design = ship_design_;
                            snap.topology = team_ind.squad_individual.topology;
                            snap.weights = team_ind.squad_individual.genome.flatten("layer_");
                            sync_activations_from_genome(
                                team_ind.squad_individual.genome, snap.topology);

                            try {
                                save_squad_variant(pool.seed.squad_genome_dir, snap);
                                ++saved;
                            } catch (const std::exception& e) {
                                std::cerr << "Failed to save squad variant: "
                                          << e.what() << "\n";
                            }
                        }

                        // NTM companion snapshot
                        {
                            Snapshot ntm_snap;
                            ntm_snap.name = base_name + "-t" + t_str + "-" + i_str + "-ntm";
                            ntm_snap.generation = static_cast<uint32_t>(generation_);
                            ntm_snap.created_timestamp = now_ts;
                            ntm_snap.net_type = NetType::NTM;
                            ntm_snap.ship_design = ship_design_;
                            ntm_snap.topology = team_ind.ntm_individual.topology;
                            ntm_snap.weights = team_ind.ntm_individual.genome.flatten("layer_");
                            sync_activations_from_genome(
                                team_ind.ntm_individual.genome, ntm_snap.topology);

                            try {
                                save_squad_variant(pool.seed.squad_genome_dir, ntm_snap);
                            } catch (const std::exception& e) {
                                std::cerr << "Failed to save NTM variant: "
                                          << e.what() << "\n";
                            }
                        }

                        ++idx;
                    }

                    std::cout << "Saved " << saved << " squad variants\n";
                    state.variants_dirty = true;
                },
                "team-sq-g" + std::to_string(generation_)));
        }

        if (!can_save) ImGui::EndDisabled();
        ImGui::SameLine();
    }

    if (ImGui::Button("Resume", ImVec2(120, 30))) {
        on_resume_(evo_config_);
        ui.pop_screen();
    }

    ImGui::End();
}

} // namespace neuroflyer
