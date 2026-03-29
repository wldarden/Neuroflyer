#include <neuroflyer/mrca_tracker.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace nf = neuroflyer;

namespace {

/// Build a minimal topology for testing.
[[nodiscard]] neuralnet::NetworkTopology make_topology() {
    neuralnet::NetworkTopology topo;
    topo.input_size = 3;
    topo.layers = {{std::size_t{2}, neuralnet::Activation::Tanh, {}}};
    return topo;
}

/// Build a minimal ShipDesign for testing.
[[nodiscard]] nf::ShipDesign make_design() {
    nf::ShipDesign design;
    design.memory_slots = 0;
    return design;
}

/// Build a weight vector. The default size is large enough that degradation
/// makes a meaningful difference relative to the base struct overhead.
[[nodiscard]] std::vector<float> make_weights(float val = 0.1f,
                                               std::size_t count = 256) {
    return std::vector<float>(count, val);
}

/// Record a generation where all elites have the given IDs.
void record(nf::MrcaTracker& tracker, uint32_t gen,
            const std::vector<uint32_t>& ids) {
    std::size_t n = ids.size();
    std::vector<neuralnet::NetworkTopology> topos(n, make_topology());
    std::vector<nf::ShipDesign> designs(n, make_design());
    std::vector<std::vector<float>> weights(n, make_weights());
    tracker.record_generation(gen, ids, topos, designs, weights);
}

} // namespace

// ---------------------------------------------------------------------------
// BasicLineageTracking
// ---------------------------------------------------------------------------

TEST(MrcaTracker, BasicLineageTracking) {
    // E=3, large memory limit, no auto-prune.
    nf::MrcaTracker tracker(3, 1024 * 1024, /*prune_interval=*/100);

    // Record 5 generations with stable IDs.
    for (uint32_t g = 0; g < 5; ++g) {
        record(tracker, g, {10, 20, 30});
    }

    // Only 3 entries (one per elite, deduplicated).
    EXPECT_EQ(tracker.entry_count(), 3u);

    // Each chain has 5 entries (one per generation, but same entry ID repeated).
    EXPECT_EQ(tracker.chain_ancestor_ids(0).size(), 5u);
    EXPECT_EQ(tracker.chain_ancestor_ids(1).size(), 5u);
    EXPECT_EQ(tracker.chain_ancestor_ids(2).size(), 5u);
}

// ---------------------------------------------------------------------------
// Dedup_EliteSurvival
// ---------------------------------------------------------------------------

TEST(MrcaTracker, Dedup_EliteSurvival) {
    nf::MrcaTracker tracker(2, 1024 * 1024, 100);

    // 3 gens with stable IDs.
    for (uint32_t g = 0; g < 3; ++g) {
        record(tracker, g, {1, 2});
    }

    EXPECT_EQ(tracker.entry_count(), 2u);

    // Gen 3: elite 0 changes to ID=3.
    record(tracker, 3, {3, 2});

    EXPECT_EQ(tracker.entry_count(), 3u);
}

// ---------------------------------------------------------------------------
// Prune_DropsUnreferencedEntries
// ---------------------------------------------------------------------------

TEST(MrcaTracker, Prune_DropsUnreferencedEntries) {
    // E=2, prune_interval=3.
    nf::MrcaTracker tracker(2, 1024 * 1024, /*prune_interval=*/3);

    // Gen 0: elite 0 has ID=1, elite 1 has ID=2 (both unique to one chain).
    record(tracker, 0, {1, 2});
    // Gen 1: elite 0 has ID=3, elite 1 has ID=4.
    record(tracker, 1, {3, 4});
    // Gen 2: both converge to ID=5. This triggers auto-prune (3 gens recorded).
    record(tracker, 2, {5, 5});

    // After prune: entries unique to one chain should be degraded.
    // Entry for ID=1 is only in chain 0 -> TopologyOnly.
    // Entry for ID=2 is only in chain 1 -> TopologyOnly.
    // Entry for ID=3 is only in chain 0 -> TopologyOnly.
    // Entry for ID=4 is only in chain 1 -> TopologyOnly.
    // Entry for ID=5 is shared by both chains (same entry_id reused
    // because both slots have the same individual hash) -> stays Full.
    EXPECT_EQ(tracker.entry_count(), 5u);
    EXPECT_EQ(tracker.full_entry_count(), 1u);
}

// ---------------------------------------------------------------------------
// MrcaComputation_SimpleCase
// ---------------------------------------------------------------------------

