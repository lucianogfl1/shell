#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include "parser.h"
#include "color.h"
#include "process_control.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>

#define MAX_INPUT_SIZE 1000
#define RUNNING        1
#define SUCCESS        1
#define QUIT           2
#define FAIL           0
#define TRUE           1
#define FALSE          0

extern int errno;

char execute_cmd(Command);
void print_layout();
void terminate_foreground(int);
void stop_foreground(int);
char try_internal_cmd(Command);
void launch_job(Command, int, int, int);
char cd_cmd(Command);
char update_jobs_status_cmd();
char history_cmd();
char quit_cmd(Command);
Job get_job(int, int);
char bg_cmd(Command cmd);
char fg_cmd(Command cmd);
void wait_job(Job);
char is_stopped(Job);
void refresh_state(Job);
void put_in_foreground(Job);
void init_shell();
void handle_redirection(struct redirection_t *);

// Global list of all jobs
Jobl job_list;

// Indicates whether a process is being executed
// in foreground or not
char exc_foreground = FALSE;

struct termios shell_tmodes;
int    shell_terminal;
int    shell_is_interactive;
pid_t  shell_pgid;

int main (int argc, char **argv, char **envp) {
    char cmd_line[MAX_INPUT_SIZE];
    char *notEOF;
    Command cmd;

    // Handling signals
    signal(SIGINT, terminate_foreground);
    signal(SIGTSTP, stop_foreground);

    // Creates a new empty job list
    job_list = create_jobl();

    init_shell();

    // Parse and execute line
    while(notEOF) {
        print_layout();

        // Read line from input
        notEOF = fgets(cmd_line, sizeof(cmd_line), stdin);

        // Case not a ctrl + d and a successful parse occurred
        if (notEOF && (cmd = parse(cmd_line))) {
            char action = execute_cmd(cmd);
            if (action == QUIT)
                break;
        }
    }

    printf("Exiting...\n");

    // Kill any remaining alive process
    Jobl_tail tail = job_list->head;

    // Look for foreground process
    while(tail) {
        if(tail->item->is_valid == VALID) {
            kill(tail->item->pid, SIGTERM);
        }
        tail = tail->next;
    }

    // Free list of commands
    free_jobl(job_list);

    return 0;
}

void init_shell() {
    // Test if the current process has
    // control over the STDIN descriptor
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty (shell_terminal);

    // If the process has control
    if(shell_is_interactive) {
        // Loop until we are in the foreground
        while(tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        // Ignore interactive and job-control signals
        // VERY IMPORTANT TO KEEP TO TERMINAL ALIVE
        signal (SIGQUIT, SIG_IGN);
        signal (SIGTTIN, SIG_IGN);
        signal (SIGTTOU, SIG_IGN);
        signal (SIGCHLD, SIG_IGN);

        // Put ourselves in our own process group
        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0) {
          printf("ERROR: Couldn't put the shell in its own process group");
          exit(1);
        }

        // Grab control of the terminal
        tcsetpgrp(shell_terminal, shell_pgid);

        // Save default terminal attributes for shell to restored later
        if (tcgetattr(shell_terminal, &shell_tmodes) == -1){
            printf("ERROR: unable to save default terminal state");
        }
    }
}

void print_layout() {
    set_color(GREEN);
    printf("\nG1> ");
    set_color(NONE);
}

