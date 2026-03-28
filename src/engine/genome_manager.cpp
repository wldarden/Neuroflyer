#include <neuroflyer/genome_manager.h>
#include <neuroflyer/name_validation.h>
#include <neuroflyer/snapshot_io.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace neuroflyer {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr const char* GENOME_BIN = "genome.bin";
constexpr const char* LINEAGE_FILE = "lineage.json";
constexpr const char* GENOMIC_LINEAGE_FILE = "genomic_lineage.json";

[[nodiscard]] std::string iso8601_now() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&time_t_now, &utc);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buf;
}

[[nodiscard]] json read_lineage(const std::string& genome_dir) {
    auto path = fs::path(genome_dir) / LINEAGE_FILE;
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot read lineage.json: " + path.string());
    }
    return json::parse(in);
}

void write_lineage(const std::string& genome_dir, const json& lineage) {
    auto path = fs::path(genome_dir) / LINEAGE_FILE;
    auto tmp_path = path.string() + ".tmp";
    {
        std::ofstream out(tmp_path);
        if (!out) {
            throw std::runtime_error("Cannot write lineage.json: " + tmp_path);
        }
        out << lineage.dump(2);
    }
    fs::rename(tmp_path, path);
}

[[nodiscard]] bool is_autosave(const std::string& filename) {
    return !filename.empty() && filename[0] == '~';
}

} // namespace

// ==================== Cross-genome lineage ====================

std::vector<GenomicLineageNode> load_genomic_lineage(const std::string& genomes_dir) {
    auto path = fs::path(genomes_dir) / GENOMIC_LINEAGE_FILE;
    if (!fs::exists(path)) {
        return {};
    }
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    auto j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.contains("genomes") || !j["genomes"].is_array()) {
        return {};
    }

    std::vector<GenomicLineageNode> result;
    for (const auto& entry : j["genomes"]) {
        GenomicLineageNode node;
        node.name = entry.value("name", "");
        // source_genome and source_variant may be null or string
        if (entry.contains("source_genome") && entry["source_genome"].is_string()) {
            node.source_genome = entry["source_genome"].get<std::string>();
        }
        if (entry.contains("source_variant") && entry["source_variant"].is_string()) {
            node.source_variant = entry["source_variant"].get<std::string>();
        }
        if (!node.name.empty()) {
            result.push_back(std::move(node));
        }
    }
    return result;
}

void save_genomic_lineage(const std::string& genomes_dir,
                          const std::vector<GenomicLineageNode>& nodes) {
    json j;
    j["genomes"] = json::array();
    for (const auto& node : nodes) {
        json entry;
        entry["name"] = node.name;
        entry["source_genome"] = node.source_genome.empty() ? json(nullptr) : json(node.source_genome);
        entry["source_variant"] = node.source_variant.empty() ? json(nullptr) : json(node.source_variant);
        j["genomes"].push_back(std::move(entry));
    }

    auto path = fs::path(genomes_dir) / GENOMIC_LINEAGE_FILE;
    auto tmp_path = path.string() + ".tmp";
    {
        std::ofstream out(tmp_path);
        if (!out) {
            throw std::runtime_error("Cannot write genomic_lineage.json: " + tmp_path);
        }
        out << j.dump(2);
    }
    fs::rename(tmp_path, path);
}

// ==================== Per-genome operations ====================

std::vector<GenomeInfo> list_genomes(const std::string& genomes_dir) {
    std::vector<GenomeInfo> result;

    if (!fs::exists(genomes_dir)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(genomes_dir)) {
        if (!entry.is_directory()) continue;

        auto genome_bin = entry.path() / GENOME_BIN;
        if (!fs::exists(genome_bin)) continue;

        GenomeInfo info;
        info.name = entry.path().filename().string();

        std::size_t bin_count = 0;
        for (const auto& file : fs::directory_iterator(entry.path())) {
            if (!file.is_regular_file()) continue;
            auto fname = file.path().filename().string();
            if (is_autosave(fname)) continue;
            if (file.path().extension() == ".bin") {
                ++bin_count;
            }
        }
        // variant_count = total .bin files minus genome.bin
        info.variant_count = (bin_count > 0) ? bin_count - 1 : 0;

        result.push_back(std::move(info));
    }

    std::sort(result.begin(), result.end(),
              [](const GenomeInfo& a, const GenomeInfo& b) {
                  return a.name < b.name;
              });

    return result;
}

