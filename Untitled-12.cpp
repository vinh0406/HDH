#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LINE 80
char history[MAX_LINE];
int backgroundJobCounter = 0;

void parseInput(char *input, char **args) {
    int i = 0;
    args[i] = strtok(input, " ");
    while (args[i] != NULL) {
        i++;
        args[i] = strtok(NULL, " ");
    }
}

void executeCommand(char **args, int background) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[0], args);
        perror("Error executing command");
        exit(1);
    } else if (pid > 0) {
        if (!background) {
            waitpid(pid, NULL, 0);
        } else {
            backgroundJobCounter++;
            printf("[%d] %d\n", backgroundJobCounter, pid);
        }
    } else {
        perror("Fork failed");
    }
}

int handleRedirection(char **args) {
    int fd;
    int stdin_backup = dup(STDIN_FILENO);
    int stdout_backup = dup(STDOUT_FILENO);

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Error: Missing output file.\n");
                return -1;
            }
            fd = open(args[i + 1], O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
            if (fd == -1) {
                perror("Error opening output file");
                return -1;
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            args[i] = NULL;
        } else if (strcmp(args[i], "<") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Error: Missing input file.\n");
                return -1;
            }
            fd = open(args[i + 1], O_RDONLY);
            if (fd == -1) {
                perror("Error opening input file");
                return -1;
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
            args[i] = NULL;
        }
    }

    return 0;
}

void executeCommandWithRedirection(char **args, int background) {
    int stdin_backup = dup(STDIN_FILENO);
    int stdout_backup = dup(STDOUT_FILENO);

    if (handleRedirection(args) == -1) {
        dup2(stdin_backup, STDIN_FILENO);
        dup2(stdout_backup, STDOUT_FILENO);
        close(stdin_backup);
        close(stdout_backup);
        return;
    }

    executeCommand(args, background);
    dup2(stdin_backup, STDIN_FILENO);
    dup2(stdout_backup, STDOUT_FILENO);
    close(stdin_backup);
    close(stdout_backup);
}

void executePipe(char **args1, char **args2, int background) {
    int fd[2];
    pipe(fd);
    pid_t pid1 = fork();

    if (pid1 == 0) {
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        close(fd[1]);
        handleRedirection(args1);
        execvp(args1[0], args1);
        perror("Error in pipe execution");
        exit(1);
    }

    pid_t pid2 = fork();

    if (pid2 == 0) {
        dup2(fd[0], STDIN_FILENO);
        close(fd[1]);
        close(fd[0]);
        handleRedirection(args2);
        execvp(args2[0], args2);
        perror("Error in pipe execution");
        exit(1);
    }

    close(fd[0]);
    close(fd[1]);

    if (!background) {
        waitpid(pid1, NULL, 0);
        waitpid(pid2, NULL, 0);
    }
}

void handleHistory(char **args, int background) {
    if (strlen(history) == 0) {
        printf("No commands in history.\n");
        return;
    }

    if (background) {
        if (strstr(history, "&") != NULL) {
            printf("Error: command & &\n");
            return;
        }
    }

    printf("Executing last command: %s\n", history);

    char *historyArgs[MAX_LINE / 2 + 1];
    parseInput(history, historyArgs);

    for (int i = 0; historyArgs[i] != NULL; i++) {
        if (strcmp(historyArgs[i], "&") == 0) {
            background = 1;
            historyArgs[i] = NULL;
        }
    }

    int pipeIndex = -1;
    for (int i = 0; historyArgs[i] != NULL; i++) {
        if (strcmp(historyArgs[i], "|") == 0) {
            pipeIndex = i;
            break;
        }
    }

    if (pipeIndex != -1) {
        historyArgs[pipeIndex] = NULL;
        char *args1[MAX_LINE / 2 + 1];
        char *args2[MAX_LINE / 2 + 1];

        memcpy(args1, historyArgs, pipeIndex * sizeof(char *));
        args1[pipeIndex] = NULL;
        parseInput(history + (historyArgs[pipeIndex + 1] - history), args2);

        executePipe(args1, args2, background);
    } else {
        executeCommandWithRedirection(historyArgs, background);
    }
}

int main(void) {
    char input[MAX_LINE];
    char *args[MAX_LINE / 2 + 1];
    int shouldRun = 1;

    while (shouldRun) {
        printf("osh> ");
        fflush(stdout);

        fgets(input, MAX_LINE, stdin);

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "!!") != 0 && strcmp(input, "!! &") != 0) {
            strcpy(history, input);
        }
        char *start = input;
        while (*start == ' ') start++;
        if (strlen(start) == 0) {
            continue;
        }

        if (strcmp(input, "exit") == 0) {
            shouldRun = 0;
            continue;
        }

        parseInput(input, args);

        int background = 0;
        for (int i = 0; args[i] != NULL; i++) {
            if (strcmp(args[i], "&") == 0) {
                background = 1;
                args[i] = NULL;
            }
        }

        int pipeIndex = -1;
            for (int i = 0; args[i] != NULL; i++) {
                if (strcmp(args[i], "|") == 0) {
                    pipeIndex = i;
                    break;
                }
            }

        if (strcmp(args[0], "!!") == 0) {  
            handleHistory(args, background);
        } else if (pipeIndex != -1) { 
            args[pipeIndex] = NULL; 
            char *args1[MAX_LINE / 2 + 1];
            char *args2[MAX_LINE / 2 + 1];

            for (int i = 0; i < pipeIndex; i++) {
                args1[i] = args[i];
            }
            args1[pipeIndex] = NULL;

            int j = 0;
            for (int i = pipeIndex + 1; args[i] != NULL; i++) {
                args2[j++] = args[i];
            }
            args2[j] = NULL;

            executePipe(args1, args2, background);
        } else {
            executeCommandWithRedirection(args, background);
        }
    }
    return 0;
}