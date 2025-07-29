#ifdef __APPLE__
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <thread>
#include <mach-o/dyld.h>
#include "utility/delay.hpp"
#include "utility/process.hpp"
#include "runtime_injector_macos.hpp"
// do not reorder
#include <lol/error.hpp>
#include <lol/patcher/patcher.hpp>
#include <lol/fs.hpp>

using namespace lol;
using namespace lol::patcher;
using namespace std::chrono_literals;

struct Context {
    std::string mod_path;
    std::string dylib_path;
    bool injection_attempted = false;
    bool using_launch_method = false;
    pid_t managed_league_pid = 0;
    
    auto set_mod_path(std::filesystem::path const& profile_path) -> void {
        mod_path = std::filesystem::absolute(profile_path.lexically_normal()).generic_string();
        if (!mod_path.ends_with('/')) {
            mod_path.push_back('/');
        }
    }
    
    auto build_interception_dylib() -> void {
        // Get the directory where mod-tools is actually located
        char path[1024];
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) == 0) {
            auto exe_path = std::filesystem::path(path);
            auto exe_dir = exe_path.parent_path();
            dylib_path = exe_dir / "libcslol_interception.dylib";
        } else {
            dylib_path = "./libcslol_interception.dylib";
        }
        
        // Check if dylib exists
        if (!std::filesystem::exists(dylib_path)) {
            throw std::runtime_error("Interception dylib not found: " + dylib_path);
        }
        
        printf("[CSLOL] Using dylib: %s\n", dylib_path.c_str());
    }
    
    auto wait_for_league_or_launch() -> uint32_t {
        printf("[CSLOL] Looking for existing League process...\n");
        
        // Check if League is already running
        auto existing_pid = Process::FindPid("/LeagueofLegends");
        if (existing_pid) {
            printf("[CSLOL] Found existing League process (PID: %d)\n", existing_pid);
            return existing_pid;
        }
        
        printf("[CSLOL] No existing League process found\n");
        
        // Check if we can do runtime injection
        if (!RuntimeInjector::is_runtime_injection_available()) {
            printf("[CSLOL] Runtime injection not available - will launch League with injection\n");
            using_launch_method = true;
            
            // Launch League with our dylib
            managed_league_pid = RuntimeInjector::launch_league_with_injection(
                RuntimeInjector::find_league_executable(), 
                dylib_path, 
                mod_path
            );
            
            if (managed_league_pid > 0) {
                printf("[CSLOL] Successfully launched League with injection (PID: %d)\n", managed_league_pid);
                return managed_league_pid;
            } else {
                printf("[CSLOL] Failed to launch League with injection\n");
                throw std::runtime_error("Failed to launch League with injection");
            }
        } else {
            printf("[CSLOL] Runtime injection available - waiting for League to start...\n");
            
            // Wait for League to start normally
            for (;;) {
                auto pid = Process::FindPid("/LeagueofLegends");
                if (pid) {
                    return pid;
                }
                std::this_thread::sleep_for(100ms);
            }
        }
    }
    
    auto attempt_injection(uint32_t pid) -> bool {
        if (injection_attempted) {
            return false; // Don't try again
        }
        
        injection_attempted = true;
        
        if (using_launch_method) {
            printf("[CSLOL] League was launched with injection - no runtime injection needed\n");
            
            // Just verify that our dylib is loaded
            std::this_thread::sleep_for(2s);
            std::string lsof_cmd = "lsof -p " + std::to_string(pid) + " 2>/dev/null | grep cslol";
            int result = system(lsof_cmd.c_str());
            if (result == 0) {
                printf("[CSLOL] ✅ CSLOL dylib is loaded in League process!\n");
                return true;
            } else {
                printf("[CSLOL] ⚠️  Could not verify dylib loading\n");
                return false;
            }
        } else {
            printf("[CSLOL] Attempting runtime injection into League process (PID: %d)\n", pid);
            
            bool success = RuntimeInjector::inject_dylib_into_process(pid, dylib_path, mod_path);
            
            if (success) {
                printf("[CSLOL] Runtime injection successful!\n");
                
                // Give it a moment to take effect
                std::this_thread::sleep_for(1s);
                
                // Verify injection worked by checking if our dylib is loaded
                std::string cmd = "lsof -p " + std::to_string(pid) + " | grep cslol";
                int result = system(cmd.c_str());
                if (result == 0) {
                    printf("[CSLOL] Verified: dylib is loaded in target process!\n");
                } else {
                    printf("[CSLOL] Warning: Could not verify dylib loading\n");
                }
                
            } else {
                printf("[CSLOL] Runtime injection failed!\n");
                printf("[CSLOL] This is expected with SIP enabled\n");
                print_manual_instructions();
            }
            
            return success;
        }
    }
    
    auto print_manual_instructions() -> void {
        printf("\n=== CSLOL Manager - Manual Setup Instructions ===\n");
        printf("Runtime injection failed due to system security settings.\n");
        printf("To use CSLOL Manager:\n\n");
        printf("Option 1 - Close League and restart through CSLOL Manager:\n");
        printf("1. Close League of Legends completely\n");
        printf("2. Start a new game through CSLOL Manager\n");
        printf("   (CSLOL Manager will launch League with mods enabled)\n\n");
        printf("Option 2 - Manual environment setup (Advanced):\n");
        printf("1. Close League completely\n");
        printf("2. Run these commands in Terminal:\n");
        printf("   export DYLD_INSERT_LIBRARIES=\"%s\"\n", dylib_path.c_str());
        printf("   export CSLOL_MOD_PATH=\"%s\"\n", mod_path.c_str());
        printf("3. Launch League from the same Terminal:\n");
        printf("   \"/Applications/League of Legends.app/Contents/LoL/Game/League of Legends\"\n");
        printf("4. To remove later, close Terminal or run:\n");
        printf("   unset DYLD_INSERT_LIBRARIES CSLOL_MOD_PATH\n");
        printf("==================================================\n\n");
    }
    
    auto monitor_league(uint32_t initial_pid) -> void {
        uint32_t current_pid = initial_pid;
        
        for (;;) {
            auto pid = Process::FindPid("/LeagueofLegends");
            if (!pid) {
                printf("[CSLOL] League process exited\n");
                injection_attempted = false; // Reset for next time
                using_launch_method = false;
                managed_league_pid = 0;
                break;
            }
            
            // Check if League restarted (new PID)
            if (pid != current_pid) {
                printf("[CSLOL] League restarted with new PID: %d\n", pid);
                current_pid = pid;
                injection_attempted = false; // Reset injection flag
                
                // If we were managing the previous process, we need to launch again
                if (using_launch_method) {
                    printf("[CSLOL] Previous managed League process ended, launching new one...\n");
                    managed_league_pid = RuntimeInjector::launch_league_with_injection(
                        RuntimeInjector::find_league_executable(), 
                        dylib_path, 
                        mod_path
                    );
                    if (managed_league_pid > 0) {
                        current_pid = managed_league_pid;
                    }
                } else {
                    // Wait a moment for League to fully initialize
                    std::this_thread::sleep_for(2s);
                    
                    // Attempt injection into new process
                    attempt_injection(current_pid);
                }
            }
            
            std::this_thread::sleep_for(1s);
        }
    }
};

