// env_test_injector.cpp - Use environment variable injection instead
#include <iostream>
#include <string>
#include <unistd.h>
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

bool test_env_injection(const std::string& dylib_path, const std::string& target_binary) {
    printf("[TEST] Testing environment variable injection\n");
    printf("[TEST] Dylib: %s\n", dylib_path.c_str());
    printf("[TEST] Target: %s\n", target_binary.c_str());
    
    // Convert to absolute path
    char abs_dylib_path[PATH_MAX];
    if (!realpath(dylib_path.c_str(), abs_dylib_path)) {
        printf("[TEST] ❌ Failed to get absolute path for dylib\n");
        return false;
    }
    
    char abs_target_path[PATH_MAX];
    if (!realpath(target_binary.c_str(), abs_target_path)) {
        printf("[TEST] ❌ Failed to get absolute path for target binary\n");
        return false;
    }
    
    printf("[TEST] Using absolute dylib path: %s\n", abs_dylib_path);
    printf("[TEST] Using absolute target path: %s\n", abs_target_path);
    
    // Verify files exist
    if (access(abs_dylib_path, R_OK) != 0) {
        printf("[TEST] ❌ Dylib not readable: %s\n", abs_dylib_path);
        return false;
    }
    
    if (access(abs_target_path, X_OK) != 0) {
        printf("[TEST] ❌ Target binary not executable: %s\n", abs_target_path);
        return false;
    }
    
    // Clean up any old test log
    unlink("/tmp/injection_test.log");
    
    // Set up environment
    std::string dyld_insert = "DYLD_INSERT_LIBRARIES=" + std::string(abs_dylib_path);
    std::string mod_path = "CSLOL_MOD_PATH=/tmp/test_mod_path/";
    
    printf("[TEST] Setting environment:\n");
    printf("[TEST]   %s\n", dyld_insert.c_str());
    printf("[TEST]   %s\n", mod_path.c_str());
    
    // Build environment array
    char* env_vars[] = {
        const_cast<char*>(dyld_insert.c_str()),
        const_cast<char*>(mod_path.c_str()),
        nullptr
    };
    
    // Copy existing environment and add our variables
    int env_count = 0;
    while (environ[env_count]) env_count++;
    
    char** new_env = new char*[env_count + 3];  // +2 for our vars, +1 for null
    
    // Copy existing environment
    for (int i = 0; i < env_count; i++) {
        new_env[i] = environ[i];
    }
    
    // Add our variables
    new_env[env_count] = const_cast<char*>(dyld_insert.c_str());
    new_env[env_count + 1] = const_cast<char*>(mod_path.c_str());
    new_env[env_count + 2] = nullptr;
    
    // Spawn the target process with our environment
    pid_t child_pid;
    char* argv[] = {
        abs_target_path,
        nullptr
    };
    
    printf("[TEST] Spawning target process with injection environment...\n");
    
    int spawn_result = posix_spawn(&child_pid, abs_target_path, nullptr, nullptr, argv, new_env);
    
    delete[] new_env;
    
    if (spawn_result != 0) {
        printf("[TEST] ❌ Failed to spawn target process: %s\n", strerror(spawn_result));
        return false;
    }
    
    printf("[TEST] ✅ Spawned target process with PID: %d\n", child_pid);
    
    // Wait a moment for the process to initialize and potentially load our dylib
    printf("[TEST] Waiting for injection to take effect...\n");
    for (int i = 0; i < 10; i++) {
        sleep(1);
        printf("[TEST] Waiting... %d/10\n", i + 1);
        
        // Check if target process is still alive
        if (kill(child_pid, 0) != 0) {
            printf("[TEST] ⚠️  Target process exited early\n");
            break;
        }
        
        // Check if our injection worked
        if (access("/tmp/injection_test.log", F_OK) == 0) {
            printf("[TEST] ✅ Injection confirmed by log file!\n");
            system("cat /tmp/injection_test.log");
            
            // Check if dylib is loaded in the process
            std::string lsof_cmd = "lsof -p " + std::to_string(child_pid) + " 2>/dev/null | grep libtest_injection";
            printf("[TEST] Checking if dylib is loaded...\n");
            int lsof_result = system(lsof_cmd.c_str());
            if (lsof_result == 0) {
                printf("[TEST] ✅ Dylib is loaded in target process!\n");
            }
            
            // Kill the test process
            printf("[TEST] Terminating test process...\n");
            kill(child_pid, SIGTERM);
            
            // Wait for it to exit
            int status;
            waitpid(child_pid, &status, 0);
            
            return true;
        }
    }
    
    printf("[TEST] ❌ No injection confirmation found\n");
    
    // Kill the test process
    printf("[TEST] Terminating test process...\n");
    kill(child_pid, SIGTERM);
    
    // Wait for it to exit
    int status;
    waitpid(child_pid, &status, 0);
    
    return false;
}

