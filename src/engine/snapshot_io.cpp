#include <neuroflyer/snapshot_io.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace neuroflyer {

namespace {

// ---- CRC32 (public-domain table-based implementation) ----

[[nodiscard]] consteval std::array<uint32_t, 256> make_crc32_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1u)));
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto CRC32_TABLE = make_crc32_table();

[[nodiscard]] uint32_t crc32(const char* data, std::size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        auto byte = static_cast<uint8_t>(data[i]);
        crc = (crc >> 8) ^ CRC32_TABLE[(crc ^ byte) & 0xFF];
    }
    return crc ^ 0xFFFFFFFFu;
}

// ---- Binary write/read helpers ----

template <typename T>
void write_val(std::ostream& out, T val) {
    if (!out.write(reinterpret_cast<const char*>(&val), sizeof(T))) {
        throw std::runtime_error("Failed to write to stream");
    }
}

template <typename T>
[[nodiscard]] T read_val(std::istream& in) {
    T val;
    if (!in.read(reinterpret_cast<char*>(&val), sizeof(T))) {
        throw std::runtime_error("Unexpected end of stream");
    }
    return val;
}

void write_string(std::ostream& out, const std::string& s) {
    if (s.size() > 65535) {
        throw std::runtime_error("string too long for snapshot format");
    }
    auto len = static_cast<uint16_t>(s.size());
    write_val(out, len);
    if (!s.empty()) {
        if (!out.write(s.data(), static_cast<std::streamsize>(s.size()))) {
            throw std::runtime_error("Failed to write string to stream");
        }
    }
}

[[nodiscard]] std::string read_string(std::istream& in) {
    auto len = read_val<uint16_t>(in);
    std::string s(len, '\0');
    if (len > 0) {
        if (!in.read(s.data(), len)) {
            throw std::runtime_error("Unexpected end of stream reading string");
        }
    }
    return s;
}

// ---- Constants ----

constexpr uint32_t MAGIC = 0x4E465300;  // "NFS\0"
constexpr uint16_t CURRENT_VERSION = 7;
constexpr uint16_t MIN_VERSION = 1;

// ---- Payload serialization ----

void write_payload(std::ostream& out, const Snapshot& snap) {
    write_string(out, snap.name);
    write_val(out, snap.generation);
    write_val(out, snap.created_timestamp);
    write_string(out, snap.parent_name);
    write_val(out, snap.run_count);  // v5+
    // v6: paired_fighter_name
    write_string(out, snap.paired_fighter_name);
    // v7: net_type
    write_val<uint8_t>(out, static_cast<uint8_t>(snap.net_type));

    // Ship design
    write_val<uint16_t>(out, static_cast<uint16_t>(snap.ship_design.sensors.size()));
    write_val(out, snap.ship_design.memory_slots);

    for (const auto& sensor : snap.ship_design.sensors) {
        write_val(out, static_cast<uint8_t>(sensor.type));
        write_val<float>(out, sensor.angle);
        write_val<float>(out, sensor.range);
        write_val<float>(out, sensor.width);
        write_val<uint8_t>(out, sensor.is_full_sensor ? 1 : 0);
        write_val<uint16_t>(out, sensor.id);
    }

    // Evolvable flags
    uint8_t evo_bits = 0;
    if (snap.ship_design.evolvable.sensor_angle) evo_bits |= 0x01;
    if (snap.ship_design.evolvable.sensor_range) evo_bits |= 0x02;
    if (snap.ship_design.evolvable.sensor_width) evo_bits |= 0x04;
    if (snap.ship_design.evolvable.activation_function) evo_bits |= 0x08;
    write_val(out, evo_bits);

    // Topology
    write_val<uint32_t>(out, static_cast<uint32_t>(snap.topology.input_size));
    write_val<uint32_t>(out, static_cast<uint32_t>(snap.topology.layers.size()));

    for (const auto& layer : snap.topology.layers) {
        write_val<uint32_t>(out, static_cast<uint32_t>(layer.output_size));
        write_val<uint32_t>(out, static_cast<uint32_t>(layer.activation));
        // v4: per-node activations
        auto num_node_acts = static_cast<uint16_t>(layer.node_activations.size());
        write_val(out, num_node_acts);
        for (auto act : layer.node_activations) {
            write_val<uint8_t>(out, static_cast<uint8_t>(act));
        }
    }

    // Weights
    auto weight_count = static_cast<uint32_t>(snap.weights.size());
    write_val(out, weight_count);
    if (weight_count > 0) {
        if (!out.write(reinterpret_cast<const char*>(snap.weights.data()),
                       static_cast<std::streamsize>(weight_count * sizeof(float)))) {
            throw std::runtime_error("Failed to write weights to stream");
        }
    }
}

