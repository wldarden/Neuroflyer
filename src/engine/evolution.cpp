#include <neuroflyer/evolution.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/snapshot_utils.h>

#include <neuralnet/adapt.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <random>

namespace neuroflyer {

void sync_activations_from_genome(
    const evolve::StructuredGenome& genome,
    neuralnet::NetworkTopology& topology) {
    for (std::size_t l = 0; l < topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (genome.has_gene(lp + "_activations")) {
            const auto& ag = genome.gene(lp + "_activations");
            topology.layers[l].node_activations.resize(ag.values.size());
            for (std::size_t n = 0; n < ag.values.size(); ++n) {
                int idx = std::clamp(static_cast<int>(std::round(ag.values[n])),
                                     0, neuralnet::ACTIVATION_COUNT - 1);
                topology.layers[l].node_activations[n] =
                    static_cast<neuralnet::Activation>(idx);
            }
        }
    }
}

uint32_t individual_hash(const Individual& ind) {
    uint32_t h = 0;
    auto flat = ind.genome.flatten_all();
    for (std::size_t i = 0; i < flat.size(); ++i) {
        uint32_t bits;
        std::memcpy(&bits, &flat[i], sizeof(bits));
        h ^= bits + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
}

std::size_t count_weight_genes(const neuralnet::NetworkTopology& topo) {
    std::size_t total = 0;
    std::size_t prev = topo.input_size;
    for (const auto& layer : topo.layers) {
        // weights + biases + per-node activations
        total += prev * layer.output_size + layer.output_size + layer.output_size;
        prev = layer.output_size;
    }
    return total;
}

evolve::StructuredGenome build_genome_skeleton(
    const ShipDesign& design,
    const neuralnet::NetworkTopology& topology) {

    evolve::StructuredGenome g;

    // Sensor genes — one linkage group per sensor
    for (std::size_t i = 0; i < design.sensors.size(); ++i) {
        const auto& s = design.sensors[i];
        std::string prefix = "sensor_" + std::to_string(i);

        g.add_gene({prefix + "_angle", {s.angle},
                    {.rate = 0.03f, .strength = 0.05f, .min_val = -1.6f, .max_val = 1.6f,
                     .evolvable = design.evolvable.sensor_angle}});
        g.add_gene({prefix + "_range", {s.range},
                    {.rate = 0.05f, .strength = 10.0f, .min_val = 30.0f, .max_val = 400.0f,
                     .evolvable = design.evolvable.sensor_range}});
        g.add_gene({prefix + "_width", {s.width},
                    {.rate = 0.05f, .strength = 0.02f, .min_val = 0.02f, .max_val = 0.6f,
                     .evolvable = design.evolvable.sensor_width}});

        g.add_linkage_group({prefix,
            {prefix + "_angle", prefix + "_range", prefix + "_width"}});
    }

    // Weight + bias genes — one linkage group per layer
    std::size_t prev_size = topology.input_size;
    for (std::size_t l = 0; l < topology.layers.size(); ++l) {
        const auto& layer = topology.layers[l];
        std::string lprefix = "layer_" + std::to_string(l);

        std::size_t weight_count = prev_size * layer.output_size;
        g.add_gene({lprefix + "_weights",
                    std::vector<float>(weight_count, 0.0f),
                    {.rate = 0.1f, .strength = 0.3f}});
        g.add_gene({lprefix + "_biases",
                    std::vector<float>(layer.output_size, 0.0f),
                    {.rate = 0.1f, .strength = 0.3f}});

        // Per-node activation genes
        // Stored as float values mapping to Activation enum, rounded to int at runtime
        std::vector<float> act_values(layer.output_size);
        if (!topology.layers[l].node_activations.empty()) {
            for (std::size_t n = 0; n < layer.output_size; ++n) {
                act_values[n] = static_cast<float>(
                    topology.layers[l].node_activations[n]);
            }
        } else {
            // Default: all nodes use the layer's default activation
            float default_act = static_cast<float>(topology.layers[l].activation);
            std::fill(act_values.begin(), act_values.end(), default_act);
        }

        g.add_gene({lprefix + "_activations", act_values,
                    {.rate = 0.05f, .strength = 0.5f,
                     .min_val = 0.0f,
                     .max_val = static_cast<float>(neuralnet::ACTIVATION_COUNT - 1),
                     .evolvable = design.evolvable.activation_function}});

        g.add_linkage_group({lprefix,
            {lprefix + "_weights", lprefix + "_biases", lprefix + "_activations"}});

        prev_size = layer.output_size;
    }

    return g;
}

Individual Individual::from_design(const ShipDesign& design,
                                    const std::vector<std::size_t>& hidden_layers,
                                    std::mt19937& rng) {
    Individual ind;
    ind.topology.input_size = compute_input_size(design);
    for (auto h : hidden_layers) {
        ind.topology.layers.push_back(
            {.output_size = h, .activation = neuralnet::Activation::Tanh});
    }
    ind.topology.layers.push_back(
        {.output_size = compute_output_size(design),
         .activation = neuralnet::Activation::Tanh});

    ind.topology.input_ids = build_input_labels(design);
    ind.topology.output_ids = build_output_ids(design);

    ind.genome = build_genome_skeleton(design, ind.topology);

    // Randomize weight and bias genes
    std::uniform_real_distribution<float> weight_dist(-1.0f, 1.0f);
    for (auto& gene : ind.genome.genes()) {
        if (gene.tag.find("_weights") != std::string::npos ||
            gene.tag.find("_biases") != std::string::npos) {
            for (auto& v : gene.values) {
                v = weight_dist(rng);
            }
        }
    }

    return ind;
}

Individual Individual::random(std::size_t input_size,
                               std::vector<std::size_t> hidden_layers,
                               std::size_t output_size,
                               std::mt19937& rng) {
    // Legacy path: build a minimal ShipDesign with the right input/output count
    // This won't have real sensor genes, just weights
    Individual ind;
    ind.topology.input_size = input_size;
    for (auto h : hidden_layers) {
        ind.topology.layers.push_back(
            {.output_size = h, .activation = neuralnet::Activation::Tanh});
    }
    ind.topology.layers.push_back(
        {.output_size = output_size, .activation = neuralnet::Activation::Tanh});

    // Build genome with weight genes only (no sensors)
    ind.genome = evolve::StructuredGenome{};
    std::size_t prev = input_size;
    std::uniform_real_distribution<float> weight_dist(-1.0f, 1.0f);
    for (std::size_t l = 0; l < ind.topology.layers.size(); ++l) {
        const auto& layer = ind.topology.layers[l];
        std::string lp = "layer_" + std::to_string(l);

        std::size_t wc = prev * layer.output_size;
        std::vector<float> wvals(wc);
        for (auto& v : wvals) v = weight_dist(rng);
        ind.genome.add_gene({lp + "_weights", std::move(wvals),
                             {.rate = 0.1f, .strength = 0.3f}});

        std::vector<float> bvals(layer.output_size, 0.0f);
        ind.genome.add_gene({lp + "_biases", std::move(bvals),
                             {.rate = 0.1f, .strength = 0.3f}});

        // Per-node activation genes (default Tanh = 2.0f, not evolvable in legacy path)
        float default_act = static_cast<float>(layer.activation);
        std::vector<float> avals(layer.output_size, default_act);
        ind.genome.add_gene({lp + "_activations", std::move(avals),
                             {.rate = 0.05f, .strength = 0.5f,
                              .min_val = 0.0f,
                              .max_val = static_cast<float>(neuralnet::ACTIVATION_COUNT - 1),
                              .evolvable = false}});

        ind.genome.add_linkage_group({lp,
            {lp + "_weights", lp + "_biases", lp + "_activations"}});
        prev = layer.output_size;
    }

    return ind;
}

neuralnet::Network Individual::build_network() const {
    auto topo = topology;
    sync_activations_from_genome(genome, topo);

    // Extract weights in layer order: weights then biases per layer
    std::vector<float> flat;
    for (std::size_t l = 0; l < topo.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (genome.has_gene(lp + "_weights")) {
            const auto& wg = genome.gene(lp + "_weights");
            flat.insert(flat.end(), wg.values.begin(), wg.values.end());
        }
        if (genome.has_gene(lp + "_biases")) {
            const auto& bg = genome.gene(lp + "_biases");
            flat.insert(flat.end(), bg.values.begin(), bg.values.end());
        }
    }
    return neuralnet::Network(topo, flat);
}

ShipDesign Individual::effective_ship_design() const {
    ShipDesign design;
    // Read sensor genes if they exist
    for (std::size_t i = 0; ; ++i) {
        std::string prefix = "sensor_" + std::to_string(i);
        if (!genome.has_gene(prefix + "_angle")) break;

        SensorDef sensor{};
        sensor.angle = genome.get(prefix + "_angle");
        sensor.range = genome.get(prefix + "_range");
        sensor.width = genome.get(prefix + "_width");
        // Type and is_full_sensor aren't evolved yet — use defaults
        sensor.type = SensorType::Occulus;
        sensor.is_full_sensor = false;  // caller should set these from template
        design.sensors.push_back(sensor);
    }
    return design;
}

bool same_topology(const Individual& a, const Individual& b) {
    if (a.topology.input_size != b.topology.input_size) return false;
    if (a.topology.layers.size() != b.topology.layers.size()) return false;
    for (std::size_t i = 0; i < a.topology.layers.size(); ++i) {
        if (a.topology.layers[i].output_size != b.topology.layers[i].output_size) return false;
    }
    return true;
}

namespace {

/// Build scroller source IDs using arena-compatible naming for sensor channels.
/// This ensures adapt_topology_inputs can match shared channels (distance, is_tower,
/// is_token, memory) when converting from scroller to arena fighter layout.
std::vector<std::string> build_scroller_source_ids(const ShipDesign& design) {
    std::vector<std::string> ids;

    int sensor_idx = 0;
    for (const auto& s : design.sensors) {
        char buf[32];
        if (!s.is_full_sensor) {
            // Sight sensor: same ID as arena
            std::snprintf(buf, sizeof(buf), "SNS %d D", sensor_idx);
            ids.push_back(buf);
        } else {
            // Full sensor scroller layout: [distance, is_tower, token_value, is_token]
            // Arena-compatible IDs for shared channels
            std::snprintf(buf, sizeof(buf), "SNS %d D", sensor_idx);
            ids.push_back(buf);
            std::snprintf(buf, sizeof(buf), "SNS %d TWR", sensor_idx);
            ids.push_back(buf);
            // token_value is scroller-only; use a unique ID that won't match arena
            std::snprintf(buf, sizeof(buf), "SNS %d $V", sensor_idx);
            ids.push_back(buf);
            // is_token maps to arena's TKN
            std::snprintf(buf, sizeof(buf), "SNS %d TKN", sensor_idx);
            ids.push_back(buf);
        }
        ++sensor_idx;
    }

    // Scroller position inputs (not in arena)
    ids.push_back("POS X");
    ids.push_back("POS Y");
    ids.push_back("SPEED");

    // Memory (same IDs as arena)
    for (uint16_t m = 0; m < design.memory_slots; ++m) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "MEM %d", m);
        ids.push_back(buf);
    }

    return ids;
}

/// Flatten only weight and bias genes (skip activations) for adapt_topology_inputs.
/// adapt_topology_inputs expects [L0_weights, L0_biases, L1_weights, L1_biases, ...]
/// but StructuredGenome::flatten("layer_") includes activation genes too.
std::vector<float> flatten_weights_and_biases(
    const evolve::StructuredGenome& genome,
    const neuralnet::NetworkTopology& topology) {

    std::vector<float> result;
    for (std::size_t l = 0; l < topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (genome.has_gene(lp + "_weights")) {
            const auto& vals = genome.gene(lp + "_weights").values;
            result.insert(result.end(), vals.begin(), vals.end());
        }
        if (genome.has_gene(lp + "_biases")) {
            const auto& vals = genome.gene(lp + "_biases").values;
            result.insert(result.end(), vals.begin(), vals.end());
        }
    }
    return result;
}

} // namespace

