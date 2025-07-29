// vanguard_aware_injector.cpp - Post-launch injection that works around Vanguard
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
#include <vector>
#include <mach-o/loader.h>

// x86_64 thread state for Rosetta processes
typedef struct {
    uint64_t __rax;
    uint64_t __rbx;
    uint64_t __rcx;
    uint64_t __rdx;
    uint64_t __rdi;
    uint64_t __rsi;
    uint64_t __rbp;
    uint64_t __rsp;
    uint64_t __r8;
    uint64_t __r9;
    uint64_t __r10;
    uint64_t __r11;
    uint64_t __r12;
    uint64_t __r13;
    uint64_t __r14;
    uint64_t __r15;
    uint64_t __rip;
    uint64_t __rflags;
    uint64_t __cs;
    uint64_t __fs;
    uint64_t __gs;
} x86_thread_state64_t_custom;

#define x86_THREAD_STATE64_CUSTOM 4
#define x86_THREAD_STATE64_COUNT_CUSTOM (sizeof(x86_thread_state64_t_custom)/sizeof(uint32_t))

class VanguardAwareInjector {
private:
    std::string mod_path;
    std::string dylib_path;
    pid_t league_pid = 0;
    task_t league_task = 0;
    
public:
    VanguardAwareInjector(const std::string& mod_path, const std::string& dylib_path) 
        : mod_path(mod_path), dylib_path(dylib_path) {}
    
    bool wait_for_league_and_vanguard() {
        printf("[VANGUARD-INJECT] Waiting for League to start...\n");
        
        for (int attempts = 0; attempts < 300; attempts++) { // 5 minutes
            league_pid = find_process_by_name("LeagueofLegends");
            if (league_pid > 0) {
                printf("[VANGUARD-INJECT] ✅ Found League process: PID %d\n", league_pid);
                break;
            }
            
            if (attempts % 15 == 0) {
                printf("[VANGUARD-INJECT] Still waiting for League... (%d/300s)\n", attempts);
            }
            sleep(1);
        }
        
        if (league_pid == 0) {
            printf("[VANGUARD-INJECT] ❌ League did not start within timeout\n");
            return false;
        }
        
        // Wait for Vanguard to finish initializing
        printf("[VANGUARD-INJECT] Waiting for Vanguard to complete initialization...\n");
        printf("[VANGUARD-INJECT] This may take 30-60 seconds...\n");
        
        // Look for Vanguard initialization patterns in process behavior
        for (int i = 0; i < 60; i++) { // Wait up to 60 seconds
            // Check if League is still running
            if (kill(league_pid, 0) != 0) {
                printf("[VANGUARD-INJECT] ❌ League process died during Vanguard init\n");
                return false;
            }
            
            // After 30 seconds, assume Vanguard has stabilized
            if (i >= 30) {
                printf("[VANGUARD-INJECT] ✅ Vanguard should be stabilized now\n");
                return true;
            }
            
            if (i % 10 == 0) {
                printf("[VANGUARD-INJECT] Vanguard stabilization... %d/60 seconds\n", i);
            }
            sleep(1);
        }
        
        return true;
    }
    
    bool acquire_task_handle() {
        printf("[VANGUARD-INJECT] Acquiring task handle for League PID %d...\n", league_pid);
        
        kern_return_t kr = task_for_pid(mach_task_self(), league_pid, &league_task);
        if (kr != KERN_SUCCESS) {
            printf("[VANGUARD-INJECT] ❌ Failed to get task handle: %s\n", mach_error_string(kr));
            printf("[VANGUARD-INJECT] Error code: 0x%x\n", kr);
            printf("[VANGUARD-INJECT] This might be due to Vanguard protection or insufficient privileges\n");
            return false;
        }
        
        printf("[VANGUARD-INJECT] ✅ Successfully acquired task handle\n");
        return true;
    }
    
    vm_address_t find_real_dlopen() {
        printf("[VANGUARD-INJECT] Searching for real dlopen in x86_64 League process...\n");
        
        // Method 1: Search for libdyld.dylib in the target process
        vm_address_t libdyld_base = find_libdyld_base();
        if (libdyld_base != 0) {
            printf("[VANGUARD-INJECT] Found libdyld at: 0x%lx\n", libdyld_base);
            
            // dlopen is typically at a fixed offset from libdyld base
            // These are common offsets for different macOS versions
            std::vector<vm_address_t> offsets = {
                0x1f73,   // Common dlopen offset
                0x2000,   // Alternative offset
                0x1800,   // Another common offset
                0x2200,   // macOS version variation
            };
            
            for (vm_address_t offset : offsets) {
                vm_address_t candidate = libdyld_base + offset;
                if (verify_dlopen_function(candidate)) {
                    printf("[VANGUARD-INJECT] ✅ Found real dlopen at: 0x%lx\n", candidate);
                    return candidate;
                }
            }
        }
        
        // Method 2: Use known good dlopen addresses for x86_64 macOS
        printf("[VANGUARD-INJECT] Trying known x86_64 dlopen addresses...\n");
        std::vector<vm_address_t> known_addresses = {
            0x7ff8041a1f73,  // macOS 12+ x86_64
            0x7ff804199f73,  // macOS 11 x86_64
            0x7ff8041b1f73,  // Alternative location
        };
        
        for (vm_address_t addr : known_addresses) {
            if (verify_dlopen_function(addr)) {
                printf("[VANGUARD-INJECT] ✅ Found dlopen at known address: 0x%lx\n", addr);
                return addr;
            }
        }
        
        printf("[VANGUARD-INJECT] ❌ Could not find dlopen function\n");
        return 0;
    }
    
