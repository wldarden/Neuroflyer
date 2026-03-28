#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace neuroflyer {

/// Persistent state for the lineage graph viewer (one instance per screen).
struct LineageGraphState {
    int selected_node = -1;
    bool needs_rebuild = true;
    std::string loaded_dir;

    struct GraphNode {
        std::string name;
        std::string file;
        int generation = 0;
        bool is_genome = false;
        bool is_mrca_stub = false;
        bool is_ancestor_genome = false;   // collapsed ancestor genome node
        bool is_child_genome = false;      // collapsed child genome node
        std::string child_genome;          // child genome name (for variants with promoted children)
        std::string topology_summary;
        float x = 0;
        float y = 0;
        std::vector<std::size_t> children;
    };
    std::vector<GraphNode> nodes;
    std::size_t root = 0;
};

/// Rebuild the lineage graph from a genome directory's lineage.json.
/// Also loads cross-genome lineage to show ancestor/descendant genomes.
/// genomes_dir is used to find genomic_lineage.json (pass "" to skip cross-genome).
void rebuild_lineage_graph(LineageGraphState& state, const std::string& genome_dir,
                           const std::string& genomes_dir = "");

/// Draw the lineage graph. Returns "" or "select:<NodeName>" on click.
[[nodiscard]] std::string draw_lineage_graph(LineageGraphState& state,
                                             const std::string& genome_dir,
                                             float graph_w, float graph_h);

} // namespace neuroflyer
