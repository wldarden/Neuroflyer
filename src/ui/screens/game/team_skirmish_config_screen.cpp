#include <neuroflyer/ui/screens/team_skirmish_config_screen.h>
#include <neuroflyer/ui/screens/team_skirmish_screen.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/ui_widget.h>

#include <neuroflyer/app_state.h>
#include <neuroflyer/genome_manager.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/snapshot_io.h>

#include <imgui.h>

#include <cstdio>
#include <iostream>
#include <memory>

namespace neuroflyer {

namespace {

// Team color labels for display
struct TeamColorDisplay { const char* name; float r, g, b; };
constexpr TeamColorDisplay k_team_colors[] = {
    {"Blue",    0.39f, 0.59f, 1.0f},
    {"Red",     1.0f,  0.39f, 0.39f},
    {"Green",   0.39f, 1.0f,  0.39f},
    {"Yellow",  1.0f,  1.0f,  0.39f},
    {"Cyan",    0.39f, 1.0f,  1.0f},
    {"Magenta", 1.0f,  0.39f, 1.0f},
    {"Orange",  1.0f,  0.71f, 0.39f},
    {"White",   0.86f, 0.86f, 0.86f},
};

} // anonymous namespace

void TeamSkirmishConfigScreen::refresh_genomes(AppState& state) {
    std::string genomes_dir = state.data_dir + "/genomes";
    auto genome_infos = list_genomes(genomes_dir);

    genomes_.clear();
    squad_variants_.clear();
    fighter_variants_.clear();

    for (const auto& info : genome_infos) {
        std::string dir = genomes_dir + "/" + info.name;
        genomes_.push_back({info.name, dir});

        // Squad variants from squad/ subdirectory
        std::vector<VariantEntry> sq_entries;
        try {
            auto sq_headers = list_squad_variants(dir);
            for (const auto& h : sq_headers) {
                std::string path = dir + "/squad/" + h.name + ".bin";
                sq_entries.push_back({h.name, path});
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to list squad variants for " << info.name
                      << ": " << e.what() << "\n";
        }
        squad_variants_.push_back(std::move(sq_entries));

        // Fighter variants from genome directory
        std::vector<VariantEntry> fi_entries;
        try {
            auto fi_headers = list_variants(dir);
            for (const auto& h : fi_headers) {
                if (h.net_type == NetType::Fighter || h.net_type == NetType::Solo) {
                    std::string path = dir + "/" + h.name + ".bin";
                    fi_entries.push_back({h.name, path});
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to list fighter variants for " << info.name
                      << ": " << e.what() << "\n";
        }
        // Also include genome.bin as a fighter option
        fi_entries.insert(fi_entries.begin(), {"genome.bin", dir + "/genome.bin"});
        fighter_variants_.push_back(std::move(fi_entries));
    }

    // Reset per-team selections to 0
    for (int t = 0; t < MAX_TEAMS; ++t) {
        squad_genome_idx_[t] = 0;
        squad_variant_idx_[t] = 0;
        fighter_genome_idx_[t] = 0;
        fighter_variant_idx_[t] = 0;
        snapshots_loaded_[t] = false;
    }

    genomes_loaded_ = true;
}

void TeamSkirmishConfigScreen::on_draw(AppState& state, Renderer& /*renderer*/,
                                        UIManager& ui) {
    if (!genomes_loaded_) {
        refresh_genomes(state);
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(display, ImGuiCond_Always);
    ImGui::Begin("##TeamSkirmishConfig", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("Team Skirmish \xe2\x80\x94 Configuration");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Left column: team setup, right column: arena params
    float avail_w = ImGui::GetContentRegionAvail().x;
    float left_w = avail_w * 0.5f - 8.0f;
    float right_w = avail_w - left_w - 16.0f;

    ImGui::BeginChild("##LeftPane", ImVec2(left_w, display.y - 70.0f), false);

    // --- Teams ---
    ui::section_header("Teams");

    {
        int teams = num_teams_;
        if (ui::input_int("Number of Teams", &teams, 2, MAX_TEAMS)) {
            num_teams_ = std::clamp(teams, 2, MAX_TEAMS);
        }
    }

    // Competition mode
    ImGui::Text("Competition Mode:");
    ImGui::SameLine();
    bool is_rr = (competition_mode_ == CompetitionMode::RoundRobin);
    bool is_ffa = (competition_mode_ == CompetitionMode::FreeForAll);
    if (ImGui::RadioButton("Round Robin", is_rr)) {
        competition_mode_ = CompetitionMode::RoundRobin;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Free-for-All", is_ffa)) {
        competition_mode_ = CompetitionMode::FreeForAll;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Per-team genome selection
    if (genomes_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
            "No genomes found. Create some in the Hangar first.");
    } else {
        for (int t = 0; t < num_teams_; ++t) {
            const auto& tc = k_team_colors[t % MAX_TEAMS];
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(tc.r, tc.g, tc.b, 1.0f));
            char team_header[32];
            std::snprintf(team_header, sizeof(team_header),
                "Team %d (%s)", t + 1, tc.name);
            ImGui::Text("%s", team_header);
            ImGui::PopStyleColor();

            // Duplicate button: copies this team's selections to a new team slot
            ImGui::SameLine();
            ImGui::PushID(t * 100 + 99);
            if (num_teams_ >= MAX_TEAMS) ImGui::BeginDisabled();
            if (ImGui::SmallButton("+Dup") && num_teams_ < MAX_TEAMS) {
                int dest = num_teams_;
                squad_genome_idx_[dest] = squad_genome_idx_[t];
                squad_variant_idx_[dest] = squad_variant_idx_[t];
                fighter_genome_idx_[dest] = fighter_genome_idx_[t];
                fighter_variant_idx_[dest] = fighter_variant_idx_[t];
                snapshots_loaded_[dest] = false;
                ++num_teams_;
            }
            if (num_teams_ >= MAX_TEAMS) ImGui::EndDisabled();
            ImGui::PopID();

            ImGui::PushID(t);

            // Squad genome selector
            {
                std::vector<const char*> genome_names;
                genome_names.reserve(genomes_.size());
                for (const auto& g : genomes_) genome_names.push_back(g.name.c_str());

                int& gi = squad_genome_idx_[t];
                gi = std::clamp(gi, 0, static_cast<int>(genomes_.size()) - 1);

                ImGui::Text("  Squad Genome:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::Combo("##SqG", &gi, genome_names.data(),
                        static_cast<int>(genome_names.size()))) {
                    squad_variant_idx_[t] = 0;
                    snapshots_loaded_[t] = false;
                }

                // Squad variant selector
                ImGui::Text("  Squad Variant:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200.0f);
                auto& sv = squad_variants_[static_cast<std::size_t>(gi)];
                if (sv.empty()) {
                    ImGui::TextDisabled("(no squad variants)");
                } else {
                    std::vector<const char*> var_names;
                    var_names.reserve(sv.size());
                    for (const auto& v : sv) var_names.push_back(v.name.c_str());
                    int& vi = squad_variant_idx_[t];
                    vi = std::clamp(vi, 0, static_cast<int>(sv.size()) - 1);
                    if (ImGui::Combo("##SqV", &vi, var_names.data(),
                            static_cast<int>(var_names.size()))) {
                        snapshots_loaded_[t] = false;
                    }
                }
            }

            // Fighter genome selector
            {
                std::vector<const char*> genome_names;
                genome_names.reserve(genomes_.size());
                for (const auto& g : genomes_) genome_names.push_back(g.name.c_str());

                int& gi = fighter_genome_idx_[t];
                gi = std::clamp(gi, 0, static_cast<int>(genomes_.size()) - 1);

                ImGui::Text("  Fighter Genome:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::Combo("##FiG", &gi, genome_names.data(),
                        static_cast<int>(genome_names.size()))) {
                    fighter_variant_idx_[t] = 0;
                    snapshots_loaded_[t] = false;
                }

                // Fighter variant selector
                ImGui::Text("  Fighter Variant:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200.0f);
                auto& fv = fighter_variants_[static_cast<std::size_t>(gi)];
                if (fv.empty()) {
                    ImGui::TextDisabled("(no fighter variants)");
                } else {
                    std::vector<const char*> var_names;
                    var_names.reserve(fv.size());
                    for (const auto& v : fv) var_names.push_back(v.name.c_str());
                    int& vi = fighter_variant_idx_[t];
                    vi = std::clamp(vi, 0, static_cast<int>(fv.size()) - 1);
                    if (ImGui::Combo("##FiV", &vi, var_names.data(),
                            static_cast<int>(var_names.size()))) {
                        snapshots_loaded_[t] = false;
                    }
                }
            }

            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##RightPane", ImVec2(right_w, display.y - 70.0f), false);

    // --- Arena ---
    ui::section_header("Arena");

    ui::slider_float("World Width", &arena_config_.world.world_width, 1000.0f, 10000.0f);
    ui::slider_float("World Height", &arena_config_.world.world_height, 1000.0f, 10000.0f);

    {
        int squads = static_cast<int>(arena_config_.world.num_squads);
        if (ui::input_int("Squads per Team", &squads, 1, 4)) {
            arena_config_.world.num_squads = static_cast<std::size_t>(squads);
        }
    }
    {
        int fighters = static_cast<int>(arena_config_.world.fighters_per_squad);
        if (ui::input_int("Fighters per Squad", &fighters, 2, 20)) {
            arena_config_.world.fighters_per_squad = static_cast<std::size_t>(fighters);
        }
    }
    {
        int towers = static_cast<int>(arena_config_.world.tower_count);
        if (ui::input_int("Towers", &towers, 0, 200)) {
            arena_config_.world.tower_count = static_cast<std::size_t>(towers);
        }
    }
    {
        int tokens = static_cast<int>(arena_config_.world.token_count);
        if (ui::input_int("Tokens", &tokens, 0, 200)) {
            arena_config_.world.token_count = static_cast<std::size_t>(tokens);
        }
    }
    {
        int time_seconds = static_cast<int>(arena_config_.time_limit_ticks / 60);
        if (ui::input_int("Time Limit (seconds)", &time_seconds, 10, 300)) {
            arena_config_.time_limit_ticks =
                static_cast<uint32_t>(time_seconds) * 60;
        }
    }

    ImGui::Spacing();

    // --- Bases ---
    ui::section_header("Bases");

    ui::slider_float("Base HP", &arena_config_.world.base_hp, 100.0f, 10000.0f);
    ui::slider_float("Base Radius", &arena_config_.world.base_radius, 20.0f, 300.0f);
    ui::slider_float("Base Bullet Damage",
        &arena_config_.world.base_bullet_damage, 1.0f, 100.0f);

    ImGui::Spacing();

    // --- Physics ---
    ui::section_header("Physics");

    ui::slider_float("Rotation Speed",
        &arena_config_.world.rotation_speed, 0.01f, 0.2f);
    ui::slider_float("Bullet Max Range",
        &arena_config_.world.bullet_max_range, 200.0f, 3000.0f);
    ui::checkbox("Wrap N/S", &arena_config_.world.wrap_ns);
    ui::checkbox("Wrap E/W", &arena_config_.world.wrap_ew);
    ui::checkbox("Friendly Fire", &arena_config_.world.friendly_fire);

    ImGui::Spacing();

    // --- Scoring ---
    ui::section_header("Scoring");

    ui::slider_float("Kill Points", &arena_config_.kill_points, 10.0f, 1000.0f);
    ui::slider_float("Death Points", &arena_config_.death_points, 0.0f, 500.0f);
    ui::slider_float("Base Hit Points",
        &arena_config_.base_hit_points, 0.0f, 500.0f);
    ImGui::TextDisabled("Base Kill Points: %.0f",
        arena_config_.base_kill_points());

    ImGui::Spacing();

    // --- Evolution ---
    ui::section_header("Evolution");

    {
        int pop = static_cast<int>(evo_config_.population_size);
        if (ui::input_int("Population Size", &pop, 4, 200)) {
            evo_config_.population_size = static_cast<std::size_t>(pop);
        }
    }

    ImGui::EndChild();

    // --- Bottom bar ---
    ImGui::Separator();
    ImGui::Spacing();

    // Validate: all teams must have squad and fighter variants selected
    bool can_start = !genomes_.empty() && num_teams_ >= 2;
    if (can_start) {
        for (int t = 0; t < num_teams_; ++t) {
            int sq_gi = squad_genome_idx_[t];
            int sq_vi = squad_variant_idx_[t];
            int fi_gi = fighter_genome_idx_[t];
            int fi_vi = fighter_variant_idx_[t];
            if (sq_gi >= static_cast<int>(squad_variants_.size())) { can_start = false; break; }
            if (squad_variants_[static_cast<std::size_t>(sq_gi)].empty()) { can_start = false; break; }
            if (fi_gi >= static_cast<int>(fighter_variants_.size())) { can_start = false; break; }
            if (fighter_variants_[static_cast<std::size_t>(fi_gi)].empty()) { can_start = false; break; }
            if (sq_vi >= static_cast<int>(squad_variants_[static_cast<std::size_t>(sq_gi)].size())) { can_start = false; break; }
            if (fi_vi >= static_cast<int>(fighter_variants_[static_cast<std::size_t>(fi_gi)].size())) { can_start = false; break; }
        }
    }

    if (!can_start) ImGui::BeginDisabled();

    if (ui::button("Start", ui::ButtonStyle::Primary, 200.0f)) {
        // Load snapshots and build TeamSkirmishConfig
        TeamSkirmishConfig config;
        config.arena = arena_config_;
        config.competition_mode = competition_mode_;

        bool load_ok = true;
        for (int t = 0; t < num_teams_; ++t) {
            int sq_gi = squad_genome_idx_[t];
            int sq_vi = squad_variant_idx_[t];
            int fi_gi = fighter_genome_idx_[t];
            int fi_vi = fighter_variant_idx_[t];

            try {
                auto sq_path = squad_variants_[static_cast<std::size_t>(sq_gi)]
                                              [static_cast<std::size_t>(sq_vi)].path;
                auto fi_path = fighter_variants_[static_cast<std::size_t>(fi_gi)]
                                                [static_cast<std::size_t>(fi_vi)].path;

                TeamSeed seed;
                seed.squad_snapshot = load_snapshot(sq_path);
                seed.fighter_snapshot = load_snapshot(fi_path);
                seed.squad_genome_dir = genomes_[static_cast<std::size_t>(sq_gi)].dir;
                seed.fighter_genome_dir = genomes_[static_cast<std::size_t>(fi_gi)].dir;
                config.team_seeds.push_back(std::move(seed));
            } catch (const std::exception& e) {
                std::cerr << "Failed to load snapshots for team " << t
                          << ": " << e.what() << "\n";
                load_ok = false;
                break;
            }
        }

        if (load_ok) {
            ui.push_screen(std::make_unique<TeamSkirmishScreen>(
                std::move(config), evo_config_));
        }
    }

    if (!can_start) ImGui::EndDisabled();

    ImGui::SameLine();

    if (ui::button("Back", ui::ButtonStyle::Secondary, 120.0f)) {
        ui.pop_screen();
    }

    if (!can_start && !genomes_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
            "Each team needs a squad variant and fighter variant selected.");
    }

    ImGui::End();

    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
    }
}

} // namespace neuroflyer
