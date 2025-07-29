#pragma once

#ifdef __APPLE__
#include <string>
#include <sys/types.h>
#include <vector>

namespace lol::patcher {

class RuntimeInjector {
public:
    // Legacy runtime injection (will fail with SIP enabled)
    static bool inject_dylib_into_process(pid_t target_pid, const std::string& dylib_path, const std::string& mod_path);
    
    // New methods for environment variable injection
    static bool is_runtime_injection_available();
    static std::string find_league_executable();
    static pid_t launch_league_with_injection(const std::string& league_path, const std::string& dylib_path, const std::string& mod_path);
    
    // Unified method that chooses the best approach
    static bool inject_dylib_best_method(const std::string& dylib_path, const std::string& mod_path);
};

} // namespace lol::patcher

#endif