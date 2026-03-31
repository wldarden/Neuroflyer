#include <neuroflyer/ui/screens/variant_viewer_screen.h>
#include <neuroflyer/ui/screens/hangar/variant_net_editor_screen.h>
#include <neuroflyer/ui/screens/arena_config_screen.h>
#include <neuroflyer/ui/screens/fighter_drill_screen.h>
#include <neuroflyer/ui/screens/fly_session_screen.h>
#include <neuroflyer/ui/screens/lineage_tree_screen.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/ui_widget.h>

#include <neuroflyer/ui/modals/confirm_modal.h>
#include <neuroflyer/ui/modals/fighter_pairing_modal.h>
#include <neuroflyer/ui/modals/input_modal.h>
#include <neuroflyer/components/lineage_graph.h>
#include <neuroflyer/components/test_bench.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/genome_manager.h>
#include <neuroflyer/name_validation.h>
#include <neuroflyer/paths.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>
#include <neuroflyer/snapshot_utils.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace neuroflyer {

// ==================== Helper: variant file path ====================

std::string VariantViewerScreen::variant_path(
    const SnapshotHeader& hdr) const {
    if (hdr.parent_name.empty()) {
        return vs_.genome_dir + "/genome.bin";
    }
    return vs_.genome_dir + "/" + hdr.name + ".bin";
}

std::string VariantViewerScreen::squad_variant_path(
    const SnapshotHeader& hdr) const {
    return vs_.genome_dir + "/squad/" + hdr.name + ".bin";
}

// ==================== on_enter ====================

void VariantViewerScreen::on_enter() {
    sub_view_ = SubView::List;
    // Mark variants dirty so the list refreshes (new autosaves may exist),
    // but do NOT clear last_genome_ — that would reset vs_ and lose the
    // user's variant selection. The genome-change detection at draw time
    // handles actual genome switches.
    // Reset stale interaction flags
    promote_pending_ = false;
    delete_pending_ = false;
    squad_delete_pending_ = false;
}

// ==================== Tab bar drawing ====================

void VariantViewerScreen::draw_tab_bar() {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    constexpr float TAB_W = 120.0f;
    constexpr float TAB_H = 28.0f;
    constexpr float TAB_GAP = 4.0f;
    constexpr float UNDERLINE_H = 3.0f;

    struct TabDef {
        const char* label;
        NetTypeTab tab;
        ImU32 underline_color;
        bool disabled;
    };

    // Purple #a29bfe, Yellow #f9ca24, Gray for disabled
    TabDef tabs[] = {
        {"Fighters",  NetTypeTab::Fighters,  IM_COL32(162, 155, 254, 255), false},
        {"Squad Nets", NetTypeTab::SquadNets, IM_COL32(249, 202, 36, 255),  false},
        {"Commander",  NetTypeTab::Commander, IM_COL32(100, 100, 100, 255), true},
    };

    float x = cursor.x;
    for (auto& t : tabs) {
        ImGui::SetCursorScreenPos(ImVec2(x, cursor.y));

        if (t.disabled) {
            ImGui::BeginDisabled();
        }

        bool clicked = ImGui::Button(t.label, ImVec2(TAB_W, TAB_H));

        if (t.disabled) {
            ImGui::EndDisabled();
        }

        // Draw underline on active tab
        if (active_tab_ == t.tab) {
            ImVec2 p0(x, cursor.y + TAB_H + 1.0f);
            ImVec2 p1(x + TAB_W, cursor.y + TAB_H + 1.0f + UNDERLINE_H);
            draw_list->AddRectFilled(p0, p1, t.underline_color);
        }

        if (clicked && !t.disabled && active_tab_ != t.tab) {
            active_tab_ = t.tab;
        }

        x += TAB_W + TAB_GAP;
    }

    ImGui::SetCursorScreenPos(ImVec2(cursor.x,
        cursor.y + TAB_H + UNDERLINE_H + 6.0f));
    ImGui::Dummy(ImVec2(0, 2));
}

// ==================== Squad variant list ====================

VariantViewerScreen::Action VariantViewerScreen::draw_squad_list(
    float content_h) {
    Action action = Action::Stay;

    if (squad_variants_.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "(no squad net variants found)");
    } else {
        if (ImGui::BeginTable("##SquadTable", 4,
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                ImVec2(0, content_h - 100.0f))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name",
                ImGuiTableColumnFlags_None, 0.30f);
            ImGui::TableSetupColumn("Gen",
                ImGuiTableColumnFlags_None, 0.10f);
            ImGui::TableSetupColumn("Paired Fighter",
                ImGuiTableColumnFlags_None, 0.30f);
            ImGui::TableSetupColumn("Created",
                ImGuiTableColumnFlags_None, 0.30f);
            ImGui::TableHeadersRow();

            for (int i = 0;
                 i < static_cast<int>(squad_variants_.size()); ++i) {
                const auto& sv =
                    squad_variants_[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                bool is_selected = (squad_selected_idx_ == i);
                if (ImGui::Selectable(sv.name.c_str(), is_selected,
                        ImGuiSelectableFlags_SpanAllColumns)) {
                    squad_selected_idx_ = i;
                }

                ImGui::TableNextColumn();
                ImGui::Text("%u", sv.generation);

                ImGui::TableNextColumn();
                if (!sv.paired_fighter_name.empty()) {
                    ImGui::TextColored(
                        ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s",
                        sv.paired_fighter_name.c_str());
                } else {
                    ImGui::TextColored(
                        ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "---");
                }

                ImGui::TableNextColumn();
                if (sv.created_timestamp > 0) {
                    ImGui::TextColored(
                        ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s",
                        format_short_date(sv.created_timestamp).c_str());
                } else {
                    ImGui::TextColored(
                        ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "---");
                }
            }

            ImGui::EndTable();
        }
    }

    return action;
}

