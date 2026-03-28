#pragma once
#include <neuroflyer/ship_design.h>
#include <neuralnet/network.h>
#include <cstdint>
#include <string>
#include <vector>

namespace neuroflyer {

struct Snapshot {
    std::string name;
    uint32_t generation = 0;
    int64_t created_timestamp = 0;
    std::string parent_name;
    uint32_t run_count = 0;  // how many training runs have used this variant as seed

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
};

struct GenomeInfo {
    std::string name;
    std::size_t variant_count = 0;
};

} // namespace neuroflyer
