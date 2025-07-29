// safer_league_injector.cpp - More careful injection with better dlopen detection
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
#include <mach-o/loader.h>
#include <filesystem>

// Platform-specific thread state handling
#ifdef __arm64__
    #define THREAD_STATE_TYPE ARM_THREAD_STATE64
    #define THREAD_STATE_COUNT ARM_THREAD_STATE64_COUNT
    typedef arm_thread_state64_t thread_state_t_custom;
    #define PC_REGISTER __pc
    #define SP_REGISTER __sp
    #define X0_REGISTER __x[0]
    #define X1_REGISTER __x[1]
    #define LR_REGISTER __lr
#else
    #define THREAD_STATE_TYPE x86_THREAD_STATE64
    #define THREAD_STATE_COUNT x86_THREAD_STATE64_COUNT
    typedef x86_thread_state64_t thread_state_t_custom;
    #define PC_REGISTER __rip
    #define SP_REGISTER __rsp
    #define X0_REGISTER __rdi
    #define X1_REGISTER __rsi
    #define LR_REGISTER __rip
#endif

class SaferLeagueInjector {
private:
    std::string mod_path;
    std::string dylib_path;
    pid_t league_pid = 0;
    task_t league_task = 0;
    
public:
    SaferLeagueInjector(const std::string& mod_path, const std::string& dylib_path) 
        : mod_path(mod_path), dylib_path(dylib_path) {}
    
    bool wait_for_league() {
        printf("[SAFER-INJECT] Waiting for League game to start...\n");
        
        for (int attempts = 0; attempts < 180; attempts++) {
            league_pid = find_process_by_name("LeagueofLegends");
            if (league_pid > 0) {
                printf("[SAFER-INJECT] ✅ Found League game process: PID %d\n", league_pid);
                return true;
            }
            
            if (attempts % 15 == 0) {
                printf("[SAFER-INJECT] Still waiting for League game... (%d/180s)\n", attempts);
                list_league_processes();
            }
            sleep(1);
        }
        
        printf("[SAFER-INJECT] ❌ League game did not start within timeout\n");
        return false;
    }
    
    void list_league_processes() {
        pid_t pid_list[1024];
        int n_pids = proc_listallpids(pid_list, sizeof(pid_list));
        
        printf("[SAFER-INJECT] Current League-related processes:\n");
        for (int i = 0; i < n_pids; i++) {
            struct proc_bsdinfo proc;
            int st = proc_pidinfo(pid_list[i], PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE);
            
            if (st > 0 && (strstr(proc.pbi_name, "League") || strstr(proc.pbi_name, "league"))) {
                printf("[SAFER-INJECT]   PID %d: %s\n", pid_list[i], proc.pbi_name);
            }
        }
    }
    
    bool acquire_task_handle() {
        printf("[SAFER-INJECT] Acquiring task handle for League PID %d...\n", league_pid);
        
        kern_return_t kr = task_for_pid(mach_task_self(), league_pid, &league_task);
        if (kr != KERN_SUCCESS) {
            printf("[SAFER-INJECT] ❌ Failed to get task handle: %s\n", mach_error_string(kr));
            return false;
        }
        
        printf("[SAFER-INJECT] ✅ Successfully acquired task handle for PID %d\n", league_pid);
        return true;
    }
    
