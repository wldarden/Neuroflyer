#pragma once
#include <neuroflyer/ship_design.h>
#include <neuralnet/network.h>
#include <cstdint>
#include <string>
#include <vector>

namespace neuroflyer {

/// Identifies the type of neural network stored in a snapshot.
/// Determines which input/output labels the net viewer displays.
enum class NetType : uint8_t {
    Solo = 0,        // Original scroller game variant (sensors + position + memory)
    Fighter = 1,     // Arena fighter (sensors + squad leader inputs + memory)
    SquadLeader = 2  // Squad leader net (strategic inputs -> tactical orders)
};

struct Snapshot {
    std::string name;
    uint32_t generation = 0;
    int64_t created_timestamp = 0;
    std::string parent_name;
    uint32_t run_count = 0;  // how many training runs have used this variant as seed
    std::string paired_fighter_name;  // v6: fighter variant this squad net was trained with (empty for fighter snapshots)
    NetType net_type = NetType::Solo;  // v7: network type for label selection

    ShipDesign ship_design;
    neuralnet::NetworkTopology topology;
    std::vector<float> weights;
};

struct SnapshotHeader {
    std::string name;
    uint32_t generation = 0;
    int64_t created_timestamp = 0;
    std::string parent_name;
    uint32_t run_count = 0;
    std::string paired_fighter_name;  // v6: fighter variant this squad net was trained with (empty for fighter snapshots)
    NetType net_type = NetType::Solo;  // v7: network type for label selection
};

struct GenomeInfo {
    std::string name;
    std::size_t variant_count = 0;
};

} // namespace neuroflyer
