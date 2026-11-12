#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace launcher {
inline constexpr std::string_view kVersion = "1.0.1";
struct Settings {
    bool enabled = true;
    int sensitive = 50; // [Camera] CameraMotion, displayed by the launcher slider.
    std::string switchCamera;
    std::string tiltRight;
    std::string tiltLeft;
    bool operator==(const Settings&) const = default;
};
Settings ReadSettings(std::string_view bytes);
std::string UpdateSettings(std::string_view bytes, Settings settings);
bool ReadFile(const std::filesystem::path& path, std::string& bytes, std::wstring& error);
bool WriteFileAtomic(const std::filesystem::path& path, std::string_view bytes, std::wstring& error);
bool LoadCurrentSettings(const std::filesystem::path& path, std::string_view defaults,
                         std::string& bytes, std::wstring& error);
bool SaveSettings(const std::filesystem::path& path, Settings settings,
                  std::string_view defaults, bool resetAll, std::wstring& error);
}