Individual convert_variant_to_fighter(
    const Individual& variant,
    const ShipDesign& design) {

    auto target_ids = build_arena_fighter_input_labels(design);
    auto target_output_ids = build_output_ids(design);

    auto source = variant;

    // If source has no input_ids, assign scroller-compatible IDs that share
    // naming with arena labels for overlapping channels (distance, is_tower,
    // is_token, memory).
    if (source.topology.input_ids.empty()) {
        source.topology.input_ids = build_scroller_source_ids(design);
    }
    if (source.topology.output_ids.empty()) {
        source.topology.output_ids = build_output_ids(design);
    }

    // Flatten only weights and biases (not activations) since
    // adapt_topology_inputs expects [L0_weights, L0_biases, L1_weights, ...]
    auto flat_weights = flatten_weights_and_biases(source.genome, source.topology);
    std::mt19937 rng(std::random_device{}());
    auto result = neuralnet::adapt_topology_inputs(
        source.topology, flat_weights, target_ids, rng);

    // Build adapted Individual
    Individual adapted;
    adapted.topology = result.adapted_topology;
    adapted.topology.output_ids = target_output_ids;
    adapted.genome = build_genome_skeleton(design, adapted.topology);

    // Fill genome weights from adapted flat weights
    std::size_t offset = 0;
    for (std::size_t l = 0; l < adapted.topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (adapted.genome.has_gene(lp + "_weights")) {
            for (auto& v : adapted.genome.gene(lp + "_weights").values) {
                v = (offset < result.adapted_weights.size())
                    ? result.adapted_weights[offset++] : 0.0f;
            }
        }
        if (adapted.genome.has_gene(lp + "_biases")) {
            for (auto& v : adapted.genome.gene(lp + "_biases").values) {
                v = (offset < result.adapted_weights.size())
                    ? result.adapted_weights[offset++] : 0.0f;
            }
        }
    }

    // Copy activation genes from source (they don't change with input adaptation)
    for (std::size_t l = 0; l < adapted.topology.layers.size(); ++l) {
        std::string atag = "layer_" + std::to_string(l) + "_activations";
        if (source.genome.has_gene(atag) && adapted.genome.has_gene(atag)) {
            adapted.genome.gene(atag).values = source.genome.gene(atag).values;
        }
    }

    return adapted;
}