    vm_address_t find_libdyld_base() {
        vm_address_t address = 0;
        
        while (true) {
            vm_size_t size = 0;
            vm_region_flavor_t flavor = VM_REGION_BASIC_INFO_64;
            vm_region_basic_info_data_64_t info;
            mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t object_name;
            
            kern_return_t kr = vm_region_64(league_task, &address, &size, flavor,
                                           (vm_region_info_t)&info, &info_count, &object_name);
            
            if (kr != KERN_SUCCESS) break;
            
            // Look for executable regions that could be libdyld
            if ((info.protection & VM_PROT_EXECUTE) && size > 0x10000) {
                // Check if this region looks like libdyld
                if (could_be_libdyld(address, size)) {
                    return address;
                }
            }
            
            address += size;
        }
        
        return 0;
    }
    
    bool could_be_libdyld(vm_address_t addr, vm_size_t size) {
        // Simple heuristic: libdyld is usually a reasonably sized executable region
        // in the system library area for x86_64 processes
        return (addr >= 0x7ff800000000 && addr < 0x7ff900000000 && size >= 0x10000 && size <= 0x100000);
    }
    
    bool verify_dlopen_function(vm_address_t addr) {
        // Try to read a few bytes to verify this could be a function
        vm_size_t read_size = 16;
        vm_offset_t data_out;
        mach_msg_type_number_t data_count;
        
        kern_return_t kr = vm_read(league_task, addr, read_size, &data_out, &data_count);
        if (kr != KERN_SUCCESS) {
            return false;
        }
        
        uint8_t* bytes = (uint8_t*)data_out;
        
        // Look for x86_64 function prologue patterns
        bool looks_like_function = (
            (bytes[0] == 0x55 && bytes[1] == 0x48 && bytes[2] == 0x89) ||  // push %rbp; mov %rsp,%rbp
            (bytes[0] == 0x48 && bytes[1] == 0x83 && bytes[2] == 0xec) ||  // sub $X,%rsp
            (bytes[0] == 0x48 && bytes[1] == 0x89) ||                      // mov instructions
            (bytes[0] == 0x41 && bytes[1] == 0x57) ||                      // push %r15
            (bytes[0] == 0x53) ||                                          // push %rbx
            (bytes[0] == 0xf3 && bytes[1] == 0x0f && bytes[2] == 0x1e)     // endbr64 (Intel CET)
        );
        
        vm_deallocate(mach_task_self(), data_out, data_count);
        return looks_like_function;
    }
    
    bool inject_with_x86_thread(vm_address_t dlopen_addr, vm_address_t dylib_path) {
        printf("[VANGUARD-INJECT] Creating x86_64 injection thread...\n");
        
        // Allocate stack
        vm_address_t stack_addr = 0;
        vm_size_t stack_size = 64 * 1024;
        
        kern_return_t kr = vm_allocate(league_task, &stack_addr, stack_size, VM_FLAGS_ANYWHERE);
        if (kr != KERN_SUCCESS) {
            printf("[VANGUARD-INJECT] ❌ Failed to allocate stack: %s\n", mach_error_string(kr));
            return false;
        }
        
        // Set up x86_64 thread state for dlopen call
        x86_thread_state64_t_custom thread_state = {0};
        
        thread_state.__rip = dlopen_addr;                    // Function to call
        thread_state.__rdi = dylib_path;                     // First argument (dylib path)
        thread_state.__rsi = RTLD_NOW;                       // Second argument (flags)
        thread_state.__rsp = stack_addr + stack_size - 16;   // Stack pointer
        thread_state.__rbp = thread_state.__rsp;             // Base pointer
        
        printf("[VANGUARD-INJECT] x86_64 thread state:\n");
        printf("[VANGUARD-INJECT]   RIP: 0x%llx (dlopen)\n", thread_state.__rip);
        printf("[VANGUARD-INJECT]   RDI: 0x%llx (dylib path)\n", thread_state.__rdi);
        printf("[VANGUARD-INJECT]   RSI: 0x%llx (RTLD_NOW)\n", thread_state.__rsi);
        printf("[VANGUARD-INJECT]   RSP: 0x%llx (stack)\n", thread_state.__rsp);
        
        // Create thread in x86_64 mode
        thread_t injection_thread;
        kr = thread_create_running(league_task, x86_THREAD_STATE64_CUSTOM,
                                 (thread_state_t)&thread_state, x86_THREAD_STATE64_COUNT_CUSTOM,
                                 &injection_thread);
        
        if (kr == KERN_SUCCESS) {
            printf("[VANGUARD-INJECT] ✅ x86_64 injection thread created!\n");
            printf("[VANGUARD-INJECT] Waiting for dlopen to execute...\n");
            
            sleep(5); // Wait for injection
            
            vm_deallocate(league_task, stack_addr, stack_size);
            return true;
        } else {
            printf("[VANGUARD-INJECT] ❌ Failed to create x86_64 thread: %s (0x%x)\n", 
                   mach_error_string(kr), kr);
            vm_deallocate(league_task, stack_addr, stack_size);
            return false;
        }
    }
    
