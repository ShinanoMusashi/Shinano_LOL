#ifdef __APPLE__
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <filesystem>
#include <string>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>     // ADD THIS for getpid()
#include <time.h>       // ADD THIS for time()

__attribute__((constructor))
static void dylib_load() {
    // File-based debug logging
    FILE* debug = fopen("/tmp/cslol_debug.log", "a");
    if (debug) {
        fprintf(debug, "[CSLOL] Dylib loaded into PID: %d at %ld\n", getpid(), time(NULL));
        fflush(debug);
        fclose(debug);
    }
    
    printf("[CSLOL] Dylib loaded!\n");
    fflush(stdout);
}

// Function pointers to original functions
static FILE* (*original_fopen)(const char*, const char*) = nullptr;
static int (*original_open)(const char*, int, ...) = nullptr;

// Path prefix for mod files (set by environment variable)
static std::string mod_path_prefix;
static bool initialized = false;

// Initialize the interception
static void init_interception() {
    if (initialized) return;
    initialized = true;
    
    // Get original function pointers
    original_fopen = (FILE*(*)(const char*, const char*))dlsym(RTLD_NEXT, "fopen");
    original_open = (int(*)(const char*, int, ...))dlsym(RTLD_NEXT, "open");
    
    // Get mod path from environment
    if (const char* prefix = getenv("CSLOL_MOD_PATH")) {
        mod_path_prefix = prefix;
        if (!mod_path_prefix.empty() && mod_path_prefix.back() != '/') {
            mod_path_prefix += '/';
        }
    }
}

// Check if a path should be redirected to mod files
static std::string get_redirected_path(const char* path) {
    if (!path || mod_path_prefix.empty()) return {};
    
    std::string original_path(path);
    
    // Check if this is a League data file we want to intercept
    if (original_path.find("/DATA/FINAL/Champions/") != std::string::npos) {
        // Extract just the filename part after "DATA/FINAL/Champions/"
        size_t pos = original_path.find("/DATA/FINAL/Champions/");
        if (pos != std::string::npos) {
            std::string relative_path = original_path.substr(pos + 1); // Remove leading slash
            std::string mod_file_path = mod_path_prefix + relative_path;
            
            // Check if mod file exists
            if (std::filesystem::exists(mod_file_path)) {
                return mod_file_path;
            }
        }
    }
    
    return {}; // No redirection
}

// Intercepted fopen function
extern "C" FILE* fopen(const char* filename, const char* mode) {
    printf("[CSLOL] fopen called: %s\n", filename ? filename : "NULL");
    fflush(stdout);
    init_interception();
    
    if (std::string redirected = get_redirected_path(filename); !redirected.empty()) {
        // Use modded file
        return original_fopen(redirected.c_str(), mode);
    }
    
    // Use original file
    return original_fopen(filename, mode);
}

// Intercepted open function
extern "C" int open(const char* path, int flags, ...) {
    init_interception();
    
    if (std::string redirected = get_redirected_path(path); !redirected.empty()) {
        // Use modded file
        if (flags & O_CREAT) {
            va_list args;
            va_start(args, flags);
            int mode = va_arg(args, int);
            va_end(args);
            return original_open(redirected.c_str(), flags, mode);
        } else {
            return original_open(redirected.c_str(), flags);
        }
    }
    
    // Use original file
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        int mode = va_arg(args, int);
        va_end(args);
        return original_open(path, flags, mode);
    } else {
        return original_open(path, flags);
    }
}

#endif
