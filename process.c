#include "headers.h"
#include <unistd.h>

/* Modify this file as needed */
int remainingtime;
int clk;

int main(int argc, char *argv[]) // Fixed the parameter name from agrc to argc
{
    initClk();

    // Get remaining time from arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <remainingtime>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    remainingtime = atoi(argv[1]); // Convert argument to an integer

    while (remainingtime > 0)
    {
        // Simulate process running
        sleep(1);
        remainingtime--;
        printf("Process %d: remaining time = %d\n", getpid(), remainingtime);
    }

    printf("Process %d: Finished execution.\n", getpid());
    return 0;
}