std::vector<SnapshotHeader> list_variants(const std::string& genome_dir) {
    std::vector<SnapshotHeader> result;

    if (!fs::exists(genome_dir)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(genome_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".bin") continue;

        auto fname = entry.path().filename().string();
        if (is_autosave(fname)) continue;

        auto header = read_snapshot_header(entry.path().string());
        result.push_back(std::move(header));
    }

    std::sort(result.begin(), result.end(),
              [](const SnapshotHeader& a, const SnapshotHeader& b) {
                  return a.name < b.name;
              });

    return result;
}

void create_genome(const std::string& genomes_dir, const Snapshot& genome) {
    if (!is_valid_name(genome.name)) {
        throw std::invalid_argument("Invalid genome name: " + genome.name);
    }

    auto dir = fs::path(genomes_dir) / genome.name;
    if (fs::exists(dir)) {
        throw std::invalid_argument("Genome directory already exists: " + dir.string());
    }

    fs::create_directories(dir);

    // Force parent_name to empty for a root genome
    Snapshot root = genome;
    root.parent_name = "";

    save_snapshot(root, (dir / GENOME_BIN).string());

    // Write initial lineage.json
    json lineage;
    lineage["nodes"] = json::array();
    lineage["nodes"].push_back({
        {"name", root.name},
        {"file", GENOME_BIN},
        {"parent", nullptr},
        {"generation", root.generation},
        {"created", iso8601_now()}
    });

    write_lineage(dir.string(), lineage);

    // Add entry to genomic_lineage.json (root genome: no source)
    auto genomic = load_genomic_lineage(genomes_dir);
    GenomicLineageNode gnode;
    gnode.name = genome.name;
    // source_genome and source_variant remain empty for fresh genomes
    genomic.push_back(std::move(gnode));
    save_genomic_lineage(genomes_dir, genomic);
}

void save_variant(const std::string& genome_dir, const Snapshot& variant) {
    if (!is_valid_name(variant.name)) {
        throw std::invalid_argument("Invalid variant name: " + variant.name);
    }

    auto bin_filename = variant.name + ".bin";
    auto bin_path = fs::path(genome_dir) / bin_filename;

    if (fs::exists(bin_path)) {
        throw std::invalid_argument("Variant already exists: " + bin_filename);
    }

    save_snapshot(variant, bin_path.string());

    // Update lineage.json
    auto lineage = read_lineage(genome_dir);

    json node;
    node["name"] = variant.name;
    node["file"] = bin_filename;
    node["parent"] = variant.parent_name.empty() ? json(nullptr) : json(variant.parent_name);
    node["generation"] = variant.generation;
    node["created"] = iso8601_now();

    lineage["nodes"].push_back(std::move(node));

    write_lineage(genome_dir, lineage);
}

void delete_variant(const std::string& genome_dir, const std::string& variant_name) {
    auto lineage = read_lineage(genome_dir);
    auto& nodes = lineage["nodes"];

    // Find the node for this variant
    std::size_t target_idx = nodes.size();
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i]["name"] == variant_name) {
            target_idx = i;
            break;
        }
    }

    if (target_idx == nodes.size()) {
        throw std::invalid_argument("Variant not found: " + variant_name);
    }

    // Forbid deleting the root genome
    if (nodes[target_idx]["file"] == GENOME_BIN) {
        throw std::invalid_argument("Cannot delete root genome");
    }

    // Get the parent of this variant
    auto parent = nodes[target_idx]["parent"];

    // If this variant has a child_genome link, relink that child genome
    // to this variant's parent in genomic_lineage.json
    std::string child_genome_name;
    if (nodes[target_idx].contains("child_genome") &&
        nodes[target_idx]["child_genome"].is_string()) {
        child_genome_name = nodes[target_idx]["child_genome"].get<std::string>();
    }

    if (!child_genome_name.empty()) {
        // Determine the genomes_dir from genome_dir (parent directory)
        auto genomes_dir = fs::path(genome_dir).parent_path().string();
        auto genomic = load_genomic_lineage(genomes_dir);

        // Find the parent variant name (string or null)
        std::string parent_variant_name;
        if (parent.is_string()) {
            parent_variant_name = parent.get<std::string>();
        }

        for (auto& gnode : genomic) {
            if (gnode.name == child_genome_name) {
                gnode.source_variant = parent_variant_name;
                break;
            }
        }
        save_genomic_lineage(genomes_dir, genomic);

        // Move the child_genome annotation to the parent variant
        if (parent.is_string()) {
            for (auto& node : nodes) {
                if (node["name"] == parent.get<std::string>()) {
                    node["child_genome"] = child_genome_name;
                    break;
                }
            }
        }
    }

    // Re-parent children: any node whose parent is variant_name gets this variant's parent
    for (auto& node : nodes) {
        if (node["parent"].is_string() && node["parent"] == variant_name) {
            node["parent"] = parent;
        }
    }

    // Remove the node
    nodes.erase(target_idx);

    write_lineage(genome_dir, lineage);

    // Delete the .bin file
    auto bin_path = fs::path(genome_dir) / (variant_name + ".bin");
    fs::remove(bin_path);
}

