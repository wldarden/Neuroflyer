#include <neuroflyer/components/pause_training.h>

#include <neuroflyer/genome_manager.h>
#include <neuroflyer/name_validation.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>

namespace neuroflyer {
namespace {

char variant_name_buf[256] = "";
std::string save_status_msg;
float save_status_timer = 0.0f;

constexpr float FOOTER_H = 50.0f;
constexpr float LEFT_FRAC = 0.55f;

} // namespace

void draw_pause_training(AppState& state, FlySessionState& fly_state) {
    auto& config = state.config;
    auto& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;

    float content_top = ImGui::GetCursorPosY();
    float content_h = sh - content_top - FOOTER_H - ImGui::GetStyle().WindowPadding.y;
    float left_w = sw * LEFT_FRAC;

    // ==================== LEFT PANE (scrollable) ====================
    ImGui::BeginChild("##LeftPane", ImVec2(left_w, content_h), true);

    // --- Save Variant Section ---
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Save Variant");
    ImGui::Dummy(ImVec2(0, 2));

    // Training context info
    if (!fly_state.active_genome_dir.empty()) {
        // Extract genome name from path
        std::string genome_name =
            std::filesystem::path(fly_state.active_genome_dir).filename().string();
        ImGui::Text("Training: %s", genome_name.c_str());
        ImGui::Text("Parent: %s", state.training_parent_name.c_str());
        ImGui::Text("Generation: %zu", fly_state.generation);
        ImGui::Dummy(ImVec2(0, 3));

        ImGui::Text("Save Best as New Variant:");
        ImGui::SetNextItemWidth(250.0f);
        ImGui::InputText("##variant_name", variant_name_buf, sizeof(variant_name_buf));
        ImGui::SameLine();
        if (ImGui::Button("Save Best")) {
            std::string vname(variant_name_buf);
            if (vname.empty()) {
                save_status_msg = "Name cannot be empty.";
                save_status_timer = 3.0f;
            } else if (!is_valid_name(vname)) {
                save_status_msg = "Invalid name. Use alphanumeric, dash, underscore, space.";
                save_status_timer = 3.0f;
            } else {
                try {
                    auto snap = best_as_snapshot(
                        vname, fly_state.population, fly_state.ship_design,
                        static_cast<uint32_t>(fly_state.generation));
                    snap.parent_name = state.training_parent_name;
                    save_variant(fly_state.active_genome_dir, snap);
                    state.variants_dirty = true;
                    state.lineage_dirty = true;
                    state.genomes_dirty = true;
                    save_status_msg = "Saved variant '" + vname + "'";
                    save_status_timer = 3.0f;
                    std::cout << "Saved variant '" << vname << "' to "
                              << fly_state.active_genome_dir << "\n";
                } catch (const std::exception& e) {
                    save_status_msg = std::string("Save failed: ") + e.what();
                    save_status_timer = 5.0f;
                    std::cerr << "Save variant failed: " << e.what() << "\n";
                }
            }
        }

        // Show status message with timer
        if (save_status_timer > 0.0f) {
            save_status_timer -= ImGui::GetIO().DeltaTime;
            ImVec4 color = (save_status_msg.find("failed") != std::string::npos ||
                            save_status_msg.find("Invalid") != std::string::npos ||
                            save_status_msg.find("empty") != std::string::npos)
                ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                : ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
            ImGui::TextColored(color, "%s", save_status_msg.c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "No active genome. Use Hangar to select a genome and start training.");
    }

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // --- Ship Settings ---
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Ship");
    ImGui::InputFloat("Ship Speed", &config.ship_speed, 0.5f, 2.0f, "%.1f");
    ImGui::InputInt("Fire Cooldown", &config.fire_cooldown, 5, 10);
    config.fire_cooldown = std::max(1, config.fire_cooldown);
    {
        const char* ship_names[] = {
            "0: Pink Fighter", "1: Blue Multi", "2: Green Cruiser",
            "3: Red Fighter", "4: Orange Interceptor", "5: Blue Stealth",
            "6: Purple Bomber", "7: Orange Rocket", "8: Blue Heavy",
            "9: Green Armored"
        };
        int ship = config.ship_type;
        if (ImGui::BeginCombo("Ship Design", ship_names[std::clamp(ship, 0, 9)])) {
            for (int i = 0; i < 10; ++i) {
                bool selected = (ship == i);
                if (ImGui::Selectable(ship_names[i], selected)) {
                    config.ship_type = i;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Dummy(ImVec2(0, 5));

    // --- World Settings ---
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "World");
    ImGui::InputFloat("Scroll Speed", &config.scroll_speed, 0.5f, 2.0f, "%.1f");
    ImGui::InputInt("Starting Difficulty", &config.starting_difficulty, 1, 5);
    config.starting_difficulty = std::max(0, config.starting_difficulty);
    ImGui::Dummy(ImVec2(0, 5));

    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
        "(Scoring settings moved to Hangar > Fitness Function)");

    ImGui::EndChild();

    // ==================== RIGHT PANE (scrollable) ====================
    ImGui::SameLine();
    ImGui::BeginChild("##RightPane", ImVec2(0, content_h), true);

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Generation History");
    ImGui::Dummy(ImVec2(0, 2));

    if (ImGui::BeginTable("##GenHistory", 4,
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
            ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Gen", ImGuiTableColumnFlags_None, 0.15f);
        ImGui::TableSetupColumn("Best", ImGuiTableColumnFlags_None, 0.3f);
        ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_None, 0.3f);
        ImGui::TableSetupColumn("Stddev", ImGuiTableColumnFlags_None, 0.25f);
        ImGui::TableHeadersRow();

        // Most recent generation first
        for (auto it = fly_state.gen_history.rbegin();
             it != fly_state.gen_history.rend(); ++it) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", it->generation);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", static_cast<double>(it->best));
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", static_cast<double>(it->avg));
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", static_cast<double>(it->stddev));
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

} // namespace neuroflyer