void mutate_individual(Individual& ind, const EvolutionConfig& /*config*/, std::mt19937& rng) {
    // StructuredGenome carries per-gene mutation configs — no external config needed
    evolve::mutate(ind.genome, rng);
}

namespace {

/// Rebuild weight/bias genes after a topology change.
/// Preserves as many existing weights as possible; new weights get small random values.
void rebuild_weight_genes(Individual& ind, std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(-0.1f, 0.1f);
    std::size_t prev = ind.topology.input_size;

    for (std::size_t l = 0; l < ind.topology.layers.size(); ++l) {
        const auto& layer = ind.topology.layers[l];
        std::string lp = "layer_" + std::to_string(l);
        std::string wtag = lp + "_weights";
        std::string btag = lp + "_biases";

        std::size_t needed_w = prev * layer.output_size;
        std::size_t needed_b = layer.output_size;

        if (ind.genome.has_gene(wtag)) {
            auto& wvals = ind.genome.gene(wtag).values;
            while (wvals.size() < needed_w) wvals.push_back(dist(rng));
            wvals.resize(needed_w);
        } else {
            std::vector<float> wvals(needed_w);
            for (auto& v : wvals) v = dist(rng);
            ind.genome.add_gene({wtag, std::move(wvals), {.rate = 0.1f, .strength = 0.3f}});
        }

        if (ind.genome.has_gene(btag)) {
            auto& bvals = ind.genome.gene(btag).values;
            while (bvals.size() < needed_b) bvals.push_back(0.0f);
            bvals.resize(needed_b);
        } else {
            ind.genome.add_gene({btag, std::vector<float>(needed_b, 0.0f),
                                 {.rate = 0.1f, .strength = 0.3f}});
        }

        // Activation genes — resize to match layer output_size
        std::string atag = lp + "_activations";
        float default_act = static_cast<float>(layer.activation);
        if (ind.genome.has_gene(atag)) {
            auto& avals = ind.genome.gene(atag).values;
            while (avals.size() < needed_b) avals.push_back(default_act);
            avals.resize(needed_b);
        } else {
            ind.genome.add_gene({atag, std::vector<float>(needed_b, default_act),
                                 {.rate = 0.05f, .strength = 0.5f,
                                  .min_val = 0.0f,
                                  .max_val = static_cast<float>(neuralnet::ACTIVATION_COUNT - 1),
                                  .evolvable = false}});
        }

        // Ensure linkage group exists
        bool has_group = false;
        for (const auto& grp : ind.genome.linkage_groups()) {
            if (grp.name == lp) { has_group = true; break; }
        }
        if (!has_group) {
            ind.genome.add_linkage_group({lp, {wtag, btag, atag}});
        }

        prev = layer.output_size;
    }

    // Clean up stale genes for layers beyond the current topology
    std::size_t num_layers = ind.topology.layers.size();
    for (std::size_t l = num_layers; ; ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (!ind.genome.has_gene(lp + "_weights")) break;
        // Can't remove genes from StructuredGenome, but we can zero them out
        // so they don't pollute flatten(). Better: we never create them in
        // add_layer/remove_layer anymore (see save/restore helpers below).
        break;
    }
}

/// Data for one layer's genes — used during add_layer/remove_layer renumbering.
struct LayerGeneData {
    std::vector<float> weights;
    std::vector<float> biases;
    std::vector<float> activations;
    evolve::GeneMutationConfig weight_config;
    evolve::GeneMutationConfig bias_config;
    evolve::GeneMutationConfig act_config;
};

/// Save all layer gene data keyed by current layer index.
std::map<std::size_t, LayerGeneData> save_layer_genes(const Individual& ind) {
    std::map<std::size_t, LayerGeneData> result;
    for (std::size_t l = 0; l < ind.topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        LayerGeneData data;
        if (ind.genome.has_gene(lp + "_weights")) {
            const auto& g = ind.genome.gene(lp + "_weights");
            data.weights = g.values;
            data.weight_config = g.mutation;
        }
        if (ind.genome.has_gene(lp + "_biases")) {
            const auto& g = ind.genome.gene(lp + "_biases");
            data.biases = g.values;
            data.bias_config = g.mutation;
        }
        if (ind.genome.has_gene(lp + "_activations")) {
            const auto& g = ind.genome.gene(lp + "_activations");
            data.activations = g.values;
            data.act_config = g.mutation;
        }
        result[l] = std::move(data);
    }
    return result;
}

/// Rebuild the genome from scratch using the current topology, restoring saved
/// gene data at their new indices. New layers get fresh random weights.
void restore_layer_genes(Individual& ind,
                         const std::map<std::size_t, LayerGeneData>& saved,
                         std::mt19937& rng) {
    // Rebuild the entire genome from the new topology
    ind.genome = build_genome_skeleton(
        ind.effective_ship_design(), ind.topology);

    std::uniform_real_distribution<float> dist(-0.1f, 0.1f);
    std::size_t prev = ind.topology.input_size;

    for (std::size_t l = 0; l < ind.topology.layers.size(); ++l) {
        const auto& layer = ind.topology.layers[l];
        std::string lp = "layer_" + std::to_string(l);

        auto it = saved.find(l);
        if (it != saved.end()) {
            const auto& data = it->second;
            // Restore weights — resize to match new dimensions
            auto& wvals = ind.genome.gene(lp + "_weights").values;
            std::size_t needed_w = prev * layer.output_size;
            for (std::size_t i = 0; i < std::min(needed_w, data.weights.size()); ++i) {
                wvals[i] = data.weights[i];
            }
            // Any extra slots keep their skeleton defaults (zero)
            for (std::size_t i = data.weights.size(); i < needed_w; ++i) {
                wvals[i] = dist(rng);
            }

            // Restore biases
            auto& bvals = ind.genome.gene(lp + "_biases").values;
            for (std::size_t i = 0; i < std::min(layer.output_size, data.biases.size()); ++i) {
                bvals[i] = data.biases[i];
            }

            // Restore activations
            if (ind.genome.has_gene(lp + "_activations") && !data.activations.empty()) {
                auto& avals = ind.genome.gene(lp + "_activations").values;
                for (std::size_t i = 0; i < std::min(layer.output_size, data.activations.size()); ++i) {
                    avals[i] = data.activations[i];
                }
            }
        } else {
            // New layer — fill weights with small random values
            auto& wvals = ind.genome.gene(lp + "_weights").values;
            for (auto& v : wvals) v = dist(rng);
        }

        prev = layer.output_size;
    }
}

} // namespace

