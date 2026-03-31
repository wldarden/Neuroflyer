#include <neuroflyer/components/lineage_graph.h>
#include <neuroflyer/genome_manager.h>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <unordered_map>

namespace neuroflyer {

void rebuild_lineage_graph(LineageGraphState& state, const std::string& genome_dir,
                           const std::string& genomes_dir) {
    state.nodes.clear();
    state.selected_node = -1;
    state.loaded_dir = genome_dir;

    std::string lineage_path =
        (std::filesystem::path(genome_dir) / "lineage.json").string();
    if (!std::filesystem::exists(lineage_path)) return;

    std::ifstream f(lineage_path);
    if (!f) return;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(f);
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse lineage: " << e.what() << "\n";
        return;
    }

    if (!j.contains("nodes") || !j["nodes"].is_array() || j["nodes"].empty())
        return;

    // Load genomic lineage for cross-genome display
    std::string current_genome_name =
        std::filesystem::path(genome_dir).filename().string();
    std::vector<GenomicLineageNode> genomic_lineage;
    std::string effective_genomes_dir = genomes_dir;
    if (effective_genomes_dir.empty()) {
        // Derive from genome_dir (parent directory)
        effective_genomes_dir =
            std::filesystem::path(genome_dir).parent_path().string();
    }
    genomic_lineage = load_genomic_lineage(effective_genomes_dir);

    // Build ancestor chain: walk UP from current genome
    std::vector<std::string> ancestors; // ordered: immediate parent first
    {
        std::string cursor = current_genome_name;
        for (int safety = 0; safety < 100; ++safety) {
            std::string src;
            for (const auto& gn : genomic_lineage) {
                if (gn.name == cursor && !gn.source_genome.empty()) {
                    src = gn.source_genome;
                    break;
                }
            }
            if (src.empty()) break;
            ancestors.push_back(src);
            cursor = src;
        }
    }

    // Add collapsed ancestor genome nodes (in reverse order: oldest first)
    std::size_t ancestor_chain_tail = 0; // index of last ancestor node (to link)
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
        LineageGraphState::GraphNode gn;
        gn.name = *it;
        gn.is_ancestor_genome = true;
        gn.is_genome = true;
        std::size_t idx = state.nodes.size();
        state.nodes.push_back(gn);

