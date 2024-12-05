#include "headers.h"
#include <string.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_PROCESSES 100
#define SHKEY_ALGO 400  // Key for shared memory segment for algorithm and quantum

// Shared memory for algorithm choice
int shmid_algo;
int *shm_algo;
int msgid;

// Define the process structure
typedef struct Process {
    int id;
    int arrival_time;
    int runtime;
    int priority;
} Process;

// Reads processes from the input file
void read_processes(const char* filename, Process processes[], int* count);

void clearResources(int);

int main() {
    signal(SIGINT, clearResources);

    // Read processes from input file
    Process processes[MAX_PROCESSES];
    int process_count = 0;
    read_processes("processes.txt", processes, &process_count);

    printf("Read %d processes from file:\n", process_count);
    for (int i = 0; i < process_count; i++) {
        printf("ID: %d, Arrival: %d, Runtime: %d, Priority: %d\n",
               processes[i].id,
               processes[i].arrival_time,
               processes[i].runtime,
               processes[i].priority);
    }

    // Ask for scheduling algorithm choice
    printf("Select a scheduling algorithm:\n");
    printf("1. Shortest Job First (SJF)\n");
    printf("2. Preemptive Highest Priority First (PHPF)\n");
    printf("3. Round Robin (RR)\n");
    int algorithm_choice;
    scanf("%d", &algorithm_choice);
    // Initialize quantum value
    int quantum = 0;
    if (algorithm_choice == 3) {
        printf("Enter the time quantum for Round Robin: ");
        scanf("%d", &quantum);
    }

    // Create shared memory for algorithm choice
    shmid_algo = shmget(SHKEY_ALGO, sizeof(int), 0666 | IPC_CREAT);
    if (shmid_algo == -1) {
        perror("Error creating shared memory for algorithm choice");
        exit(EXIT_FAILURE);
    }
    shm_algo = (int*)shmat(shmid_algo, NULL, 0);
    if (shm_algo == (void*)-1) {
        perror("Error attaching shared memory for algorithm choice");
        exit(EXIT_FAILURE);
    }
    *shm_algo = algorithm_choice;
    // Write algorithm choice and quantum to shared memory
    shm_algo[0] = algorithm_choice;
    shm_algo[1] = quantum;

    // Fork and execute Scheduler and Clock
    pid_t scheduler_pid = fork();
    if (scheduler_pid == -1) {
        perror("Error executing Scheduler");
        exit(EXIT_FAILURE);
    } else if (scheduler_pid == 0) {
        execl("./scheduler.out", "scheduler.out", NULL);
    }

    pid_t clock_pid = fork();
    if (clock_pid == -1) {
        perror("Error executing Clock");
        exit(EXIT_FAILURE);
    }
    else if (clock_pid == 0) {
        execl("./clk.out", "clk.out", NULL);
    }

    // Initialize the clock
    initClk();

    // Create message queue
    key_t key = ftok("scheduler", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) {
        perror("Error creating message queue");
        exit(EXIT_FAILURE);
    }

    // Send processes to scheduler
    for (int i = 0; i < process_count; i++) {
        while (getClk() < processes[i].arrival_time) {
            sleep(1);
        }

        struct msg_buffer message;
        message.message_type = 1;
        message.process.id = processes[i].id;
        message.process.arrival_time = processes[i].arrival_time;
        message.process.runtime = processes[i].runtime;
        message.process.priority = processes[i].priority;

        if (msgsnd(msgid, &message, sizeof(message.process), 0) == -1) {
            perror("Error sending message to Scheduler");
            exit(EXIT_FAILURE);
        }

        printf("Process Generator: Sent process %d to Scheduler at time %d\n",
               processes[i].id, getClk());
    }

    // Notify scheduler last process has been sent
    // by sending a process with id -1
    struct msg_buffer message;
    message.message_type = 1;
    message.process.id = -1;
    message.process.arrival_time = -1;
    message.process.runtime = 999;
    message.process.priority = 999;
    if (msgsnd(msgid, &message, sizeof(message.process), 0) == -1) {
        perror("Error sending message to Scheduler");
        exit(EXIT_FAILURE);
    } else {
        printf("Process Generator: Sent all processes to Scheduler.\n");
    }

    // Wait for child processes
    waitpid(scheduler_pid, NULL, 0);

    // Cleanup
    destroyClk(true);
    shmdt(shm_algo);
    shmctl(shmid_algo, IPC_RMID, NULL);
    msgctl(msgid, IPC_RMID, NULL);

    return 0;
}


void clearResources(int signum) {
    destroyClk(true);
    shmdt(shm_algo);
    shmctl(shmid_algo, IPC_RMID, NULL);
    msgctl(msgid, IPC_RMID, NULL);
    printf("Process Generator: Resources cleared and exiting.\n");
    exit(0);
}

void read_processes(const char* filename, Process processes[], int* count) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Error opening processes.txt");
        exit(EXIT_FAILURE);
    }

    char line[256];
    fgets(line, sizeof(line), file); // Skip the header line

    *count = 0;
    while (fscanf(file, "%d\t%d\t%d\t%d", &processes[*count].id,
        &processes[*count].arrival_time,
        &processes[*count].runtime,
        &processes[*count].priority) != EOF) {
        (*count)++;
        }

        fclose(file);
}