TEST(MrcaTracker, MrcaComputation_SimpleCase) {
    nf::MrcaTracker tracker(2, 1024 * 1024, 100);

    // Gen 0-4: both elites share individual ID=1. Same hash → shared entry.
    for (uint32_t g = 0; g < 5; ++g) {
        record(tracker, g, {1, 1});
    }
    // Gen 5: elite 0 becomes ID=2, elite 1 becomes ID=3.
    record(tracker, 5, {2, 3});

    // Only 1 shared entry for ID=1, plus 2 entries for the diverged IDs.
    EXPECT_EQ(tracker.entry_count(), 3u);

    auto tree = tracker.compute_mrca_tree({0, 1});
    ASSERT_FALSE(tree.empty());

    // Tree: MRCA root (shared ancestor) + 2 leaves = 3 nodes.
    EXPECT_EQ(tree.size(), 3u);

    std::size_t root_idx = SIZE_MAX;
    for (std::size_t i = 0; i < tree.size(); ++i) {
        if (tree[i].parent_index == SIZE_MAX) {
            root_idx = i;
            break;
        }
    }
    ASSERT_NE(root_idx, SIZE_MAX);
    EXPECT_EQ(tree[root_idx].children.size(), 2u);

    // The MRCA should be the shared ancestor (individual_id=1).
    EXPECT_EQ(tree[root_idx].entry.individual_id, 1u);
}

// ---------------------------------------------------------------------------
// MrcaComputation_SharedAncestor
// ---------------------------------------------------------------------------

TEST(MrcaTracker, MrcaComputation_NoSharedAncestor) {
    nf::MrcaTracker tracker(2, 1024 * 1024, 100);

    // Elites always have different IDs — no shared ancestor possible.
    record(tracker, 0, {1, 2});
    record(tracker, 1, {1, 2});
    record(tracker, 2, {1, 2});
    record(tracker, 3, {1, 3});

    auto tree = tracker.compute_mrca_tree({0, 1});
    ASSERT_FALSE(tree.empty());

    // No shared entry IDs → synthetic root + 2 flat children.
    std::size_t root_count = 0;
    for (const auto& node : tree) {
        if (node.parent_index == SIZE_MAX) ++root_count;
    }
    EXPECT_EQ(root_count, 1u);
    EXPECT_EQ(tree.size(), 3u);
}

// ---------------------------------------------------------------------------
// MrcaComputation_ThreeWay
// ---------------------------------------------------------------------------

TEST(MrcaTracker, MrcaComputation_ThreeWay) {
    nf::MrcaTracker tracker(3, 1024 * 1024, 100);

    // Gen 0-1: all three elites share individual ID=1 (shared entry).
    record(tracker, 0, {1, 1, 1});
    record(tracker, 1, {1, 1, 1});
    // Gen 2: elite 2 diverges to ID=4.
    record(tracker, 2, {1, 1, 4});
    record(tracker, 3, {1, 1, 4});
    record(tracker, 4, {1, 1, 4});
    // Gen 5: elite 0 and 1 diverge from each other.
    record(tracker, 5, {2, 3, 4});
    record(tracker, 6, {2, 3, 4});

    auto tree = tracker.compute_mrca_tree({0, 1, 2});
    ASSERT_FALSE(tree.empty());

    std::size_t root_idx = SIZE_MAX;
    for (std::size_t i = 0; i < tree.size(); ++i) {
        if (tree[i].parent_index == SIZE_MAX) {
            root_idx = i;
            break;
        }
    }
    ASSERT_NE(root_idx, SIZE_MAX);

    // Tree: root MRCA (shared ancestor at gen 0-1)
    //       ├── internal MRCA (slots 0+1 shared until gen 4)
    //       │   ├── leaf slot 0 (ID=2)
    //       │   └── leaf slot 1 (ID=3)
    //       └── leaf slot 2 (ID=4)
    // = 5 nodes: root + internal + 3 leaves.
    EXPECT_EQ(tree.size(), 5u);
    EXPECT_EQ(tree[root_idx].children.size(), 2u);

    // Root MRCA should be the shared ancestor (individual_id=1).
    EXPECT_EQ(tree[root_idx].entry.individual_id, 1u);

    // Verify single root.
    std::size_t root_count = 0;
    for (const auto& node : tree) {
        if (node.parent_index == SIZE_MAX) ++root_count;
    }
    EXPECT_EQ(root_count, 1u);
}

// ---------------------------------------------------------------------------
// MemoryBudget_DegradeLeastIsolated
// ---------------------------------------------------------------------------

