#include "LauncherSettings.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <vector>

namespace launcher {
namespace {
template<class C> using Text = std::basic_string<C>;
template<class C> using View = std::basic_string_view<C>;
template<class C> Text<C> Ascii(std::string_view value) {
    if constexpr (sizeof(C)==1) return Text<C>(value.begin(),value.end());
    else {
        const auto size=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
        std::wstring wide(size,L'\0');
        MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),wide.data(),size);
        return Text<C>(wide.begin(),wide.end());
    }
}
template<class C> View<C> Trim(View<C> value) {
    const auto space = [](C c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!value.empty() && space(value.front())) value.remove_prefix(1);
    while (!value.empty() && space(value.back())) value.remove_suffix(1);
    return value;
}
template<class C> bool Same(View<C> value, std::string_view expected) {
    if (value.size() != expected.size()) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto c = value[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        auto other = expected[i];
        if (other >= 'A' && other <= 'Z') other += 'a' - 'A';
        if (c != other) return false;
    }
    return true;
}
struct Line { std::size_t begin, end, next; };
template<class C> std::vector<Line> Lines(const Text<C>& text) {
    std::vector<Line> result;
    std::size_t pos = 0;
    if constexpr (sizeof(C) == 1) {
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) pos = 3;
    } else if (!text.empty() && text[0] == 0xFEFF) pos = 1;
    while (pos < text.size()) {
        auto end = text.find(static_cast<C>('\n'), pos);
        const auto next = end == Text<C>::npos ? text.size() : end + 1;
        if (end == Text<C>::npos) end = text.size();
        if (end > pos && text[end - 1] == '\r') --end;
        result.push_back({pos, end, next});
        pos = next;
    }
    return result;
}
template<class C> Text<C> ReadValue(const Text<C>& text, std::string_view section, std::string_view key) {
    bool inside = false;
    for (const auto line : Lines(text)) {
        auto content = Trim(View<C>(text).substr(line.begin, line.end - line.begin));
        if (content.empty() || content.front() == ';' || content.front() == '#') continue;
        if (content.front() == '[') {
            const auto end = content.find(static_cast<C>(']'));
            inside = end != View<C>::npos && Same(Trim(content.substr(1, end - 1)), section);
            continue;
        }
        const auto equals = content.find(static_cast<C>('='));
        if (inside && equals != View<C>::npos && Same(Trim(content.substr(0, equals)), key)) {
            auto value = content.substr(equals + 1);
            const auto comment = value.find(static_cast<C>(';'));
            if (comment != View<C>::npos) value = value.substr(0, comment);
            const auto slash = value.find(Ascii<C>("//"));
            if (slash != View<C>::npos) value = value.substr(0, slash);
            return Text<C>(Trim(value));
        }
    }
    return {};
}
template<class C> void SetValue(Text<C>& text, std::string_view section, std::string_view key, std::string_view value) {
    const auto spans = Lines(text);
    const auto newline = text.find(Ascii<C>("\r\n")) != Text<C>::npos ? Ascii<C>("\r\n") : Ascii<C>("\n");
    bool inside = false, foundSection = false;
    std::size_t insert = text.size();
    struct Edit { std::size_t begin, length; Text<C> value; };
    std::vector<Edit> edits;
    for (const auto line : spans) {
        const auto raw = View<C>(text).substr(line.begin, line.end - line.begin);
        const auto trimmed = Trim(raw);
        if (!trimmed.empty() && trimmed.front() == '[') {
            if (inside) insert = line.begin;
            const auto end = trimmed.find(static_cast<C>(']'));
            inside = end != View<C>::npos && Same(Trim(trimmed.substr(1, end - 1)), section);
            if (inside) { foundSection = true; insert = line.next; }
            continue;
        }
        if (!inside) continue;
        insert = line.next;
        if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#') continue;
        const auto equals = raw.find(static_cast<C>('='));
        if (equals == View<C>::npos || !Same(Trim(raw.substr(0, equals)), key)) continue;
        auto begin = equals + 1;
        while (begin < raw.size() && (raw[begin] == ' ' || raw[begin] == '\t')) ++begin;
        auto end = begin;
        while (end < raw.size() && raw[end] != ';' &&
               !(raw[end] == '/' && end + 1 < raw.size() && raw[end + 1] == '/')) ++end;
        while (end > begin && (raw[end - 1] == ' ' || raw[end - 1] == '\t')) --end;
        edits.push_back({line.begin + begin, end - begin, Ascii<C>(value)});
    }
    if (!edits.empty()) {
        for (auto it = edits.rbegin(); it != edits.rend(); ++it) text.replace(it->begin, it->length, it->value);
        return;
    }
    Text<C> added;
    if (!foundSection) {
        insert = text.size();
        if (!text.empty() && text.back() != '\n') added += newline;
        added += Ascii<C>("[") + Ascii<C>(section) + Ascii<C>("]") + newline;
    } else if (insert && text[insert - 1] != '\n') added += newline;
    added += Ascii<C>(key) + Ascii<C>("=") + Ascii<C>(value) + newline;
    text.insert(insert, added);
}
bool Utf16(std::string_view bytes) {
    return bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE;
}
std::u16string Decode16(std::string_view bytes) {
    std::u16string text;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2)
        text.push_back(static_cast<char16_t>(static_cast<unsigned char>(bytes[i]) |
            (static_cast<unsigned char>(bytes[i + 1]) << 8)));
    return text;
}
template<class C> Settings Parse(const Text<C>& text) {
    Settings settings;
    settings.enabled = !Same(View<C>(ReadValue(text, "Camera", "EnableFirstPersonNarrative")), "0");
    const auto binding = [&](std::string_view section, std::string_view key, std::string_view fallback) {
        const auto value = ReadValue(text, section, key);
        if (value.empty()) return std::string(fallback);
        if constexpr (sizeof(C) == 1) return std::string(value.begin(), value.end());
        else {
            const std::wstring wide(value.begin(), value.end());
            const int size = WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
            std::string output(size,'\0');
            WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),output.data(),size,nullptr,nullptr);
            return output;
        }
    };
    settings.switchCamera = binding("Input", "toggleKeyboardButton",
        binding("Camera", "toggleKeyboardButton", binding("Camera", "tuggleKeyboardButton", "F6")));
    settings.tiltRight = binding("Input", "turnback_right_btn", "MOUSE_FORWARD");
    settings.tiltLeft = binding("Input", "turnback_left_btn", "MOUSE_BACK");
    const auto value = ReadValue(text, "Camera", "CameraMotion");
    std::string narrow;
    for (const auto c : value) { if (c < '0' || c > '9') return settings; narrow += static_cast<char>(c); }
    int number{};
    const auto result = std::from_chars(narrow.data(), narrow.data() + narrow.size(), number);
    if (result.ec == std::errc{} && result.ptr == narrow.data() + narrow.size()) settings.sensitive = std::clamp(number, 0, 100);
    return settings;
}
template<class C> void Update(Text<C>& text, Settings settings) {
    const float motion = static_cast<float>(std::clamp(settings.sensitive, 0, 100)) / 100.0F;
    const auto lerp = [motion](float atZero, float atHundred) noexcept {
        return atZero + (atHundred - atZero) * motion;
    };
    const auto setFloat = [&](std::string_view key, float value) {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%.3f", value);
        SetValue(text, "Camera", key, buffer);
    };
    SetValue(text, "Camera", "EnableFirstPersonNarrative", settings.enabled ? "1" : "0");
    SetValue(text, "Camera", "CameraMotion", std::to_string(std::clamp(settings.sensitive, 0, 100)));
    // Persist the effective values. The DLL reads these values directly at game start.
    setFloat("HeadShakeXIntence", lerp(0.020F, 0.100F));
    setFloat("aim_HeadShakeXIntence", 0.020F);
    setFloat("HeadShakeYIntence", lerp(0.102F, 0.400F));
    setFloat("aim_HeadShakeYIntence", lerp(0.010F, 0.020F));
    setFloat("HeadShakeZIntence", lerp(0.102F, 0.400F));
    setFloat("aim_HeadShakeZIntence", lerp(0.010F, 0.020F));
    setFloat("alan_wake_aim_horizontal_rotate", 0.000F);
    setFloat("alan_wake_aim_vertical_rotate", lerp(0.100F, -0.100F));
    setFloat("tim_breaker_aim_horizontal_rotate", 0.000F);
    setFloat("tim_breaker_aim_vertical_rotate", lerp(0.100F, -0.100F));
    setFloat("jesse_faden_aim_horizontal_rotate", 0.000F);
    setFloat("jesse_faden_aim_vertical_rotate", lerp(0.100F, -0.100F));
    setFloat("janina_gavankar_aim_horizontal_rotate", 0.000F);
    setFloat("janina_gavankar_aim_vertical_rotate", lerp(0.100F, -0.100F));
    if (!settings.switchCamera.empty()) SetValue(text, "Input", "toggleKeyboardButton", settings.switchCamera);
    if (!settings.tiltRight.empty()) SetValue(text, "Input", "turnback_right_btn", settings.tiltRight);
    if (!settings.tiltLeft.empty()) SetValue(text, "Input", "turnback_left_btn", settings.tiltLeft);
}
std::wstring Error(std::wstring_view action, DWORD code) {
    wchar_t* message{};
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    std::wstring result(action);
    result += L": ";
    result += message ? message : std::to_wstring(code);
    if (message) LocalFree(message);
    return result;
}
}
Settings ReadSettings(std::string_view bytes) {
    return Utf16(bytes) ? Parse(Decode16(bytes)) : Parse(std::string(bytes));
}
std::string UpdateSettings(std::string_view bytes, Settings settings) {
    if (Utf16(bytes)) {
        auto text = Decode16(bytes); Update(text, settings);
        std::string output;
        for (const auto unit : text) { output += static_cast<char>(unit & 255); output += static_cast<char>(unit >> 8); }
        return output;
    }
    std::string output(bytes); Update(output, settings); return output;
}
bool ReadFile(const std::filesystem::path& path, std::string& bytes, std::wstring& error) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = Error(L"Cannot read settings", GetLastError()); return false; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(file); error = L"Settings file is too large or unreadable."; return false;
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart)); DWORD got{};
    const bool ok = ::ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr) && got == bytes.size();
    const auto code = GetLastError(); CloseHandle(file);
    if (!ok) error = Error(L"Cannot read settings", code);
    return ok;
}
bool WriteFileAtomic(const std::filesystem::path& path, std::string_view bytes, std::wstring& error) {
    static std::atomic<unsigned> serial{};
    auto temporary = path;
    temporary += L".launcher-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) +
        L"-" + std::to_wstring(serial.fetch_add(1)) + L".tmp";
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = Error(L"Cannot save settings", GetLastError()); return false; }
    DWORD written{};
    bool ok = ::WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size() && FlushFileBuffers(file);
    auto code = GetLastError(); CloseHandle(file);
    if (ok) { ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE; code = GetLastError(); }
    if (!ok) { DeleteFileW(temporary.c_str()); error = Error(L"Cannot save settings", code); }
    return ok;
}
bool LoadCurrentSettings(const std::filesystem::path& path, std::string_view defaults,
                         std::string& bytes, std::wstring& error) {
    const auto matchesVersion = [](std::string_view data) {
        if (Utf16(data)) return Same(View<char16_t>(ReadValue(Decode16(data), "Camera", "version")), kVersion);
        return Same(View<char>(ReadValue(std::string(data), "Camera", "version")), kVersion);
    };
    if (!matchesVersion(defaults)) {
        error = L"Bundled settings version does not match the launcher version.";
        return false;
    }
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (!ReadFile(path, bytes, error)) return false;
        if (matchesVersion(bytes)) return true;
    } else {
        const auto code = GetLastError();
        if (code != ERROR_FILE_NOT_FOUND) { error = Error(L"Cannot open settings", code); return false; }
    }
    if (!WriteFileAtomic(path, defaults, error)) return false;
    bytes.assign(defaults);
    return true;
}
bool SaveSettings(const std::filesystem::path& path, Settings settings,
                  std::string_view defaults, bool resetAll, std::wstring& error) {
    std::string bytes;
    const auto attributes = GetFileAttributesW(path.c_str());
    if (!resetAll && attributes != INVALID_FILE_ATTRIBUTES) {
        if (!ReadFile(path, bytes, error)) return false;
    } else if (!resetAll && GetLastError() != ERROR_FILE_NOT_FOUND) {
        error = Error(L"Cannot open settings", GetLastError()); return false;
    } else bytes = defaults;
    return WriteFileAtomic(path, UpdateSettings(bytes, settings), error);
}
}