void add_node(Individual& ind, std::mt19937& rng) {
    auto num_hidden = ind.topology.layers.size() - 1;
    if (num_hidden == 0) return;

    std::uniform_int_distribution<std::size_t> layer_dist(0, num_hidden - 1);
    auto idx = layer_dist(rng);
    ind.topology.layers[idx].output_size += 1;
    rebuild_weight_genes(ind, rng);
}

void remove_node(Individual& ind, std::mt19937& rng) {
    auto num_hidden = ind.topology.layers.size() - 1;
    if (num_hidden == 0) return;

    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < num_hidden; ++i) {
        if (ind.topology.layers[i].output_size > 1) {
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) return;

    std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
    auto idx = candidates[dist(rng)];
    ind.topology.layers[idx].output_size -= 1;
    rebuild_weight_genes(ind, rng);
}

void add_layer(Individual& ind, std::mt19937& rng) {
    auto num_hidden = ind.topology.layers.size() - 1;
    std::uniform_int_distribution<std::size_t> pos_dist(0, num_hidden);
    auto pos = pos_dist(rng);

    // Save gene data keyed by OLD layer index before topology changes
    auto saved = save_layer_genes(ind);

    // Insert the new layer into topology
    neuralnet::LayerDef new_layer{.output_size = 1, .activation = neuralnet::Activation::Tanh};
    ind.topology.layers.insert(
        ind.topology.layers.begin() + static_cast<long>(pos), new_layer);

    // Remap: old layers at index >= pos shift up by 1
    std::map<std::size_t, LayerGeneData> remapped;
    for (auto& [old_idx, data] : saved) {
        std::size_t new_idx = (old_idx >= pos) ? old_idx + 1 : old_idx;
        remapped[new_idx] = std::move(data);
    }

    // Rebuild genome from new topology, restoring saved data at new indices
    restore_layer_genes(ind, remapped, rng);
}

