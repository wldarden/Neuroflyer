#include <neuroflyer/ui/screens/fighter_drill_pause_screen.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/ui_widget.h>

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>

namespace neuroflyer {

FighterDrillPauseScreen::FighterDrillPauseScreen(
    std::vector<Individual> population,
    std::size_t generation,
    ShipDesign ship_design,
    std::string genome_dir,
    std::string variant_name,
    EvolutionConfig evo_config,
    std::function<void(const EvolutionConfig&)> on_resume)
    : population_(std::move(population))
    , generation_(generation)
    , ship_design_(std::move(ship_design))
    , genome_dir_(std::move(genome_dir))
    , variant_name_(std::move(variant_name))
    , evo_config_(std::move(evo_config))
    , on_resume_(std::move(on_resume)) {}

void FighterDrillPauseScreen::on_draw(AppState& /*state*/, Renderer& renderer,
                                       UIManager& ui) {
    // Build sorted indices on first draw
    if (!indices_built_) {
        sorted_indices_.resize(population_.size());
        for (std::size_t i = 0; i < sorted_indices_.size(); ++i)
            sorted_indices_[i] = i;

        std::sort(sorted_indices_.begin(), sorted_indices_.end(),
            [&](std::size_t a, std::size_t b) {
                return population_[a].fitness > population_[b].fitness;
            });

        selected_.assign(population_.size(), false);
        indices_built_ = true;
    }

    // Escape / Space: resume
    if (!ui.input_blocked() && (ImGui::IsKeyPressed(ImGuiKey_Escape)
            || ImGui::IsKeyPressed(ImGuiKey_Space))) {
        on_resume_(evo_config_);
        ui.pop_screen();
        return;
    }

    // Full-screen overlay window
    float screen_w = static_cast<float>(renderer.screen_w());
    float screen_h = static_cast<float>(renderer.screen_h());
    ImGui::SetNextWindowPos(ImVec2(screen_w * 0.1f, screen_h * 0.05f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(screen_w * 0.8f, screen_h * 0.9f),
                             ImGuiCond_Always);
    ImGui::Begin("Fighter Drill — Paused", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse);

    // Tab bar
    if (ImGui::BeginTabBar("##DrillPauseTabs")) {
        if (ImGui::BeginTabItem("Evolution")) {
            active_tab_ = Tab::Evolution;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Save Variants")) {
            active_tab_ = Tab::SaveVariants;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();

    float avail_h = ImGui::GetContentRegionAvail().y - 45.0f;

    if (active_tab_ == Tab::Evolution) {
        // ==================== Evolution Tab ====================
        ImGui::BeginChild("##EvoPane", ImVec2(0, avail_h), true);

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

    } else {
        // ==================== Save Variants Tab ====================
        float avail_w = ImGui::GetContentRegionAvail().x;
        float list_w = avail_w * 0.6f;
        float info_w = avail_w - list_w - 10.0f;

        // ---- Left: Fighter list ----
        ImGui::BeginChild("##FighterList", ImVec2(list_w, avail_h), true);

        // Select All / Clear
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

        for (std::size_t rank = 0; rank < sorted_indices_.size(); ++rank) {
            std::size_t pop_idx = sorted_indices_[rank];
            if (pop_idx >= population_.size()) continue;

            float fitness = population_[pop_idx].fitness;
            bool is_elite = rank < evo_config_.elitism_count;

            char label[64];
            if (is_elite) {
                std::snprintf(label, sizeof(label), "Fighter #%zu (Elite)",
                    rank + 1);
            } else {
                std::snprintf(label, sizeof(label), "Fighter #%zu", rank + 1);
            }

            ImGui::PushID(static_cast<int>(rank));

            bool is_selected = rank < selected_.size() && selected_[rank];
            if (ImGui::Selectable("##sel", is_selected,
                    ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 36))) {
                if (rank < selected_.size())
                    selected_[rank] = !selected_[rank];
            }

            ImGui::SameLine(5.0f);
            ImGui::BeginGroup();
            if (is_elite) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                    "%s", label);
            } else {
                ImGui::Text("%s", label);
            }
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 1.0f),
                "Fitness: %d", static_cast<int>(fitness));
            ImGui::EndGroup();

            ImGui::PopID();
        }

        ImGui::EndChild();

        // ---- Right: Info panel ----
        ImGui::SameLine();
        ImGui::BeginChild("##InfoPanel", ImVec2(info_w, avail_h), true);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f),
            "Fighter Drill");
        ImGui::Separator();
        ImGui::Text("Generation: %zu", generation_);
        ImGui::Text("Population: %zu", population_.size());
        ImGui::Text("Variant: %s", variant_name_.c_str());

        int sel_count = 0;
        for (bool s : selected_) if (s) ++sel_count;
        ImGui::Spacing();
        ImGui::Text("Selected: %d", sel_count);

        if (genome_dir_.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                "No genome directory.\nSaving disabled.");
        }

        ImGui::EndChild();
    }

    // ==================== Bottom bar ====================
    ImGui::Separator();

    if (active_tab_ == Tab::SaveVariants) {
        int sel_count = 0;
        for (bool s : selected_) if (s) ++sel_count;
        bool can_save = sel_count > 0 && !genome_dir_.empty();
        if (!can_save) ImGui::BeginDisabled();

        char btn_label[64];
        std::snprintf(btn_label, sizeof(btn_label),
            "Save %d Selected", sel_count);
        if (ImGui::Button(btn_label, ImVec2(180, 30))) {
            int saved = 0;
            for (std::size_t rank = 0; rank < sorted_indices_.size(); ++rank) {
                if (rank >= selected_.size() || !selected_[rank]) continue;
                std::size_t pop_idx = sorted_indices_[rank];
                if (pop_idx >= population_.size()) continue;

                const auto& ind = population_[pop_idx];

                Snapshot snap;
                snap.name = variant_name_ + "-drill-g"
                    + std::to_string(generation_) + "-"
                    + std::to_string(saved + 1);
                snap.ship_design = ship_design_;
                snap.topology = ind.topology;
                snap.weights = ind.genome.flatten("layer_");
                snap.generation = static_cast<uint32_t>(generation_);
                snap.parent_name = variant_name_;
                snap.net_type = NetType::Fighter;
                snap.created_timestamp =
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();

                std::string path = genome_dir_ + "/" + snap.name + ".bin";
                try {
                    save_snapshot(snap, path);
                    ++saved;
                } catch (const std::exception& e) {
                    std::cerr << "Failed to save variant: "
                              << e.what() << "\n";
                }
            }

            std::cout << "Saved " << saved
                      << " fighter drill variants to " << genome_dir_ << "\n";
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
