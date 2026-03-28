#pragma once
#include <neuroflyer/snapshot.h>
#include <iosfwd>
#include <string>

namespace neuroflyer {

void save_snapshot(const Snapshot& snapshot, std::ostream& out);
[[nodiscard]] Snapshot load_snapshot(std::istream& in);
[[nodiscard]] SnapshotHeader read_snapshot_header(std::istream& in);

// File-based convenience overloads
void save_snapshot(const Snapshot& snapshot, const std::string& path);
[[nodiscard]] Snapshot load_snapshot(const std::string& path);
[[nodiscard]] SnapshotHeader read_snapshot_header(const std::string& path);

} // namespace neuroflyer