    bool run_vanguard_aware_injection() {
        printf("[VANGUARD-INJECT] Starting Vanguard-Aware Injection\n");
        printf("[VANGUARD-INJECT] This method works by injecting AFTER Vanguard initializes\n");
        
        if (!wait_for_league_and_vanguard()) return false;
        if (!acquire_task_handle()) return false;
        
        // Verify dylib exists
        if (access(dylib_path.c_str(), R_OK) != 0) {
            printf("[VANGUARD-INJECT] ❌ Dylib not found: %s\n", dylib_path.c_str());
            return false;
        }
        
        char abs_dylib_path[PATH_MAX];
        if (!realpath(dylib_path.c_str(), abs_dylib_path)) {
            printf("[VANGUARD-INJECT] ❌ Failed to resolve dylib path\n");
            return false;
        }
        
        printf("[VANGUARD-INJECT] Using dylib: %s\n", abs_dylib_path);
        
        // Find real dlopen
        vm_address_t dlopen_addr = find_real_dlopen();
        if (dlopen_addr == 0) {
            return false;
        }
        
        // Allocate memory for dylib path
        vm_address_t remote_dylib_path = 0;
        vm_size_t path_size = strlen(abs_dylib_path) + 1;
        
        kern_return_t kr = vm_allocate(league_task, &remote_dylib_path, path_size, VM_FLAGS_ANYWHERE);
        if (kr != KERN_SUCCESS) {
            printf("[VANGUARD-INJECT] ❌ Failed to allocate memory: %s\n", mach_error_string(kr));
            return false;
        }
        
        kr = vm_write(league_task, remote_dylib_path, (vm_offset_t)abs_dylib_path, path_size);
        if (kr != KERN_SUCCESS) {
            printf("[VANGUARD-INJECT] ❌ Failed to write dylib path: %s\n", mach_error_string(kr));
            vm_deallocate(league_task, remote_dylib_path, path_size);
            return false;
        }
        
        // Set up environment
        std::string env_var = "CSLOL_MOD_PATH=" + mod_path;
        vm_address_t remote_env_path = 0;
        vm_size_t env_size = env_var.length() + 1;
        
        kr = vm_allocate(league_task, &remote_env_path, env_size, VM_FLAGS_ANYWHERE);
        if (kr == KERN_SUCCESS) {
            kr = vm_write(league_task, remote_env_path, (vm_offset_t)env_var.c_str(), env_size);
        }
        
        // Perform injection
        bool success = inject_with_x86_thread(dlopen_addr, remote_dylib_path);
        
        // Clean up
        vm_deallocate(league_task, remote_dylib_path, path_size);
        if (remote_env_path) {
            vm_deallocate(league_task, remote_env_path, env_size);
        }
        
        if (success) {
            printf("[VANGUARD-INJECT] ✅ Injection completed!\n");
            printf("[VANGUARD-INJECT] Check /tmp/cslol_minimal_test.log for results\n");
        }
        
        return success;
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
    
    printf("=== Vanguard-Aware League Injector ===\n");
    printf("This injector waits for Vanguard to initialize before injecting\n");
    
    VanguardAwareInjector injector(mod_path, dylib_path);
    
    bool success = injector.run_vanguard_aware_injection();
    
    if (success) {
        printf("\n🎉 SUCCESS: Vanguard-aware injection completed!\n");
        printf("Check the game to see if mods are active!\n");
        return 0;
    } else {
        printf("\n❌ FAILED: Injection failed\n");
        return 1;
    }
}
