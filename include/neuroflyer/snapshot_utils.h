#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/snapshot.h>

namespace neuroflyer {

/// Load a variant snapshot as an Individual (for viewer/test bench).
[[nodiscard]] Individual snapshot_to_individual(const Snapshot& snap);

} // namespace neuroflyer