        if (it != ancestors.rbegin()) {
            state.nodes[ancestor_chain_tail].children.push_back(idx);
        }
        ancestor_chain_tail = idx;
    }

    // Now load the current genome's nodes from lineage.json
    std::size_t first_current_idx = state.nodes.size();
    for (const auto& n : j["nodes"]) {
        LineageGraphState::GraphNode gn;
        gn.name = n.value("name", "");
        gn.file = n.contains("file") && !n["file"].is_null()
                      ? n["file"].get<std::string>()
                      : "";
        gn.generation = n.value("generation", 0);
        gn.is_mrca_stub = n.value("mrca_stub", false);
        gn.topology_summary = n.value("topology_summary", "");
        // Check for child_genome field
        if (n.contains("child_genome") && n["child_genome"].is_string()) {
            gn.child_genome = n["child_genome"].get<std::string>();
        }
        state.nodes.push_back(gn);
    }

    std::unordered_map<std::string, std::size_t> name_to_idx;
    for (std::size_t i = first_current_idx; i < state.nodes.size(); ++i) {
        name_to_idx[state.nodes[i].name] = i;
    }

    std::size_t current_root = first_current_idx; // default
    for (std::size_t i = first_current_idx; i < state.nodes.size(); ++i) {
        std::size_t json_idx = i - first_current_idx;
        const auto& n = j["nodes"][json_idx];
        std::string parent =
            n.contains("parent") && !n["parent"].is_null()
                ? n["parent"].get<std::string>()
                : "";
        if (!parent.empty()) {
            auto it2 = name_to_idx.find(parent);
            if (it2 != name_to_idx.end()) {
                state.nodes[it2->second].children.push_back(i);
            }
        } else {
            current_root = i;
            state.nodes[i].is_genome = true;
        }
    }

    // Attach orphan nodes (parent referenced but not found) to the root
    // so the layout algorithm visits every node.
    {
        std::vector<bool> is_child(state.nodes.size(), false);
        is_child[current_root] = true;
        for (std::size_t i = first_current_idx; i < state.nodes.size(); ++i) {
            for (auto c : state.nodes[i].children) {
                is_child[c] = true;
            }
        }
        for (std::size_t i = first_current_idx; i < state.nodes.size(); ++i) {
            if (!is_child[i]) {
                state.nodes[current_root].children.push_back(i);
            }
        }
    }

    // Link ancestor chain to current genome's root
    if (!ancestors.empty()) {
        state.nodes[ancestor_chain_tail].children.push_back(current_root);
        state.root = 0; // oldest ancestor
    } else {
        state.root = current_root;
    }

    // Add collapsed child genome nodes for variants that have child_genome
    for (std::size_t i = first_current_idx; i < state.nodes.size(); ++i) {
        if (!state.nodes[i].child_genome.empty()) {
            LineageGraphState::GraphNode child_gn;
            child_gn.name = state.nodes[i].child_genome;
            child_gn.is_child_genome = true;
            child_gn.is_genome = true;
            std::size_t idx = state.nodes.size();
            state.nodes.push_back(child_gn);
            state.nodes[i].children.push_back(idx);
        }
    }

    // Layout: tree with root at top, children spread below
    constexpr float NODE_W = 150.0f;
    constexpr float NODE_H = 50.0f;
    constexpr float PAD_X = 20.0f;
    constexpr float PAD_Y = 30.0f;

    std::vector<int> subtree_w(state.nodes.size(), 1);
    std::function<int(std::size_t)> calc_w = [&](std::size_t idx) -> int {
        auto& node = state.nodes[idx];
        if (node.children.empty()) {
            subtree_w[idx] = 1;
            return 1;
        }
        int w = 0;
        for (auto c : node.children) w += calc_w(c);
        subtree_w[idx] = w;
        return w;
    };
    calc_w(state.root);

    std::function<void(std::size_t, float, float, float)> layout =
        [&](std::size_t idx, float left, float y, float width) {
            state.nodes[idx].x = left + width * 0.5f;
            state.nodes[idx].y = y;
            float cx = left;
            for (auto c : state.nodes[idx].children) {
                float cw = width * static_cast<float>(subtree_w[c]) /
                           static_cast<float>(subtree_w[idx]);
                layout(c, cx, y + NODE_H + PAD_Y, cw);
                cx += cw;
            }
        };

    float total =
        static_cast<float>(subtree_w[state.root]) * (NODE_W + PAD_X);
    layout(state.root, PAD_X, PAD_Y, total);
    state.needs_rebuild = false;
}