void remove_layer(Individual& ind, std::mt19937& rng) {
    auto num_hidden = ind.topology.layers.size() - 1;
    if (num_hidden <= 1) return;

    std::uniform_int_distribution<std::size_t> dist(0, num_hidden - 1);
    auto idx = dist(rng);

    // Save gene data keyed by OLD layer index before topology changes
    auto saved = save_layer_genes(ind);
    saved.erase(idx);  // drop the removed layer's genes

    // Remove from topology
    ind.topology.layers.erase(
        ind.topology.layers.begin() + static_cast<long>(idx));

    // Remap: old layers at index > idx shift down by 1
    std::map<std::size_t, LayerGeneData> remapped;
    for (auto& [old_idx, data] : saved) {
        std::size_t new_idx = (old_idx > idx) ? old_idx - 1 : old_idx;
        remapped[new_idx] = std::move(data);
    }

    // Rebuild genome from new topology, restoring saved data at new indices
    restore_layer_genes(ind, remapped, rng);
}

void apply_mutations(Individual& ind, const EvolutionConfig& config, std::mt19937& rng) {
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    // Topology mutations (rare)
    if (chance(rng) < config.add_node_chance) add_node(ind, rng);
    if (chance(rng) < config.remove_node_chance) remove_node(ind, rng);
    if (chance(rng) < config.add_layer_chance) add_layer(ind, rng);
    if (chance(rng) < config.remove_layer_chance) remove_layer(ind, rng);

    // Gene-level mutation (weights + any evolvable params)
    evolve::mutate(ind.genome, rng);
}

