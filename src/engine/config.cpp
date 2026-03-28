#include <neuroflyer/config.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace neuroflyer {

GameConfig GameConfig::load(const std::string& path) {
    GameConfig cfg;
    std::ifstream file(path);
    if (!file.is_open()) return cfg;

    try {
        nlohmann::json j = nlohmann::json::parse(file);

        // Scoring
        if (j.contains("pts_per_distance"))  cfg.pts_per_distance = j["pts_per_distance"];
        if (j.contains("pts_per_tower"))     cfg.pts_per_tower = j["pts_per_tower"];
        if (j.contains("pts_per_token"))     cfg.pts_per_token = j["pts_per_token"];
        if (j.contains("pts_per_bullet"))    cfg.pts_per_bullet = j["pts_per_bullet"];

        // Population
        if (j.contains("population_size"))   cfg.population_size = j["population_size"].get<std::size_t>();
        if (j.contains("elitism_count"))     cfg.elitism_count = j["elitism_count"].get<std::size_t>();

        // Ship
        if (j.contains("ship_speed"))        cfg.ship_speed = j["ship_speed"];
        if (j.contains("fire_cooldown"))     cfg.fire_cooldown = j["fire_cooldown"];
        if (j.contains("ship_type"))         cfg.ship_type = j["ship_type"];

        // World
        if (j.contains("scroll_speed"))         cfg.scroll_speed = j["scroll_speed"];
        if (j.contains("starting_difficulty"))  cfg.starting_difficulty = j["starting_difficulty"];
        if (j.contains("active_genome"))       cfg.active_genome = j["active_genome"].get<std::string>();
        if (j.contains("autosave_interval"))   cfg.autosave_interval = j["autosave_interval"].get<int>();
        if (j.contains("mrca_memory_limit_mb")) cfg.mrca_memory_limit_mb = j["mrca_memory_limit_mb"].get<int>();
        if (j.contains("mrca_prune_interval")) cfg.mrca_prune_interval = j["mrca_prune_interval"].get<int>();

        // Graphics
        if (j.contains("window_width"))      cfg.window_width = j["window_width"].get<int>();
        if (j.contains("window_height"))     cfg.window_height = j["window_height"].get<int>();
        if (j.contains("fullscreen"))        cfg.fullscreen = j["fullscreen"].get<bool>();
        if (j.contains("vsync"))             cfg.vsync = j["vsync"].get<bool>();
        if (j.contains("max_fps"))           cfg.max_fps = j["max_fps"].get<int>();

        // Position scoring multipliers
        if (j.contains("x_center_mult"))     cfg.x_center_mult = j["x_center_mult"];
        if (j.contains("x_edge_mult"))       cfg.x_edge_mult = j["x_edge_mult"];
        if (j.contains("y_bottom_mult"))     cfg.y_bottom_mult = j["y_bottom_mult"];
        if (j.contains("y_center_mult"))     cfg.y_center_mult = j["y_center_mult"];
        if (j.contains("y_top_mult"))        cfg.y_top_mult = j["y_top_mult"];
    } catch (const std::exception& e) {
        std::cerr << "[neuroflyer] Failed to parse settings: " << e.what() << "\n";
    }
    return cfg;
}

void GameConfig::save(const std::string& path) const {
    nlohmann::json j;

    // Scoring
    j["pts_per_distance"]  = pts_per_distance;
    j["pts_per_tower"]     = pts_per_tower;
    j["pts_per_token"]     = pts_per_token;
    j["pts_per_bullet"]    = pts_per_bullet;

    // Population
    j["population_size"]   = population_size;
    j["elitism_count"]     = elitism_count;

    // Ship
    j["ship_speed"]        = ship_speed;
    j["fire_cooldown"]     = fire_cooldown;
    j["ship_type"]         = ship_type;

    // World
    j["scroll_speed"]         = scroll_speed;
    j["starting_difficulty"]  = starting_difficulty;
    j["active_genome"]       = active_genome;
    j["autosave_interval"]   = autosave_interval;
    j["mrca_memory_limit_mb"] = mrca_memory_limit_mb;
    j["mrca_prune_interval"] = mrca_prune_interval;

    // Graphics
    j["window_width"]      = window_width;
    j["window_height"]     = window_height;
    j["fullscreen"]        = fullscreen;
    j["vsync"]             = vsync;
    j["max_fps"]           = max_fps;

    // Position scoring multipliers
    j["x_center_mult"]     = x_center_mult;
    j["x_edge_mult"]       = x_edge_mult;
    j["y_bottom_mult"]     = y_bottom_mult;
    j["y_center_mult"]     = y_center_mult;
    j["y_top_mult"]        = y_top_mult;

    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path);
    out << j.dump(2) << "\n";
}

} // namespace neuroflyer