TEST(MrcaTracker, MemoryBudget_DegradeLeastIsolated) {
    // First measure how much memory a single entry takes.
    {
        nf::MrcaTracker probe(1, 1024 * 1024, 100);
        record(probe, 0, {1});
        ASSERT_GT(probe.memory_usage_bytes(), 0u);
    }

    // Use a budget that fits roughly 3 entries but not 10.
    // A single entry with our test topology (3 inputs, 1 layer of 2, 8 weights)
    // is sizeof(MrcaEntry) + capacity overheads. Measure it:
    nf::MrcaTracker probe(1, 1024 * 1024, 100);
    record(probe, 0, {1});
    std::size_t one_entry = probe.memory_usage_bytes();

    // Budget that allows ~3 Full entries.
    std::size_t budget = one_entry * 3 + one_entry / 2;
    nf::MrcaTracker tracker(2, budget, /*prune_interval=*/100);

    // Record several generations with changing elites (10 new entries).
    record(tracker, 0, {1, 2});
    record(tracker, 1, {3, 4});
    record(tracker, 2, {5, 6});
    record(tracker, 3, {7, 8});
    record(tracker, 4, {9, 10});

    // Some entries should have been degraded.
    EXPECT_LT(tracker.full_entry_count(), tracker.entry_count());

    // Memory should be at or below budget.
    EXPECT_LE(tracker.memory_usage_bytes(), budget);
}

// ---------------------------------------------------------------------------
// MemoryBudget_IsolationScoreOrder
// ---------------------------------------------------------------------------

TEST(MrcaTracker, MemoryBudget_IsolationScoreOrder) {
    // First measure per-entry memory.
    nf::MrcaTracker probe(1, 1024 * 1024, 100);
    record(probe, 0, {100});
    std::size_t one_entry = probe.memory_usage_bytes();

    // Budget that allows ~3 Full entries (out of 5 total).
    std::size_t budget = one_entry * 3 + one_entry / 2;
    nf::MrcaTracker tracker(1, budget, /*prune_interval=*/100);

    // Entries at gens: 0, 1, 2, 10, 20.
    // Isolation scores: gen 0 -> min(INF, 1)=1, gen 1 -> min(1,1)=1,
    //   gen 2 -> min(1,8)=1, gen 10 -> min(8,10)=8, gen 20 -> min(10,INF)=10.
    // Least isolated: gen 0, 1, 2 (score=1) should be degraded first.
    record(tracker, 0, {100});
    record(tracker, 1, {101});
    record(tracker, 2, {102});
    record(tracker, 10, {103});
    record(tracker, 20, {104});

    EXPECT_EQ(tracker.entry_count(), 5u);
    EXPECT_GT(tracker.full_entry_count(), 0u);
    EXPECT_LT(tracker.full_entry_count(), 5u);
    EXPECT_LE(tracker.memory_usage_bytes(), budget);
}

// ---------------------------------------------------------------------------
// EmptyTracker
// ---------------------------------------------------------------------------

TEST(MrcaTracker, EmptyTracker) {
    nf::MrcaTracker tracker(3, 1024 * 1024, 100);
    EXPECT_EQ(tracker.entry_count(), 0u);
    EXPECT_EQ(tracker.full_entry_count(), 0u);
    EXPECT_EQ(tracker.memory_usage_bytes(), 0u);

    auto tree = tracker.compute_mrca_tree({});
    EXPECT_TRUE(tree.empty());
}

// ---------------------------------------------------------------------------
// SingleElite_MrcaTree
// ---------------------------------------------------------------------------

TEST(MrcaTracker, SingleElite_MrcaTree) {
    nf::MrcaTracker tracker(2, 1024 * 1024, 100);
    record(tracker, 0, {1, 2});
    record(tracker, 1, {1, 2});

    // MRCA tree with a single elite slot -> just a leaf.
    auto tree = tracker.compute_mrca_tree({0});
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0].parent_index, SIZE_MAX);
    EXPECT_TRUE(tree[0].children.empty());
}

// ---------------------------------------------------------------------------
// MrcaComputation_WalkthroughScenario
// ---------------------------------------------------------------------------
// Mirrors the walkthrough: 3 elite slots, all start from the same clone,
// then mutants progressively displace elites over 5 generations.
//
//   Gen 1: all 3 slots = 0xAA (Alpha clones, shared entry)
//   Gen 2: slot 0 = 0xBB (M1), slots 1-2 = 0xAA
//   Gen 3: slot 0 = 0xBB, slot 1 = 0xAA, slot 2 = 0xCC (M2)
//   Gen 4: slot 0 = 0xBB, slot 1 = 0xDD (M3), slot 2 = 0xCC
//   Gen 5: no changes
//
// Expected tree when saving all 3 at gen 5:
//   MRCA (Alpha, gen 1) ─┬─ M1 (slot 0)
//                         └─ MRCA (Alpha persisted, gen 1) ─┬─ M3 (slot 1)
//                                                           └─ M2 (slot 2)

