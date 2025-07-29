// debug_league_launcher.cpp - Debug League launcher issues
#include <iostream>
#include <string>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <sys/wait.h>
#include <filesystem>
#include <signal.h>
#include <errno.h>

extern char **environ;

int main() {
    printf("=== Debug League Launcher ===\n");
    
    // 1. Check for League executable locations
    std::vector<std::string> possible_paths = {
        "/Applications/League of Legends.app/Contents/LoL/Game/LeagueofLegends.app/Contents/MacOS/LeagueofLegends",
        "/Applications/League of Legends.app/Contents/LoL/Game/League of Legends.app/Contents/MacOS/League of Legends",
        "/Applications/League of Legends.app/Contents/LoL/Game/LeagueofLegends"
    };
    
    std::string found_league_path;
    printf("\n1. Looking for League executable...\n");
    for (const auto& path : possible_paths) {
        printf("   Checking: %s\n", path.c_str());
        if (access(path.c_str(), F_OK) == 0) {
            printf("   ✅ Found! Checking if executable...\n");
            if (access(path.c_str(), X_OK) == 0) {
                printf("   ✅ Executable!\n");
                found_league_path = path;
                break;
            } else {
                printf("   ❌ Not executable\n");
            }
        } else {
            printf("   ❌ Not found\n");
        }
    }
    
    if (found_league_path.empty()) {
        printf("\n❌ Could not find League executable!\n");
        printf("Let's check the League installation structure:\n");
        
        // Check if the main app exists
        std::string main_app = "/Applications/League of Legends.app";
        if (access(main_app.c_str(), F_OK) == 0) {
            printf("✅ Found: %s\n", main_app.c_str());
            
            // List contents
            printf("\nListing contents of League app:\n");
            system("ls -la '/Applications/League of Legends.app/Contents/LoL/Game/' 2>/dev/null || echo 'Game directory not found'");
            
            printf("\nLooking for any executable files:\n");
            system("find '/Applications/League of Legends.app/Contents/LoL/Game/' -type f -perm +111 2>/dev/null | head -10");
        } else {
            printf("❌ League of Legends.app not found in /Applications/\n");
            printf("Let's see what League-related apps exist:\n");
            system("ls -la /Applications/ | grep -i league");
        }
        return 1;
    }
    
    printf("\n2. Found League executable: %s\n", found_league_path.c_str());
    
    // 2. Check for test dylib
    printf("\n3. Looking for test dylib...\n");
    std::vector<std::string> dylib_paths = {
        "./libtest_injection.dylib",
        "./libcslol_interception.dylib",
        "../libtest_injection.dylib"
    };
    
    std::string found_dylib_path;
    for (const auto& path : dylib_paths) {
        printf("   Checking: %s\n", path.c_str());
        if (access(path.c_str(), F_OK) == 0) {
            printf("   ✅ Found!\n");
            found_dylib_path = path;
            break;
        } else {
            printf("   ❌ Not found\n");
        }
    }
    
    if (found_dylib_path.empty()) {
        printf("\n⚠️  No test dylib found, but continuing...\n");
        found_dylib_path = "./libtest_injection.dylib";
    }
    
    // 3. Get absolute paths
    printf("\n4. Resolving absolute paths...\n");
    char abs_league_path[PATH_MAX];
    char abs_dylib_path[PATH_MAX];
    
    if (!realpath(found_league_path.c_str(), abs_league_path)) {
        printf("❌ Failed to resolve League path: %s\n", strerror(errno));
        return 1;
    }
    printf("   League absolute path: %s\n", abs_league_path);
    
    if (access(found_dylib_path.c_str(), F_OK) == 0) {
        if (!realpath(found_dylib_path.c_str(), abs_dylib_path)) {
            printf("❌ Failed to resolve dylib path: %s\n", strerror(errno));
            return 1;
        }
        printf("   Dylib absolute path: %s\n", abs_dylib_path);
    } else {
        strcpy(abs_dylib_path, found_dylib_path.c_str());
        printf("   Dylib path (not verified): %s\n", abs_dylib_path);
    }
    
    // 4. Test launch (with a timeout)
    printf("\n5. Testing League launch...\n");
    printf("⚠️  This will actually try to launch League - press Ctrl+C within 5 seconds to cancel\n");
    for (int i = 5; i > 0; i--) {
        printf("   Starting in %d seconds...\n", i);
        sleep(1);
    }
    
    // Set up environment
    std::string dyld_insert = "DYLD_INSERT_LIBRARIES=" + std::string(abs_dylib_path);
    std::string mod_path = "CSLOL_MOD_PATH=/tmp/test_mod_path/";
    
    printf("Setting environment:\n");
    printf("   %s\n", dyld_insert.c_str());
    printf("   %s\n", mod_path.c_str());
    
    // Count existing environment variables
    int env_count = 0;
    while (environ[env_count]) env_count++;
    
    // Create new environment array
    char** new_env = new char*[env_count + 3];
    
    // Copy existing environment
    for (int i = 0; i < env_count; i++) {
        new_env[i] = environ[i];
    }
    
    // Add our variables
    new_env[env_count] = const_cast<char*>(dyld_insert.c_str());
    new_env[env_count + 1] = const_cast<char*>(mod_path.c_str());
    new_env[env_count + 2] = nullptr;
    
    // Set up argv
    char* argv[] = {
        const_cast<char*>(abs_league_path),
        nullptr
    };
    
    printf("\nAttempting to spawn League...\n");
    
    pid_t league_pid;
    int spawn_result = posix_spawn(&league_pid, abs_league_path, nullptr, nullptr, argv, new_env);
    
    delete[] new_env;
    
    if (spawn_result != 0) {
        printf("❌ Failed to spawn League: %s (error code: %d)\n", strerror(spawn_result), spawn_result);
        
        // Additional debugging
        printf("\nDebugging spawn failure:\n");
        printf("   errno: %d (%s)\n", errno, strerror(errno));
        
        // Try a simple test
        printf("\nTrying to run League without environment variables:\n");
        system("'/Applications/League of Legends.app/Contents/LoL/Game/League of Legends' --version 2>&1 | head -5 || echo 'Direct execution failed'");
        
        return 1;
    }
    
    printf("✅ Successfully spawned League with PID: %d\n", league_pid);
    
    // Give it a few seconds to start
    printf("Waiting 10 seconds for League to initialize...\n");
    for (int i = 0; i < 10; i++) {
        sleep(1);
        if (kill(league_pid, 0) != 0) {
            printf("⚠️  League process exited at %d seconds\n", i + 1);
            break;
        }
        printf("   League still running... %d/10\n", i + 1);
    }
    
    // Check if our injection worked (if dylib exists)
    if (access(abs_dylib_path, F_OK) == 0) {
        printf("\nChecking if dylib was loaded...\n");
        std::string lsof_cmd = "lsof -p " + std::to_string(league_pid) + " 2>/dev/null | grep injection";
        int lsof_result = system(lsof_cmd.c_str());
        if (lsof_result == 0) {
            printf("✅ Dylib appears to be loaded!\n");
        } else {
            printf("❌ Dylib not found in process\n");
        }
    }
    
    // Clean up
    printf("\nTerminating League test process...\n");
    kill(league_pid, SIGTERM);
    
    int status;
    waitpid(league_pid, &status, 0);
    
    printf("✅ Debug test completed successfully!\n");
    return 0;
}
