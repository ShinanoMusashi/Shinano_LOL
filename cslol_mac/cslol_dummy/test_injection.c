// test_injection.c - Simple dylib to test injection
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Constructor that runs when dylib is loaded
__attribute__((constructor))
void on_load() {
    printf("🎉 SUCCESS: Test dylib loaded into PID %d!\n", getpid());
    
    // Write to a log file to verify injection
    FILE* log = fopen("/tmp/injection_test.log", "w");
    if (log) {
        fprintf(log, "Injection successful into PID %d\n", getpid());
        fclose(log);
    }
    
    // Also check environment variable
    const char* mod_path = getenv("CSLOL_MOD_PATH");
    if (mod_path) {
        printf("✅ Found CSLOL_MOD_PATH: %s\n", mod_path);
    } else {
        printf("❌ CSLOL_MOD_PATH not found\n");
    }
}
