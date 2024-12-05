#include "headers.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <signal.h>

// Shared memory for algorithm choice
int shmid_algo;
int *shm_algo;
int msgid;

void clearResources(int signum);
void lastProcess(int signum);

#define SHKEY_ALGO 400 // Shared memory key for algorithm choice
FILE* log_file;

// Function to log process events
void log_event(int time, PCB* process, const char* state, float TA, float WTA) {
    if (strcmp(state, "Finished") == 0) {
        fprintf(log_file, "At time %d process %d %s arr %d total %d remain %d wait %d TA %.2f WTA %.2f\n",
                time, process->id, state, process->arrival_time, process->runtime,
                process->remaining_time, time - process->arrival_time - process->runtime, TA, WTA);
    } else {
        fprintf(log_file, "At time %d process %d %s arr %d total %d remain %d wait %d\n",
                time, process->id, state, process->arrival_time, process->runtime,
                process->remaining_time, time - process->arrival_time - (process->runtime - process->remaining_time));
    }
}

// Function to write performance metrics
void write_performance_metrics(int total_runtime, int total_waiting_time, int process_count, float* WTA_values, int completed_count) {
    FILE* perf_file = fopen("scheduler.perf", "w");
    if (!perf_file) {
        perror("Error opening performance file");
        exit(EXIT_FAILURE);
    }

    if (completed_count == 0) { // No processes completed
        fprintf(perf_file, "CPU utilization = 0.00%%\n");
        fprintf(perf_file, "Avg WTA = 0.00\n");
        fprintf(perf_file, "Avg Waiting = 0.00\n");
        fprintf(perf_file, "Std WTA = 0.00\n");
        fclose(perf_file);
        return;
    }

    // Calculate Avg WTA, Avg Waiting Time
    float avg_wta = 0, avg_waiting_time = (float)total_waiting_time / completed_count;
    float std_wta = 0;

    for (int i = 0; i < completed_count; i++) {
        avg_wta += WTA_values[i];
    }
    avg_wta /= completed_count;

    for (int i = 0; i < completed_count; i++) {
        std_wta += pow(WTA_values[i] - avg_wta, 2);
    }
    std_wta = sqrt(std_wta / completed_count);

    // Calculate CPU utilization
    int total_time_elapsed = getClk(); // Total simulation time
    float cpu_utilization = (float)total_runtime / total_time_elapsed * 100;

    // Write metrics to file
    fprintf(perf_file, "CPU utilization = %.2f%%\n", cpu_utilization);
    fprintf(perf_file, "Avg WTA = %.2f\n", avg_wta);
    fprintf(perf_file, "Avg Waiting = %.2f\n", avg_waiting_time);
    fprintf(perf_file, "Std WTA = %.2f\n", std_wta);

    fclose(perf_file);
}

