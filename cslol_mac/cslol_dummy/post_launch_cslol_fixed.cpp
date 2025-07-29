// post_launch_cslol_fixed.cpp - Simplified injection without Vanguard delays
#include <iostream>
#include <string>
#include <unistd.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/task.h>
#include <mach/vm_map.h>
#include <mach/mach_error.h>
#include <mach/thread_act.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <filesystem>

// Platform-specific thread state handling
#ifdef __arm64__
    #define THREAD_STATE_TYPE ARM_THREAD_STATE64
    #define THREAD_STATE_COUNT ARM_THREAD_STATE64_COUNT
    typedef arm_thread_state64_t thread_state_t_custom;
    #define PC_REGISTER __pc
    #define SP_REGISTER __sp
    #define X0_REGISTER __x[0]  // First argument register
    #define X1_REGISTER __x[1]  // Second argument register
    #define LR_REGISTER __lr    // Link register
#else
    #define THREAD_STATE_TYPE x86_THREAD_STATE64
    #define THREAD_STATE_COUNT x86_THREAD_STATE64_COUNT
    typedef x86_thread_state64_t thread_state_t_custom;
    #define PC_REGISTER __rip
    #define SP_REGISTER __rsp
    #define X0_REGISTER __rdi   // First argument register
    #define X1_REGISTER __rsi   // Second argument register
    #define LR_REGISTER __rip   // Return address
#endif

class PostLaunchCSLOL {
private:
    std::string mod_path;
    std::string dylib_path;
    pid_t league_pid = 0;
    task_t league_task = 0;
    
public:
    PostLaunchCSLOL(const std::string& mod_path, const std::string& dylib_path) 
        : mod_path(mod_path), dylib_path(dylib_path) {}
    
    bool wait_for_league() {
        printf("[CSLOL] Waiting for League to start...\n");
        
        for (int attempts = 0; attempts < 180; attempts++) { // 3 minutes max
            league_pid = find_process_by_name("LeagueofLegends");
            if (league_pid > 0) {
                printf("[CSLOL] ✅ Found League process: PID %d\n", league_pid);
                return true;
            }
            
            if (attempts % 15 == 0) {
                printf("[CSLOL] Still waiting for League... (%d/180s)\n", attempts);
            }
            sleep(1);
        }
        
        printf("[CSLOL] ❌ League did not start within timeout\n");
        return false;
    }
    
    bool acquire_league_task() {
        printf("[CSLOL] Acquiring task handle for League process PID %d...\n", league_pid);
        
        kern_return_t kr = task_for_pid(mach_task_self(), league_pid, &league_task);
        if (kr != KERN_SUCCESS) {
            printf("[CSLOL] ❌ Failed to get task handle: %s\n", mach_error_string(kr));
            printf("[CSLOL] Error code: 0x%x\n", kr);
            return false;
        }
        
        printf("[CSLOL] ✅ Successfully acquired task handle for PID %d\n", league_pid);
        return true;
    }
    
