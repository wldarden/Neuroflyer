#include <neuroflyer/genome_manager.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace nf = neuroflyer;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

/// Build a minimal Snapshot suitable for binary serialization.
[[nodiscard]] nf::Snapshot make_snapshot(const std::string& name,
                                         const std::string& parent,
                                         uint32_t generation) {
    nf::Snapshot snap;
    snap.name = name;
    snap.parent_name = parent;
    snap.generation = generation;
    snap.created_timestamp = 1711300000;
    snap.ship_design.memory_slots = 0;
    snap.topology.input_size = 3;
    snap.topology.layers = {{std::size_t{2}, neuralnet::Activation::Tanh, {}}};
    snap.weights.resize(3 * 2 + 2, 0.1f);
    return snap;
}

[[nodiscard]] json read_lineage_json(const std::string& genome_dir) {
    auto path = fs::path(genome_dir) / "lineage.json";
    std::ifstream in(path);
    return json::parse(in);
}

} // namespace

class GenomeManagerTest : public ::testing::Test {
protected:
    fs::path tmp_dir_;

    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() / "nf_genome_manager_test";
        fs::remove_all(tmp_dir_);
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override {
        fs::remove_all(tmp_dir_);
    }

    [[nodiscard]] std::string genomes_dir() const {
        return tmp_dir_.string();
    }

    [[nodiscard]] std::string genome_dir() const {
        return (tmp_dir_ / "TestGenome").string();
    }
};

TEST_F(GenomeManagerTest, CreateAndList) {
    auto snap = make_snapshot("Lotus", "", 0);
    nf::create_genome(genomes_dir(), snap);

    auto genomes = nf::list_genomes(genomes_dir());
    ASSERT_EQ(genomes.size(), 1u);
    EXPECT_EQ(genomes[0].name, "Lotus");
    EXPECT_EQ(genomes[0].variant_count, 0u);

    // Create a second genome
    auto snap2 = make_snapshot("Fern", "", 0);
    nf::create_genome(genomes_dir(), snap2);

    genomes = nf::list_genomes(genomes_dir());
    ASSERT_EQ(genomes.size(), 2u);
    // Sorted alphabetically
    EXPECT_EQ(genomes[0].name, "Fern");
    EXPECT_EQ(genomes[1].name, "Lotus");
}

TEST_F(GenomeManagerTest, SaveAndListVariants) {
    auto genome = make_snapshot("Lotus", "", 0);
    nf::create_genome(genomes_dir(), genome);
    auto genome_dir = (tmp_dir_ / "Lotus").string();

    auto variant = make_snapshot("Lotus V1", "Lotus", 100);
    nf::save_variant(genome_dir, variant);

    auto headers = nf::list_variants(genome_dir);
    ASSERT_EQ(headers.size(), 2u);
    // Sorted by name: "Lotus" (genome.bin) < "Lotus V1"
    EXPECT_EQ(headers[0].name, "Lotus");
    EXPECT_EQ(headers[0].parent_name, "");
    EXPECT_EQ(headers[1].name, "Lotus V1");
    EXPECT_EQ(headers[1].parent_name, "Lotus");
    EXPECT_EQ(headers[1].generation, 100u);

    // list_genomes should show variant_count=1
    auto genomes = nf::list_genomes(genomes_dir());
    ASSERT_EQ(genomes.size(), 1u);
    EXPECT_EQ(genomes[0].variant_count, 1u);
}

TEST_F(GenomeManagerTest, SaveVariant_DuplicateName_Throws) {
    auto genome = make_snapshot("Lotus", "", 0);
    nf::create_genome(genomes_dir(), genome);
    auto genome_dir = (tmp_dir_ / "Lotus").string();

    auto variant = make_snapshot("V1", "Lotus", 50);
    nf::save_variant(genome_dir, variant);

    // Try saving again with same name
    auto dup = make_snapshot("V1", "Lotus", 60);
    EXPECT_THROW(nf::save_variant(genome_dir, dup), std::invalid_argument);
}

TEST_F(GenomeManagerTest, SaveVariant_InvalidName_Throws) {
    auto genome = make_snapshot("Lotus", "", 0);
    nf::create_genome(genomes_dir(), genome);
    auto genome_dir = (tmp_dir_ / "Lotus").string();

    // Empty name
    auto bad1 = make_snapshot("", "Lotus", 50);
    EXPECT_THROW(nf::save_variant(genome_dir, bad1), std::invalid_argument);

    // Name with slash
    auto bad2 = make_snapshot("a/b", "Lotus", 50);
    EXPECT_THROW(nf::save_variant(genome_dir, bad2), std::invalid_argument);
}

