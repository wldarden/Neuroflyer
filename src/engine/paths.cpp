#include <neuroflyer/paths.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

namespace neuroflyer {

std::string data_dir() {
    // Try relative path first (works from build dir or project root)
    if (std::filesystem::exists("neuroflyer/data"))
        return "neuroflyer/data";
    if (std::filesystem::exists("data"))
        return "data";
    // Dev mode: derive from source file location
    std::filesystem::path src_dir = std::filesystem::path(__FILE__).parent_path().parent_path();
    std::filesystem::path dev_data = src_dir / "data";
    if (std::filesystem::exists(dev_data))
        return dev_data.string();
    // Create default
    std::filesystem::create_directories("data/nets");
    return "data";
}

std::string asset_dir() {
    if (std::filesystem::exists("neuroflyer/assets"))
        return "neuroflyer/assets";
    if (std::filesystem::exists("assets"))
        return "assets";
    std::filesystem::path src_dir = std::filesystem::path(__FILE__).parent_path().parent_path();
    std::filesystem::path dev_assets = src_dir / "assets";
    if (std::filesystem::exists(dev_assets))
        return dev_assets.string();
    return "assets";
}

std::string format_short_date(int64_t timestamp) {
    if (timestamp <= 0) return "---";
    auto time = static_cast<std::time_t>(timestamp);
    std::tm tm_buf{};
    localtime_r(&time, &tm_buf);
    const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s %d", months[tm_buf.tm_mon], tm_buf.tm_mday);
    return buf;
}

} // namespace neuroflyer
