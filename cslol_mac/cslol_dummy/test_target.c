// universal_test_target.c - Can be compiled for both architectures
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/sysctl.h>

volatile int running = 1;

void signal_handler(int sig) {
    printf("Received signal %d, shutting down...\n", sig);
    running = 0;
}

void print_arch_info() {
    printf("Architecture info:\n");
    
#ifdef __x86_64__
    printf("  Compiled for: x86_64\n");
#elif defined(__arm64__)
    printf("  Compiled for: ARM64\n");
#else
    printf("  Compiled for: Unknown\n");
#endif

    // Check if running under Rosetta
    int ret = 0;
    size_t size = sizeof(ret);
    if (sysctlbyname("sysctl.proc_translated", &ret, &size, NULL, 0) == 0) {
        if (ret == 1) {
            printf("  Running under: Rosetta 2 (x86_64 emulation)\n");
        } else {
            printf("  Running under: Native execution\n");
        }
    } else {
        printf("  Running under: Could not determine\n");
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Universal test target started (PID: %d)\n", getpid());
    print_arch_info();
    printf("Waiting for injection... Press Ctrl+C to exit\n");
    
    int counter = 0;
    while(running) {
        if (counter % 10 == 0) {  // Print less frequently
            printf("Tick %d - still running...\n", counter);
        }
        sleep(1);
        counter++;
    }
    
    printf("Test target shutting down\n");
    return 0;
}