TEST_F(GenomeManagerTest, DeleteVariant_Leaf) {
    auto genome = make_snapshot("Lotus", "", 0);
    nf::create_genome(genomes_dir(), genome);
    auto genome_dir = (tmp_dir_ / "Lotus").string();

    auto variant = make_snapshot("V1", "Lotus", 50);
    nf::save_variant(genome_dir, variant);

    // Verify .bin exists
    ASSERT_TRUE(fs::exists(fs::path(genome_dir) / "V1.bin"));

    nf::delete_variant(genome_dir, "V1");

    // .bin gone
    EXPECT_FALSE(fs::exists(fs::path(genome_dir) / "V1.bin"));

    // lineage.json updated: only genome node remains
    auto lineage = read_lineage_json(genome_dir);
    ASSERT_EQ(lineage["nodes"].size(), 1u);
    EXPECT_EQ(lineage["nodes"][0]["name"], "Lotus");
}

TEST_F(GenomeManagerTest, DeleteVariant_WithChildren_ReparentsChildren) {
    auto genome = make_snapshot("Lotus", "", 0);
    nf::create_genome(genomes_dir(), genome);
    auto genome_dir = (tmp_dir_ / "Lotus").string();

    // genome -> A -> B
    auto a = make_snapshot("A", "Lotus", 50);
    nf::save_variant(genome_dir, a);

    auto b = make_snapshot("B", "A", 100);
    nf::save_variant(genome_dir, b);

    // Delete A: B should be re-parented to Lotus
    nf::delete_variant(genome_dir, "A");

    auto lineage = read_lineage_json(genome_dir);
    ASSERT_EQ(lineage["nodes"].size(), 2u);

    // Find B in lineage
    const json* b_node = nullptr;
    for (const auto& node : lineage["nodes"]) {
        if (node["name"] == "B") {
            b_node = &node;
            break;
        }
    }
    ASSERT_NE(b_node, nullptr);
    EXPECT_EQ((*b_node)["parent"], "Lotus");
}

TEST_F(GenomeManagerTest, DeleteGenome_Throws) {
    auto genome = make_snapshot("Lotus", "", 0);
    nf::create_genome(genomes_dir(), genome);
    auto genome_dir = (tmp_dir_ / "Lotus").string();

    EXPECT_THROW(nf::delete_variant(genome_dir, "Lotus"), std::invalid_argument);
}

TEST_F(GenomeManagerTest, PromoteToGenome) {
    auto genome = make_snapshot("Lotus", "", 0);
    nf::create_genome(genomes_dir(), genome);
    auto source_dir = (tmp_dir_ / "Lotus").string();

    auto variant = make_snapshot("V1", "Lotus", 200);
    nf::save_variant(source_dir, variant);

    nf::promote_to_genome(genomes_dir(), source_dir, "V1", "Orchid");

    // New genome directory exists
    auto new_dir = tmp_dir_ / "Orchid";
    EXPECT_TRUE(fs::exists(new_dir / "genome.bin"));
    EXPECT_TRUE(fs::exists(new_dir / "lineage.json"));

    // Verify the new genome's data
    auto loaded = nf::load_snapshot((new_dir / "genome.bin").string());
    EXPECT_EQ(loaded.name, "Orchid");
    EXPECT_EQ(loaded.parent_name, "");
    EXPECT_EQ(loaded.generation, 200u);

    // Original variant still exists
    EXPECT_TRUE(fs::exists(fs::path(source_dir) / "V1.bin"));

    // Verify genomic_lineage.json has correct cross-genome link
    auto genomic = nf::load_genomic_lineage(genomes_dir());
    ASSERT_EQ(genomic.size(), 2u);
    // Find Orchid entry
    const nf::GenomicLineageNode* orchid_node = nullptr;
    for (const auto& gn : genomic) {
        if (gn.name == "Orchid") {
            orchid_node = &gn;
            break;
        }
    }
    ASSERT_NE(orchid_node, nullptr);
    EXPECT_EQ(orchid_node->source_genome, "Lotus");
    EXPECT_EQ(orchid_node->source_variant, "V1");

    // Verify source genome's lineage.json has child_genome on V1
    auto source_lineage = read_lineage_json(source_dir);
    bool found_child_link = false;
    for (const auto& node : source_lineage["nodes"]) {
        if (node["name"] == "V1" &&
            node.contains("child_genome") &&
            node["child_genome"] == "Orchid") {
            found_child_link = true;
            break;
        }
    }
    EXPECT_TRUE(found_child_link);
}