std::vector<Individual> create_population(
    std::size_t input_size,
    std::vector<std::size_t> hidden_layers,
    std::size_t output_size,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    std::vector<Individual> pop;
    pop.reserve(config.population_size);
    for (std::size_t i = 0; i < config.population_size; ++i) {
        pop.push_back(Individual::random(input_size, hidden_layers, output_size, rng));
    }
    return pop;
}

std::vector<Individual> evolve_population(
    std::vector<Individual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    if (population.empty()) return {};

    // Sort by fitness descending
    std::sort(population.begin(), population.end(),
              [](const Individual& a, const Individual& b) { return a.fitness > b.fitness; });

    std::vector<Individual> next;
    next.reserve(config.population_size);

    // Elitism
    for (std::size_t i = 0; i < config.elitism_count && i < population.size(); ++i) {
        next.push_back(population[i]);  // copy
    }

    // Tournament selection + crossover/mutation
    std::uniform_int_distribution<std::size_t> idx_dist(0, population.size() - 1);

    while (next.size() < config.population_size) {
        // Tournament select parent A
        std::size_t best_a = idx_dist(rng);
        for (std::size_t t = 1; t < config.tournament_size; ++t) {
            auto candidate = idx_dist(rng);
            if (population[candidate].fitness > population[best_a].fitness) {
                best_a = candidate;
            }
        }

        Individual child;

        // Try to find a same-topology partner for crossover
        bool crossed = false;
        for (int attempt = 0; attempt < 5; ++attempt) {
            std::size_t best_b = idx_dist(rng);
            for (std::size_t t = 1; t < config.tournament_size; ++t) {
                auto candidate = idx_dist(rng);
                if (population[candidate].fitness > population[best_b].fitness) {
                    best_b = candidate;
                }
            }

            if (best_b != best_a && same_topology(population[best_a], population[best_b])) {
                child.topology = population[best_a].topology;
                // Linkage-group-aware crossover
                child.genome = evolve::crossover(
                    population[best_a].genome, population[best_b].genome, rng);
                crossed = true;
                break;
            }
        }

        if (!crossed) {
            // Asexual — copy parent
            child.topology = population[best_a].topology;
            child.genome = population[best_a].genome;  // copy
        }

        apply_mutations(child, config, rng);
        next.push_back(std::move(child));
    }

    return next;
}

