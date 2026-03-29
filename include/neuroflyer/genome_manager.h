#pragma once
#include <neuroflyer/mrca_tracker.h>
#include <neuroflyer/snapshot.h>
#include <cstdint>
#include <string>
#include <vector>

namespace neuroflyer {

// ==================== Cross-genome lineage ====================

/// One node in the genome-level lineage tree (stored in genomic_lineage.json).
struct GenomicLineageNode {
    std::string name;
    std::string source_genome;   // empty for root genomes (created fresh)
    std::string source_variant;  // the variant that was promoted
};

/// Load the genome-level lineage from genomes_dir/genomic_lineage.json.
/// Returns empty vector if the file does not exist yet.
[[nodiscard]] std::vector<GenomicLineageNode> load_genomic_lineage(const std::string& genomes_dir);

/// Save the genome-level lineage to genomes_dir/genomic_lineage.json.
void save_genomic_lineage(const std::string& genomes_dir,
                          const std::vector<GenomicLineageNode>& nodes);

// ==================== Per-genome operations ====================

/// List all genomes (directories in genomes_dir that contain genome.bin).
[[nodiscard]] std::vector<GenomeInfo> list_genomes(const std::string& genomes_dir);

/// List all snapshot headers for a genome (genome.bin + all variant .bin files).
[[nodiscard]] std::vector<SnapshotHeader> list_variants(const std::string& genome_dir);

/// Create a new genome: makes directory, writes genome.bin, initializes lineage.json.
/// Also adds a root entry (source_genome="") to genomic_lineage.json.
void create_genome(const std::string& genomes_dir, const Snapshot& genome);

/// Save a variant .bin into a genome's directory and update lineage.json.
/// Throws if name already exists or is invalid.
void save_variant(const std::string& genome_dir, const Snapshot& variant);

/// Delete a variant. Re-parents its children to the variant's parent in lineage.json.
/// If the variant has a child_genome link, relinks that child genome to the
/// deleted variant's parent variant in genomic_lineage.json.
/// Throws if trying to delete genome.bin.
void delete_variant(const std::string& genome_dir, const std::string& variant_name);

/// Promote a variant to a new genome (copy as genome.bin in new directory).
/// Records cross-genome lineage in genomic_lineage.json and marks the source
/// variant with a child_genome field in the source genome's lineage.json.
void promote_to_genome(const std::string& genomes_dir,
                       const std::string& source_genome_dir,
                       const std::string& variant_name,
                       const std::string& new_genome_name);

/// Delete a genome and all its variants.
/// Relinks child genomes to this genome's parent genome in genomic_lineage.json.
/// Updates the source genome's lineage.json child_genome links accordingly.
void delete_genome(const std::string& genomes_dir, const std::string& genome_name);

/// Rebuild lineage.json from .bin file headers (recovery/repair).
void rebuild_lineage(const std::string& genome_dir);

/// Scan all genome directories for orphaned ~autosave.bin files (from crashes).
/// Renames each to "autosave-{date}.bin" and adds to lineage.json.
/// Returns list of recovery descriptions for UI notification.
[[nodiscard]] std::vector<std::string> recover_autosaves(const std::string& genomes_dir);

/// Write an auto-save file atomically (via temp + rename).
/// Writes to genome_dir/~autosave.bin. Does NOT update lineage.json.
void write_autosave(const std::string& genome_dir, const Snapshot& snapshot);

/// Delete the auto-save file if it exists. Called on clean exit from training.
void delete_autosave(const std::string& genome_dir);

// ==================== Squad variant operations ====================

/// List squad net variants from {genome_dir}/squad/
[[nodiscard]] std::vector<SnapshotHeader> list_squad_variants(const std::string& genome_dir);

/// Save a squad net variant to {genome_dir}/squad/{name}.bin
void save_squad_variant(const std::string& genome_dir, const Snapshot& variant);

/// Delete a squad net variant from {genome_dir}/squad/
void delete_squad_variant(const std::string& genome_dir, const std::string& variant_name);

/// Save multiple elite variants with MRCA lineage stubs.
/// Computes the MRCA tree from the tracker, creates MRCA stub entries in
/// lineage.json, and saves each variant .bin with the correct parent.
/// elite_slots, individual_ids, and snapshots must be parallel vectors.
void save_elite_variants_with_mrca(
    const std::string& genome_dir,
    const std::string& training_parent,
    const MrcaTracker& tracker,
    const std::vector<std::size_t>& elite_slots,
    const std::vector<uint32_t>& individual_ids,
    std::vector<Snapshot>& snapshots);

} // namespace neuroflyer
