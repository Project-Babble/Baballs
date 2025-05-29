#pragma once

#ifdef _WIN32
// Helper function to convert string to wstring
static inline std::wstring to_path_string(const std::string& str) {
    std::wstring result;
    result.assign(str.begin(), str.end());
    return result;
}
#else
static inline const std::string &to_path_string(const std::string& str) {
    return str;
}
#endif