// ==================== Squad action panel ====================

VariantViewerScreen::Action VariantViewerScreen::draw_squad_actions(
    AppState& state, UIManager& /*ui*/, float /*content_h*/) {
    Action action = Action::Stay;

    constexpr float BTN_W = 280.0f;
    constexpr float BTN_H = 35.0f;

    // ---- Create Squad Net ----
    if (ImGui::Button("Create Squad Net", ImVec2(BTN_W, BTN_H))) {
        show_create_squad_modal_ = true;
        squad_net_name_[0] = '\0';
        squad_hidden_layers_ = 1;
        squad_layer_sizes_[0] = 8;
        squad_layer_sizes_[1] = 4;
        squad_layer_sizes_[2] = 4;
        squad_layer_sizes_[3] = 4;
        ImGui::OpenPopup("Create Squad Net##modal");
    }

    // ---- Create Squad Net modal ----
    if (show_create_squad_modal_) {
        ImGui::OpenPopup("Create Squad Net##modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Create Squad Net##modal", &show_create_squad_modal_,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(250);
        ImGui::InputText("##squad_name", squad_net_name_, sizeof(squad_net_name_));

        ImGui::Dummy(ImVec2(0, 5));
        ImGui::Text("Hidden Layers:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ImGui::SliderInt("##squad_layers", &squad_hidden_layers_, 1, 4);

        ImGui::Dummy(ImVec2(0, 3));
        for (int i = 0; i < squad_hidden_layers_; ++i) {
            char label[32];
            std::snprintf(label, sizeof(label), "Layer %d Size:", i + 1);
            ImGui::Text("%s", label);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            char slider_id[32];
            std::snprintf(slider_id, sizeof(slider_id), "##squad_lsize_%d", i);
            ImGui::SliderInt(slider_id, &squad_layer_sizes_[i], 1, 32);
        }

        ImGui::Dummy(ImVec2(0, 10));

        // Validation
        bool name_valid = is_valid_name(squad_net_name_);
        bool name_empty = (squad_net_name_[0] == '\0');

        // Check for duplicate names
        bool name_duplicate = false;
        if (name_valid) {
            for (const auto& sv : squad_variants_) {
                if (sv.name == squad_net_name_) {
                    name_duplicate = true;
                    break;
                }
            }
        }

        if (!name_empty && !name_valid) {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
                "Invalid name (alphanumeric, -, _ only)");
        }
        if (name_duplicate) {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
                "A squad net with this name already exists");
        }

        bool can_create = name_valid && !name_duplicate;

        float button_w = 120.0f;
        float spacing = ImGui::GetContentRegionAvail().x - button_w * 2;
        if (ImGui::Button("Cancel", ImVec2(button_w, 30))) {
            show_create_squad_modal_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, spacing);
        if (!can_create) ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(button_w, 30))) {
            // Build hidden_sizes vector
            std::vector<std::size_t> hidden_sizes;
            for (int i = 0; i < squad_hidden_layers_; ++i) {
                hidden_sizes.push_back(
                    static_cast<std::size_t>(squad_layer_sizes_[i]));
            }

            // Create random individual with squad leader topology
            auto ind = Individual::random(14, hidden_sizes, 5, state.rng);

            // Build snapshot
            Snapshot snap;
            snap.name = squad_net_name_;
            snap.generation = 0;
            snap.created_timestamp =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            snap.parent_name = "";
            snap.net_type = NetType::SquadLeader;
            snap.topology = ind.topology;
            snap.weights = ind.genome.flatten("layer_");
            // ship_design left default — squad nets don't have sensors

            try {
                save_squad_variant(vs_.genome_dir, snap);
                std::cout << "Created squad net '" << snap.name << "'\n";
                state.variants_dirty = true;
            } catch (const std::exception& e) {
                std::cerr << "Failed to create squad net: "
                          << e.what() << "\n";
            }

            show_create_squad_modal_ = false;
            ImGui::CloseCurrentPopup();
        }
        if (!can_create) ImGui::EndDisabled();

        ImGui::EndPopup();
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // ---- Training Scenarios ----
    ImGui::TextColored(
        ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Training Scenarios");
    ImGui::Dummy(ImVec2(0, 3));

    if (ImGui::Button("Squad vs Squad", ImVec2(BTN_W, BTN_H))) {
        action = Action::SquadTrainVsSquad;
    }
    ImGui::Dummy(ImVec2(0, 3));
    if (ImGui::Button("Base Attack", ImVec2(BTN_W, BTN_H))) {
        action = Action::SquadTrainBaseAttack;
    }
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::BeginDisabled();
    ImGui::Button("Base Defense (Coming Soon)", ImVec2(BTN_W, BTN_H));
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // ---- Fighter Pairing ----
    ImGui::TextColored(
        ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Fighter Pairing");
    ImGui::Dummy(ImVec2(0, 3));

    if (paired_fighter_name_.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
            "No fighter selected");
    } else {
        ImGui::Text("Paired: %s", paired_fighter_name_.c_str());
    }
    ImGui::Dummy(ImVec2(0, 3));
    if (ImGui::Button("[Change]##pairing", ImVec2(100, 28))) {
        action = Action::SquadChangeFighter;
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // ---- Inspection (if squad variant selected) ----
    bool has_selection = !squad_variants_.empty() &&
        squad_selected_idx_ >= 0 &&
        static_cast<std::size_t>(squad_selected_idx_) < squad_variants_.size();

    if (has_selection) {
        const auto& sel =
            squad_variants_[static_cast<std::size_t>(squad_selected_idx_)];

        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Inspection");
        ImGui::Dummy(ImVec2(0, 3));
        ImGui::Text("Selected: %s", sel.name.c_str());
        ImGui::Dummy(ImVec2(0, 3));
        if (ImGui::Button("View Squad Net", ImVec2(BTN_W, BTN_H))) {
            action = Action::SquadViewNet;
        }

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 5));

        // ---- Management ----
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Management");
        ImGui::Dummy(ImVec2(0, 3));
        if (ImGui::Button("Delete Variant##squad", ImVec2(BTN_W, BTN_H))) {
            action = Action::SquadDelete;
        }
    }

    return action;
}