// Note: This operation is not atomic across multiple files. A crash between
// writes can leave inconsistent state. Acceptable for single-user desktop app.
void promote_to_genome(const std::string& genomes_dir,
                       const std::string& source_genome_dir,
                       const std::string& variant_name,
                       const std::string& new_genome_name) {
    auto variant_file = fs::path(source_genome_dir) / (variant_name + ".bin");
    if (!fs::exists(variant_file)) {
        throw std::invalid_argument("Variant file not found: " + variant_file.string());
    }

    std::string source_genome_name = fs::path(source_genome_dir).filename().string();

    auto snap = load_snapshot(variant_file.string());
    // Set parent_name to source genome name (for genomic link)
    snap.parent_name = source_genome_name;
    snap.name = new_genome_name;

    // create_genome will add a root entry; we need to fix it after to set source info.
    // Instead, create the genome first (which adds a root entry), then update.
    // We temporarily clear parent_name for create_genome (it forces empty).
    snap.parent_name = "";
    create_genome(genomes_dir, snap);

    // Now update genomic_lineage.json: replace the root entry with source info
    auto genomic = load_genomic_lineage(genomes_dir);
    for (auto& gnode : genomic) {
        if (gnode.name == new_genome_name) {
            gnode.source_genome = source_genome_name;
            gnode.source_variant = variant_name;
            break;
        }
    }
    save_genomic_lineage(genomes_dir, genomic);

    // Update source genome's lineage.json: mark the source variant with child_genome
    auto source_lineage = read_lineage(source_genome_dir);
    for (auto& node : source_lineage["nodes"]) {
        if (node["name"] == variant_name) {
            node["child_genome"] = new_genome_name;
            break;
        }
    }
    write_lineage(source_genome_dir, source_lineage);
}

void delete_genome(const std::string& genomes_dir, const std::string& genome_name) {
    auto genomic = load_genomic_lineage(genomes_dir);

    // Find this genome's entry
    std::string my_source_genome;
    std::string my_source_variant;
    bool found = false;
    for (const auto& gnode : genomic) {
        if (gnode.name == genome_name) {
            my_source_genome = gnode.source_genome;
            my_source_variant = gnode.source_variant;
            found = true;
            break;
        }
    }
    if (!found) {
        // Genome predates genomic_lineage.json — treat as root, just delete.
        // Add a temporary entry so the rest of the logic works.
        genomic.push_back({genome_name, "", ""});
    }

    // Find all child genomes (entries where source_genome == genome_name)
    std::vector<std::string> child_names;
    for (const auto& gnode : genomic) {
        if (gnode.source_genome == genome_name) {
            child_names.push_back(gnode.name);
        }
    }

    // Relink each child genome to this genome's parent
    for (auto& gnode : genomic) {
        if (gnode.source_genome == genome_name) {
            gnode.source_genome = my_source_genome;
            gnode.source_variant = my_source_variant;
        }
    }

    // Update source genome's lineage.json if it exists:
    // The variant that had child_genome==genome_name should now point to each child
    if (!my_source_genome.empty()) {
        auto source_dir = fs::path(genomes_dir) / my_source_genome;
        if (fs::exists(source_dir / LINEAGE_FILE)) {
            auto source_lineage = read_lineage(source_dir.string());
            for (auto& node : source_lineage["nodes"]) {
                if (node.contains("child_genome") &&
                    node["child_genome"].is_string() &&
                    node["child_genome"].get<std::string>() == genome_name) {
                    if (child_names.size() == 1) {
                        // Single child: just reassign
                        node["child_genome"] = child_names[0];
                    } else if (child_names.empty()) {
                        // No children: remove the link
                        node.erase("child_genome");
                    } else {
                        // Known limitation: only the first child genome's cross-genome lineage is preserved.
                        // Additional children lose their lineage link to the deleted genome.
                        // Multiple children: store first, rest get separate links
                        // (simplified: store as array for multiple)
                        node["child_genome"] = child_names[0];
                    }
                    break;
                }
            }
            write_lineage(source_dir.string(), source_lineage);
        }
    }

    // Remove this genome's entry from genomic lineage
    genomic.erase(
        std::remove_if(genomic.begin(), genomic.end(),
                        [&](const GenomicLineageNode& g) {
                            return g.name == genome_name;
                        }),
        genomic.end());

    // Save updated genomic lineage
    save_genomic_lineage(genomes_dir, genomic);

    // Delete the genome directory
    auto genome_dir = fs::path(genomes_dir) / genome_name;
    if (fs::exists(genome_dir)) {
        fs::remove_all(genome_dir);
    }
}