void launch_job(Command cmd, int foreground, int in, int out) {
    Job new_job = (Job) malloc(sizeof(struct job));
    pid_t pid;

    // Assign the command related to the job
    new_job->cmd = cmd;

    // Child process
    if ((pid = fork()) == 0) {
        char **cmd_args = get_cmd_args(cmd);
        int i;

        // Put the current process in its own process group
        pid = getpid();
        setpgid(pid, pid);

        // Case the command is supposed to execute in foreground
        // remove the '&' from the args
        if(!foreground)
            cmd_args[get_cmd_argc(cmd) - 1] = NULL;

        // Else, in case we are in foreground,
        // grab control over the terminal
        else if (tcsetpgrp (shell_terminal, pid) == -1){
            printf("ERROR: unable to grab control over the terminal I/O\n");
        }

        //signal (SIGINT, SIG_DFL);
        //signal (SIGQUIT, SIG_DFL);
        //signal (SIGTSTP, SIG_DFL);
        //signal (SIGTTIN, SIG_DFL);
        //signal (SIGTTOU, SIG_DFL);
        //signal (SIGCHLD, SIG_DFL);

        // Handles redirection
        struct redirection_t* r = extract_redirection(cmd_args, ">", ROUT);
        if (r) {
            handle_redirection(r);
        }
        free(r);
        r = extract_redirection(cmd_args, ">>", ROUT_APPEND);
        if (r) {
            handle_redirection(r);
        }
        free(r);
        r = extract_redirection(cmd_args, "<", RIN);
        if (r) {
            handle_redirection(r);
        }
        free(r);
        r = extract_redirection(cmd_args, "2>", RERR);
        if (r) {
            handle_redirection(r);
        }
        free(r);

        // Handle pipes
        if (in != 0) {
            dup2 (in, 0);
            close (in);
        }
        if (out != 1) {
            dup2 (out, 1);
            close (out);
        }

        // Change the child code to the called external command
        if (execvp(cmd_args[0], cmd_args) < 0) {
            set_color(RED);
            printf("ERROR: Command ");
            print_cmd(cmd);
            printf(" not found (error code %d)\n", errno);
            exit(1);
        }
    }
    // Parent process
    else {
        // Add the job into the job list
        add_job(job_list, new_job);

        // Set is parameters
        new_job->pid = pid;
        new_job->jid = job_list->jid_count;
        new_job->status = -1;
        new_job->is_foreground = FALSE;

        // Put the new job into its own process group
        setpgid(pid, pid);

        // Wait for the child in foreground to terminate
        if (foreground) {
            put_in_foreground(new_job);
        }
    }
}

void handle_redirection(struct redirection_t *r) {
    int fd;

    switch(r->type) {
        case ROUT:
            fd = fileno(fopen(r->file, "w+"));
            dup2(fd, STDOUT_FILENO);
            close(fd);
        case RIN:
            fd = fileno(fopen(r->file, "r"));
            dup2(fd, STDIN_FILENO);
            close(fd);
            break;
        case ROUT_APPEND:
            fd = fileno(fopen(r->file, "a+"));
            dup2(fd, STDOUT_FILENO);
            close(fd);
        case RERR:
            fd = fileno(fopen(r->file, "w+"));
            dup2(fd, STDERR_FILENO);
            close(fd);
            break;;
    }
}

void put_in_foreground(Job job) {
    // Pass the control of the terminal to the child
    if (tcsetpgrp (shell_terminal, job->pid)== -1){
        printf("ERROR: unable to pass control to the child\n");
    }

    job->is_foreground = TRUE;
    wait_job(job);

    // Give back the control to the current process
    // Put the shell back in the foreground
    tcsetpgrp (shell_terminal, shell_pgid);

    // Restore the initial saved state
    tcgetattr (shell_terminal, &shell_tmodes);
    tcsetattr (shell_terminal, TCSADRAIN, &shell_tmodes);
}

// Wait for a job to terminate or be stopped
void wait_job(Job job) {
    exc_foreground = TRUE;
    // WUNTRACED is used so the waitpid will also
    // return if the process is stopped
    waitpid(job->pid, &job->status, WUNTRACED);
    if (!WIFSTOPPED(job->status)) {
        invalidate_job(job);
    }
    exc_foreground = FALSE;
}