// ==================== Variant list drawing ====================

VariantViewerScreen::Action VariantViewerScreen::draw_variant_list(
    AppState& state, UIManager& ui) {
    Action action = Action::Stay;

    // Refresh variant list if needed
    if (state.variants_dirty) {
        try {
            vs_.variants = list_variants(vs_.genome_dir);
        } catch (const std::exception& e) {
            std::cerr << "Failed to list variants: " << e.what() << "\n";
            vs_.variants.clear();
        }
        vs_.selected_idx = std::clamp(vs_.selected_idx, -1,
            std::max(-1, static_cast<int>(vs_.variants.size()) - 1));

        // Also refresh squad variants
        try {
            squad_variants_ = list_squad_variants(vs_.genome_dir);
        } catch (const std::exception& e) {
            std::cerr << "Failed to list squad variants: " << e.what() << "\n";
            squad_variants_.clear();
        }
        squad_selected_idx_ = std::clamp(squad_selected_idx_, 0,
            std::max(0, static_cast<int>(squad_variants_.size()) - 1));

        state.variants_dirty = false;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sw = display.x;
    const float sh = display.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGui::Begin("##VariantScreen", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.5f, 1.0f));
    char title_buf[128];
    std::snprintf(title_buf, sizeof(title_buf),
        "GENOME: %s", vs_.genome_name.c_str());
    float title_w = ImGui::CalcTextSize(title_buf).x;
    ImGui::SetCursorPosX((sw - title_w) * 0.5f);
    ImGui::Text("%s", title_buf);
    ImGui::PopStyleColor();
    ImGui::Separator();

    float content_top = ImGui::GetCursorPosY();
    float footer_h = 50.0f;
    float content_h =
        sh - content_top - footer_h - ImGui::GetStyle().WindowPadding.y;
    float left_w = sw * 0.5f;

    // ==================== LEFT: Variant List ====================
    ImGui::BeginChild("##VariantList", ImVec2(left_w, content_h), true);

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Variants");
    ImGui::Dummy(ImVec2(0, 3));

    // Tab bar: Fighters | Squad Nets | Commander
    draw_tab_bar();

    if (active_tab_ == NetTypeTab::Fighters) {
        auto is_visible = [](FilterMode mode, NetType nt) -> bool {
            return mode == FilterMode::All ||
                   (mode == FilterMode::SoloOnly  && nt == NetType::Solo) ||
                   (mode == FilterMode::SquadOnly && nt == NetType::Fighter);
        };

        // ---- Filter toggle: All | Solo | Squad ----
        {
            constexpr float FILTER_BTN_W = 60.0f;

            struct FilterBtn { const char* label; FilterMode mode; };
            constexpr FilterBtn FILTER_BTNS[] = {
                {"All",   FilterMode::All},
                {"Solo",  FilterMode::SoloOnly},
                {"Squad", FilterMode::SquadOnly},
            };

            for (const auto& fb : FILTER_BTNS) {
                bool is_active = (fighter_filter_ == fb.mode);
                if (ui::button(fb.label,
                        is_active ? ui::ButtonStyle::Primary
                                  : ui::ButtonStyle::Secondary,
                        FILTER_BTN_W)) {
                    if (!is_active) {
                        fighter_filter_ = fb.mode;
                        // Reset selection if current selection is no longer visible
                        if (!vs_.variants.empty() && vs_.selected_idx >= 0 &&
                            static_cast<std::size_t>(vs_.selected_idx) <
                                vs_.variants.size()) {
                            const auto& sel = vs_.variants[
                                static_cast<std::size_t>(vs_.selected_idx)];
                            if (!is_visible(fb.mode, sel.net_type)) {
                                vs_.selected_idx = -1;
                                for (int i = 0;
                                     i < static_cast<int>(vs_.variants.size());
                                     ++i) {
                                    const auto& v = vs_.variants[
                                        static_cast<std::size_t>(i)];
                                    if (is_visible(fb.mode, v.net_type)) {
                                        vs_.selected_idx = i;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::Dummy(ImVec2(0, 4));
        }

        // ---- Fighter variant table ----
        if (vs_.variants.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "(no variants found)");
        } else {
            // Build filtered index list
            std::vector<int> filtered_indices;
            filtered_indices.reserve(vs_.variants.size());
            for (int i = 0;
                 i < static_cast<int>(vs_.variants.size()); ++i) {
                const auto& v =
                    vs_.variants[static_cast<std::size_t>(i)];
                if (is_visible(fighter_filter_, v.net_type)) {
                    filtered_indices.push_back(i);
                }
            }

            if (filtered_indices.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                    "(no variants match filter)");
            } else if (ImGui::BeginTable("##VarTable", 5,
                    ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                    ImVec2(0, content_h - 100.0f))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name",
                    ImGuiTableColumnFlags_None, 0.30f);
                ImGui::TableSetupColumn("Gen",
                    ImGuiTableColumnFlags_None, 0.10f);
                ImGui::TableSetupColumn("Runs",
                    ImGuiTableColumnFlags_None, 0.10f);
                ImGui::TableSetupColumn("Created",
                    ImGuiTableColumnFlags_None, 0.18f);
                ImGui::TableSetupColumn("Parent",
                    ImGuiTableColumnFlags_None, 0.32f);
                ImGui::TableHeadersRow();

                for (const int i : filtered_indices) {
                    const auto& v =
                        vs_.variants[static_cast<std::size_t>(i)];
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();

                    // Type badge
                    if (v.net_type == NetType::Fighter) {
                        ImGui::TextColored(
                            ImVec4(0.64f, 0.61f, 0.99f, 1.0f), "[SQUAD]");
                    } else if (v.net_type == NetType::Solo) {
                        ImGui::TextColored(
                            ImVec4(0.0f, 0.82f, 0.83f, 1.0f), "[SOLO]");
                    } else {
                        ImGui::TextColored(
                            ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[?]");
                    }
                    ImGui::SameLine();

                    bool is_selected = (vs_.selected_idx == i);
                    if (ImGui::Selectable(v.name.c_str(), is_selected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
                        vs_.selected_idx = i;
                    }

                    ImGui::TableNextColumn();
                    ImGui::Text("%u", v.generation);

                    ImGui::TableNextColumn();
                    if (v.run_count > 0) {
                        ImGui::Text("%u", v.run_count);
                    } else {
                        ImGui::TextColored(
                            ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "0");
                    }

                    ImGui::TableNextColumn();
                    if (v.created_timestamp > 0) {
                        ImGui::TextColored(
                            ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s",
                            format_short_date(v.created_timestamp).c_str());
                    } else {
                        ImGui::TextColored(
                            ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "---");
                    }

                    ImGui::TableNextColumn();
                    if (v.parent_name.empty()) {
                        ImGui::TextColored(
                            ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "(root)");
                    } else {
                        ImGui::Text("%s", v.parent_name.c_str());
                    }
                }

                ImGui::EndTable();
            }
        }
    } else if (active_tab_ == NetTypeTab::SquadNets) {
        // ---- Squad net variant table ----
        auto squad_action = draw_squad_list(content_h);
        if (squad_action != Action::Stay) {
            action = squad_action;
        }
    }

    ImGui::EndChild();

    // Vertical divider between panels
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 win_pos = ImGui::GetWindowPos();
        float pad = ImGui::GetStyle().WindowPadding.x;
        float divider_x = win_pos.x + pad + left_w +
            ImGui::GetStyle().ItemSpacing.x * 0.5f;
        float top_y = win_pos.y + content_top;
        draw->AddLine(
            ImVec2(divider_x, top_y),
            ImVec2(divider_x, top_y + content_h),
            IM_COL32(80, 80, 80, 200), 1.0f);
    }

    // ==================== RIGHT: Details & Actions ====================
    ImGui::SameLine();
    ImGui::BeginChild("##VariantActions", ImVec2(0, content_h), true);

    if (active_tab_ == NetTypeTab::SquadNets) {
        // ---- Squad net action panel ----
        auto squad_act = draw_squad_actions(state, ui, content_h);
        if (squad_act != Action::Stay) {
            action = squad_act;
        }
    } else if (!vs_.variants.empty() &&
        vs_.selected_idx >= 0 &&
        static_cast<std::size_t>(vs_.selected_idx) < vs_.variants.size()) {

        const auto& sel =
            vs_.variants[static_cast<std::size_t>(vs_.selected_idx)];

        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Selected Variant");
        ImGui::Dummy(ImVec2(0, 3));
        ImGui::Text("Name:       %s", sel.name.c_str());
        ImGui::Text("Generation: %u", sel.generation);
        ImGui::Text("Parent:     %s",
            sel.parent_name.empty() ? "(root)" : sel.parent_name.c_str());

        // Show timestamp if available
        if (sel.created_timestamp > 0) {
            auto tp = std::chrono::system_clock::from_time_t(
                static_cast<std::time_t>(sel.created_timestamp));
            auto time = std::chrono::system_clock::to_time_t(tp);
            std::tm tm_buf{};
            localtime_r(&time, &tm_buf);
            char date_str[64];
            std::snprintf(date_str, sizeof(date_str),
                "%04d-%02d-%02d %02d:%02d",
                tm_buf.tm_year + 1900, tm_buf.tm_mon + 1,
                tm_buf.tm_mday, tm_buf.tm_hour, tm_buf.tm_min);
            ImGui::Text("Created:    %s", date_str);
        }

        ImGui::Dummy(ImVec2(0, 15));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));

        // ---- Training ----
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Training");
        ImGui::Dummy(ImVec2(0, 3));
        constexpr float BTN_W = 280.0f;
        constexpr float BTN_H = 35.0f;

        if (ImGui::Button("Train Fresh (Random Weights)",
                ImVec2(BTN_W, BTN_H))) {
            action = Action::TrainFresh;
        }
        ImGui::Dummy(ImVec2(0, 3));
        if (ImGui::Button("Train from This Variant",
                ImVec2(BTN_W, BTN_H))) {
            action = Action::TrainFrom;
        }

        ImGui::Spacing();
        if (ImGui::Button("Arena: Fresh (Random Weights)",
                ImVec2(BTN_W, BTN_H))) {
            action = Action::ArenaFresh;
        }
        ImGui::Dummy(ImVec2(0, 3));
        if (ImGui::Button("Arena: From This Variant",
                ImVec2(BTN_W, BTN_H))) {
            action = Action::ArenaFrom;
        }
        ImGui::Dummy(ImVec2(0, 3));
        if (ImGui::Button("Fighter Drill",
                ImVec2(BTN_W, BTN_H))) {
            action = Action::FighterDrill;
        }

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 5));

        // ---- Inspection ----
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Inspection");
        ImGui::Dummy(ImVec2(0, 3));
        if (ImGui::Button("View Neural Net",
                ImVec2(BTN_W, BTN_H))) {
            action = Action::ViewNet;
        }
        ImGui::Dummy(ImVec2(0, 3));
        if (ImGui::Button("Sensor Testing",
                ImVec2(BTN_W, BTN_H))) {
            action = Action::TestBench;
        }
        ImGui::Dummy(ImVec2(0, 3));
        if (ImGui::Button("Lineage Tree",
                ImVec2(BTN_W, BTN_H))) {
            action = Action::LineageTree;
        }

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 5));

        // ---- Evolution Settings ----
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Evolution Settings");
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
            "Checked = can mutate");
        {
            // Load current flags from genome
            if (!evo_loaded_ || state.variants_dirty) {
                std::string gpath = vs_.genome_dir + "/genome.bin";
                try {
                    auto gsnap = load_snapshot(gpath);
                    evo_flags_ = gsnap.ship_design.evolvable;
                } catch (const std::exception& e) {
                    std::cerr << "Failed to load genome evo flags: " << e.what() << "\n";
                }
                evo_loaded_ = true;
            }

            bool changed = false;
            if (ImGui::Checkbox("Sensor Angles##evo", &evo_flags_.sensor_angle)) changed = true;
            if (ImGui::Checkbox("Sensor Range##evo", &evo_flags_.sensor_range)) changed = true;
            if (ImGui::Checkbox("Sensor Width##evo", &evo_flags_.sensor_width)) changed = true;

            if (changed) {
                // Save updated flags back to genome.bin
                std::string gpath = vs_.genome_dir + "/genome.bin";
                try {
                    auto gsnap = load_snapshot(gpath);
                    gsnap.ship_design.evolvable = evo_flags_;
                    save_snapshot(gsnap, gpath);
                } catch (const std::exception& e) {
                    std::cerr << "Failed to save evolution flags: " << e.what() << "\n";
                }
            }
        }
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 5));

        // ---- Management ----
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Management");
        ImGui::Dummy(ImVec2(0, 3));
        if (ImGui::Button("Promote to New Genome",
                ImVec2(BTN_W, BTN_H))) {
            ui.push_modal(std::make_unique<InputModal>(
                "Promote to New Genome",
                "Enter a name for the new genome:",
                [this](const std::string& name) {
                    promote_name_ = name;
                    promote_pending_ = true;
                }));
        }
        ImGui::Dummy(ImVec2(0, 3));

        // Don't allow deleting the base genome via variant delete
        // (use Delete Genome in hangar instead)
        bool is_base = sel.parent_name.empty();
        if (is_base) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Delete Variant",
                ImVec2(BTN_W, BTN_H))) {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                "Delete variant '%s'?\n\n"
                "Children will be re-parented to this variant's parent.",
                sel.name.c_str());
            std::string del_name = sel.name;
            ui.push_modal(std::make_unique<ConfirmModal>(
                "Delete Variant", msg,
                [this, del_name]() {
                    delete_variant_name_ = del_name;
                    delete_pending_ = true;
                }));
        }
        if (is_base) {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextColored(
                ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(base genome)");
        }

        // ---- Lineage Graph View ----
        if (vs_.show_lineage) {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 5));
            ImGui::TextColored(
                ImVec4(0.2f, 0.8f, 0.9f, 1.0f), "Lineage Tree");
            ImGui::Dummy(ImVec2(0, 3));

            // Graph on left, action panel on right when a node is selected
            float avail_w = ImGui::GetContentRegionAvail().x;
            float panel_w =
                (lineage_state_.selected_node >= 0) ? 220.0f : 0.0f;
            float graph_w =
                avail_w - panel_w - (panel_w > 0 ? 10.0f : 0.0f);
            float graph_h = 300.0f;

            // Pass genomes_dir for cross-genome lineage display
            [[maybe_unused]] std::string vv_genomes_dir =
                std::filesystem::path(vs_.genome_dir).parent_path().string();
            [[maybe_unused]] auto graph_action =
                draw_lineage_graph(
                    lineage_state_, vs_.genome_dir,
                    graph_w, graph_h);

            // Right panel for selected node
            if (lineage_state_.selected_node >= 0) {
                ImGui::SameLine();
                ImGui::BeginChild("##LineagePanel",
                    ImVec2(panel_w, graph_h), true);

                auto& sel_node = lineage_state_.nodes[
                    static_cast<std::size_t>(
                        lineage_state_.selected_node)];
                ImGui::TextColored(
                    ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                    "%s", sel_node.name.c_str());
                ImGui::Separator();

                if (sel_node.is_genome) {
                    ImGui::TextColored(
                        ImVec4(0.9f, 0.55f, 0.1f, 1.0f),
                        "Genome (Root)");
                } else if (sel_node.is_mrca_stub) {
                    ImGui::TextColored(
                        ImVec4(0.6f, 0.6f, 0.3f, 1.0f),
                        "Branch Point");
                } else {
                    ImGui::TextColored(
                        ImVec4(0.3f, 0.7f, 0.9f, 1.0f),
                        "Variant");
                }

                ImGui::Text("Generation: %d",
                    sel_node.generation);
                if (!sel_node.file.empty()) {
                    ImGui::TextColored(
                        ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                        "%s", sel_node.file.c_str());
                }

                ImGui::Dummy(ImVec2(0, 10));
                ImGui::TextColored(
                    ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Actions");

                if (!sel_node.is_mrca_stub
                    && !sel_node.is_genome) {
                    if (ImGui::Button("Set as Default",
                            ImVec2(200, 28))) {
                        for (int vi = 0;
                             vi < static_cast<int>(
                                 vs_.variants.size());
                             ++vi) {
                            if (vs_.variants[
                                    static_cast<std::size_t>(vi)]
                                    .name == sel_node.name) {
                                vs_.selected_idx = vi;
                                break;
                            }
                        }
                    }
                    ImGui::Dummy(ImVec2(0, 3));
                    if (ImGui::Button("Create As Genome",
                            ImVec2(200, 28))) {
                        for (int vi = 0;
                             vi < static_cast<int>(
                                 vs_.variants.size());
                             ++vi) {
                            if (vs_.variants[
                                    static_cast<std::size_t>(vi)]
                                    .name == sel_node.name) {
                                vs_.selected_idx = vi;
                                break;
                            }
                        }
                        ui.push_modal(std::make_unique<InputModal>(
                            "Promote to New Genome",
                            "Enter a name for the new genome:",
                            [this](const std::string& name) {
                                promote_name_ = name;
                                promote_pending_ = true;
                            }));
                    }
                    ImGui::Dummy(ImVec2(0, 3));
                    if (ImGui::Button("View Neural Net##lineage",
                            ImVec2(200, 28))) {
                        for (int vi = 0;
                             vi < static_cast<int>(
                                 vs_.variants.size());
                             ++vi) {
                            if (vs_.variants[
                                    static_cast<std::size_t>(vi)]
                                    .name == sel_node.name) {
                                vs_.selected_idx = vi;
                                break;
                            }
                        }
                        action = Action::ViewNet;
                    }
                }

                if (sel_node.is_genome) {
                    ImGui::TextColored(
                        ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                        "(Root genome cannot be modified)");
                }

                ImGui::EndChild();
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "No variant selected.");
    }

    ImGui::EndChild();

    // ==================== FOOTER ====================
    ImGui::Separator();
    float pad = ImGui::GetStyle().WindowPadding.x;
    float footer_y = ImGui::GetCursorPosY();

    ImGui::SetCursorPos(ImVec2(pad, footer_y + 5.0f));
    if (ImGui::Button("Back to Hangar", ImVec2(200, 35))) {
        action = Action::Back;
    }

    ImGui::End();

    return action;
}

