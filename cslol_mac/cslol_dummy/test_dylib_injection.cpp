// test_dylib_injection.cpp - Simple test program to verify dylib injection works
#include <iostream>
#include <unistd.h>
#include <libproc.h>

int main() {
    std::cout << "=== Test Program for Dylib Injection ===" << std::endl;
    
    pid_t pid = getpid();
    
    // Get process name
    struct proc_bsdinfo proc_info;
    int result = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &proc_info, PROC_PIDTBSDINFO_SIZE);
    
    if (result > 0) {
        std::cout << "Process PID: " << pid << std::endl;
        std::cout << "Process name: " << proc_info.pbi_name << std::endl;
    }
    
    // Check environment variables
    const char* cslol_path = getenv("CSLOL_MOD_PATH");
    if (cslol_path) {
        std::cout << "CSLOL_MOD_PATH: " << cslol_path << std::endl;
    } else {
        std::cout << "CSLOL_MOD_PATH not set" << std::endl;
    }
    
    std::cout << "Sleeping for 10 seconds to allow dylib to initialize..." << std::endl;
    sleep(10);
    
    std::cout << "Test program completed" << std::endl;
    
    return 0;
}