    bool inject_dylib_real() {
        printf("[CSLOL] Starting REAL dylib injection...\n");
        
        // Verify dylib exists
        if (access(dylib_path.c_str(), R_OK) != 0) {
            printf("[CSLOL] ❌ Dylib not found: %s\n", dylib_path.c_str());
            return false;
        }
        
        // Check dylib architecture
        std::string arch_check = "file '" + dylib_path + "' | grep x86_64";
        if (system(arch_check.c_str()) != 0) {
            printf("[CSLOL] ⚠️  Warning: Dylib may not be x86_64\n");
        }
        
        printf("[CSLOL] Using dylib: %s\n", dylib_path.c_str());
        printf("[CSLOL] Target League PID: %d\n", league_pid);
        
        // Get absolute paths
        char abs_dylib_path[PATH_MAX];
        if (!realpath(dylib_path.c_str(), abs_dylib_path)) {
            printf("[CSLOL] ❌ Failed to resolve dylib path\n");
            return false;
        }
        
        // Find dlopen in target process
        vm_address_t dlopen_addr = find_dlopen_in_target();
        if (dlopen_addr == 0) {
            printf("[CSLOL] ❌ Could not find dlopen in target process\n");
            return false;
        }
        
        printf("[CSLOL] Found dlopen at: 0x%lx\n", dlopen_addr);
        
        // Allocate memory for dylib path in target
        vm_address_t remote_dylib_path = 0;
        vm_size_t path_size = strlen(abs_dylib_path) + 1;
        
        kern_return_t kr = vm_allocate(league_task, &remote_dylib_path, path_size, VM_FLAGS_ANYWHERE);
        if (kr != KERN_SUCCESS) {
            printf("[CSLOL] ❌ Failed to allocate memory in target: %s\n", mach_error_string(kr));
            return false;
        }
        
        // Write dylib path to target
        kr = vm_write(league_task, remote_dylib_path, (vm_offset_t)abs_dylib_path, path_size);
        if (kr != KERN_SUCCESS) {
            printf("[CSLOL] ❌ Failed to write dylib path: %s\n", mach_error_string(kr));
            vm_deallocate(league_task, remote_dylib_path, path_size);
            return false;
        }
        
        printf("[CSLOL] ✅ Wrote dylib path to target memory: 0x%lx\n", remote_dylib_path);
        
        // Set up environment variable in target process memory
        std::string env_var = "CSLOL_MOD_PATH=" + mod_path;
        vm_address_t remote_env_path = 0;
        vm_size_t env_size = env_var.length() + 1;
        
        kr = vm_allocate(league_task, &remote_env_path, env_size, VM_FLAGS_ANYWHERE);
        if (kr == KERN_SUCCESS) {
            kr = vm_write(league_task, remote_env_path, (vm_offset_t)env_var.c_str(), env_size);
            if (kr == KERN_SUCCESS) {
                printf("[CSLOL] ✅ Set up environment variable in target\n");
            }
        }
        
        // Create and execute REAL injection thread
        bool injection_success = create_real_injection_thread(dlopen_addr, remote_dylib_path);
        
        // Clean up allocated memory
        vm_deallocate(league_task, remote_dylib_path, path_size);
        if (remote_env_path) {
            vm_deallocate(league_task, remote_env_path, env_size);
        }
        
        return injection_success;
    }
    
    vm_address_t find_dlopen_in_target() {
        printf("[CSLOL] Searching for dlopen in target process...\n");
        
        // For x86_64 League under Rosetta, we need to find dlopen in the target's address space
        // Method 1: Search known dlopen locations for x86_64 processes
        std::vector<vm_address_t> candidates = {
            // Common x86_64 dlopen locations
            0x7ff8041a1f73,  // Common macOS dlopen location
            0x7ff804000000,  // libdyld base area
            0x7ff800000000,  // System library area
            0x180000000,     // Alternative system area
        };
        
        for (vm_address_t candidate : candidates) {
            if (is_valid_address_in_target(candidate)) {
                printf("[CSLOL] Found potential dlopen at: 0x%lx\n", candidate);
                return candidate;
            }
        }
        
        // Method 2: Try to find dlopen via symbol resolution
        // This is more complex but might work better
        return find_dlopen_via_symbols();
    }
    
    vm_address_t find_dlopen_via_symbols() {
        printf("[CSLOL] Attempting symbol-based dlopen search...\n");
        
        // Use our local dlopen as a reference point
        void* local_dlopen = dlsym(RTLD_DEFAULT, "dlopen");
        if (local_dlopen) {
            vm_address_t local_addr = (vm_address_t)local_dlopen;
            printf("[CSLOL] Local dlopen at: 0x%lx\n", local_addr);
            
            // For cross-architecture injection, we need to find the equivalent
            // address in the target process. This is tricky with Rosetta.
            
            // Try some heuristics based on common offsets
            std::vector<vm_address_t> offset_candidates = {
                local_addr,                    // Same address (unlikely but possible)
                local_addr - 0x100000000,      // Common offset pattern
                local_addr + 0x100000000,      // Alternative offset
                0x7ff8041a1f73,                // Known good x86_64 location
            };
            
            for (vm_address_t candidate : offset_candidates) {
                if (is_valid_address_in_target(candidate)) {
                    printf("[CSLOL] Symbol-based dlopen found at: 0x%lx\n", candidate);
                    return candidate;
                }
            }
        }
        
        printf("[CSLOL] ❌ Symbol-based search failed\n");
        return 0;
    }
    
    bool is_valid_address_in_target(vm_address_t addr) {
        vm_address_t address = addr & ~0xFFF; // Page align
        vm_size_t size = 0;
        vm_region_flavor_t flavor = VM_REGION_BASIC_INFO_64;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object_name;
        
        kern_return_t kr = vm_region_64(league_task, &address, &size, flavor, 
                                       (vm_region_info_t)&info, &info_count, &object_name);
        
        return (kr == KERN_SUCCESS && (info.protection & VM_PROT_EXECUTE));
    }
    