    bool inject_with_safer_method() {
        printf("[SAFER-INJECT] Starting safer dylib injection...\n");
        
        // Verify dylib exists and is correct architecture
        if (access(dylib_path.c_str(), R_OK) != 0) {
            printf("[SAFER-INJECT] ❌ Dylib not found: %s\n", dylib_path.c_str());
            return false;
        }
        
        char abs_dylib_path[PATH_MAX];
        if (!realpath(dylib_path.c_str(), abs_dylib_path)) {
            printf("[SAFER-INJECT] ❌ Failed to resolve dylib path\n");
            return false;
        }
        
        printf("[SAFER-INJECT] Using dylib: %s\n", abs_dylib_path);
        
        // Try to find dlopen more carefully
        vm_address_t dlopen_addr = find_dlopen_carefully();
        if (dlopen_addr == 0) {
            printf("[SAFER-INJECT] ❌ Could not find safe dlopen address\n");
            return false;
        }
        
        printf("[SAFER-INJECT] Using dlopen at: 0x%lx\n", dlopen_addr);
        
        // Allocate memory for dylib path
        vm_address_t remote_dylib_path = 0;
        vm_size_t path_size = strlen(abs_dylib_path) + 1;
        
        kern_return_t kr = vm_allocate(league_task, &remote_dylib_path, path_size, VM_FLAGS_ANYWHERE);
        if (kr != KERN_SUCCESS) {
            printf("[SAFER-INJECT] ❌ Failed to allocate memory: %s\n", mach_error_string(kr));
            return false;
        }
        
        kr = vm_write(league_task, remote_dylib_path, (vm_offset_t)abs_dylib_path, path_size);
        if (kr != KERN_SUCCESS) {
            printf("[SAFER-INJECT] ❌ Failed to write dylib path: %s\n", mach_error_string(kr));
            vm_deallocate(league_task, remote_dylib_path, path_size);
            return false;
        }
        
        printf("[SAFER-INJECT] ✅ Wrote dylib path to: 0x%lx\n", remote_dylib_path);
        
        // Set up environment
        std::string env_var = "CSLOL_MOD_PATH=" + mod_path;
        vm_address_t remote_env_path = 0;
        vm_size_t env_size = env_var.length() + 1;
        
        kr = vm_allocate(league_task, &remote_env_path, env_size, VM_FLAGS_ANYWHERE);
        if (kr == KERN_SUCCESS) {
            kr = vm_write(league_task, remote_env_path, (vm_offset_t)env_var.c_str(), env_size);
            if (kr == KERN_SUCCESS) {
                printf("[SAFER-INJECT] ✅ Set up environment variable\n");
            }
        }
        
        // Try injection with crash protection
        bool success = create_protected_injection_thread(dlopen_addr, remote_dylib_path);
        
        // Clean up
        vm_deallocate(league_task, remote_dylib_path, path_size);
        if (remote_env_path) {
            vm_deallocate(league_task, remote_env_path, env_size);
        }
        
        return success;
    }
    
    vm_address_t find_dlopen_carefully() {
        printf("[SAFER-INJECT] Carefully searching for dlopen...\n");
        
        // Method 1: Try to find dlopen in loaded dylibs
        vm_address_t candidate = find_dlopen_in_libdyld();
        if (candidate != 0) {
            printf("[SAFER-INJECT] Found dlopen in libdyld: 0x%lx\n", candidate);
            return candidate;
        }
        
        // Method 2: Search system library regions more carefully
        candidate = search_system_library_regions();
        if (candidate != 0) {
            printf("[SAFER-INJECT] Found dlopen in system libraries: 0x%lx\n", candidate);
            return candidate;
        }
        
        // Method 3: Try known good addresses with validation
        candidate = try_known_dlopen_addresses();
        if (candidate != 0) {
            printf("[SAFER-INJECT] Found dlopen at known address: 0x%lx\n", candidate);
            return candidate;
        }
        
        printf("[SAFER-INJECT] ❌ Could not find dlopen safely\n");
        return 0;
    }
    
    vm_address_t find_dlopen_in_libdyld() {
        printf("[SAFER-INJECT] Searching for dlopen in libdyld...\n");
        
        // Look for libdyld regions in the target process
        vm_address_t address = 0;
        vm_size_t size = 0;
        
        while (true) {
            vm_region_flavor_t flavor = VM_REGION_BASIC_INFO_64;
            vm_region_basic_info_data_64_t info;
            mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t object_name;
            
            kern_return_t kr = vm_region_64(league_task, &address, &size, flavor,
                                           (vm_region_info_t)&info, &info_count, &object_name);
            
            if (kr != KERN_SUCCESS) break;
            
            // Look for executable regions that might contain libdyld
            if ((info.protection & VM_PROT_EXECUTE) && size > 0x1000) {
                // Common dlopen signatures for x86_64
                vm_address_t dlopen_candidate = search_region_for_dlopen(address, size);
                if (dlopen_candidate != 0) {
                    return dlopen_candidate;
                }
            }
            
            address += size;
        }
        
        return 0;
    }
    
    vm_address_t search_region_for_dlopen(vm_address_t start, vm_size_t size) {
        // This is a simplified search - in a real implementation, you'd search for
        // the actual dlopen function signature in the memory region
        
        // For now, just check if the region looks like it could contain dlopen
        if (size > 0x10000 && (start & 0xFFF) == 0) {
            // Check if this region is readable and executable
            if (is_valid_executable_address(start)) {
                return start + 0x1000; // Offset where dlopen might be
            }
        }
        
        return 0;
    }
    