// ==================== Genomic lineage tests ====================

TEST_F(GenomeManagerTest, GenomicLineage_CreateGenomeAddsEntry) {
    auto snap = make_snapshot("Alpha", "", 0);
    nf::create_genome(genomes_dir(), snap);

    auto genomic = nf::load_genomic_lineage(genomes_dir());
    ASSERT_EQ(genomic.size(), 1u);
    EXPECT_EQ(genomic[0].name, "Alpha");
    EXPECT_TRUE(genomic[0].source_genome.empty());
    EXPECT_TRUE(genomic[0].source_variant.empty());
}

TEST_F(GenomeManagerTest, GenomicLineage_LoadEmpty) {
    // No genomic_lineage.json file yet
    auto genomic = nf::load_genomic_lineage(genomes_dir());
    EXPECT_TRUE(genomic.empty());
}

TEST_F(GenomeManagerTest, DeleteGenome_Basic) {
    auto snap = make_snapshot("Alpha", "", 0);
    nf::create_genome(genomes_dir(), snap);

    auto alpha_dir = (tmp_dir_ / "Alpha").string();
    auto v1 = make_snapshot("A v1", "Alpha", 100);
    nf::save_variant(alpha_dir, v1);

    // Directory exists before delete
    ASSERT_TRUE(fs::exists(tmp_dir_ / "Alpha"));

    nf::delete_genome(genomes_dir(), "Alpha");

    // Directory should be gone
    EXPECT_FALSE(fs::exists(tmp_dir_ / "Alpha"));

    // genomic_lineage.json should be empty
    auto genomic = nf::load_genomic_lineage(genomes_dir());
    EXPECT_TRUE(genomic.empty());
}

TEST_F(GenomeManagerTest, DeleteGenome_RelinksChildren) {
    // Alpha -> Beta -> Charlie (via promote)
    auto alpha_snap = make_snapshot("Alpha", "", 0);
    nf::create_genome(genomes_dir(), alpha_snap);
    auto alpha_dir = (tmp_dir_ / "Alpha").string();

    auto a_v1 = make_snapshot("Alpha v3", "Alpha", 300);
    nf::save_variant(alpha_dir, a_v1);

    nf::promote_to_genome(genomes_dir(), alpha_dir, "Alpha v3", "Beta");
    auto beta_dir = (tmp_dir_ / "Beta").string();

    auto b_v1 = make_snapshot("Beta v2", "Beta", 500);
    nf::save_variant(beta_dir, b_v1);

    nf::promote_to_genome(genomes_dir(), beta_dir, "Beta v2", "Charlie");

    // Verify chain: Alpha -> Beta -> Charlie
    auto genomic = nf::load_genomic_lineage(genomes_dir());
    ASSERT_EQ(genomic.size(), 3u);

    // Delete Beta: Charlie should relink to Alpha
    nf::delete_genome(genomes_dir(), "Beta");

    EXPECT_FALSE(fs::exists(tmp_dir_ / "Beta"));

    genomic = nf::load_genomic_lineage(genomes_dir());
    ASSERT_EQ(genomic.size(), 2u);

    // Find Charlie
    const nf::GenomicLineageNode* charlie = nullptr;
    for (const auto& gn : genomic) {
        if (gn.name == "Charlie") {
            charlie = &gn;
            break;
        }
    }
    ASSERT_NE(charlie, nullptr);
    EXPECT_EQ(charlie->source_genome, "Alpha");
    EXPECT_EQ(charlie->source_variant, "Alpha v3");
}

