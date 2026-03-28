#include <neuroflyer/mrca_tracker.h>

#include <algorithm>
#include <cassert>
#include <functional>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace neuroflyer {

// ---------------------------------------------------------------------------
// MrcaEntry
// ---------------------------------------------------------------------------

std::size_t MrcaEntry::memory_bytes() const {
    std::size_t bytes = sizeof(MrcaEntry);
    bytes += weights.capacity() * sizeof(float);
    bytes += topology.layers.capacity() * sizeof(neuralnet::LayerDef);
    bytes += ship_design.sensors.capacity() * sizeof(SensorDef);
    return bytes;
}

// ---------------------------------------------------------------------------
// MrcaTracker construction
// ---------------------------------------------------------------------------

MrcaTracker::MrcaTracker(std::size_t elite_count,
                         std::size_t memory_limit_bytes,
                         std::size_t prune_interval)
    : elite_count_{elite_count}
    , memory_limit_bytes_{memory_limit_bytes}
    , prune_interval_{prune_interval}
    , chains_(elite_count)
    , last_elite_ids_(elite_count, std::numeric_limits<uint32_t>::max()) {}

// ---------------------------------------------------------------------------
// record_generation
// ---------------------------------------------------------------------------

void MrcaTracker::record_generation(
    uint32_t generation,
    const std::vector<uint32_t>& elite_ids,
    const std::vector<neuralnet::NetworkTopology>& topologies,
    const std::vector<ShipDesign>& ship_designs,
    const std::vector<std::vector<float>>& weights) {

    // Clamp to the smaller of configured elite_count_ or what was actually passed.
    // This handles small populations where fewer elites than expected are available.
    auto n = std::min({elite_count_,
                       elite_ids.size(),
                       topologies.size(),
                       ship_designs.size(),
                       weights.size()});

    for (std::size_t i = 0; i < n; ++i) {
        if (elite_ids[i] != last_elite_ids_[i]) {
            // New occupant — create entry
            uint32_t eid = next_entry_id_++;
            MrcaEntry entry;
            entry.level = MrcaEntry::Level::Full;
            entry.generation = generation;
            entry.individual_id = elite_ids[i];
            entry.topology = topologies[i];
            entry.ship_design = ship_designs[i];
            entry.weights = weights[i];

            current_memory_ += entry.memory_bytes();
            entries_.emplace(eid, std::move(entry));
            chains_[i].ancestor_ids.push_back(eid);
            last_elite_ids_[i] = elite_ids[i];
        } else {
            // Same occupant — reuse last entry ID
            assert(!chains_[i].ancestor_ids.empty());
            chains_[i].ancestor_ids.push_back(chains_[i].ancestor_ids.back());
        }
    }

    ++generations_since_prune_;
    if (generations_since_prune_ >= prune_interval_) {
        prune();
    }
    enforce_memory_budget();
}

// ---------------------------------------------------------------------------
// prune
// ---------------------------------------------------------------------------

void MrcaTracker::prune() {
    // Count how many distinct elite chains reference each entry.
    std::unordered_map<uint32_t, std::size_t> ref_chains;
    for (const auto& chain : chains_) {
        // Collect unique entry IDs in this chain.
        std::unordered_set<uint32_t> seen;
        for (uint32_t eid : chain.ancestor_ids) {
            seen.insert(eid);
        }
        for (uint32_t eid : seen) {
            ref_chains[eid]++;
        }
    }

    // Degrade entries referenced by <=1 chain (not shared).
    for (auto& [eid, entry] : entries_) {
        if (ref_chains[eid] <= 1 && entry.level == MrcaEntry::Level::Full) {
            std::size_t old_bytes = entry.memory_bytes();
            entry.weights.clear();
            entry.weights.shrink_to_fit();
            entry.level = MrcaEntry::Level::TopologyOnly;
            std::size_t new_bytes = entry.memory_bytes();
            if (old_bytes > new_bytes) {
                current_memory_ -= (old_bytes - new_bytes);
            }
        }
    }

    generations_since_prune_ = 0;
}