Snapshot create_random_snapshot(
    const std::string& name,
    const ShipDesign& design,
    const std::vector<std::size_t>& hidden_layers,
    std::mt19937& rng) {

    auto ind = Individual::from_design(design, hidden_layers, rng);

    Snapshot snap;
    snap.name = name;
    snap.generation = 0;
    snap.created_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    snap.parent_name = "";
    snap.ship_design = design;
    snap.topology = ind.topology;
    sync_activations_from_genome(ind.genome, snap.topology);

    // Extract flat weights + biases + activations (in genome insertion order)
    snap.weights = ind.genome.flatten("layer_");

    return snap;
}

Snapshot best_as_snapshot(
    const std::string& name,
    const std::vector<Individual>& population,
    const ShipDesign& design,
    uint32_t generation) {

    if (population.empty()) {
        throw std::runtime_error("best_as_snapshot called with empty population");
    }

    auto it = std::max_element(population.begin(), population.end(),
        [](const Individual& a, const Individual& b) {
            return a.fitness < b.fitness;
        });

    Snapshot snap;
    snap.name = name;
    snap.generation = generation;
    snap.created_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    snap.parent_name = "";
    snap.ship_design = design;
    snap.topology = it->topology;
    sync_activations_from_genome(it->genome, snap.topology);

    // Extract flat weights + biases + activations (in genome insertion order)
    snap.weights = it->genome.flatten("layer_");

    return snap;
}

Individual snapshot_to_individual(const Snapshot& snap) {
    Individual ind;
    ind.topology = snap.topology;
    ind.genome = build_genome_skeleton(snap.ship_design, snap.topology);
    // Fill weight + bias + activation genes from flat snapshot weights
    std::size_t offset = 0;
    for (std::size_t l = 0; l < snap.topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (ind.genome.has_gene(lp + "_weights")) {
            for (auto& v : ind.genome.gene(lp + "_weights").values) {
                v = (offset < snap.weights.size()) ? snap.weights[offset++] : 0.0f;
            }
        }
        if (ind.genome.has_gene(lp + "_biases")) {
            for (auto& v : ind.genome.gene(lp + "_biases").values) {
                v = (offset < snap.weights.size()) ? snap.weights[offset++] : 0.0f;
            }
        }
        // Activations: only overwrite if data is available in snap.weights
        // (old files don't include activations — skeleton already has correct values from topology)
        if (ind.genome.has_gene(lp + "_activations") && offset < snap.weights.size()) {
            for (auto& v : ind.genome.gene(lp + "_activations").values) {
                v = (offset < snap.weights.size()) ? snap.weights[offset++] : v;
            }
        }
    }
    ind.fitness = 0.0f;
    return ind;
}

std::string AdaptReport::message() const {
    std::string msg;
    if (!added.empty()) {
        msg += "Adding nodes with random weights:";
        for (const auto& id : added) msg += " " + id;
        msg += "\n";
    }
    if (!removed.empty()) {
        msg += "Removing unused nodes:";
        for (const auto& id : removed) msg += " " + id;
    }
    return msg;
}