auto patcher::run(std::function<void(Message, char const*)> update,
                  std::filesystem::path const& profile_path,
                  std::filesystem::path const& config_path,
                  std::filesystem::path const& game_path,
                  lol::fs::names const& opts) -> void {
    auto ctx = Context{};
    ctx.set_mod_path(profile_path);
    
    try {
        // Build/locate the interception dylib
        ctx.build_interception_dylib();
        
        printf("[CSLOL] CSLOL Manager - Smart Injection Mode\n");
        printf("[CSLOL] Mod path: %s\n", ctx.mod_path.c_str());
        
        // Check system capabilities
        if (RuntimeInjector::is_runtime_injection_available()) {
            printf("[CSLOL] Runtime injection available\n");
        } else {
            printf("[CSLOL] Runtime injection blocked - will use launch method\n");
        }
        
        for (;;) {
            update(M_WAIT_START, "");
            printf("[CSLOL] Waiting for League of Legends...\n");
            
            auto pid = ctx.wait_for_league_or_launch();
            update(M_FOUND, "");
            printf("[CSLOL] League process ready (PID: %d)\n", pid);
            
            // Wait a moment for League to fully initialize
            if (!ctx.using_launch_method) {
                std::this_thread::sleep_for(2s);
            }
            
            update(M_SCAN, "");
            update(M_PATCH, "");
            
            // Attempt injection (or verify if using launch method)
            ctx.attempt_injection(pid);
            
            update(M_WAIT_EXIT, "");
            printf("[CSLOL] Monitoring League process...\n");
            
            // Monitor League process
            ctx.monitor_league(pid);
            
            update(M_DONE, "");
        }
    } catch (std::exception const& e) {
        printf("[CSLOL] Error: %s\n", e.what());
        throw;
    }
}

#endif