char execute_cmd(Command cmd) {
    #ifdef DEBUG
        set_color(RGREEN);
        printf ("Executing ");
        set_color(GREEN);
        print_cmd(cmd);
        set_color(RGREEN);
        printf("...\n");
    #endif

    set_color(WHITE);

    // Handle pipes
    int pipes_count = count_pipes(cmd), i, in = 0, fd[2];
    char action = SUCCESS;

    Command* pipe_cmds = break_into_commands(cmd, pipes_count);

    for (i = 0; i < pipes_count + 1; i++) {
        // Only execute internal if there are no pipes
        if (pipes_count == 0) {
            // Try to execute as internal, launch an external job otherwise
            if ((!(action = try_internal_cmd(pipe_cmds[i])) && action != QUIT)) {

                launch_job(pipe_cmds[i], is_foreground(pipe_cmds[i]), 0, 1);

            } else {
                Job new_job = (Job) malloc(sizeof(struct job));

                // Add the job into the job list
                add_job(job_list, new_job);

                // Set is parameters
                new_job->cmd = pipe_cmds[i];
                new_job->pid = -1;
                new_job->jid = job_list->jid_count;
                new_job->status = -1;
                new_job->is_valid = INVALID;
            }
        }
        else {
            // First from the last but one
            if (i < pipes_count) {
                pipe(fd);
                launch_job(pipe_cmds[i], is_foreground(pipe_cmds[i]),
                           in, fd[1]);
                close (fd [1]);
                in = fd [0];
            } else {
                //dup2(fd[1], 1);
                launch_job(pipe_cmds[i], is_foreground(pipe_cmds[i]),
                           in, 1);
            }
        }
    }

    return action;
}

char update_jobs_status_cmd() {
    Jobl_tail tail = job_list->head;

    set_color(BLUE);
    printf("JID\tPID\tSTATUS   \tCOMMAND\n");

    // While there are items on the list
    while(tail) {
        Job item = tail->item;
        int r_pid;

        if (item->is_valid == VALID) {
            // Get the status related tot he process id
            r_pid = waitpid(item->pid, &item->status, WUNTRACED | WNOHANG | WCONTINUED);

            // The process has terminated, or had never been created
            if (r_pid < 0) {
                // Invalidate job
                invalidate_job(item);
            }
            // The process exists and had its state changed
            else {
                printf("%d\t%d\t", item->jid, (int) item->pid);
                if (WIFEXITED(item->status)) {
                    printf("Exited   \t");
                }
                else if (WIFSIGNALED(item->status)) {
                    printf("Killed   \t");
                }
                else if (WIFSTOPPED(item->status)) {
                    printf("Stopped  \t");
                }
                else if (WIFCONTINUED(item->status)) {
                    printf("Continued\t");
                }
                else {
                    printf("Running  \t");
                }
                print_cmd(item->cmd); printf("\n");
            }
        }
        tail = tail->next;
    }
    return SUCCESS;
}

void terminate_foreground(int signo) {
    Jobl_tail tail = job_list->head;

    // Look for foreground process
    while(tail) {
        if(tail->item->is_valid &&
           tail->item->is_foreground) {
            kill(tail->item->pid, SIGINT);
            invalidate_job(tail->item);
        }
        tail = tail->next;
    }

    if (!exc_foreground)
        print_layout();
}

void stop_foreground(int signo) {
    Jobl_tail tail = job_list->head;

    // Look for foreground process
    while(tail) {
        if(tail->item->is_valid &&
           tail->item->is_foreground) {
            kill(tail->item->pid, SIGTSTP);
            tail->item->is_foreground = FALSE;
        }
        tail = tail->next;
    }

    if (!exc_foreground)
        print_layout();
}

char cd_cmd(Command cmd) {
    char **args = get_cmd_args(cmd);

    // Try to change directory
    if (chdir(args[1]) == -1) {
        printf("cd: \"%s\": No such file or directory\n", args[1]);
        return SUCCESS;
    }

    // Job executed with success
    return SUCCESS;
}

char history_cmd() {
    Jobl_tail tail = job_list->head;
    int       i    = 1;

    set_color(BLUE);
    printf("BASH HISTORY\n");
    while(tail) {
        Job item = tail->item;
        printf("%d\t", i++);
        print_cmd(item->cmd);
        printf("\n");
        tail = tail->next;
    }
    return SUCCESS;
}

char quit_cmd(Command cmd) {
    if (get_cmd_argc(cmd) != 1) {
        set_color(RED);
        printf("ERROR: quit must have no parameters\n");
        set_color(NONE);
        return SUCCESS;
    }
    return QUIT;
}