std::pair<Individual, AdaptReport> adapt_individual_inputs(
    const Individual& source,
    const std::vector<std::string>& target_input_ids,
    const ShipDesign& design,
    std::mt19937& rng) {

    // Legacy net (no IDs) or exact match -> no adaptation
    if (source.topology.input_ids.empty() || source.topology.input_ids == target_input_ids) {
        return {source, {}};
    }

    // Flatten only weights and biases (not activations) for adapt_topology_inputs
    auto flat_weights = flatten_weights_and_biases(source.genome, source.topology);
    auto result = neuralnet::adapt_topology_inputs(
        source.topology, flat_weights, target_input_ids, rng);

    // Build adapted Individual
    Individual adapted;
    adapted.topology = result.adapted_topology;
    adapted.topology.output_ids = build_output_ids(design);
    adapted.genome = build_genome_skeleton(design, adapted.topology);

    // Fill genome from adapted weights (same pattern as snapshot_to_individual)
    std::size_t offset = 0;
    for (std::size_t l = 0; l < adapted.topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (adapted.genome.has_gene(lp + "_weights")) {
            for (auto& v : adapted.genome.gene(lp + "_weights").values) {
                v = (offset < result.adapted_weights.size())
                    ? result.adapted_weights[offset++] : 0.0f;
            }
        }
        if (adapted.genome.has_gene(lp + "_biases")) {
            for (auto& v : adapted.genome.gene(lp + "_biases").values) {
                v = (offset < result.adapted_weights.size())
                    ? result.adapted_weights[offset++] : 0.0f;
            }
        }
    }

    // Copy activation genes from source (they don't change with input adaptation)
    for (std::size_t l = 0; l < adapted.topology.layers.size(); ++l) {
        std::string atag = "layer_" + std::to_string(l) + "_activations";
        if (source.genome.has_gene(atag) && adapted.genome.has_gene(atag)) {
            adapted.genome.gene(atag).values = source.genome.gene(atag).values;
        }
    }

    AdaptReport report;
    report.added = result.added_ids;
    report.removed = result.removed_ids;
    return {adapted, report};
}

std::vector<Individual> create_population_from_snapshot(
    const Snapshot& snapshot,
    std::size_t population_size,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    std::vector<Individual> pop;
    pop.reserve(population_size);

    // Build the first individual from the snapshot
    Individual first;
    first.topology = snapshot.topology;
    first.genome = build_genome_skeleton(snapshot.ship_design, snapshot.topology);

    // Fill weight + bias + activation genes from flat snapshot weights
    std::size_t offset = 0;
    for (std::size_t l = 0; l < snapshot.topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (first.genome.has_gene(lp + "_weights")) {
            auto& wvals = first.genome.gene(lp + "_weights").values;
            for (auto& v : wvals) {
                v = (offset < snapshot.weights.size()) ? snapshot.weights[offset++] : 0.0f;
            }
        }
        if (first.genome.has_gene(lp + "_biases")) {
            auto& bvals = first.genome.gene(lp + "_biases").values;
            for (auto& v : bvals) {
                v = (offset < snapshot.weights.size()) ? snapshot.weights[offset++] : 0.0f;
            }
        }
        // Activations: only overwrite if data is available in snap.weights
        // (old files don't include activations — skeleton already has correct values from topology)
        if (first.genome.has_gene(lp + "_activations") && offset < snapshot.weights.size()) {
            auto& avals = first.genome.gene(lp + "_activations").values;
            for (auto& v : avals) {
                v = (offset < snapshot.weights.size()) ? snapshot.weights[offset++] : v;
            }
        }
    }
    pop.push_back(first);

    // Remaining: mutated copies
    for (std::size_t i = 1; i < population_size; ++i) {
        Individual copy;
        copy.topology = first.topology;
        copy.genome = first.genome;  // deep copy

        // Temporarily boost mutation strength for diversity
        for (auto& gene : copy.genome.genes()) {
            if (gene.tag.find("_weights") != std::string::npos ||
                gene.tag.find("_biases") != std::string::npos) {
                gene.mutation.strength =
                    0.3f * (1.0f + static_cast<float>(i) * 0.3f);
            }
        }
        evolve::mutate(copy.genome, rng);

        // Reset mutation strengths to defaults
        for (auto& gene : copy.genome.genes()) {
            if (gene.tag.find("_weights") != std::string::npos ||
                gene.tag.find("_biases") != std::string::npos) {
                gene.mutation.strength = 0.3f;
            }
        }

        apply_mutations(copy, config, rng);
        pop.push_back(std::move(copy));
    }

    return pop;
}

} // namespace neuroflyer
