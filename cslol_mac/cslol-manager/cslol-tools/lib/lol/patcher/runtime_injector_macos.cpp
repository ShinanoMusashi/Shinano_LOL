#ifdef __APPLE__
#include "runtime_injector_macos.hpp"
#include <mach/mach.h>
#include <mach/task.h>
#include <mach/thread_act.h>
#include <mach/vm_map.h>
#include <mach/mach_error.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <libproc.h>

extern char **environ;

namespace lol::patcher {

// Check if SIP is enabled and runtime injection is blocked
bool RuntimeInjector::is_runtime_injection_available() {
    // Try to check SIP status
    FILE* sip_check = popen("csrutil status 2>/dev/null", "r");
    if (sip_check) {
        char buffer[256];
        bool sip_enabled = false;
        while (fgets(buffer, sizeof(buffer), sip_check)) {
            if (strstr(buffer, "enabled")) {
                sip_enabled = true;
                break;
            }
        }
        pclose(sip_check);
        
        if (sip_enabled) {
            printf("[CSLOL] SIP is enabled - runtime injection not available\n");
            return false;
        }
    }
    
    printf("[CSLOL] SIP appears to be disabled - runtime injection may work\n");
    return true;
}

// Find League of Legends executable path
std::string RuntimeInjector::find_league_executable() {
    // Look for common League locations - updated with correct path structure
    std::vector<std::string> possible_paths = {
        "/Applications/League of Legends.app/Contents/LoL/Game/LeagueofLegends.app/Contents/MacOS/LeagueofLegends",
        "/Applications/League of Legends.app/Contents/LoL/Game/League of Legends.app/Contents/MacOS/League of Legends",
        "/Applications/League of Legends.app/Contents/LoL/Game/LeagueofLegends", // Legacy fallback
    };
    
    for (const auto& path : possible_paths) {
        printf("[CSLOL] Checking: %s\n", path.c_str());
        if (access(path.c_str(), X_OK) == 0) {
            printf("[CSLOL] ✅ Found League executable: %s\n", path.c_str());
            return path;
        }
    }
    
    // If not found, try to search dynamically
    printf("[CSLOL] Standard paths not found, searching League installation...\n");
    
    // Check if the main League app exists
    std::string main_app = "/Applications/League of Legends.app/Contents/LoL/Game";
    if (access(main_app.c_str(), F_OK) == 0) {
        // Look for any executable named LeagueofLegends
        FILE* find_cmd = popen("find '/Applications/League of Legends.app/Contents/LoL/Game' -name 'LeagueofLegends' -type f -perm +111 2>/dev/null", "r");
        if (find_cmd) {
            char found_path[PATH_MAX];
            if (fgets(found_path, sizeof(found_path), find_cmd)) {
                // Remove newline
                found_path[strcspn(found_path, "\n")] = 0;
                pclose(find_cmd);
                printf("[CSLOL] ✅ Found League executable via search: %s\n", found_path);
                return std::string(found_path);
            }
            pclose(find_cmd);
        }
    }
    
    printf("[CSLOL] ❌ Could not find League executable\n");
    return "";
}

// Launch League with environment variable injection
pid_t RuntimeInjector::launch_league_with_injection(const std::string& league_path, 
                                                   const std::string& dylib_path, 
                                                   const std::string& mod_path) {
    printf("[CSLOL] Launching League through Riot Client with environment injection\n");
    printf("[CSLOL] Dylib path: %s\n", dylib_path.c_str());
    printf("[CSLOL] Mod path: %s\n", mod_path.c_str());
    
    // Find Riot Client executable
    std::string riot_client_path = "/Users/Shared/Riot Games/Riot Client.app/Contents/MacOS/RiotClientServices";
    
    // Check alternative locations for Riot Client
    std::vector<std::string> riot_paths = {
        "/Users/Shared/Riot Games/Riot Client.app/Contents/MacOS/RiotClientServices",
        "/Applications/Riot Client.app/Contents/MacOS/RiotClientServices",
        "/Users/" + std::string(getenv("USER")) + "/Applications/Riot Client.app/Contents/MacOS/RiotClientServices"
    };
    
    std::string found_riot_path;
    for (const auto& path : riot_paths) {
        printf("[CSLOL] Checking Riot Client: %s\n", path.c_str());
        if (access(path.c_str(), X_OK) == 0) {
            found_riot_path = path;
            printf("[CSLOL] ✅ Found Riot Client: %s\n", path.c_str());
            break;
        }
    }
    
    if (found_riot_path.empty()) {
        printf("[CSLOL] ❌ Could not find Riot Client executable\n");
        printf("[CSLOL] Please ensure Riot Client is installed\n");
        return 0;
    }
    
    // Verify dylib exists and check architecture
    if (access(dylib_path.c_str(), R_OK) != 0) {
        printf("[CSLOL] ❌ Dylib not found or not readable: %s\n", dylib_path.c_str());
        return 0;
    }
    
    // Check dylib architecture
    std::string arch_check = "file '" + dylib_path + "' | grep -q x86_64";
    int arch_result = system(arch_check.c_str());
    if (arch_result != 0) {
        printf("[CSLOL] ⚠️  WARNING: Dylib may not be x86_64 architecture\n");
        printf("[CSLOL] League runs under Rosetta and requires x86_64 dylibs\n");
        printf("[CSLOL] Compile your dylib with: gcc -arch x86_64 -shared -fPIC ...\n");
    } else {
        printf("[CSLOL] ✅ Dylib appears to be x86_64 compatible\n");
    }
    
    // Get absolute paths
    char abs_riot_path[PATH_MAX];
    char abs_dylib_path[PATH_MAX];
    
    if (!realpath(found_riot_path.c_str(), abs_riot_path)) {
        printf("[CSLOL] ❌ Failed to resolve Riot Client path\n");
        return 0;
    }
    
    if (!realpath(dylib_path.c_str(), abs_dylib_path)) {
        printf("[CSLOL] ❌ Failed to resolve dylib path\n");
        return 0;
    }
    
    // Kill any existing League/Riot processes first
    printf("[CSLOL] Terminating existing League/Riot processes...\n");
    system("pkill -f 'League' 2>/dev/null || true");
    system("pkill -f 'Riot' 2>/dev/null || true");
    sleep(2); // Give processes time to terminate
    
    // Set up environment variables
    std::string dyld_insert = "DYLD_INSERT_LIBRARIES=" + std::string(abs_dylib_path);
    std::string cslol_mod_path = "CSLOL_MOD_PATH=" + mod_path;
    
    printf("[CSLOL] Setting environment:\n");
    printf("[CSLOL]   %s\n", dyld_insert.c_str());
    printf("[CSLOL]   %s\n", cslol_mod_path.c_str());
    
    // Count existing environment variables
    int env_count = 0;
    while (environ[env_count]) env_count++;
    
    // Create new environment array
    char** new_env = new char*[env_count + 3]; // +2 for our vars, +1 for null
    
    // Copy existing environment
    for (int i = 0; i < env_count; i++) {
        new_env[i] = environ[i];
    }
    
    // Add our variables
    new_env[env_count] = const_cast<char*>(dyld_insert.c_str());
    new_env[env_count + 1] = const_cast<char*>(cslol_mod_path.c_str());
    new_env[env_count + 2] = nullptr;
    
    // Set up argv for Riot Client with League launch parameters
    char* argv[] = {
        const_cast<char*>(abs_riot_path),
        const_cast<char*>("--launch-product=league_of_legends"),
        const_cast<char*>("--launch-patchline=live"),
        nullptr
    };
    
    // Spawn Riot Client process
    pid_t riot_pid;
    int spawn_result = posix_spawn(&riot_pid, abs_riot_path, nullptr, nullptr, argv, new_env);
    
    delete[] new_env;
    
    if (spawn_result != 0) {
        printf("[CSLOL] ❌ Failed to spawn Riot Client: %s\n", strerror(spawn_result));
        return 0;
    }
    
    printf("[CSLOL] ✅ Successfully launched Riot Client with PID: %d\n", riot_pid);
    printf("[CSLOL] Waiting for League game process to start...\n");
    
    // Wait for League game process to appear (may take a while)
    pid_t league_pid = 0;
    for (int attempts = 0; attempts < 60; attempts++) { // Wait up to 60 seconds
        sleep(1);
        
        // Look for League game process
        pid_t pid_list[1024];
        int n_pids = proc_listallpids(pid_list, sizeof(pid_list));
        
        for (int i = 0; i < n_pids; i++) {
            struct proc_bsdinfo proc;
            int st = proc_pidinfo(pid_list[i], PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE);
            
            if (st > 0 && strcmp(proc.pbi_name, "LeagueofLegends") == 0) {
                league_pid = pid_list[i];
                printf("[CSLOL] ✅ Found League game process: PID %d\n", league_pid);
                break;
            }
        }
        
        if (league_pid > 0) break;
        
        if (attempts % 10 == 0) {
            printf("[CSLOL] Still waiting for League game process... (%d/60s)\n", attempts);
        }
        
        // Check if Riot Client is still running
        if (kill(riot_pid, 0) != 0) {
            printf("[CSLOL] ⚠️  Riot Client process exited\n");
            break;
        }
    }
    
    if (league_pid == 0) {
        printf("[CSLOL] ❌ League game process did not start within timeout\n");
        printf("[CSLOL] You may need to manually start a game through the client\n");
        return riot_pid; // Return Riot Client PID instead
    }
    
    // Give League a moment to fully initialize
    printf("[CSLOL] Waiting for League to fully initialize...\n");
    sleep(5);
    
    // Check if our dylib was loaded
    std::string lsof_cmd = "lsof -p " + std::to_string(league_pid) + " 2>/dev/null | grep cslol";
    printf("[CSLOL] Checking if dylib was loaded...\n");
    int lsof_result = system(lsof_cmd.c_str());
    if (lsof_result == 0) {
        printf("[CSLOL] ✅ CSLOL dylib appears to be loaded!\n");
    } else {
        printf("[CSLOL] ⚠️  Could not verify dylib loading - may load when entering game\n");
    }
    
    return league_pid;
}

// Legacy runtime injection (kept for compatibility, but will fail with SIP)
bool RuntimeInjector::inject_dylib_into_process(pid_t target_pid, const std::string& dylib_path, const std::string& mod_path) {
    printf("[CSLOL] WARNING: Attempting legacy runtime injection (likely to fail with SIP enabled)\n");
    printf("[CSLOL] Consider using launch_league_with_injection() instead\n");
    
    // Check if runtime injection is even possible
    if (!is_runtime_injection_available()) {
        printf("[CSLOL] Runtime injection not available due to system security settings\n");
        return false;
    }
    
    printf("[CSLOL] Attempting runtime injection into PID: %d\n", target_pid);
    printf("[CSLOL] Dylib path: %s\n", dylib_path.c_str());
    
    task_t target_task;
    kern_return_t kr = task_for_pid(mach_task_self(), target_pid, &target_task);
    if (kr != KERN_SUCCESS) {
        printf("[CSLOL] Failed to get task for PID %d: %s\n", target_pid, mach_error_string(kr));
        printf("[CSLOL] This is expected with SIP enabled\n");
        return false;
    }
    
    printf("[CSLOL] Successfully got task for target process\n");
    
    // Check if dylib file exists
    if (access(dylib_path.c_str(), F_OK) != 0) {
        printf("[CSLOL] Dylib file does not exist: %s\n", dylib_path.c_str());
        return false;
    }
    
    // Set the environment variable in our process first
    std::string env_string = "CSLOL_MOD_PATH=" + mod_path;
    putenv(const_cast<char*>(env_string.c_str()));
    printf("[CSLOL] Set environment variable: %s\n", env_string.c_str());
    
    // The rest of the runtime injection code would go here, but it will likely fail
    // with SIP enabled, so we just return false
    printf("[CSLOL] Runtime injection blocked by system security\n");
    return false;
}

// New unified injection method that chooses the best approach
bool RuntimeInjector::inject_dylib_best_method(const std::string& dylib_path, const std::string& mod_path) {
    printf("[CSLOL] Choosing best injection method...\n");
    
    // First, try to find if League is already running
    pid_t existing_league = 0;
    pid_t pid_list[1024];
    int n_pids = proc_listallpids(pid_list, sizeof(pid_list));
    
    for (int i = 0; i < n_pids; i++) {
        struct proc_bsdinfo proc;
        int st = proc_pidinfo(pid_list[i], PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE);
        
        if (st > 0 && (strcmp(proc.pbi_name, "LeagueofLegends") == 0 || 
                       strcmp(proc.pbi_name, "League of Legends") == 0)) {
            existing_league = pid_list[i];
            printf("[CSLOL] Found existing League process: PID %d\n", existing_league);
            break;
        }
    }
    
    if (existing_league > 0) {
        printf("[CSLOL] League is already running - runtime injection required\n");
        
        if (is_runtime_injection_available()) {
            printf("[CSLOL] Attempting runtime injection...\n");
            return inject_dylib_into_process(existing_league, dylib_path, mod_path);
        } else {
            printf("[CSLOL] Runtime injection not available\n");
            printf("[CSLOL] Please close League and restart through CSLOL Manager\n");
            return false;
        }
    } else {
        printf("[CSLOL] No existing League process - will launch with injection\n");
        
        std::string league_path = find_league_executable();
        if (league_path.empty()) {
            printf("[CSLOL] Could not find League executable\n");
            return false;
        }
        
        pid_t league_pid = launch_league_with_injection(league_path, dylib_path, mod_path);
        return league_pid > 0;
    }
}

} // namespace lol::patcher

#endif