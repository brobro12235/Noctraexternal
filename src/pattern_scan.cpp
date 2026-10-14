#include "pattern_scan.hpp"
#include <Psapi.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace noctra {
namespace pattern {

std::vector<PatternByte> parse_pattern(const std::string& pattern_str)
{
    std::vector<PatternByte> bytes;
    size_t i = 0;
    while (i < pattern_str.size()) {
        if (pattern_str[i] == '?' || pattern_str[i] == '?') {
            // single byte wildcard
            bytes.push_back({0, true});
            ++i;
            // skip possible extra ? if pattern uses ?? for byte wildcard
            if (i < pattern_str.size() && pattern_str[i] == '?')
                ++i;
        } else {
            // need two hex digits
            if (i + 1 >= pattern_str.size())
                throw std::invalid_argument("Invalid pattern string: incomplete hex byte");
            std::string hex = pattern_str.substr(i, 2);
            uint8_t val;
            std::istringstream iss(hex);
            iss >> std::hex >> val;
            bytes.push_back({val, false});
            i += 2;
        }
    }
    return bytes;
}

uintptr_t scan_module(HMODULE module_handle, const std::vector<PatternByte>& pattern)
{
    if (!module_handle)
        return 0;

    // Get module information
    MODULEINFO modinfo{};
    if (!GetModuleInformation(GetCurrentProcess(), module_handle, &modinfo, sizeof(modinfo)))
        return 0;

    const uint8_t* start = static_cast<const uint8_t*>(modinfo.lpBaseOfDll);
    const size_t size = modinfo.SizeOfImage;

    if (pattern.empty())
        return 0;

    for (size_t offset = 0; offset < size; ++offset) {
        bool match = true;
        for (size_t p = 0; p < pattern.size(); ++p) {
            if (offset + p >= size) {
                match = false;
                break;
            }
            const uint8_t byte = start[offset + p];
            if (!pattern[p].wildcard && byte != pattern[p].value) {
                match = false;
                break;
            }
        }
        if (match)
            return reinterpret_cast<uintptr_t>(start) + offset;
    }
    return 0;
}

uintptr_t scan_exe(const std::string& pattern_str)
{
    auto pat = parse_pattern(pattern_str);
    return scan_module(GetModuleHandleA(nullptr), pat);
}

} // namespace pattern
} // namespace noctra