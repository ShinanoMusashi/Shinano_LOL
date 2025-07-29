// libcslol_minimal_test_x86.cpp - Updated test dylib with better process detection
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <libproc.h>
#include <mach/mach.h>

// Forward declarations
extern "C" void cslol_init() __attribute__((constructor));
extern "C" void cslol_cleanup() __attribute__((destructor));

void cslol_write_log(const char* format, ...) {
    FILE* log_file = fopen("/tmp/cslol_minimal_test.log", "a");
    if (log_file) {
        va_list args;
        va_start(args, format);
        fprintf(log_file, "[CSLOL] ");
        vfprintf(log_file, format, args);
        fprintf(log_file, "\n");
        va_end(args);
        fflush(log_file);
        fclose(log_file);
    }
}

const char* get_process_name() {
    static char process_name[256] = {0};
    
    // Method 1: Try libproc (more reliable)
    pid_t pid = getpid();
    struct proc_bsdinfo proc_info;
    int result = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &proc_info, PROC_PIDTBSDINFO_SIZE);
    
    if (result > 0) {
        strncpy(process_name, proc_info.pbi_name, sizeof(process_name) - 1);
        return process_name;
    }
    
    // Method 2: Fallback to getprogname
    const char* prog_name = getprogname();
    if (prog_name) {
        strncpy(process_name, prog_name, sizeof(process_name) - 1);
        return process_name;
    }
    
    return "unknown";
}

bool is_league_process() {
    const char* process_name = get_process_name();
    
    // Check for various League process names
    return (strstr(process_name, "League") != NULL ||
            strstr(process_name, "league") != NULL ||
            strcmp(process_name, "LeagueofLegends") == 0);
}

void log_process_info() {
    pid_t current_pid = getpid();
    const char* process_name = get_process_name();
    
    cslol_write_log("=== DETAILED PROCESS INFO ===");
    cslol_write_log("Current PID: %d", current_pid);
    cslol_write_log("Process name (libproc): %s", process_name);
    cslol_write_log("Process name (getprogname): %s", getprogname() ? getprogname() : "NULL");
    
    // Get parent process info too
    pid_t ppid = getppid();
    cslol_write_log("Parent PID: %d", ppid);
    
    if (ppid > 0) {
        struct proc_bsdinfo parent_info;
        int result = proc_pidinfo(ppid, PROC_PIDTBSDINFO, 0, &parent_info, PROC_PIDTBSDINFO_SIZE);
        if (result > 0) {
            cslol_write_log("Parent process name: %s", parent_info.pbi_name);
        }
    }
    
    // Check if we're in the right process
    if (is_league_process()) {
        cslol_write_log("✅ SUCCESS: Injected into League process!");
    } else {
        cslol_write_log("❌ ERROR: Injected into wrong process!");
        cslol_write_log("Expected: LeagueofLegends, Got: %s", process_name);
    }
}

void log_environment_info() {
    cslol_write_log("=== ENVIRONMENT INFO ===");
    
    // Get the mod path from environment
    const char* mod_path = getenv("CSLOL_MOD_PATH");
    if (mod_path) {
        cslol_write_log("CSLOL_MOD_PATH: %s", mod_path);
        
        // Check if the mod path exists
        struct stat st;
        if (stat(mod_path, &st) == 0) {
            cslol_write_log("✅ Mod path exists and is accessible");
            if (S_ISDIR(st.st_mode)) {
                cslol_write_log("✅ Mod path is a directory");
            } else {
                cslol_write_log("⚠️  Mod path is not a directory");
            }
        } else {
            cslol_write_log("❌ WARNING: Mod path does not exist or is not accessible");
        }
    } else {
        cslol_write_log("❌ WARNING: CSLOL_MOD_PATH environment variable not set");
    }
    
    // Log some other useful environment variables
    const char* dyld_path = getenv("DYLD_LIBRARY_PATH");
    if (dyld_path) {
        cslol_write_log("DYLD_LIBRARY_PATH: %s", dyld_path);
    }
    
    const char* user = getenv("USER");
    if (user) {
        cslol_write_log("USER: %s", user);
    }
}

void log_injection_timestamp() {
    cslol_write_log("=== INJECTION TIMESTAMP ===");
    
    // Get current time
    time_t rawtime;
    struct tm * timeinfo;
    char buffer[80];
    
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    cslol_write_log("Injection time: %s", buffer);
}

extern "C" void cslol_init() {
    // Clear the log file first
    FILE* log_file = fopen("/tmp/cslol_minimal_test.log", "w");
    if (log_file) {
        fclose(log_file);
    }
    
    cslol_write_log("=== CSLOL MINIMAL TEST DYLIB LOADED (UPDATED) ===");
    
    log_injection_timestamp();
    log_process_info();
    log_environment_info();
    
    cslol_write_log("=== CSLOL MINIMAL TEST INITIALIZATION COMPLETE ===");
}

extern "C" void cslol_cleanup() {
    cslol_write_log("=== CSLOL MINIMAL TEST DYLIB UNLOADED ===");
}