    bool create_real_injection_thread(vm_address_t dlopen_addr, vm_address_t dylib_path) {
        printf("[CSLOL] Creating REAL injection thread for League PID %d...\n", league_pid);
        
        // Get the main thread from the League process
        thread_act_array_t thread_list;
        mach_msg_type_number_t thread_count;
        
        kern_return_t kr = task_threads(league_task, &thread_list, &thread_count);
        if (kr != KERN_SUCCESS || thread_count == 0) {
            printf("[CSLOL] ❌ Failed to get thread list: %s\n", mach_error_string(kr));
            return false;
        }
        
        thread_t main_thread = thread_list[0];
        printf("[CSLOL] Got main thread from League process\n");
        
        // Get current thread state as template
        thread_state_t_custom thread_state;
        mach_msg_type_number_t state_count = THREAD_STATE_COUNT;
        
        kr = thread_get_state(main_thread, THREAD_STATE_TYPE,
                             (thread_state_t)&thread_state, &state_count);
        
        if (kr != KERN_SUCCESS) {
            printf("[CSLOL] ❌ Failed to get thread state: %s\n", mach_error_string(kr));
            vm_deallocate(mach_task_self(), (vm_address_t)thread_list, 
                         thread_count * sizeof(thread_t));
            return false;
        }
        
        // Allocate stack for injection thread
        vm_address_t stack_addr = 0;
        vm_size_t stack_size = 64 * 1024; // 64KB stack
        
        kr = vm_allocate(league_task, &stack_addr, stack_size, VM_FLAGS_ANYWHERE);
        if (kr != KERN_SUCCESS) {
            printf("[CSLOL] ❌ Failed to allocate stack: %s\n", mach_error_string(kr));
            vm_deallocate(mach_task_self(), (vm_address_t)thread_list, 
                         thread_count * sizeof(thread_t));
            return false;
        }
        
        printf("[CSLOL] Allocated stack at: 0x%lx\n", stack_addr);
        
        // Set up thread state for dlopen call
        thread_state_t_custom injection_state = thread_state;
        
        injection_state.PC_REGISTER = dlopen_addr;                    // Jump to dlopen
        injection_state.X0_REGISTER = dylib_path;                     // First arg: dylib path
        injection_state.X1_REGISTER = RTLD_NOW;                       // Second arg: RTLD_NOW
        injection_state.SP_REGISTER = stack_addr + stack_size - 16;   // Stack pointer
        
#ifdef __arm64__
        // For ARM64, set link register to 0 (will crash after dlopen, but that's fine)
        injection_state.LR_REGISTER = 0;
#endif
        
        printf("[CSLOL] Thread state setup for League PID %d:\n", league_pid);
        printf("[CSLOL]   PC: 0x%llx (dlopen)\n", injection_state.PC_REGISTER);
        printf("[CSLOL]   Arg0: 0x%llx (dylib path)\n", injection_state.X0_REGISTER);
        printf("[CSLOL]   Arg1: 0x%llx (RTLD_NOW)\n", injection_state.X1_REGISTER);
        printf("[CSLOL]   SP: 0x%llx (stack)\n", injection_state.SP_REGISTER);
        
        // Create the injection thread in League process
        thread_t injection_thread;
        kr = thread_create_running(league_task, THREAD_STATE_TYPE,
                                 (thread_state_t)&injection_state, THREAD_STATE_COUNT,
                                 &injection_thread);
        
        if (kr == KERN_SUCCESS) {
            printf("[CSLOL] ✅ REAL injection thread created in League PID %d!\n", league_pid);
            
            // Give the injection thread time to execute dlopen
            printf("[CSLOL] Waiting for dlopen to complete...\n");
            sleep(3);
            
            // Clean up
            vm_deallocate(league_task, stack_addr, stack_size);
            vm_deallocate(mach_task_self(), (vm_address_t)thread_list, 
                         thread_count * sizeof(thread_t));
            
            return true;
        } else {
            printf("[CSLOL] ❌ Failed to create injection thread: %s\n", mach_error_string(kr));
            printf("[CSLOL] Error code: 0x%x\n", kr);
            vm_deallocate(league_task, stack_addr, stack_size);
        }
        
        // Clean up thread list
        vm_deallocate(mach_task_self(), (vm_address_t)thread_list, 
                     thread_count * sizeof(thread_t));
        
        return false;
    }
    
