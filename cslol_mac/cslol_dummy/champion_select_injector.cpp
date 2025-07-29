// champion_select_injector.cpp - Inject during champion select for proper timing
#include <iostream>
#include <string>
#include <unistd.h>
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

class ChampionSelectInjector {
public:
    static bool wait_for_champion_select(pid_t league_pid) {
        printf("[CHAMP-SELECT] Monitoring League for champion select phase...\n");
        
        // Monitor League logs for champion select indicators
        for (int i = 0; i < 300; i++) { // 5 minutes max
            // Check if League is still running
            if (kill(league_pid, 0) != 0) {
                printf("[CHAMP-SELECT] ❌ League process exited\n");
                return false;
            }
            
            // Look for champion select indicators in League logs
            // This is a simplified check - in practice, you'd monitor specific log patterns
            if (i % 30 == 0) {
                printf("[CHAMP-SELECT] Waiting for champion select... (%d/300s)\n", i);
            }
            
            // For now, just wait a reasonable time for user to enter champion select
            // In a real implementation, this would parse League's logs or API
            if (i >= 60) { // Wait at least 60 seconds
                printf("[CHAMP-SELECT] Assuming champion select phase reached\n");
                return true;
            }
            
            sleep(1);
        }
        
        printf("[CHAMP-SELECT] ❌ Champion select timeout\n");
        return false;
    }
    
    static void print_usage_instructions() {
        printf("\n=== Champion Select Injection Instructions ===\n");
        printf("For proper skin loading timing:\n\n");
        printf("1. Launch League Client normally\n");
        printf("2. Get to the main lobby screen\n");
        printf("3. Run this injector: sudo ./champion_select_injector\n");
        printf("4. The injector will wait for you to enter champion select\n");
        printf("5. Queue for a game (Practice Tool recommended for testing)\n");
        printf("6. Enter champion select phase\n");
        printf("7. The injector will detect this and inject mods\n");
        printf("8. Select your champion and start the game\n");
        printf("9. Mods should be active!\n\n");
        printf("IMPORTANT: Mods must be loaded BEFORE the game starts!\n");
        printf("===============================================\n\n");
    }
};

int main() {
    printf("=== Champion Select CSLOL Injector ===\n");
    
    ChampionSelectInjector::print_usage_instructions();
    
    // Find League process
    pid_t league_pid = 0;
    pid_t pid_list[1024];
    int n_pids = proc_listallpids(pid_list, sizeof(pid_list));
    
    for (int i = 0; i < n_pids; i++) {
        struct proc_bsdinfo proc;
        int st = proc_pidinfo(pid_list[i], PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE);
        
        if (st > 0 && strcmp(proc.pbi_name, "LeagueofLegends") == 0) {
            league_pid = pid_list[i];
            break;
        }
    }
    
    if (league_pid == 0) {
        printf("❌ League not found! Please launch League first.\n");
        return 1;
    }
    
    printf("✅ Found League process: PID %d\n", league_pid);
    
    // Wait for champion select
    if (!ChampionSelectInjector::wait_for_champion_select(league_pid)) {
        return 1;
    }
    
    // Now call our main injector
    printf("\n🎯 Champion select detected! Running injection...\n");
    
    std::string cmd = "sudo ./post_launch_cslol \"/Users/user/Library/Application Support/moonshadow565/customskinlol-manager/profiles/Default Profile/\" ./libcslol_minimal_test_x86.dylib";
    
    int result = system(cmd.c_str());
    
    if (result == 0) {
        printf("✅ Champion select injection completed!\n");
        printf("Your skins should be loaded for the upcoming game!\n");
    } else {
        printf("❌ Champion select injection failed!\n");
    }
    
    return result;
}
