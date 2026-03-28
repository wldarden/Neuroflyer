#pragma once

#include <string>
#include <cstdint>

namespace neuroflyer {

[[nodiscard]] std::string data_dir();
[[nodiscard]] std::string asset_dir();
[[nodiscard]] std::string format_short_date(int64_t timestamp);

} // namespace neuroflyer