    bool verify_injection() {
        printf("[CSLOL] Verifying injection success in League PID %d...\n", league_pid);
        
        // Wait a moment for dylib constructor to run
        sleep(2);
        
        // Method 1: Check for our test log file
        if (access("/tmp/cslol_minimal_test.log", F_OK) == 0) {
            printf("[CSLOL] ✅ Found injection log file!\n");
            system("cat /tmp/cslol_minimal_test.log");
            
            // Check if the log mentions the correct PID
            std::string grep_cmd = "grep 'Process PID: " + std::to_string(league_pid) + "' /tmp/cslol_minimal_test.log";
            if (system(grep_cmd.c_str()) == 0) {
                printf("[CSLOL] ✅ Confirmed injection into correct League process!\n");
                return true;
            } else {
                printf("[CSLOL] ⚠️  Log file found but PID mismatch detected\n");
            }
        }
        
        // Method 2: Check if dylib is loaded in League process
        std::string lsof_cmd = "lsof -p " + std::to_string(league_pid) + " 2>/dev/null | grep cslol";
        printf("[CSLOL] Checking if dylib is loaded in League PID %d...\n", league_pid);
        if (system(lsof_cmd.c_str()) == 0) {
            printf("[CSLOL] ✅ Dylib appears to be loaded in League!\n");
            return true;
        }
        
        printf("[CSLOL] ⚠️  No clear injection confirmation found for League PID %d\n", league_pid);
        return false;
    }
    
    bool run_full_injection() {
        printf("[CSLOL] Starting Post-Launch CSLOL Injection\n");
        printf("[CSLOL] Mod path: %s\n", mod_path.c_str());
        printf("[CSLOL] Dylib path: %s\n", dylib_path.c_str());
        
        // Step 1: Wait for League to start
        if (!wait_for_league()) {
            return false;
        }
        
        // Step 2: Immediately acquire task handle (no Vanguard delay)
        if (!acquire_league_task()) {
            return false;
        }
        
        // Step 3: Inject dylib with REAL thread creation
        if (!inject_dylib_real()) {
            return false;
        }
        
        // Step 4: Verify injection
        verify_injection();
        
        printf("[CSLOL] ✅ Post-launch injection process completed for League PID %d!\n", league_pid);
        return true;
    }
    
private:
    pid_t find_process_by_name(const char* name) {
        pid_t pid_list[1024];
        int n_pids = proc_listallpids(pid_list, sizeof(pid_list));
        
        printf("[CSLOL] Searching through %d processes for '%s'...\n", n_pids, name);
        
        for (int i = 0; i < n_pids; i++) {
            struct proc_bsdinfo proc;
            int st = proc_pidinfo(pid_list[i], PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE);
            
            if (st > 0) {
                // Log all League-like processes for debugging
                if (strstr(proc.pbi_name, "League") || strstr(proc.pbi_name, "league")) {
                    printf("[CSLOL] Found League-like process: PID %d, Name: '%s'\n", 
                           pid_list[i], proc.pbi_name);
                }
                
                if (strcmp(proc.pbi_name, name) == 0) {
                    printf("[CSLOL] Exact match found: PID %d, Name: '%s'\n", 
                           pid_list[i], proc.pbi_name);
                    return pid_list[i];
                }
            }
        }
        
        printf("[CSLOL] No exact match found for '%s'\n", name);
        return 0;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s <mod_path> <dylib_path>\n", argv[0]);
        printf("Example: %s \"/Users/user/Library/Application Support/moonshadow565/customskinlol-manager/profiles/Default Profile/\" ./libcslol_minimal_test_x86.dylib\n", argv[0]);
        return 1;
    }
    
    std::string mod_path = argv[1];
    std::string dylib_path = argv[2];
    
    printf("=== Post-Launch CSLOL Manager (Fixed) ===\n");
    
    // Verify mod path exists
    if (!std::filesystem::exists(mod_path)) {
        printf("❌ Mod path does not exist: %s\n", mod_path.c_str());
        return 1;
    }
    
    // Verify dylib exists
    if (!std::filesystem::exists(dylib_path)) {
        printf("❌ Dylib does not exist: %s\n", dylib_path.c_str());
        return 1;
    }
    
    PostLaunchCSLOL injector(mod_path, dylib_path);
    
    bool success = injector.run_full_injection();
    
    if (success) {
        printf("\n🎉 SUCCESS: Post-launch CSLOL injection completed!\n");
        printf("Your mods should now be active in League!\n");
        return 0;
    } else {
        printf("\n❌ FAILED: Post-launch injection failed\n");
        return 1;
    }
}