// Test injection on an already running process using different methods
bool test_runtime_methods(pid_t target_pid, const std::string& dylib_path) {
    printf("\n[TEST] Testing runtime injection methods on PID: %d\n", target_pid);
    
    // Method 1: Try to see what we can discover about the process
    printf("[TEST] Method 1: Process analysis\n");
    
    // Check what libraries the process already has loaded
    std::string lsof_cmd = "lsof -p " + std::to_string(target_pid) + " 2>/dev/null | grep '\\.dylib'";
    printf("[TEST] Current libraries loaded in target:\n");
    system(lsof_cmd.c_str());
    
    // Check process environment
    std::string env_cmd = "ps -p " + std::to_string(target_pid) + " -o pid,command";
    printf("[TEST] Target process info:\n");
    system(env_cmd.c_str());
    
    // Method 2: Check what injection obstacles exist
    printf("\n[TEST] Method 2: Security analysis\n");
    
    // Check SIP status
    printf("[TEST] SIP status:\n");
    system("csrutil status");
    
    // Check if process has specific protections
    std::string codesign_cmd = "codesign -dvvv --entitlements - /proc/" + std::to_string(target_pid) + "/exe 2>/dev/null || echo 'Could not check entitlements'";
    printf("[TEST] Process code signing info:\n");
    system(codesign_cmd.c_str());
    
    return false;  // We're just analyzing, not actually injecting
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s <mode> <target_binary_or_pid> [dylib_path]\n", argv[0]);
        printf("Modes:\n");
        printf("  env <target_binary> <dylib_path>  - Test environment variable injection\n");
        printf("  runtime <target_pid> [dylib_path] - Analyze runtime injection possibilities\n");
        printf("\nExamples:\n");
        printf("  %s env ./test_target ./libtest_injection.dylib\n", argv[0]);
        printf("  %s runtime 12345\n", argv[0]);
        return 1;
    }
    
    const char* mode = argv[1];
    const char* target = argv[2];
    const char* dylib_path = argc > 3 ? argv[3] : nullptr;
    
    printf("[TEST] Environment Variable Injection Test\n");
    printf("[TEST] Mode: %s\n", mode);
    
    if (strcmp(mode, "env") == 0) {
        if (!dylib_path) {
            printf("[TEST] ❌ Dylib path required for env mode\n");
            return 1;
        }
        
        bool result = test_env_injection(dylib_path, target);
        printf("\n%s ENVIRONMENT INJECTION TEST %s!\n", 
               result ? "✅" : "❌", result ? "PASSED" : "FAILED");
        return result ? 0 : 1;
        
    } else if (strcmp(mode, "runtime") == 0) {
        pid_t pid = atoi(target);
        if (pid <= 0) {
            printf("[TEST] ❌ Invalid PID: %s\n", target);
            return 1;
        }
        
        test_runtime_methods(pid, dylib_path ? dylib_path : "");
        return 0;
        
    } else {
        printf("[TEST] ❌ Unknown mode: %s\n", mode);
        return 1;
    }
}
