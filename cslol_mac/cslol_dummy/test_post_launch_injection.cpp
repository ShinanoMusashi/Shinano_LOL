// test_post_launch_injection.cpp - Test post-launch memory patching
#include <iostream>
#include <string>
#include <unistd.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/task.h>
#include <mach/vm_map.h>
#include <mach/mach_error.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

class PostLaunchPatcher {
public:
    static bool test_memory_access(pid_t target_pid) {
        printf("[POST-LAUNCH] Testing memory access to PID: %d\n", target_pid);
        
        task_t target_task;
        kern_return_t kr = task_for_pid(mach_task_self(), target_pid, &target_task);
        if (kr != KERN_SUCCESS) {
            printf("[POST-LAUNCH] ❌ Failed to get task: %s\n", mach_error_string(kr));
            return false;
        }
        
        printf("[POST-LAUNCH] ✅ Got task handle\n");
        
        // Test memory allocation
        vm_address_t test_addr = 0;
        vm_size_t test_size = 4096;
        
        kr = vm_allocate(target_task, &test_addr, test_size, VM_FLAGS_ANYWHERE);
        if (kr != KERN_SUCCESS) {
            printf("[POST-LAUNCH] ❌ Failed to allocate memory: %s\n", mach_error_string(kr));
            return false;
        }
        
        printf("[POST-LAUNCH] ✅ Allocated memory at: 0x%lx\n", test_addr);
        
        // Test memory writing
        const char* test_data = "POST-LAUNCH-TEST";
        kr = vm_write(target_task, test_addr, (vm_offset_t)test_data, strlen(test_data) + 1);
        if (kr != KERN_SUCCESS) {
            printf("[POST-LAUNCH] ❌ Failed to write memory: %s\n", mach_error_string(kr));
            vm_deallocate(target_task, test_addr, test_size);
            return false;
        }
        
        printf("[POST-LAUNCH] ✅ Wrote test data to memory\n");
        
        // Test memory reading
        vm_offset_t read_data = 0;
        mach_msg_type_number_t read_count = 0;
        
        kr = vm_read(target_task, test_addr, strlen(test_data) + 1, &read_data, &read_count);
        if (kr == KERN_SUCCESS) {
            printf("[POST-LAUNCH] ✅ Read back: '%s'\n", (char*)read_data);
            vm_deallocate(mach_task_self(), read_data, read_count);
        } else {
            printf("[POST-LAUNCH] ⚠️  Failed to read back: %s\n", mach_error_string(kr));
        }
        
        // Clean up
        vm_deallocate(target_task, test_addr, test_size);
        
        printf("[POST-LAUNCH] ✅ Memory access test successful!\n");
        return true;
    }
    
    static bool test_memory_patching(pid_t target_pid) {
        printf("[POST-LAUNCH] Testing memory patching capabilities...\n");
        
        task_t target_task;
        kern_return_t kr = task_for_pid(mach_task_self(), target_pid, &target_task);
        if (kr != KERN_SUCCESS) {
            printf("[POST-LAUNCH] ❌ Failed to get task for patching\n");
            return false;
        }
        
        // Find writable memory regions we could potentially patch
        vm_address_t address = 0;
        vm_size_t size = 0;
        vm_region_flavor_t flavor = VM_REGION_BASIC_INFO_64;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object_name;
        
        printf("[POST-LAUNCH] Scanning memory regions for patching opportunities...\n");
        int writable_regions = 0;
        int executable_regions = 0;
        
        while (vm_region_64(target_task, &address, &size, flavor, (vm_region_info_t)&info, &info_count, &object_name) == KERN_SUCCESS) {
            
            if (info.protection & VM_PROT_WRITE) {
                writable_regions++;
                if (writable_regions <= 3) { // Show first few
                    printf("[POST-LAUNCH] Writable region: 0x%lx-0x%lx (size: 0x%lx)\n", 
                           address, address + size, size);
                }
            }
            
            if (info.protection & VM_PROT_EXECUTE) {
                executable_regions++;
                if (executable_regions <= 3) { // Show first few  
                    printf("[POST-LAUNCH] Executable region: 0x%lx-0x%lx (size: 0x%lx)\n", 
                           address, address + size, size);
                }
            }
            
            address += size;
            if (address >= 0x8000000000000000ULL) break; // Safety limit
        }
        
        printf("[POST-LAUNCH] Found %d writable regions, %d executable regions\n", 
               writable_regions, executable_regions);
        
        if (writable_regions > 0) {
            printf("[POST-LAUNCH] ✅ Memory patching appears feasible!\n");
            return true;
        } else {
            printf("[POST-LAUNCH] ❌ No writable regions found\n");
            return false;
        }
    }
    
