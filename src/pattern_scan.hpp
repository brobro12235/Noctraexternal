#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>

namespace noctra {
namespace pattern {

// Represents a byte with a wildcard (0xFF means ignore)
struct PatternByte {
    uint8_t value;
    bool wildcard;
};

// Convert a pattern string like "xx??xxxx" to vector<PatternByte>
// 'x' or 'X' means match the following two hex digits.
// '?' means wildcard.
std::vector<PatternByte> parse_pattern(const std::string& pattern_str);

// Scan a module for the given pattern. Returns the first match address or 0.
// module_handle: handle to the module (GetModuleHandleA can be used).
// pattern: parsed pattern bytes.
// Returns RVA (relative virtual address) if found, else 0.
uintptr_t scan_module(HMODULE module_handle, const std::vector<PatternByte>& pattern);

// Convenience: scan the main executable for a pattern string.
uintptr_t scan_exe(const std::string& pattern_str);

} // namespace pattern
} // namespace noctra