// ---------------------------------------------------------------------------
// enforce_memory_budget / degrade_least_isolated
// ---------------------------------------------------------------------------

void MrcaTracker::enforce_memory_budget() {
    while (current_memory_ > memory_limit_bytes_) {
        // Check if there are any Full entries left to degrade.
        bool found = false;
        for (const auto& [_, entry] : entries_) {
            if (entry.level == MrcaEntry::Level::Full) {
                found = true;
                break;
            }
        }
        if (!found) {
            break;  // Nothing left to degrade.
        }
        degrade_least_isolated();
    }
}

void MrcaTracker::degrade_least_isolated() {
    // Collect all Full-level entries with their generations and IDs.
    struct Candidate {
        uint32_t entry_id;
        uint32_t generation;
    };
    std::vector<Candidate> candidates;
    for (const auto& [eid, entry] : entries_) {
        if (entry.level == MrcaEntry::Level::Full) {
            candidates.push_back({eid, entry.generation});
        }
    }
    if (candidates.empty()) {
        return;
    }

    // Sort by generation.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.generation < b.generation;
              });

    // Compute isolation scores and find minimum.
    uint32_t worst_id = candidates[0].entry_id;
    uint32_t worst_score = std::numeric_limits<uint32_t>::max();

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        uint32_t gap_prev = std::numeric_limits<uint32_t>::max();
        uint32_t gap_next = std::numeric_limits<uint32_t>::max();

        if (i > 0) {
            gap_prev = candidates[i].generation - candidates[i - 1].generation;
        }
        if (i + 1 < candidates.size()) {
            gap_next = candidates[i + 1].generation - candidates[i].generation;
        }

        uint32_t isolation = std::min(gap_prev, gap_next);
        if (isolation < worst_score) {
            worst_score = isolation;
            worst_id = candidates[i].entry_id;
        }
    }

    // Degrade the least isolated entry.
    auto it = entries_.find(worst_id);
    assert(it != entries_.end());
    auto& entry = it->second;
    std::size_t old_bytes = entry.memory_bytes();
    entry.weights.clear();
    entry.weights.shrink_to_fit();
    entry.level = MrcaEntry::Level::TopologyOnly;
    std::size_t new_bytes = entry.memory_bytes();
    if (old_bytes > new_bytes) {
        current_memory_ -= (old_bytes - new_bytes);
    }
}

// ---------------------------------------------------------------------------
// compute_mrca_tree
// ---------------------------------------------------------------------------