std::string draw_lineage_graph(LineageGraphState& state,
                               const std::string& genome_dir, float graph_w,
                               float graph_h) {
    if (state.needs_rebuild || state.loaded_dir != genome_dir) {
        rebuild_lineage_graph(state, genome_dir);
    }

    if (state.nodes.empty()) {
        ImGui::Text("No lineage data available.");
        return "";
    }

    std::string action;

    constexpr float NODE_W = 140.0f;
    constexpr float NODE_H = 44.0f;
    constexpr float NODE_R = 8.0f;

    // Colors -- Portal inspired
    constexpr ImU32 COL_GENOME = IM_COL32(230, 140, 30, 255);
    constexpr ImU32 COL_VARIANT = IM_COL32(80, 180, 230, 255);
    constexpr ImU32 COL_SELECTED = IM_COL32(255, 210, 50, 255);
    constexpr ImU32 COL_MRCA = IM_COL32(140, 140, 80, 200);
    constexpr ImU32 COL_EDGE = IM_COL32(120, 120, 140, 180);
    constexpr ImU32 COL_ANCESTOR = IM_COL32(160, 100, 180, 220);
    constexpr ImU32 COL_CHILD_GENOME = IM_COL32(100, 180, 120, 220);

    ImGui::BeginChild("##LineageCanvas", ImVec2(graph_w, graph_h), true,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Draw edges (bezier curves)
    std::function<void(std::size_t)> draw_edges = [&](std::size_t idx) {
        auto& node = state.nodes[idx];
        for (auto c : node.children) {
            auto& child = state.nodes[c];
            float x1 = origin.x + node.x;
            float y1 = origin.y + node.y + NODE_H;
            float x2 = origin.x + child.x;
            float y2 = origin.y + child.y;
            float mid = (y1 + y2) * 0.5f;
            dl->AddBezierCubic(ImVec2(x1, y1), ImVec2(x1, mid),
                               ImVec2(x2, mid), ImVec2(x2, y2), COL_EDGE,
                               2.0f);
            draw_edges(c);
        }
    };
    draw_edges(state.root);

    // Draw nodes
    ImVec2 mouse = ImGui::GetMousePos();
    for (std::size_t i = 0; i < state.nodes.size(); ++i) {
        auto& node = state.nodes[i];
        float nx = origin.x + node.x - NODE_W * 0.5f;
        float ny = origin.y + node.y;
        ImVec2 tl(nx, ny);
        ImVec2 br(nx + NODE_W, ny + NODE_H);

        bool selected = (state.selected_node == static_cast<int>(i));
        bool hovered = (mouse.x >= tl.x && mouse.x <= br.x &&
                        mouse.y >= tl.y && mouse.y <= br.y);

        ImU32 bg = 0;
        if (selected)
            bg = COL_SELECTED;
        else if (node.is_ancestor_genome)
            bg = COL_ANCESTOR;
        else if (node.is_child_genome)
            bg = COL_CHILD_GENOME;
        else if (node.is_mrca_stub)
            bg = COL_MRCA;
        else if (node.is_genome)
            bg = COL_GENOME;
        else
            bg = COL_VARIANT;

        if (hovered && !selected) {
            // Brighten on hover
            int rr = static_cast<int>((bg >> 0) & 0xFF) + 25;
            int gg = static_cast<int>((bg >> 8) & 0xFF) + 25;
            int bb = static_cast<int>((bg >> 16) & 0xFF) + 25;
            bg = IM_COL32(rr > 255 ? 255 : rr, gg > 255 ? 255 : gg,
                          bb > 255 ? 255 : bb, 255);
        }

        dl->AddRectFilled(tl, br, bg, NODE_R);
        dl->AddRect(tl, br,
                    selected ? IM_COL32(255, 255, 255, 255)
                             : IM_COL32(180, 180, 180, 100),
                    NODE_R, 0, selected ? 2.5f : 1.0f);

        // Text -- dark on bright backgrounds (genome, selected), light on
        // dark (variant)
        ImU32 txt = (selected || node.is_genome)
                        ? IM_COL32(25, 25, 25, 255)
                        : IM_COL32(240, 240, 240, 255);
        // Name
        ImVec2 name_sz = ImGui::CalcTextSize(node.name.c_str());
        float name_x = nx + (NODE_W - name_sz.x) * 0.5f;
        dl->AddText(ImVec2(name_x, ny + 5.0f), txt, node.name.c_str());

        // Generation
        char gen_buf[32];
        std::snprintf(gen_buf, sizeof(gen_buf), "gen %d", node.generation);
        ImVec2 gen_sz = ImGui::CalcTextSize(gen_buf);
        float gen_x = nx + (NODE_W - gen_sz.x) * 0.5f;
        ImU32 gen_col = selected ? IM_COL32(60, 60, 60, 255)
                                 : IM_COL32(160, 160, 170, 255);
        dl->AddText(ImVec2(gen_x, ny + NODE_H - 18.0f), gen_col, gen_buf);

        // Click
        if (hovered && ImGui::IsMouseClicked(0)) {
            state.selected_node = static_cast<int>(i);
            action = "select:" + node.name;
        }
    }

    // Reserve space for scrolling
    float max_x = 0;
    float max_y = 0;
    for (const auto& n : state.nodes) {
        max_x = std::max(max_x, n.x + NODE_W);
        max_y = std::max(max_y, n.y + NODE_H + 20.0f);
    }
    ImGui::Dummy(ImVec2(max_x, max_y));

    ImGui::EndChild();
    return action;
}

} // namespace neuroflyer