TEST_F(GenomeManagerTest, DeleteVariant_WithChildGenome_RelinksGenomicLineage) {
    auto alpha_snap = make_snapshot("Alpha", "", 0);
    nf::create_genome(genomes_dir(), alpha_snap);
    auto alpha_dir = (tmp_dir_ / "Alpha").string();

    // Alpha -> V1 -> V2, promote V2 to Beta
    auto v1 = make_snapshot("V1", "Alpha", 100);
    nf::save_variant(alpha_dir, v1);

    auto v2 = make_snapshot("V2", "V1", 200);
    nf::save_variant(alpha_dir, v2);

    nf::promote_to_genome(genomes_dir(), alpha_dir, "V2", "Beta");

    // Verify V2 has child_genome: Beta
    auto lineage = read_lineage_json(alpha_dir);
    bool v2_has_child = false;
    for (const auto& node : lineage["nodes"]) {
        if (node["name"] == "V2" && node.contains("child_genome") &&
            node["child_genome"] == "Beta") {
            v2_has_child = true;
        }
    }
    ASSERT_TRUE(v2_has_child);

    // Delete V2: Beta should relink to V1
    nf::delete_variant(alpha_dir, "V2");

    auto genomic = nf::load_genomic_lineage(genomes_dir());
    const nf::GenomicLineageNode* beta = nullptr;
    for (const auto& gn : genomic) {
        if (gn.name == "Beta") {
            beta = &gn;
            break;
        }
    }
    ASSERT_NE(beta, nullptr);
    EXPECT_EQ(beta->source_variant, "V1");

    // V1 should now have child_genome: Beta
    lineage = read_lineage_json(alpha_dir);
    bool v1_has_child = false;
    for (const auto& node : lineage["nodes"]) {
        if (node["name"] == "V1" && node.contains("child_genome") &&
            node["child_genome"] == "Beta") {
            v1_has_child = true;
        }
    }
    EXPECT_TRUE(v1_has_child);
}

TEST_F(GenomeManagerTest, RebuildLineage) {
    auto genome = make_snapshot("Lotus", "", 0);
    nf::create_genome(genomes_dir(), genome);
    auto genome_dir = (tmp_dir_ / "Lotus").string();

    auto v1 = make_snapshot("V1", "Lotus", 50);
    nf::save_variant(genome_dir, v1);

    auto v2 = make_snapshot("V2", "V1", 100);
    nf::save_variant(genome_dir, v2);

    // Delete lineage.json
    fs::remove(fs::path(genome_dir) / "lineage.json");
    ASSERT_FALSE(fs::exists(fs::path(genome_dir) / "lineage.json"));

    // Rebuild
    nf::rebuild_lineage(genome_dir);

    ASSERT_TRUE(fs::exists(fs::path(genome_dir) / "lineage.json"));

    auto lineage = read_lineage_json(genome_dir);
    ASSERT_EQ(lineage["nodes"].size(), 3u);

    // genome.bin should be first (root)
    EXPECT_EQ(lineage["nodes"][0]["file"], "genome.bin");
    EXPECT_EQ(lineage["nodes"][0]["name"], "Lotus");
    EXPECT_TRUE(lineage["nodes"][0]["parent"].is_null());

    // Find V1 and V2 and check parent relationships
    bool found_v1 = false;
    bool found_v2 = false;
    for (const auto& node : lineage["nodes"]) {
        if (node["name"] == "V1") {
            found_v1 = true;
            EXPECT_EQ(node["parent"], "Lotus");
            EXPECT_EQ(node["generation"], 50u);
        }
        if (node["name"] == "V2") {
            found_v2 = true;
            EXPECT_EQ(node["parent"], "V1");
            EXPECT_EQ(node["generation"], 100u);
        }
    }
    EXPECT_TRUE(found_v1);
    EXPECT_TRUE(found_v2);
}