std::vector<MrcaTracker::MrcaNode> MrcaTracker::compute_mrca_tree(
    const std::vector<std::size_t>& elite_indices) const {

    if (elite_indices.empty()) {
        return {};
    }

    std::vector<MrcaNode> result;

    // Helper: find the latest generation index where two chains share the
    // same entry ID (walking forward from oldest).
    // Returns SIZE_MAX if no common entry ID exists.
    auto find_mrca_gen = [&](std::size_t slot_a, std::size_t slot_b) -> std::size_t {
        const auto& chain_a = chains_[slot_a].ancestor_ids;
        const auto& chain_b = chains_[slot_b].ancestor_ids;
        std::size_t min_len = std::min(chain_a.size(), chain_b.size());
        std::size_t last_common = SIZE_MAX;
        for (std::size_t g = 0; g < min_len; ++g) {
            if (chain_a[g] == chain_b[g]) {
                last_common = g;
            } else {
                break;
            }
        }
        return last_common;
    };

    // Helper to create a leaf node for an elite slot.
    auto make_leaf = [&](std::size_t slot) -> std::size_t {
        MrcaNode node;
        uint32_t eid = chains_[slot].ancestor_ids.back();
        auto it = entries_.find(eid);
        assert(it != entries_.end());
        node.entry = it->second;
        node.parent_index = SIZE_MAX;
        std::size_t idx = result.size();
        result.push_back(std::move(node));
        return idx;
    };

    // Recursive split: build a subtree for a set of elite slot indices.
    // Uses a std::function to allow recursion.
    std::function<std::size_t(const std::vector<std::size_t>&)> build;
    build = [&](const std::vector<std::size_t>& slots) -> std::size_t {
        if (slots.size() == 1) {
            return make_leaf(slots[0]);
        }

        // Find the shallowest (earliest) pairwise MRCA among all pairs.
        std::size_t shallowest_gen = SIZE_MAX;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            for (std::size_t j = i + 1; j < slots.size(); ++j) {
                std::size_t g = find_mrca_gen(slots[i], slots[j]);
                if (g != SIZE_MAX && (shallowest_gen == SIZE_MAX || g < shallowest_gen)) {
                    shallowest_gen = g;
                }
            }
        }

        if (shallowest_gen == SIZE_MAX) {
            // No common ancestor among any pair. Create a synthetic root
            // using the first slot's earliest entry, and make all slots
            // direct children.
            MrcaNode root_node;
            uint32_t eid = chains_[slots[0]].ancestor_ids.front();
            auto it = entries_.find(eid);
            assert(it != entries_.end());
            root_node.entry = it->second;
            root_node.parent_index = SIZE_MAX;
            std::size_t root_idx = result.size();
            result.push_back(std::move(root_node));

            for (std::size_t slot : slots) {
                std::size_t leaf_idx = make_leaf(slot);
                result[leaf_idx].parent_index = root_idx;
                result[root_idx].children.push_back(leaf_idx);
            }
            return root_idx;
        }

        // The MRCA node uses the entry at shallowest_gen from one of the
        // chains that participates in this MRCA (they agree at that index).
        // Find a pair that achieves shallowest_gen.
        uint32_t mrca_eid = chains_[slots[0]].ancestor_ids[shallowest_gen];
        auto it = entries_.find(mrca_eid);
        assert(it != entries_.end());

        MrcaNode mrca_node;
        mrca_node.entry = it->second;
        mrca_node.parent_index = SIZE_MAX;
        std::size_t mrca_idx = result.size();
        result.push_back(std::move(mrca_node));

        // Group the slots by which ones share a more recent common ancestor.
        // Two slots are in the same group if their pairwise MRCA is strictly
        // more recent (higher gen index) than shallowest_gen.
        std::vector<bool> assigned(slots.size(), false);
        std::vector<std::vector<std::size_t>> groups;

        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (assigned[i]) continue;
            std::vector<std::size_t> group = {slots[i]};
            assigned[i] = true;
            for (std::size_t j = i + 1; j < slots.size(); ++j) {
                if (assigned[j]) continue;
                std::size_t g = find_mrca_gen(slots[i], slots[j]);
                if (g != SIZE_MAX && g > shallowest_gen) {
                    group.push_back(slots[j]);
                    assigned[j] = true;
                }
            }
            groups.push_back(std::move(group));
        }

        // Recurse on each group.
        for (auto& group : groups) {
            std::size_t child_idx = build(group);
            result[child_idx].parent_index = mrca_idx;
            result[mrca_idx].children.push_back(child_idx);
        }

        return mrca_idx;
    };

    build(elite_indices);
    return result;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::size_t MrcaTracker::memory_usage_bytes() const {
    return current_memory_;
}

std::size_t MrcaTracker::entry_count() const {
    return entries_.size();
}

std::size_t MrcaTracker::full_entry_count() const {
    std::size_t count = 0;
    for (const auto& [_, entry] : entries_) {
        if (entry.level == MrcaEntry::Level::Full) {
            ++count;
        }
    }
    return count;
}

const std::vector<uint32_t>& MrcaTracker::chain_ancestor_ids(
    std::size_t elite_slot) const {
    assert(elite_slot < chains_.size());
    return chains_[elite_slot].ancestor_ids;
}

} // namespace neuroflyer