// Main Function
int main() {
    bool all_finished = false;
    int clk;
    signal(SIGINT, clearResources);

    initClk();
    log_file = fopen("scheduler.log", "w");
    if (!log_file) {
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }

    key_t key = ftok("scheduler", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) {
        perror("Error creating message queue");
        exit(EXIT_FAILURE);
    }

    struct msg_buffer message;
    PCB processes[MAX_PROCESSES];
    int process_count = 0;
    PCB* running_process = NULL;

    // Shared memory for algorithm choice
    shmid_algo = shmget(SHKEY_ALGO, sizeof(int), 0444);
    if (shmid_algo == -1) {
        perror("Error accessing shared memory for algorithm choice");
        exit(EXIT_FAILURE);
    }

    shm_algo = (int*)shmat(shmid_algo, NULL, 0);
    if (shm_algo == (void*)-1) {
        perror("Error attaching shared memory for algorithm choice");
        exit(EXIT_FAILURE);
    }
    int algorithm_choice = shm_algo[0];
    int time_quantum = shm_algo[1];
    shmdt(shm_algo);

    float WTA_values[MAX_PROCESSES];
    int total_waiting_time = 0, total_turnaround_time = 0, total_runtime = 0, completed_count = 0;
    while (1) {
        int current_time = getClk();

        // Receive processes
        if (msgrcv(msgid, &message, sizeof(message.process), 1, IPC_NOWAIT) != -1) {
            PCB new_process = {
                .id = message.process.id,
                .arrival_time = message.process.arrival_time,
                .runtime = message.process.runtime,
                .remaining_time = message.process.runtime,
                .priority = message.process.priority
            };
            strcpy(new_process.state, "Ready");
            processes[process_count++] = new_process;
            log_event(current_time, &new_process, "Arrived", 0, 0);
            printf("Scheduler: Received process %d at time %d\n", new_process.id, current_time);
        }

        switch (algorithm_choice) {
            case 1: { // Shortest Job First (SJF)
                int shortest_job_index = -1;
                for (int i = 0; i < process_count; i++) {
                    if (strcmp(processes[i].state, "Ready") == 0 && processes[i].arrival_time <= current_time) {
                        if (shortest_job_index == -1 ||
                            processes[i].remaining_time < processes[shortest_job_index].remaining_time) {
                            shortest_job_index = i;
                        }
                    }
                }

                if (shortest_job_index != -1) {
                    running_process = &processes[shortest_job_index];
                    if (running_process->id == -1) {
                        printf("Scheduler: No processes to run at time %d\n", current_time);
                        all_finished = true;
                        break;
                    }

                    if (strcmp(running_process->state, "Ready") == 0) {
                        log_event(current_time, running_process, "Started", 0, 0);
                        pid_t child_pid = fork();
                        if (child_pid == -1) {
                            perror("Error forking child process");
                            exit(EXIT_FAILURE);
                        } else if (child_pid == 0) {
                            char runtime_str[10];
                            sprintf(runtime_str, "%d", running_process->runtime);
                            execl("./process.out", "process.out", runtime_str, NULL);
                        }
                        running_process->pid = child_pid;
                        strcpy(running_process->state, "Running");
                        printf("Scheduler: Process %d started at time %d\n", running_process->id, current_time);
                    }

                    // Simulate one time unit of execution
                    while (running_process->remaining_time > 0) {
                        sleep(1);
                        running_process->remaining_time--;
                    }

                    int turnaround_time = current_time - running_process->arrival_time;
                    float weighted_ta = turnaround_time / (float)running_process->runtime;
                    log_event(getClk(), running_process, "Finished", turnaround_time, weighted_ta);

                    strcpy(running_process->state, "Finished");
                    printf("Scheduler: Process %d finished at time %d\n", running_process->id, getClk());
                }
                break;
            }

            case 2: { // Preemptive Highest Priority First (PHPF)
                int highest_priority_index = -1;
                // Select the highest-priority process ready to run
                for (int i = 0; i < process_count; i++) {
                    if (strcmp(processes[i].state, "Ready") == 0 && processes[i].arrival_time <= current_time) {
                        if (highest_priority_index == -1 || processes[i].priority < processes[highest_priority_index].priority) {
                            highest_priority_index = i;
                        }
                    }
                }

                if (highest_priority_index != -1) {
                    PCB* next_process = &processes[highest_priority_index];

                    if (!running_process) {
                        running_process = next_process;
                        if (running_process->id == -1) {
                            printf("Scheduler: No processes to run at time %d\n", current_time);
                            all_finished = true;
                            break;
                        }
                        pid_t child_pid = fork();
                        if (child_pid == -1) {
                            perror("Error forking child process");
                            exit(EXIT_FAILURE);
                        } else if (child_pid == 0) {
                            char runtime_str[10];
                            sprintf(runtime_str, "%d", running_process->runtime);
                            execl("./process.out", "process.out", runtime_str, NULL);
                        }
                        running_process->pid = child_pid;
                        log_event(current_time, running_process, "Started", 0, 0);
                        strcpy(running_process->state, "Running");
                        printf("Scheduler: Process %d started at time %d\n", running_process->id, current_time);
                    } else if (running_process->priority > next_process->priority) {
                        kill(running_process->pid, SIGSTOP);
                        log_event(current_time, running_process, "Stopped", 0, 0);
                        strcpy(running_process->state, "Stopped");
                        printf("Scheduler: Process %d stopped at time %d\n", running_process->id, current_time);

                        running_process = next_process;
                        log_event(current_time, running_process, "Started", 0, 0);
                    }

                    if (strcmp(running_process->state, "Ready") == 0) {
                        pid_t child_pid = fork();
                        if (child_pid == -1) {
                            perror("Error forking child process");
                            exit(EXIT_FAILURE);
                        } else if (child_pid == 0) {
                            char runtime_str[10];
                            sprintf(runtime_str, "%d", running_process->runtime);
                            execl("./process.out", "process.out", runtime_str, NULL);
                        }
                        running_process->pid = child_pid;
                        log_event(current_time, running_process, "Started", 0, 0);
                        printf("Scheduler: Process %d started at time %d\n", running_process->id, current_time);
                    } else if (strcmp(running_process->state, "Stopped") == 0) {
                        kill(running_process->pid, SIGCONT);
                        log_event(current_time, running_process, "Resumed", 0, 0);
                        printf("Scheduler: Process %d resumed at time %d\n", running_process->id, current_time);
                    }

                    strcpy(running_process->state, "Running");

                    // Simulate one time unit of execution
                    sleep(1);
                    running_process->remaining_time--;

                    // If the process finishes, mark it as "Finished" and calculate performance metrics
                    if (running_process->remaining_time <= 0) {
                        int turnaround_time = current_time - running_process->arrival_time;
                        float weighted_ta = (float)turnaround_time / running_process->runtime;

                        printf("Scheduler: Process %d finished at time %d\n", running_process->id, getClk());
                        log_event(getClk(), running_process, "Finished", turnaround_time, weighted_ta);

                        total_waiting_time += (turnaround_time - running_process->runtime);
                        WTA_values[completed_count++] = weighted_ta;
                        total_turnaround_time += turnaround_time;

                        strcpy(running_process->state, "Finished");
                        running_process = NULL; // Reset the running process pointer
                    }
                }
                break;
            }



            case 3: { // Round Robin (RR)
                for (int i = 0; i < process_count; i++) {
                    PCB* current_process = &processes[i];
                    if (strcmp(current_process->state, "Ready") == 0 && current_process->arrival_time <= current_time) {
                        if (current_process->id == -1) {
                            printf("Scheduler: No processes to run at time %d\n", current_time);
                            all_finished = true;
                            break;
                        }

                        if (current_process->remaining_time != current_process->runtime) {
                            kill(current_process->pid, SIGCONT);
                            log_event(current_time, current_process, "Resumed", 0, 0);
                            printf("Scheduler: Process %d resumed at time %d\n", current_process->id, current_time);
                        } else {
                            pid_t child_pid = fork();
                            if (child_pid == -1) {
                                perror("Error forking child process");
                                exit(EXIT_FAILURE);
                            } else if (child_pid == 0) {
                                char runtime_str[10];
                                sprintf(runtime_str, "%d", current_process->runtime);
                                execl("./process.out", "process.out", runtime_str, NULL);
                            }
                            current_process->pid = child_pid;
                            log_event(current_time, current_process, "Started", 0, 0);
                            strcpy(current_process->state, "Running");
                            printf("Scheduler: Process %d started at time %d\n", current_process->id, current_time);
                        }

                        // Execute the process for a time quantum or until it finishes
                        // If the process finishes, calculate performance metrics
                        int execute_time = (current_process->remaining_time > time_quantum)
                        ? time_quantum
                        : current_process->remaining_time;

                        // Simulate process execution
                        sleep(execute_time);
                        current_process->remaining_time -= execute_time;

                        if (current_process->remaining_time <= 0) {
                            int turnaround_time = current_time - current_process->arrival_time;
                            float weighted_ta = turnaround_time / (float)current_process->runtime;
                            log_event(getClk(), current_process, "Finished", turnaround_time, weighted_ta);
                            printf("Scheduler: Process %d finished at time %d\n", current_process->id, getClk());
                            strcpy(current_process->state, "Finished");
                        } else {
                            kill(current_process->pid, SIGSTOP);
                            log_event(getClk(), current_process, "Stopped", 0, 0);
                            strcpy(current_process->state, "Ready");
                            printf("Scheduler: Process %d stopped at time %d\n", current_process->id, getClk());
                        }
                    }
                }
                break;
            }
            default:
                printf("Invalid scheduling algorithm choice.\n");
                exit(EXIT_FAILURE);
        }

        // Example: Process Completion
        if (running_process && running_process->remaining_time <= 0) {
            int turnaround_time = current_time - running_process->arrival_time;
            float weighted_ta = (float)turnaround_time / running_process->runtime;

            total_waiting_time += turnaround_time - running_process->runtime;
            total_runtime += running_process->runtime;
            WTA_values[completed_count++] = weighted_ta;

            log_event(getClk(), running_process, "Finished", turnaround_time, weighted_ta);
            strcpy(running_process->state, "Finished");
            running_process = NULL; // Reset pointer
        }

        if (all_finished)
            break;

        sleep(1); // Simulate scheduler working every second
    }

    // Write performance metrics
    write_performance_metrics(total_turnaround_time, total_waiting_time, process_count, WTA_values, completed_count);

    // Cleanup resources
    fclose(log_file);
    destroyClk(false);
    msgctl(msgid, IPC_RMID, NULL);
    printf("Scheduler: All processes completed. Exiting.\n");
    return 0;
}

void clearResources(int signum) {
    destroyClk(false);
    shmdt(shm_algo);
    shmctl(shmid_algo, IPC_RMID, NULL);
    msgctl(msgid, IPC_RMID, NULL);
    printf("Process Generator: Resources cleared and exiting.\n");
    exit(0);
}