void rebuild_lineage(const std::string& genome_dir) {
    json lineage;
    lineage["nodes"] = json::array();

    for (const auto& entry : fs::directory_iterator(genome_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".bin") continue;

        auto fname = entry.path().filename().string();
        if (is_autosave(fname)) continue;

        auto header = read_snapshot_header(entry.path().string());

        json node;
        node["name"] = header.name;
        node["file"] = fname;
        node["parent"] = header.parent_name.empty() ? json(nullptr) : json(header.parent_name);
        node["generation"] = header.generation;
        node["created"] = iso8601_now();

        lineage["nodes"].push_back(std::move(node));
    }

    // Sort nodes: genome.bin first, then alphabetical
    std::sort(lineage["nodes"].begin(), lineage["nodes"].end(),
              [](const json& a, const json& b) {
                  bool a_is_root = a["file"] == GENOME_BIN;
                  bool b_is_root = b["file"] == GENOME_BIN;
                  if (a_is_root != b_is_root) return a_is_root;
                  return a["name"].get<std::string>() < b["name"].get<std::string>();
              });

    write_lineage(genome_dir, lineage);
}

std::vector<std::string> recover_autosaves(const std::string& genomes_dir) {
    std::vector<std::string> recovered;

    if (!fs::exists(genomes_dir)) {
        return recovered;
    }

    // Build today's date string "YYYY-MM-DD"
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&time_t_now, &utc);
    char date_buf[16];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &utc);
    const std::string date_str(date_buf);

    for (const auto& entry : fs::directory_iterator(genomes_dir)) {
        if (!entry.is_directory()) continue;

        auto genome_bin = entry.path() / GENOME_BIN;
        if (!fs::exists(genome_bin)) continue;

        auto autosave_path = entry.path() / "~autosave.bin";
        if (!fs::exists(autosave_path)) continue;

        const std::string genome_dir = entry.path().string();

        // Read the header to get metadata
        auto header = read_snapshot_header(autosave_path.string());

        // Generate a unique name: "autosave-YYYY-MM-DD" with suffix if needed
        std::string base_name = "autosave-" + date_str;
        std::string new_name = base_name;
        {
            int suffix = 2;
            while (fs::exists(fs::path(genome_dir) / (new_name + ".bin"))) {
                new_name = base_name + "-" + std::to_string(suffix);
                ++suffix;
            }
        }

        // Load full snapshot, update name, save to new file, delete old
        auto snap = load_snapshot(autosave_path.string());
        snap.name = new_name;
        auto new_bin_path = fs::path(genome_dir) / (new_name + ".bin");
        save_snapshot(snap, new_bin_path.string());
        fs::remove(autosave_path);

        // Add to lineage.json
        auto lineage = read_lineage(genome_dir);
        json node;
        node["name"] = new_name;
        node["file"] = new_name + ".bin";
        node["parent"] = header.parent_name.empty() ? json(nullptr) : json(header.parent_name);
        node["generation"] = header.generation;
        node["created"] = iso8601_now();
        lineage["nodes"].push_back(std::move(node));
        write_lineage(genome_dir, lineage);

        // Build a human-readable description
        recovered.push_back(entry.path().filename().string() +
                             " (gen " + std::to_string(header.generation) + ")");
    }

    return recovered;
}

void write_autosave(const std::string& genome_dir, const Snapshot& snapshot) {
    auto path = fs::path(genome_dir) / "~autosave.bin";
    save_snapshot(snapshot, path.string());
}

void delete_autosave(const std::string& genome_dir) {
    auto bin_path = fs::path(genome_dir) / "~autosave.bin";
    if (fs::exists(bin_path)) {
        fs::remove(bin_path);
    }

    auto tmp_path = fs::path(genome_dir) / "~autosave.tmp";
    if (fs::exists(tmp_path)) {
        fs::remove(tmp_path);
    }
}

} // namespace neuroflyer