TEST_F(GenomeManagerTest, RecoverAutosaves) {
    // Create a genome
    auto snap = make_snapshot("TestGenome", "", 0);
    nf::create_genome(genomes_dir(), snap);

    // Simulate a crash: manually write a ~autosave.bin
    auto autosave = make_snapshot("~autosave", "TestGenome", 150);
    nf::save_snapshot(autosave, genome_dir() + "/~autosave.bin");

    // Recover
    auto recovered = nf::recover_autosaves(genomes_dir());
    ASSERT_EQ(recovered.size(), 1u);

    // Verify: ~autosave.bin is gone
    EXPECT_FALSE(fs::exists(genome_dir() + "/~autosave.bin"));

    // Verify: a new variant .bin exists
    auto variants = nf::list_variants(genome_dir());
    EXPECT_GE(variants.size(), 2u);  // genome + at least 1 recovered variant

    // Verify: lineage.json has the recovered node
    // (check by listing variants and finding one with generation 150)
    bool found = false;
    for (const auto& v : variants) {
        if (v.generation == 150) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(GenomeManagerTest, WriteAutosave) {
    auto snap = make_snapshot("TestGenome", "", 0);
    nf::create_genome(genomes_dir(), snap);

    auto autosave_snap = make_snapshot("~autosave", "TestGenome", 50);
    nf::write_autosave(genome_dir(), autosave_snap);

    // File should exist
    EXPECT_TRUE(std::filesystem::exists(genome_dir() + "/~autosave.bin"));

    // Should be loadable
    auto loaded = nf::load_snapshot(genome_dir() + "/~autosave.bin");
    EXPECT_EQ(loaded.generation, 50u);
}

TEST_F(GenomeManagerTest, DeleteAutosave) {
    auto snap = make_snapshot("TestGenome", "", 0);
    nf::create_genome(genomes_dir(), snap);

    auto autosave_snap = make_snapshot("~autosave", "TestGenome", 50);
    nf::write_autosave(genome_dir(), autosave_snap);
    EXPECT_TRUE(std::filesystem::exists(genome_dir() + "/~autosave.bin"));

    nf::delete_autosave(genome_dir());
    EXPECT_FALSE(std::filesystem::exists(genome_dir() + "/~autosave.bin"));
}

TEST_F(GenomeManagerTest, DeleteAutosave_NoFileIsNoop) {
    auto snap = make_snapshot("TestGenome", "", 0);
    nf::create_genome(genomes_dir(), snap);

    // Should not throw when no autosave exists
    EXPECT_NO_THROW(nf::delete_autosave(genome_dir()));
}

TEST_F(GenomeManagerTest, ListVariants_SkipsAutosave) {
    auto snap = make_snapshot("TestGenome", "", 0);
    nf::create_genome(genomes_dir(), snap);

    // Write an autosave
    auto autosave_snap = make_snapshot("~autosave", "TestGenome", 50);
    nf::write_autosave(genome_dir(), autosave_snap);

    // list_variants should NOT include the autosave
    auto variants = nf::list_variants(genome_dir());
    for (const auto& v : variants) {
        EXPECT_NE(v.name, "~autosave") << "Autosave should not appear in variant list";
    }
}

// ==================== Squad variant tests ====================

TEST_F(GenomeManagerTest, ListSquadVariantsEmptyDir) {
    // genome_dir() points to a non-existent path; list_squad_variants on a dir
    // with no squad/ subdir should return empty
    auto genome = make_snapshot("TestGenome", "", 0);
    nf::create_genome(genomes_dir(), genome);

    auto variants = nf::list_squad_variants(genome_dir());
    EXPECT_TRUE(variants.empty());
}

TEST_F(GenomeManagerTest, SaveAndListSquadVariant) {
    auto genome = make_snapshot("TestGenome", "", 0);
    nf::create_genome(genomes_dir(), genome);

    nf::Snapshot snap;
    snap.name = "squad-v1";
    snap.generation = 10;
    snap.paired_fighter_name = "elite-fighter";
    snap.ship_design.memory_slots = 2;
    snap.topology.input_size = 8;
    snap.topology.layers = {{std::size_t{4}, neuralnet::Activation::Tanh, {}}};
    snap.weights = {0.1f, 0.2f};

    nf::save_squad_variant(genome_dir(), snap);

    auto variants = nf::list_squad_variants(genome_dir());
    ASSERT_EQ(variants.size(), 1u);
    EXPECT_EQ(variants[0].name, "squad-v1");
    EXPECT_EQ(variants[0].paired_fighter_name, "elite-fighter");
}

TEST_F(GenomeManagerTest, DeleteSquadVariant) {
    auto genome = make_snapshot("TestGenome", "", 0);
    nf::create_genome(genomes_dir(), genome);

    nf::Snapshot snap;
    snap.name = "squad-to-delete";
    snap.generation = 5;
    snap.ship_design.memory_slots = 2;
    snap.topology.input_size = 4;
    snap.topology.layers = {{std::size_t{2}, neuralnet::Activation::Tanh, {}}};
    snap.weights = {0.5f};

    nf::save_squad_variant(genome_dir(), snap);
    ASSERT_EQ(nf::list_squad_variants(genome_dir()).size(), 1u);

    nf::delete_squad_variant(genome_dir(), "squad-to-delete");
    EXPECT_TRUE(nf::list_squad_variants(genome_dir()).empty());
}