// ==================== on_draw ====================

void VariantViewerScreen::on_draw(
    AppState& state, Renderer& renderer, UIManager& ui) {
    (void)renderer;  // no longer used directly for net rendering

    // Process pending modal results
    if (promote_pending_) {
        promote_pending_ = false;
        if (is_valid_name(promote_name_)) {
            try {
                std::string genomes_dir = state.data_dir + "/genomes";
                const auto& sel =
                    vs_.variants[static_cast<std::size_t>(vs_.selected_idx)];
                promote_to_genome(genomes_dir, vs_.genome_dir,
                                  sel.name, promote_name_);
                std::cout << "Promoted '" << sel.name
                          << "' to new genome '" << promote_name_ << "'\n";
                state.genomes_dirty = true;
                state.lineage_dirty = true;
            } catch (const std::exception& e) {
                std::cerr << "Promote failed: " << e.what() << "\n";
            }
        }
    }
    if (delete_pending_) {
        delete_pending_ = false;
        try {
            delete_variant(vs_.genome_dir, delete_variant_name_);
            std::cout << "Deleted variant '" << delete_variant_name_ << "'\n";
            state.variants_dirty = true;
            state.lineage_dirty = true;
            state.genomes_dirty = true;
            if (vs_.selected_idx > 0) {
                --vs_.selected_idx;
            }
        } catch (const std::exception& e) {
            std::cerr << "Delete failed: " << e.what() << "\n";
        }
    }
    if (squad_delete_pending_) {
        squad_delete_pending_ = false;
        try {
            delete_squad_variant(vs_.genome_dir, squad_delete_name_);
            std::cout << "Deleted squad variant '" << squad_delete_name_ << "'\n";
            state.variants_dirty = true;
            if (squad_selected_idx_ > 0) {
                --squad_selected_idx_;
            }
        } catch (const std::exception& e) {
            std::cerr << "Squad delete failed: " << e.what() << "\n";
        }
    }

    // Detect genome change and reinitialize
    if (state.active_genome != last_genome_) {
        last_genome_ = state.active_genome;
        vs_ = {};
        vs_.genome_name = state.active_genome;
        vs_.genome_dir =
            state.data_dir + "/genomes/" + state.active_genome;
        state.variants_dirty = true;
        state.lineage_dirty = true;
        evo_loaded_ = false;
        sub_view_ = SubView::List;
        // Reset squad state for new genome
        squad_variants_.clear();
        squad_selected_idx_ = 0;
        paired_fighter_name_.clear();
    }

    // Ensure lineage graph rebuilds with cross-genome info
    if (state.lineage_dirty && !vs_.genome_dir.empty()) {
        std::string genomes_dir = state.data_dir + "/genomes";
        rebuild_lineage_graph(lineage_state_, vs_.genome_dir, genomes_dir);
        state.lineage_dirty = false;
    }

    // Escape = go back one level (skip if a modal is blocking input)
    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        switch (sub_view_) {
        case SubView::List:
            // If lineage tree is open, close it first
            if (vs_.show_lineage) {
                vs_.show_lineage = false;
            } else {
                ui.pop_screen();
            }
            return;
        case SubView::TestBench:
            sub_view_ = SubView::List;
            return;
        }
    }

    switch (sub_view_) {
    case SubView::List: {
        auto action = draw_variant_list(state, ui);

        switch (action) {
        case Action::Back:
            ui.pop_screen();
            break;

        case Action::TrainFresh: {
            try {
                std::string genome_path = vs_.genome_dir + "/genome.bin";
                auto genome_snap = load_snapshot(genome_path);
                // Increment run count and re-save
                ++genome_snap.run_count;
                save_snapshot(genome_snap, genome_path);
                EvolutionConfig evo_cfg;
                evo_cfg.population_size = state.config.population_size;
                evo_cfg.elitism_count = state.config.elitism_count;
                state.pending_population =
                    create_population_from_snapshot(
                        genome_snap,
                        state.config.population_size,
                        evo_cfg, state.rng);
                state.pending_ship_design = genome_snap.ship_design;
                state.training_parent_name = genome_snap.name;
                state.config.active_genome =
                    std::filesystem::path(vs_.genome_dir)
                    .filename().string();
                state.config.save(state.settings_path);
                std::cout << "TrainFresh: seeding from genome '"
                          << genome_snap.name << "' ("
                          << state.pending_population.size()
                          << " individuals)\n";
                state.return_to_variant_view = true;
                ui.push_screen(std::make_unique<FlySessionScreen>());
            } catch (const std::exception& e) {
                std::cerr << "TrainFresh failed: "
                          << e.what() << "\n";
            }
            break;
        }

        case Action::TrainFrom: {
            if (!vs_.variants.empty() &&
                vs_.selected_idx >= 0 &&
                static_cast<std::size_t>(vs_.selected_idx)
                    < vs_.variants.size()) {
                const auto& sel = vs_.variants[
                    static_cast<std::size_t>(vs_.selected_idx)];
                try {
                    std::string vpath = variant_path(sel);
                    auto variant_snap = load_snapshot(vpath);
                    // Increment run count and re-save
                    ++variant_snap.run_count;
                    save_snapshot(variant_snap, vpath);
                    EvolutionConfig evo_cfg;
                    evo_cfg.population_size =
                        state.config.population_size;
                    evo_cfg.elitism_count =
                        state.config.elitism_count;
                    state.pending_population =
                        create_population_from_snapshot(
                            variant_snap,
                            state.config.population_size,
                            evo_cfg, state.rng);
                    state.pending_ship_design = variant_snap.ship_design;
                    state.training_parent_name = sel.name;
                    state.config.active_genome =
                        std::filesystem::path(vs_.genome_dir)
                        .filename().string();
                    state.config.save(state.settings_path);
                    std::cout << "TrainFrom: seeding from variant '"
                              << sel.name << "' ("
                              << state.pending_population.size()
                              << " individuals)\n";
                    state.return_to_variant_view = true;
                    ui.push_screen(std::make_unique<FlySessionScreen>());
                } catch (const std::exception& e) {
                    std::cerr << "TrainFrom failed: "
                              << e.what() << "\n";
                }
            }
            break;
        }

        case Action::ArenaFresh: {
            try {
                auto genome_snap = load_snapshot(
                    vs_.genome_dir + "/genome.bin");
                EvolutionConfig evo_cfg;
                evo_cfg.population_size = state.config.population_size;
                evo_cfg.elitism_count = state.config.elitism_count;
                state.pending_population =
                    create_population_from_snapshot(
                        genome_snap,
                        state.config.population_size,
                        evo_cfg, state.rng);
                state.pending_ship_design = genome_snap.ship_design;
                state.training_parent_name = genome_snap.name;
                state.config.active_genome =
                    std::filesystem::path(vs_.genome_dir)
                    .filename().string();
                state.config.save(state.settings_path);
                state.return_to_variant_view = true;
                ui.push_screen(std::make_unique<ArenaConfigScreen>());
            } catch (const std::exception& e) {
                std::cerr << "ArenaFresh failed: "
                          << e.what() << "\n";
            }
            break;
        }

        case Action::ArenaFrom: {
            if (!vs_.variants.empty() &&
                vs_.selected_idx >= 0 &&
                static_cast<std::size_t>(vs_.selected_idx)
                    < vs_.variants.size()) {
                const auto& sel = vs_.variants[
                    static_cast<std::size_t>(vs_.selected_idx)];
                try {
                    auto variant_snap = load_snapshot(
                        variant_path(sel));
                    EvolutionConfig evo_cfg;
                    evo_cfg.population_size =
                        state.config.population_size;
                    evo_cfg.elitism_count =
                        state.config.elitism_count;
                    state.pending_population =
                        create_population_from_snapshot(
                            variant_snap,
                            state.config.population_size,
                            evo_cfg, state.rng);
                    state.pending_ship_design = variant_snap.ship_design;
                    state.training_parent_name = sel.name;
                    state.config.active_genome =
                        std::filesystem::path(vs_.genome_dir)
                        .filename().string();
                    state.config.save(state.settings_path);
                    state.return_to_variant_view = true;
                    ui.push_screen(std::make_unique<ArenaConfigScreen>());
                } catch (const std::exception& e) {
                    std::cerr << "ArenaFrom failed: "
                              << e.what() << "\n";
                }
            }
            break;
        }

        case Action::FighterDrill: {
            if (!vs_.variants.empty() &&
                vs_.selected_idx >= 0 &&
                static_cast<std::size_t>(vs_.selected_idx)
                    < vs_.variants.size()) {
                const auto& sel = vs_.variants[
                    static_cast<std::size_t>(vs_.selected_idx)];
                try {
                    auto snap = load_snapshot(variant_path(sel));
                    state.return_to_variant_view = true;
                    ui.push_screen(std::make_unique<FighterDrillScreen>(
                        std::move(snap), vs_.genome_dir, sel.name));
                } catch (const std::exception& e) {
                    std::cerr << "FighterDrill failed: "
                              << e.what() << "\n";
                }
            }
            break;
        }

        case Action::ViewNet: {
            if (!vs_.variants.empty() &&
                vs_.selected_idx >= 0 &&
                static_cast<std::size_t>(vs_.selected_idx)
                    < vs_.variants.size()) {
                const auto& sel_var = vs_.variants[
                    static_cast<std::size_t>(vs_.selected_idx)];
                std::string var_path = variant_path(sel_var);
                try {
                    auto snap = load_snapshot(var_path);
                    auto ind = snapshot_to_individual(snap);
                    auto net = ind.build_network();
                    ui.push_screen(std::make_unique<VariantNetEditorScreen>(
                        std::move(ind), std::move(net), snap.ship_design,
                        var_path, sel_var.name, snap.net_type));
                } catch (const std::exception& e) {
                    std::cerr
                        << "Failed to load variant for viewing: "
                        << e.what() << "\n";
                }
            }
            break;
        }

        case Action::TestBench: {
            if (!vs_.variants.empty() &&
                vs_.selected_idx >= 0 &&
                static_cast<std::size_t>(vs_.selected_idx)
                    < vs_.variants.size()) {
                const auto& sel_var = vs_.variants[
                    static_cast<std::size_t>(vs_.selected_idx)];
                std::string var_path = variant_path(sel_var);
                try {
                    auto snap = load_snapshot(var_path);
                    auto ind = snapshot_to_individual(snap);

                    networks_.clear();
                    networks_.push_back(ind.build_network());

                    test_bench_state_ = {};
                    test_bench_state_.design = snap.ship_design;
                    test_bench_state_.variant_path = var_path;
                    test_bench_state_.design_backup = snap.ship_design;
                    test_bench_state_.config_backup = state.config;

                    sub_view_ = SubView::TestBench;
                    std::cout << "Test bench loaded variant: "
                              << sel_var.name << "\n";
                } catch (const std::exception& e) {
                    std::cerr
                        << "Failed to load variant for test bench: "
                        << e.what() << "\n";
                }
            }
            break;
        }

        case Action::PromoteToGenome:
        case Action::DeleteVariant:
            // Handled via UIModal push/pop + pending flags
            break;
        case Action::LineageTree:
            ui.push_screen(
                std::make_unique<LineageTreeScreen>());
            break;

        // ---- Squad actions ----
        case Action::SquadTrainVsSquad: {
            if (paired_fighter_name_.empty()) {
                ui.push_modal(std::make_unique<FighterPairingModal>(
                    vs_.variants,
                    [this](const std::string& name) {
                        paired_fighter_name_ = name;
                    }));
            } else {
                state.squad_training_mode = true;
                state.base_attack_mode = false;
                state.squad_paired_fighter_name = paired_fighter_name_;
                state.squad_training_genome_dir = vs_.genome_dir;
                // Pass selected squad variant name (if any) to seed squad nets
                if (!squad_variants_.empty() && squad_selected_idx_ >= 0 &&
                    static_cast<std::size_t>(squad_selected_idx_) < squad_variants_.size()) {
                    state.squad_variant_name = squad_variants_[
                        static_cast<std::size_t>(squad_selected_idx_)].name;
                } else {
                    state.squad_variant_name.clear();
                }
                state.return_to_variant_view = true;
                ui.push_screen(std::make_unique<ArenaConfigScreen>());
            }
            break;
        }

        case Action::SquadTrainBaseAttack: {
            if (paired_fighter_name_.empty()) {
                ui.push_modal(std::make_unique<FighterPairingModal>(
                    vs_.variants,
                    [this](const std::string& name) {
                        paired_fighter_name_ = name;
                    }));
            } else {
                state.squad_training_mode = true;
                state.base_attack_mode = true;
                state.squad_paired_fighter_name = paired_fighter_name_;
                state.squad_training_genome_dir = vs_.genome_dir;
                // Pass selected squad variant name (if any) to seed squad nets
                if (!squad_variants_.empty() && squad_selected_idx_ >= 0 &&
                    static_cast<std::size_t>(squad_selected_idx_) < squad_variants_.size()) {
                    state.squad_variant_name = squad_variants_[
                        static_cast<std::size_t>(squad_selected_idx_)].name;
                } else {
                    state.squad_variant_name.clear();
                }
                state.return_to_variant_view = true;
                ui.push_screen(std::make_unique<ArenaConfigScreen>());
            }
            break;
        }

        case Action::SquadChangeFighter: {
            ui.push_modal(std::make_unique<FighterPairingModal>(
                vs_.variants,
                [this](const std::string& name) {
                    paired_fighter_name_ = name;
                }));
            break;
        }

        case Action::SquadViewNet: {
            if (!squad_variants_.empty() &&
                squad_selected_idx_ >= 0 &&
                static_cast<std::size_t>(squad_selected_idx_)
                    < squad_variants_.size()) {
                const auto& sel_sq = squad_variants_[
                    static_cast<std::size_t>(squad_selected_idx_)];
                std::string sq_path = squad_variant_path(sel_sq);
                try {
                    auto snap = load_snapshot(sq_path);
                    auto ind = snapshot_to_individual(snap);
                    auto net = ind.build_network();
                    ui.push_screen(std::make_unique<VariantNetEditorScreen>(
                        std::move(ind), std::move(net), snap.ship_design,
                        sq_path, sel_sq.name, snap.net_type));
                } catch (const std::exception& e) {
                    std::cerr
                        << "Failed to load squad variant for viewing: "
                        << e.what() << "\n";
                }
            }
            break;
        }

        case Action::SquadDelete: {
            if (!squad_variants_.empty() &&
                squad_selected_idx_ >= 0 &&
                static_cast<std::size_t>(squad_selected_idx_)
                    < squad_variants_.size()) {
                const auto& sel_sq = squad_variants_[
                    static_cast<std::size_t>(squad_selected_idx_)];
                char msg[256];
                std::snprintf(msg, sizeof(msg),
                    "Delete squad variant '%s'?",
                    sel_sq.name.c_str());
                std::string del_name = sel_sq.name;
                ui.push_modal(std::make_unique<ConfirmModal>(
                    "Delete Squad Variant", msg,
                    [this, del_name]() {
                        squad_delete_name_ = del_name;
                        squad_delete_pending_ = true;
                    }));
            }
            break;
        }

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
            // Save sensor design back to the variant's .bin file
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
            sub_view_ = SubView::List;
            state.variants_dirty = true;
            state.lineage_dirty = true;
        } else if (test_bench_state_.wants_cancel) {
            // Restore design backup on cancel
            sub_view_ = SubView::List;
            state.variants_dirty = true;
        }
        break;
    }
    }
}

} // namespace neuroflyer
