#pragma once
#include <neuroflyer/ship_design.h>
#include <neuralnet/network.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace neuroflyer {

struct MrcaEntry {
    enum class Level { Full, TopologyOnly };

    Level level = Level::Full;
    uint32_t generation = 0;
    uint32_t individual_id = 0;

    // Always present (both levels):
    neuralnet::NetworkTopology topology;
    ShipDesign ship_design;

    // Present only at Level::Full (cleared when downgraded):
    std::vector<float> weights;

    /// Approximate memory usage in bytes.
    [[nodiscard]] std::size_t memory_bytes() const;
};

/// Tracks elite lineage during training for MRCA computation.
class MrcaTracker {
public:
    MrcaTracker(std::size_t elite_count, std::size_t memory_limit_bytes,
                std::size_t prune_interval);

    /// Record one generation. Each vector is indexed by elite slot (size = elite_count).
    /// Only stores a new entry when an elite slot's individual_id changes.
    void record_generation(uint32_t generation,
                           const std::vector<uint32_t>& elite_ids,
                           const std::vector<neuralnet::NetworkTopology>& topologies,
                           const std::vector<ShipDesign>& ship_designs,
                           const std::vector<std::vector<float>>& weights);

    /// Prune entries that appear in <=1 elite chain.
    /// Called automatically every prune_interval generations, or manually at save time.
    void prune();

    /// Result node for MRCA tree computation.
    struct MrcaNode {
        MrcaEntry entry;
        std::size_t parent_index;  // index into result vector, SIZE_MAX = root
        std::vector<std::size_t> children;  // indices into result vector
    };

    /// Compute MRCA tree for a set of elite slot indices.
    /// Returns a tree of MrcaNodes. The leaf nodes correspond to the given elite slots.
    /// Internal nodes are MRCA branch points.
    [[nodiscard]] std::vector<MrcaNode> compute_mrca_tree(
        const std::vector<std::size_t>& elite_indices) const;

    /// Current memory usage of all stored entries.
    [[nodiscard]] std::size_t memory_usage_bytes() const;

    /// Number of stored entries (for testing).
    [[nodiscard]] std::size_t entry_count() const;

    /// Number of full-level entries (for testing).
    [[nodiscard]] std::size_t full_entry_count() const;

    /// Access a chain's ancestor IDs (for testing).
    [[nodiscard]] const std::vector<uint32_t>& chain_ancestor_ids(
        std::size_t elite_slot) const;

private:
    void enforce_memory_budget();
    void degrade_least_isolated();

    struct EliteChain {
        std::vector<uint32_t> ancestor_ids;  // one entry_id per generation
    };

    std::size_t elite_count_;
    std::size_t memory_limit_bytes_;
    std::size_t prune_interval_;
    uint32_t generations_since_prune_ = 0;

    // Entry store: unique ID -> MrcaEntry
    std::unordered_map<uint32_t, MrcaEntry> entries_;
    uint32_t next_entry_id_ = 0;

    // Per-elite lineage chain
    std::vector<EliteChain> chains_;

    // Last known elite IDs (for dedup -- only store when changed)
    std::vector<uint32_t> last_elite_ids_;

    std::size_t current_memory_ = 0;
};

} // namespace neuroflyer
