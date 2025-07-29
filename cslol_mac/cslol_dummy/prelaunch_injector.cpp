// simple_dyld_injector.cpp - Use DYLD_INSERT_LIBRARIES for safe injection
#include <iostream>
#include <string>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <filesystem>
#include <sys/stat.h>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s <mod_path> <dylib_path>\n", argv[0]);
        printf("Example: %s \"/Users/user/Library/Application Support/moonshadow565/customskinlol-manager/profiles/Default Profile/\" ./libcslol_minimal_test_x86_updated.dylib\n", argv[0]);
        return 1;
    }
    
    std::string mod_path = argv[1];
    std::string dylib_path = argv[2];
    
    printf("=== DYLD_INSERT_LIBRARIES League Injector ===\n");
    
    // Verify paths exist
    if (!std::filesystem::exists(mod_path)) {
        printf("❌ Mod path does not exist: %s\n", mod_path.c_str());
        return 1;
    }
    
    if (!std::filesystem::exists(dylib_path)) {
        printf("❌ Dylib does not exist: %s\n", dylib_path.c_str());
        return 1;
    }
    
    // Get absolute path to dylib
    char abs_dylib_path[PATH_MAX];
    if (!realpath(dylib_path.c_str(), abs_dylib_path)) {
        printf("❌ Could not resolve dylib path\n");
        return 1;
    }
    
    printf("✅ Mod path: %s\n", mod_path.c_str());
    printf("✅ Dylib path: %s\n", abs_dylib_path);
    
    // Create injection script
    std::string script_path = "/tmp/league_injection_setup.sh";
    FILE* script = fopen(script_path.c_str(), "w");
    if (!script) {
        printf("❌ Could not create setup script\n");
        return 1;
    }
    
    fprintf(script, "#!/bin/bash\n");
    fprintf(script, "# League of Legends Mod Injection Setup\n");
    fprintf(script, "export DYLD_INSERT_LIBRARIES=\"%s\"\n", abs_dylib_path);
    fprintf(script, "export CSLOL_MOD_PATH=\"%s\"\n", mod_path.c_str());
    fprintf(script, "export DYLD_FORCE_FLAT_NAMESPACE=1\n");
    fprintf(script, "echo \"[DYLD-INJECT] Environment ready for League injection\"\n");
    fprintf(script, "echo \"[DYLD-INJECT] DYLD_INSERT_LIBRARIES=%s\"\n", abs_dylib_path);
    fprintf(script, "echo \"[DYLD-INJECT] CSLOL_MOD_PATH=%s\"\n", mod_path.c_str());
    fprintf(script, "echo \"[DYLD-INJECT] Now launch League from the client!\"\n");
    
    fclose(script);
    
    // Make script executable
    chmod(script_path.c_str(), 0755);
    
    printf("\n=== INJECTION SETUP COMPLETE ===\n");
    printf("A setup script has been created: %s\n", script_path.c_str());
    printf("\nTo inject mods into League:\n");
    printf("1. Open a new terminal\n");
    printf("2. Run: source %s\n", script_path.c_str());
    printf("3. Launch League from that same terminal with: open '/Applications/League of Legends.app'\n");
    printf("4. Or find the League executable and run it directly from that terminal\n");
    printf("\nAlternatively, set the environment variables manually:\n");
    printf("export DYLD_INSERT_LIBRARIES=\"%s\"\n", abs_dylib_path);
    printf("export CSLOL_MOD_PATH=\"%s\"\n", mod_path.c_str());
    printf("export DYLD_FORCE_FLAT_NAMESPACE=1\n");
    printf("\nThen launch League from that terminal session.\n");
    printf("\n⚠️  IMPORTANT: League must be launched from a terminal with these environment variables set!\n");
    
    return 0;
}
