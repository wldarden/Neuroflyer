#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/snapshot.h>

#include <random>
#include <string>
#include <utility>
#include <vector>

namespace neuroflyer {

/// Load a variant snapshot as an Individual (for viewer/test bench).
[[nodiscard]] Individual snapshot_to_individual(const Snapshot& snap);

/// Report of what changed during network adaptation.
struct AdaptReport {
    std::vector<std::string> added;    // node IDs that were missing, filled with random weights
    std::vector<std::string> removed;  // node IDs that were extra, dropped
    [[nodiscard]] bool needed() const { return !added.empty() || !removed.empty(); }
    [[nodiscard]] std::string message() const;
};

/// Adapt an Individual's topology to match target input IDs.
/// If source has no input_ids (legacy net), returns source unchanged with empty report.
/// If source input_ids already match target, returns source unchanged with empty report.
[[nodiscard]] std::pair<Individual, AdaptReport> adapt_individual_inputs(
    const Individual& source,
    const std::vector<std::string>& target_input_ids,
    const ShipDesign& design,
    std::mt19937& rng);

} // namespace neuroflyer