TEST(MrcaTracker, MrcaComputation_WalkthroughScenario) {
    nf::MrcaTracker tracker(3, 1024 * 1024, 100);

    record(tracker, 1, {0xAA, 0xAA, 0xAA});
    record(tracker, 2, {0xBB, 0xAA, 0xAA});
    record(tracker, 3, {0xBB, 0xAA, 0xCC});
    record(tracker, 4, {0xBB, 0xDD, 0xCC});
    record(tracker, 5, {0xBB, 0xDD, 0xCC});

    // Shared entry for 0xAA + one each for 0xBB, 0xCC, 0xDD = 4 entries.
    EXPECT_EQ(tracker.entry_count(), 4u);

    // All chains should have 5 generations recorded.
    EXPECT_EQ(tracker.chain_ancestor_ids(0).size(), 5u);
    EXPECT_EQ(tracker.chain_ancestor_ids(1).size(), 5u);
    EXPECT_EQ(tracker.chain_ancestor_ids(2).size(), 5u);

    // Chains 0, 1, 2 should share the same entry ID at gen index 0.
    EXPECT_EQ(tracker.chain_ancestor_ids(0)[0],
              tracker.chain_ancestor_ids(1)[0]);
    EXPECT_EQ(tracker.chain_ancestor_ids(1)[0],
              tracker.chain_ancestor_ids(2)[0]);

    // Chains 1 and 2 should still share at gen index 1 (both still 0xAA).
    EXPECT_EQ(tracker.chain_ancestor_ids(1)[1],
              tracker.chain_ancestor_ids(2)[1]);

    // Chain 0 should have diverged at gen index 1 (0xBB != 0xAA).
    EXPECT_NE(tracker.chain_ancestor_ids(0)[1],
              tracker.chain_ancestor_ids(1)[1]);

    auto tree = tracker.compute_mrca_tree({0, 1, 2});
    ASSERT_FALSE(tree.empty());

    // 5 nodes: root MRCA + internal MRCA + 3 leaves.
    EXPECT_EQ(tree.size(), 5u);

    // Find root.
    std::size_t root_idx = SIZE_MAX;
    for (std::size_t i = 0; i < tree.size(); ++i) {
        if (tree[i].parent_index == SIZE_MAX) {
            root_idx = i;
            break;
        }
    }
    ASSERT_NE(root_idx, SIZE_MAX);

    // Root is the shared Alpha ancestor.
    EXPECT_EQ(tree[root_idx].entry.individual_id, 0xAAu);
    EXPECT_EQ(tree[root_idx].children.size(), 2u);

    // One child should be a leaf (M1), the other an internal MRCA node.
    std::size_t leaf_child = SIZE_MAX;
    std::size_t internal_child = SIZE_MAX;
    for (std::size_t c : tree[root_idx].children) {
        if (tree[c].children.empty()) {
            leaf_child = c;
        } else {
            internal_child = c;
        }
    }
    ASSERT_NE(leaf_child, SIZE_MAX);
    ASSERT_NE(internal_child, SIZE_MAX);

    // The leaf directly under root is M1 (0xBB — diverged earliest).
    EXPECT_EQ(tree[leaf_child].entry.individual_id, 0xBBu);

    // The internal node is the second MRCA (Alpha persisted in slots 1 & 2).
    EXPECT_EQ(tree[internal_child].entry.individual_id, 0xAAu);
    EXPECT_EQ(tree[internal_child].children.size(), 2u);

    // Its two children are M3 (0xDD) and M2 (0xCC).
    std::vector<uint32_t> grandchild_ids;
    for (std::size_t c : tree[internal_child].children) {
        EXPECT_TRUE(tree[c].children.empty());  // both are leaves
        grandchild_ids.push_back(tree[c].entry.individual_id);
    }
    std::sort(grandchild_ids.begin(), grandchild_ids.end());
    EXPECT_EQ(grandchild_ids[0], 0xCCu);  // M2
    EXPECT_EQ(grandchild_ids[1], 0xDDu);  // M3
}

// ---------------------------------------------------------------------------
// SharedEntryIds_SameHashSameGeneration
// ---------------------------------------------------------------------------
// Verifies that two slots with the same individual hash in the same
// generation share one entry (not two).

TEST(MrcaTracker, SharedEntryIds_SameHashSameGeneration) {
    nf::MrcaTracker tracker(3, 1024 * 1024, 100);

    // All three slots have ID=42 — should produce 1 entry, not 3.
    record(tracker, 0, {42, 42, 42});
    EXPECT_EQ(tracker.entry_count(), 1u);

    // All chains reference the same entry ID.
    EXPECT_EQ(tracker.chain_ancestor_ids(0)[0],
              tracker.chain_ancestor_ids(1)[0]);
    EXPECT_EQ(tracker.chain_ancestor_ids(1)[0],
              tracker.chain_ancestor_ids(2)[0]);

    // Two slots share, one is different — should produce 2 entries.
    record(tracker, 1, {42, 99, 42});
    EXPECT_EQ(tracker.entry_count(), 2u);
}