Snapshot parse_payload(std::istream& in, uint16_t version) {
    Snapshot snap;

    snap.name = read_string(in);
    snap.generation = read_val<uint32_t>(in);
    snap.created_timestamp = read_val<int64_t>(in);
    snap.parent_name = read_string(in);
    if (version >= 5) {
        snap.run_count = read_val<uint32_t>(in);
    }
    // v6: paired_fighter_name
    if (version >= 6) {
        snap.paired_fighter_name = read_string(in);
    }
    // v7: net_type
    if (version >= 7) {
        snap.net_type = static_cast<NetType>(read_val<uint8_t>(in));
    }

    // Ship design
    auto num_sensors = read_val<uint16_t>(in);
    snap.ship_design.memory_slots = read_val<uint16_t>(in);

    snap.ship_design.sensors.resize(num_sensors);
    for (auto& sensor : snap.ship_design.sensors) {
        sensor.type = static_cast<SensorType>(read_val<uint8_t>(in));
        sensor.angle = read_val<float>(in);
        sensor.range = read_val<float>(in);
        sensor.width = read_val<float>(in);
        sensor.is_full_sensor = read_val<uint8_t>(in) != 0;
        if (version >= 3) {
            sensor.id = read_val<uint16_t>(in);
        }
        // v1/v2: id stays 0 (default)
    }

    // Evolvable flags (version 2+)
    if (version >= 2) {
        auto evo_bits = read_val<uint8_t>(in);
        snap.ship_design.evolvable.sensor_angle = (evo_bits & 0x01) != 0;
        snap.ship_design.evolvable.sensor_range = (evo_bits & 0x02) != 0;
        snap.ship_design.evolvable.sensor_width = (evo_bits & 0x04) != 0;
        snap.ship_design.evolvable.activation_function = (evo_bits & 0x08) != 0;
    }
    // Version 1: evolvable defaults are all false (from EvolvableFlags default init)

    // Topology
    snap.topology.input_size = read_val<uint32_t>(in);
    auto num_layers = read_val<uint32_t>(in);

    snap.topology.layers.resize(num_layers);
    for (auto& layer : snap.topology.layers) {
        layer.output_size = read_val<uint32_t>(in);
        layer.activation = static_cast<neuralnet::Activation>(read_val<uint32_t>(in));
        if (version >= 4) {
            auto num_node_acts = read_val<uint16_t>(in);
            layer.node_activations.resize(num_node_acts);
            for (auto& act : layer.node_activations) {
                act = static_cast<neuralnet::Activation>(read_val<uint8_t>(in));
            }
        }
        // v1/v2/v3: node_activations stays empty (per-layer default)
    }

    // Weights
    auto weight_count = read_val<uint32_t>(in);
    snap.weights.resize(weight_count);
    if (weight_count > 0) {
        if (!in.read(reinterpret_cast<char*>(snap.weights.data()),
                     static_cast<std::streamsize>(weight_count * sizeof(float)))) {
            throw std::runtime_error("Unexpected end of stream reading weights");
        }
    }

    // Backfill sensor IDs for older format versions
    assign_sensor_ids(snap.ship_design);

    return snap;
}

SnapshotHeader parse_header_fields(std::istream& in, uint16_t version) {
    SnapshotHeader header;
    header.name = read_string(in);
    header.generation = read_val<uint32_t>(in);
    header.created_timestamp = read_val<int64_t>(in);
    header.parent_name = read_string(in);
    if (version >= 5) {
        header.run_count = read_val<uint32_t>(in);
    }
    // v6: paired_fighter_name
    if (version >= 6) {
        header.paired_fighter_name = read_string(in);
    }
    // v7: net_type
    if (version >= 7) {
        header.net_type = static_cast<NetType>(read_val<uint8_t>(in));
    }
    return header;
}

} // namespace

// ---- Stream-based API ----

void save_snapshot(const Snapshot& snapshot, std::ostream& out) {
    // Serialize payload to buffer first so we can compute CRC
    std::ostringstream payload_buf;
    write_payload(payload_buf, snapshot);
    std::string payload = payload_buf.str();

    uint32_t checksum = crc32(payload.data(), payload.size());

    // Write header: magic, version, crc
    write_val(out, MAGIC);
    write_val(out, CURRENT_VERSION);
    write_val(out, checksum);

    // Write payload
    if (!out.write(payload.data(), static_cast<std::streamsize>(payload.size()))) {
        throw std::runtime_error("Failed to write payload to stream");
    }
}

Snapshot load_snapshot(std::istream& in) {
    auto magic = read_val<uint32_t>(in);
    if (magic != MAGIC) {
        throw std::runtime_error("Invalid snapshot file: bad magic number");
    }

    auto version = read_val<uint16_t>(in);
    if (version < MIN_VERSION || version > CURRENT_VERSION) {
        throw std::runtime_error("Unsupported snapshot version: " + std::to_string(version));
    }

    auto stored_crc = read_val<uint32_t>(in);

    // Read all remaining bytes as payload
    std::string payload(std::istreambuf_iterator<char>(in), {});

    uint32_t computed_crc = crc32(payload.data(), payload.size());
    if (computed_crc != stored_crc) {
        throw std::runtime_error("Snapshot CRC mismatch: file is corrupted");
    }

    std::istringstream payload_stream(payload);
    return parse_payload(payload_stream, version);
}

SnapshotHeader read_snapshot_header(std::istream& in) {
    auto magic = read_val<uint32_t>(in);
    if (magic != MAGIC) {
        throw std::runtime_error("Invalid snapshot file: bad magic number");
    }

    auto version = read_val<uint16_t>(in);
    if (version < MIN_VERSION || version > CURRENT_VERSION) {
        throw std::runtime_error("Unsupported snapshot version: " + std::to_string(version));
    }

    // Skip CRC (4 bytes) — header-only read doesn't validate
    static_cast<void>(read_val<uint32_t>(in));

    return parse_header_fields(in, version);
}

// ---- File-based convenience overloads ----

void save_snapshot(const Snapshot& snapshot, const std::string& path) {
    auto tmp_path = path + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Cannot open file for writing: " + tmp_path);
        }
        save_snapshot(snapshot, out);
    }
    std::filesystem::rename(tmp_path, path);
}

Snapshot load_snapshot(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open file for reading: " + path);
    }
    return load_snapshot(in);
}

SnapshotHeader read_snapshot_header(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open file for reading: " + path);
    }
    return read_snapshot_header(in);
}

} // namespace neuroflyer