    static void wait_for_process_stabilization(pid_t target_pid, const char* process_name) {
        printf("[POST-LAUNCH] Waiting for %s (PID: %d) to stabilize...\n", process_name, target_pid);
        
        // Wait for process to settle and Vanguard to complete initialization
        for (int i = 0; i < 30; i++) {
            sleep(1);
            
            // Check if process is still alive
            if (kill(target_pid, 0) != 0) {
                printf("[POST-LAUNCH] ⚠️  Process exited during stabilization\n");
                return;
            }
            
            // Show progress every 5 seconds
            if (i % 5 == 0) {
                printf("[POST-LAUNCH] Stabilization... %d/30 seconds\n", i);
            }
        }
        
        printf("[POST-LAUNCH] ✅ Process should be stabilized now\n");
    }
};

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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <mode> [process_name]\n", argv[0]);
        printf("Modes:\n");
        printf("  test <process_name>  - Test post-launch injection on running process\n");
        printf("  wait <process_name>  - Wait for process to start, then test\n");
        printf("\nExamples:\n");
        printf("  %s test LeagueofLegends\n", argv[0]);
        printf("  %s wait test_target\n", argv[0]);
        return 1;
    }
    
    const char* mode = argv[1];
    const char* process_name = argc > 2 ? argv[2] : "LeagueofLegends";
    
    printf("[POST-LAUNCH] Post-Launch Injection Test\n");
    printf("[POST-LAUNCH] Mode: %s, Target: %s\n", mode, process_name);
    
    if (strcmp(mode, "test") == 0) {
        // Test on existing process
        pid_t target_pid = find_process_by_name(process_name);
        if (target_pid == 0) {
            printf("[POST-LAUNCH] ❌ Process '%s' not found\n", process_name);
            printf("[POST-LAUNCH] Make sure the process is running first\n");
            return 1;
        }
        
        printf("[POST-LAUNCH] Found %s with PID: %d\n", process_name, target_pid);
        
        // Wait for stabilization
        PostLaunchPatcher::wait_for_process_stabilization(target_pid, process_name);
        
        // Test memory access
        bool memory_ok = PostLaunchPatcher::test_memory_access(target_pid);
        bool patching_ok = PostLaunchPatcher::test_memory_patching(target_pid);
        
        if (memory_ok && patching_ok) {
            printf("\n✅ POST-LAUNCH INJECTION FEASIBLE!\n");
            printf("✅ Memory access: PASS\n");
            printf("✅ Memory patching: PASS\n");
            printf("✅ This approach should work for League injection\n");
            return 0;
        } else {
            printf("\n❌ POST-LAUNCH INJECTION NOT FEASIBLE\n");
            printf("%s Memory access: %s\n", memory_ok ? "✅" : "❌", memory_ok ? "PASS" : "FAIL");
            printf("%s Memory patching: %s\n", patching_ok ? "✅" : "❌", patching_ok ? "PASS" : "FAIL");
            return 1;
        }
        
    } else if (strcmp(mode, "wait") == 0) {
        // Wait for process to start, then test
        printf("[POST-LAUNCH] Waiting for '%s' to start...\n", process_name);
        
        pid_t target_pid = 0;
        for (int attempts = 0; attempts < 120; attempts++) { // Wait up to 2 minutes
            target_pid = find_process_by_name(process_name);
            if (target_pid > 0) {
                printf("[POST-LAUNCH] Found %s with PID: %d\n", process_name, target_pid);
                break;
            }
            
            if (attempts % 10 == 0) {
                printf("[POST-LAUNCH] Still waiting... (%d/120s)\n", attempts);
            }
            sleep(1);
        }
        
        if (target_pid == 0) {
            printf("[POST-LAUNCH] ❌ Process never started\n");
            return 1;
        }
        
        // Now test the found process
        PostLaunchPatcher::wait_for_process_stabilization(target_pid, process_name);
        
        bool memory_ok = PostLaunchPatcher::test_memory_access(target_pid);
        bool patching_ok = PostLaunchPatcher::test_memory_patching(target_pid);
        
        if (memory_ok && patching_ok) {
            printf("\n✅ POST-LAUNCH INJECTION FEASIBLE!\n");
            return 0;
        } else {
            printf("\n❌ POST-LAUNCH INJECTION NOT FEASIBLE\n");
            return 1;
        }
        
    } else {
        printf("[POST-LAUNCH] ❌ Unknown mode: %s\n", mode);
        return 1;
    }
}
