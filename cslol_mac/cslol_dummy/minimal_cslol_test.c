// minimal_cslol_test.c - Minimal CSLOL dylib for testing
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

// Write to a log file that we can check
void write_log(const char* message) {
    FILE* log = fopen("/tmp/cslol_minimal_test.log", "a");
    if (log) {
        fprintf(log, "[CSLOL] %s\n", message);
        fflush(log);
        fclose(log);
    }
}

// Constructor that runs when dylib is loaded
__attribute__((constructor))
void cslol_init() {
    write_log("=== CSLOL Minimal Test Dylib Loaded ===");
    
    // Log basic info
    char msg[512];
    snprintf(msg, sizeof(msg), "Process PID: %d", getpid());
    write_log(msg);
    
    // Check environment variables
    const char* mod_path = getenv("CSLOL_MOD_PATH");
    if (mod_path) {
        snprintf(msg, sizeof(msg), "CSLOL_MOD_PATH: %s", mod_path);
        write_log(msg);
        
        // Check if mod path exists
        struct stat st;
        if (stat(mod_path, &st) == 0) {
            write_log("Mod path exists and is accessible");
        } else {
            write_log("WARNING: Mod path does not exist or is not accessible");
        }
    } else {
        write_log("WARNING: CSLOL_MOD_PATH environment variable not set");
    }
    
    // Check if we're in the right process
    const char* proc_name = getprogname();
    if (proc_name) {
        snprintf(msg, sizeof(msg), "Process name: %s", proc_name);
        write_log(msg);
        
        if (strstr(proc_name, "League") || strstr(proc_name, "league")) {
            write_log("✅ Successfully injected into League process!");
        } else {
            write_log("⚠️  Injected into non-League process");
        }
    }
    
    write_log("=== CSLOL Minimal Test Initialization Complete ===");
}

// Destructor that runs when dylib is unloaded
__attribute__((destructor))
void cslol_cleanup() {
    write_log("=== CSLOL Minimal Test Dylib Unloaded ===");
}