Job get_job(int pid, int jid) {
    Jobl_tail tail = job_list->head;

    // Look for pid
    if (jid == -1) {
        while(tail) {
            if(tail->item->pid == pid)
                return tail->item;
            tail = tail->next;
        }
    }
    // Else, look for jid
    else {
        while(tail) {
            if(tail->item->jid == jid)
                return tail->item;
            tail = tail->next;
        }
    }
    // None found
    return NULL;

}

char is_stopped(Job job) {
    int r_pid = waitpid(job->pid, &job->status, WUNTRACED | WNOHANG | WCONTINUED);

    if (r_pid < 0) {
        invalidate_job(job);
        return FALSE;
    }
    else {
        return WIFSTOPPED(job->status);
    }
}

void refresh_state(Job job) {
    int r_pid = waitpid(job->pid, &job->status, WUNTRACED | WNOHANG | WCONTINUED);

    if (r_pid < 0 ||
        WIFSIGNALED(job->status) ||
        WIFEXITED(job->status)) {
        invalidate_job(job);
    }
}

char bg_cmd(Command cmd) {
    char **args = get_cmd_args(cmd);
    Job job = NULL;

    // Handles incorrect input
    if (get_cmd_argc(cmd) != 2) {
        set_color(RED);
        printf("ERROR: expecting bg <pid || %%jid>\n");
        set_color(NONE);
        return SUCCESS;
    }

    // Gets job by pid or jid
    if (args[1][0] == '%')
        job = get_job(-1, atoi(&args[1][1]));
    else
        job = get_job(atoi(args[1]), -1);

    // If there is a paused job
    if (job && job->is_valid != INVALID) {
        // Check if the job is stopped
        if (!is_stopped(job)) {
            set_color(RED);
            printf("ERROR: The process %d (%%%d) is not stopped\n",
                   job->pid, job->jid);
            set_color(NONE);
            return SUCCESS;
        }

        // Send signal to continue process
        kill(job->pid, SIGCONT);

        set_color(BLUE);
        printf("Job %d (%%%d) continued in background...\n",
               job->pid, job->jid);
        set_color(NONE);
    } else {
        set_color(RED);
        printf("ERROR: Job not found\n");
        set_color(NONE);
    }

    return SUCCESS;
}

char fg_cmd(Command cmd) {
    char **args = get_cmd_args(cmd);
    Job job = NULL;

    // Handles incorrect input
    if (get_cmd_argc(cmd) != 2) {
        set_color(RED);
        printf("ERROR: expecting fg <pid || %%jid>\n");
        set_color(NONE);
        return SUCCESS;
    }

    // Gets job by pid or jid
    if (args[1][0] == '%')
        job = get_job(-1, atoi(&args[1][1]));
    else
        job = get_job(atoi(args[1]), -1);

    // If there is a paused job
    if (job) {
        refresh_state(job);

        // Check if the job is stopped
        if (job->is_valid == INVALID) {
            set_color(RED);
            printf("ERROR: Job not found\n");
            set_color(NONE);
            return SUCCESS;
        }

        // Send signal to continue process
        kill(job->pid, SIGCONT);

        set_color(BLUE);
        printf("Job %d (%%%d) continued in foreground...\n",
               job->pid, job->jid);
        set_color(NONE);

        put_in_foreground(job);
    } else {
        set_color(RED);
        printf("ERROR: Job not found\n");
        set_color(NONE);
    }

    return SUCCESS;
}

char try_internal_cmd(Command cmd) {
    // Case change directory
    if (!strcmp(get_cmd_name(cmd), "cd")) {
        return cd_cmd(cmd);
    }
    // Internal command jobs
    else if(!strcmp(get_cmd_name(cmd), "jobs")) {
        // Get the status of each job into the job list
        return update_jobs_status_cmd();
    }
    // Show all called commands
    else if(!strcmp(get_cmd_name(cmd), "history")) {
        return history_cmd();
    }
    // Quit (close)
    else if(!strcmp(get_cmd_name(cmd), "quit")) {
        return quit_cmd(cmd);
    }
    // Move process to background
    else if(!strcmp(get_cmd_name(cmd), "bg")) {
        return bg_cmd(cmd);
    }
    // Move process to foreground
    else if(!strcmp(get_cmd_name(cmd), "fg")) {
        return fg_cmd(cmd);
    }

    return FAIL;
}