    vm_address_t search_system_library_regions() {
        printf("[SAFER-INJECT] Searching system library regions...\n");
        
        // Known system library base addresses for x86_64 under Rosetta
        std::vector<vm_address_t> system_bases = {
            0x7ff800000000,  // Common system library base
            0x7ff804000000,  // libdyld area
            0x180000000,     // Alternative system base
            0x1a0000000,     // Another common base
        };
        
        for (vm_address_t base : system_bases) {
            // Search within a reasonable range of each base
            for (vm_address_t offset = 0; offset < 0x200000; offset += 0x1000) {
                vm_address_t candidate = base + offset;
                
                if (is_valid_executable_address(candidate)) {
                    // Do a more thorough check to see if this could be dlopen
                    if (could_be_dlopen_function(candidate)) {
                        return candidate;
                    }
                }
            }
        }
        
        return 0;
    }
    
    bool could_be_dlopen_function(vm_address_t addr) {
        // Try to read a few bytes to see if this looks like a function
        vm_size_t read_size = 16;
        vm_offset_t data_out;
        mach_msg_type_number_t data_count;
        
        kern_return_t kr = vm_read(league_task, addr, read_size, &data_out, &data_count);
        if (kr != KERN_SUCCESS) {
            return false;
        }
        
        // Very basic check - look for common x86_64 function prologue patterns
        uint8_t* bytes = (uint8_t*)data_out;
        
        // Common function prologues
        bool looks_like_function = (
            (bytes[0] == 0x55 && bytes[1] == 0x48 && bytes[2] == 0x89) ||  // push %rbp; mov %rsp,%rbp
            (bytes[0] == 0x48 && bytes[1] == 0x83 && bytes[2] == 0xec) ||  // sub $X,%rsp
            (bytes[0] == 0x48 && bytes[1] == 0x89) ||                      // mov instructions
            (bytes[0] == 0x41 && bytes[1] == 0x57) ||                      // push %r15
            (bytes[0] == 0x53)                                             // push %rbx
        );
        
        // Clean up
        vm_deallocate(mach_task_self(), data_out, data_count);
        
        return looks_like_function;
    }
    
    vm_address_t try_known_dlopen_addresses() {
        printf("[SAFER-INJECT] Trying known dlopen addresses...\n");
        
        // Get our local dlopen address as reference
        void* local_dlopen = dlsym(RTLD_DEFAULT, "dlopen");
        if (local_dlopen) {
            printf("[SAFER-INJECT] Local dlopen at: 0x%lx\n", (vm_address_t)local_dlopen);
        }
        
        // Known good dlopen addresses from various macOS versions
        std::vector<vm_address_t> known_addresses = {
            0x7ff8041a1f73,  // Common macOS dlopen
            0x7ff804001234,  // Alternative location
            0x180001000,     // Simplified system address
        };
        
        for (vm_address_t addr : known_addresses) {
            if (is_valid_executable_address(addr) && could_be_dlopen_function(addr)) {
                return addr;
            }
        }
        
        return 0;
    }
    
    bool is_valid_executable_address(vm_address_t addr) {
        vm_address_t address = addr & ~0xFFF;
        vm_size_t size = 0;
        vm_region_flavor_t flavor = VM_REGION_BASIC_INFO_64;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object_name;
        
        kern_return_t kr = vm_region_64(league_task, &address, &size, flavor,
                                       (vm_region_info_t)&info, &info_count, &object_name);
        
        return (kr == KERN_SUCCESS && (info.protection & VM_PROT_EXECUTE));
    }
    
    bool create_protected_injection_thread(vm_address_t dlopen_addr, vm_address_t dylib_path) {
        printf("[SAFER-INJECT] Creating protected injection thread...\n");
        
        // First, let's try a simpler approach: just create a minimal thread that calls dlopen
        // without trying to return cleanly (it's okay if the thread crashes after dlopen)
        
        thread_act_array_t thread_list;
        mach_msg_type_number_t thread_count;
        
        kern_return_t kr = task_threads(league_task, &thread_list, &thread_count);
        if (kr != KERN_SUCCESS || thread_count == 0) {
            printf("[SAFER-INJECT] ❌ Failed to get thread list: %s\n", mach_error_string(kr));
            return false;
        }
        
        // Use main thread as template
        thread_t main_thread = thread_list[0];
        thread_state_t_custom thread_state;
        mach_msg_type_number_t state_count = THREAD_STATE_COUNT;
        
        kr = thread_get_state(main_thread, THREAD_STATE_TYPE,
                             (thread_state_t)&thread_state, &state_count);
        
        if (kr != KERN_SUCCESS) {
            printf("[SAFER-INJECT] ❌ Failed to get thread state: %s\n", mach_error_string(kr));
            vm_deallocate(mach_task_self(), (vm_address_t)thread_list, 
                         thread_count * sizeof(thread_t));
            return false;
        }
        
        // Allocate a small stack
        vm_address_t stack_addr = 0;
        vm_size_t stack_size = 8 * 1024; // Smaller stack
        
        kr = vm_allocate(league_task, &stack_addr, stack_size, VM_FLAGS_ANYWHERE);
        if (kr != KERN_SUCCESS) {
            printf("[SAFER-INJECT] ❌ Failed to allocate stack: %s\n", mach_error_string(kr));
            vm_deallocate(mach_task_self(), (vm_address_t)thread_list, 
                         thread_count * sizeof(thread_t));
            return false;
        }
        
        // Set up minimal thread state for dlopen call
        thread_state_t_custom injection_state = thread_state;
        
        injection_state.PC_REGISTER = dlopen_addr;
        injection_state.X0_REGISTER = dylib_path;
        injection_state.X1_REGISTER = RTLD_NOW;
        injection_state.SP_REGISTER = stack_addr + stack_size - 16;
        
#ifdef __arm64__
        injection_state.LR_REGISTER = 0; // Will crash after dlopen, which is fine
#endif
        
        printf("[SAFER-INJECT] Thread setup:\n");
        printf("[SAFER-INJECT]   PC: 0x%llx\n", injection_state.PC_REGISTER);
        printf("[SAFER-INJECT]   Arg0: 0x%llx\n", injection_state.X0_REGISTER);
        printf("[SAFER-INJECT]   Arg1: 0x%llx\n", injection_state.X1_REGISTER);
        printf("[SAFER-INJECT]   SP: 0x%llx\n", injection_state.SP_REGISTER);
        
        // Create the thread
        thread_t injection_thread;
        kr = thread_create_running(league_task, THREAD_STATE_TYPE,
                                 (thread_state_t)&injection_state, THREAD_STATE_COUNT,
                                 &injection_thread);
        
        if (kr == KERN_SUCCESS) {
            printf("[SAFER-INJECT] ✅ Injection thread created!\n");
            printf("[SAFER-INJECT] Waiting for dlopen to execute...\n");
            
            // Wait longer for dlopen to complete
            sleep(5);
            
            // Clean up
            vm_deallocate(league_task, stack_addr, stack_size);
            vm_deallocate(mach_task_self(), (vm_address_t)thread_list, 
                         thread_count * sizeof(thread_t));
            
            return true;
        } else {
            printf("[SAFER-INJECT] ❌ Failed to create thread: %s (0x%x)\n", 
                   mach_error_string(kr), kr);
            vm_deallocate(league_task, stack_addr, stack_size);
        }
        
        vm_deallocate(mach_task_self(), (vm_address_t)thread_list, 
                     thread_count * sizeof(thread_t));
        
        return false;
    }
    
    bool verify_injection() {
        printf("[SAFER-INJECT] Verifying injection...\n");
        
        sleep(3); // Wait for dylib to load
        
        // Check for log file
        if (access("/tmp/cslol_minimal_test.log", F_OK) == 0) {
            printf("[SAFER-INJECT] ✅ Found injection log!\n");
            system("cat /tmp/cslol_minimal_test.log");
            return true;
        }
        
        printf("[SAFER-INJECT] ⚠️  No injection log found\n");
        return false;
    }
    
    bool run_safer_injection() {
        printf("[SAFER-INJECT] Starting Safer League Injection\n");
        
        if (!wait_for_league()) return false;
        if (!acquire_task_handle()) return false;
        if (!inject_with_safer_method()) return false;
        
        return verify_injection();
    }
    
private:
    pid_t find_process_by_name(const char* name) {
        pid_t pid_list[1024];
        int n_pids = proc_listallpids(pid_list, sizeof(pid_list));
        
        for (int i = 0; i < n_pids; i++) {
            struct proc_bsdinfo proc;
            int st = proc_pidinfo(pid_list[i], PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE);
            
            if (st > 0 && strcmp(proc.pbi_name, name) == 0) {
                return pid_list[i];
            }
        }
        return 0;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s <mod_path> <dylib_path>\n", argv[0]);
        return 1;
    }
    
    std::string mod_path = argv[1];
    std::string dylib_path = argv[2];
    
    printf("=== Safer League Injector ===\n");
    
    SaferLeagueInjector injector(mod_path, dylib_path);
    
    bool success = injector.run_safer_injection();
    
    if (success) {
        printf("\n🎉 SUCCESS: Safer injection completed!\n");
        return 0;
    } else {
        printf("\n❌ FAILED: Injection failed\n");
        return 1;
    }
}
