#include <stdlib.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#define MAXLINE 1024
#define MAXLINE_STATUS 1024
#define MAXLINE_CMD 1024

/* 
    Implements our crash shell, a simple Unix shell that allows users to run
    background or foreground processes by typing in cmds and args.

    The shell manages a table of jobs corresponding to each process. 
    Jobs are commands you run and corresponds exactly to one process. 
    Each job has a unique job ID (starting from 1) and a PID assigned by OS.
    
    Jobs can have the following states: 
        - running:   job is placed in the bg or started with &
        - finished:  job terminated normally 
        - suspended: job is paused (Ctrl+Z) for a fg job (or a bg job?)
        - killed:    job terminated by a signal (Ctrl+C or Ctrl+\)
        - continued: suspended job has resumed execution
        - failed:    job failed to start
        - killed (core dumped): job terminated by a signal 
                                and core dumped (seg fault)


    The shell receives certain signals and updates the job status accordingly.
    
    Signals handled by the shell:
        - SIGINT:  (CTRL + C) interrupt a process
        - SIGTSTP: (CTRL + Z) pauses a process
        - SIGQUIT: (CTRL + \) quits a process
        - SIGCHLD: child process terminates or is suspended
        - SIGCONT: continue a suspended process
        - SIGKILL: terminate a process immediately
        - SIGSEGV: invalid memory access (segmentation fault)
*/
  
// data types and global variables ----------------------------------------------------------------------

typedef enum { 
    running, 
    suspended, 
    finished, 
    killed, 
    killed_core_dumped, 
    continued,
    failed
} job_status;  // current state of a job


typedef struct{
    pid_t pid;                // pid of a job
    int job_id;               // sequential job ID starting at 1
    job_status status;        // current status of a job
    char cmd[MAXLINE_CMD];    // command name
} job;                        // a job managed by the shell

typedef struct {
    job jobs[MAXLINE];  // array of jobs ever started (not just active)
    int job_count;      // total number of jobs ever started 
    pid_t fg;           // PID of the current foreground job, or -1 if none 
} shell;         // our shell 
    

// initialize the shell's job/process table and foreground job PID
static shell processes = {.fg = -1, .job_count = 0};


// helper functions ----------------------------------------------------------------------

// returns string value of job_status enums
const char *job_status_strings(job_status status) { 
    switch (status){
    case running:
        return "running";
    case suspended:
        return "suspended";
    case finished:
        return "finished";
    case killed:
        return "killed";
    case killed_core_dumped:
        return "killed (core dumped)";
    case continued:
        return "continued";
    case failed:
        return "failed";
    default:
        return "unknown";
    }
}

static void add_newjob(pid_t pid, const char *cmd, job_status status);
static void update_status_jobPID(pid_t pid, job_status new_status);
static bool can_parse_as_int(const char *str);
  

// adds a new job to the shell and prints status output
static void add_newjob(pid_t pid, const char *cmd, job_status status){ 

    // check if we exceed job limit which is
    // 32 active jobs (running or suspended)
    if (processes.job_count >= 32) {
        fprintf(stderr, "ERROR: too many jobs\n");
        return;
    }

    // create a ptr to the next available job slot in the processes.jobs array
    job *newjob = &processes.jobs[processes.job_count];

    // set the new job's fields: PID, job ID, status, and command
    newjob->pid = pid;
    newjob->job_id = processes.job_count + 1; 
    newjob->status = status;

    strncpy(newjob->cmd, cmd, MAXLINE_CMD - 1);
    newjob->cmd[MAXLINE_CMD - 1] = '\0';   

    // print the status output required by spec: [id] (pid)  status  cmd
    printf("[%d] (%d)  %s  %s\n",
            newjob->job_id, newjob->pid,
            job_status_strings(newjob->status), newjob->cmd);

    fflush(stdout);  // ensure output is printed immediately 

    // increment job count after adding the new job
    processes.job_count++;
}   


// update the status of the job with PID and print the new status output
static void update_status_jobPID(pid_t pid, job_status new_status) {

    for (int i = 0; i < processes.job_count; i++) {
        
        // find the job with the matching PID
        if (processes.jobs[i].pid == pid) {

            // update the job's status
            processes.jobs[i].status = new_status;

            // print the new status output 
            printf("[%d] (%d)  %s  %s\n",
                processes.jobs[i].job_id, pid,
                job_status_strings(new_status), processes.jobs[i].cmd);

            // flush stdout 
            fflush(stdout);

            return;
        }
    }

    // if no job with PID is found, print an error message
    fprintf(stderr, "ERROR: job with PID %d not found\n", pid);
}


// helper func to check if a string can be parsed as non-negative int
// used in bg, fg, and nuke commands to validate job ID and PID arguments
static bool can_parse_as_int(const char *str){

    // case 1: just '%' with no number after, it is not a valid job ID
    if (str[0] == '%' && str[1] == '\0') return false;

    // case 2: empty string is not a valid job ID
    if (str[0] == '\0') return false;

    // case 3: if the string starts with a '%' followed by non-digit characters, it's not a valid job ID
    int start = str[0] == '%' ? 1 : 0;
    for (int i = start; str[i] != '\0'; i++) {
        if (!(str[i] >= '0' && str[i] <= '9')) return false;
    }   

    // otherwise, the string can be parsed as a non-negative int (job ID or PID)
    return true;
    
}

// signal handlers ----------------------------------------------------------------------

// waitpid: parent process will wait until a child process changes state before continuing.
// WNOHANG: if no child has changed state, return 0 immediately instead of waiting
// WUNTRACED + WCONTINUED: extend waitpid beyond terminated, also catch stopped and resumed child processes

// while loop: allows the shell to continue running and accepting user input
// while child processes are still running in the background,

// WIFEXITED, WIFSIGNALED, WCOREDUMP, WIFSTOPPED, WIFCONTINUED 
// returns status macros used to check the status of a child process after waitpid returns.


// sigchld_handle: reap child processes and updates the job table accordingly.
void sigchld_handle(int sig) {
    (void)sig;
    int saved_errno = errno;        // waitpid can overwrite whatever the error state   
    pid_t pid;                      // use saved_errno to restore state when signal handler returns
    int status;

    // while loop: keeps reaping until no children are left -> ensures no zombie processes
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        for (int i = 0; i < processes.job_count; i++) {
            if (processes.jobs[i].pid == pid) {             // find the job with the matching PID

                if (WIFEXITED(status)) {                         // if child process terminated normally
                    update_status_jobPID(pid, finished);         // update the job status to finished 
                    if (processes.fg == pid) processes.fg = -1;  // reset fg to -1 since there is no longer a fg job 
                } 
                else if (WIFSIGNALED(status)) {                     // if child process was killed by a signal
                    update_status_jobPID(pid, WCOREDUMP(status) ?   // update the job status to killed (core dumped) 
                                   killed_core_dumped : killed);    // update the job status to killed
                    if (processes.fg == pid) processes.fg = -1;     // reset fg to -1 since there is no longer a fg job 
                } 
                else if (WIFSTOPPED(status)) {                      // if child process was suspended by a signal (Ctrl+Z)
                    update_status_jobPID(pid, suspended);           // update the job status to suspended 
                    if (processes.fg == pid) processes.fg = -1;     // if the suspended job was the foreground job,
                                                                    // reset fg to -1 since there is no longer a fg job 
                } 
                else if (WIFCONTINUED(status)) {               // if child process was resumed by SIGCONT 
                    update_status_jobPID(pid, continued);      // update the job status to continued
                }
                break;
            }
        }
    }

    // restore errno 
    errno = saved_errno;
}

// sigquit_handle:                          // forwards SIGQUIT to the fg (Ctrl+\)
void sigquit_handle(int sig) {
    (void)sig;
    if (processes.fg > 0) {                 // send SIGQUIT to child process in the fg 
        kill(processes.fg, SIGQUIT);        // via kill and passing the PID of the fg job
    } else {
        exit(0);                            // if no fg job, quit the shell
    }
}

// sigint_handle:                           // forwards SIGINT to the fg (Ctrl+C)
void sigint_handle(int sig) {
    (void)sig;
    if (processes.fg > 0) {
        kill(processes.fg, SIGINT);
    }
}

// sigtstp_handle:                          // forwards SIGTSTP to the fg (Ctrl+Z)
void sigtstp_handle(int sig) {
    (void)sig;
    if (processes.fg > 0) {
        kill(processes.fg, SIGTSTP);
    }
}

// sigcont_handle:                          // forwards SIGCONT to the fg
void sigcont_handle(int sig) {
    (void)sig;
    if (processes.fg > 0) {
        kill(processes.fg, SIGCONT);
    }
}

// install_sig_handlers ----------------------------------------------------------------------

// sigaction: install signals handlers for SIGCHLD, SIGINT, SIGQUIT, SIGTSTP, and SIGCONT
// sigemptyset: initializes the signal set to exclude all signals
// sa_mask: specifies a set of signals to be blocked during the execution of the signal handler

void install_sig_handlers() {

    struct sigaction sa_chld, sa_int, sa_quit, sa_tstp, sa_cont;

    // SIGCHLD 
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_handler = sigchld_handle;
    sa_chld.sa_flags   = SA_RESTART;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("ERROR: failed to install SIGCHLD handler");
        exit(EXIT_FAILURE);
    }

    // SIGINT 
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_handler = sigint_handle;
    sa_int.sa_flags   = SA_RESTART;
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("ERROR: failed to install SIGINT handler");
        exit(EXIT_FAILURE);
    }

    // SIGQUIT 
    sigemptyset(&sa_quit.sa_mask);
    sa_quit.sa_handler = sigquit_handle;
    sa_quit.sa_flags   = SA_RESTART;
    if (sigaction(SIGQUIT, &sa_quit, NULL) == -1) {
        perror("ERROR: failed to install SIGQUIT handler");
        exit(EXIT_FAILURE);
    }

    // SIGTSTP 
    sigemptyset(&sa_tstp.sa_mask);
    sa_tstp.sa_handler = sigtstp_handle;
    sa_tstp.sa_flags   = SA_RESTART;
    if (sigaction(SIGTSTP, &sa_tstp, NULL) == -1) {
        perror("ERROR: failed to install SIGTSTP handler");
        exit(EXIT_FAILURE);
    }

    // SIGCONT 
    sigemptyset(&sa_cont.sa_mask);
    sa_cont.sa_handler = sigcont_handle;
    sa_cont.sa_flags   = SA_RESTART;
    if (sigaction(SIGCONT, &sa_cont, NULL) == -1) {
        perror("ERROR: failed to install SIGCONT handler");
        exit(EXIT_FAILURE);
    }
}


// block/unblock signals ----------------------------------------------------------------------

// sigprocmask: block signals to avoid race conditions b/t main shell loop and signals handlers 
// if a child process terminates or is suspended before parent adds it to the job table, 
// child process is orphaned when SIGCHLD handler runs in parent procees, it will throw error 
// since there is no job entry with the PID of the child process, cannot update the status of the job. 

// by blocking the signals before spawning a child process and unblocking after adding newjob 
// we ensure SIGCHLD handler will not run until the child process is added to the job table, 
// so the handler can find the job with the PID and update its status correctly. 

// if a child process terminates or is suspended while signals are blocked, 
// it will remain in a waitable state until signals are unblocked,
// then SIGCHLD handler can reap it and update the job status without any race conditions.


void block_signals() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGTSTP);
    sigaddset(&sigset, SIGCONT);
    if (sigprocmask(SIG_BLOCK, &sigset, NULL) == -1) {
        perror("ERROR: failed to block signals");
    }
}

// unblock_signals
void unblock_signals() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGTSTP);
    sigaddset(&sigset, SIGCONT);
    if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) == -1) {
        perror("ERROR: failed to unblock signals");
    }
}

    
// commands ---------------------------------------------------------------------------

// our shell supports the following commands for managing jobs

// jobs_cmd:  lists all running or suspended jobs that are running.
// bg_cmd:    resumes one or more suspended jobs in the background
// fg_cmd:    moves a background or suspended job to the foreground
// nuke_cmd:  sends SIGKILL to all active jobs or specified jobs



// jobs_cmd: lists all running or suspended jobs
void jobs_cmd(const char **toks) {

    // check no arguments are passed to jobs command
    if (toks[1] != NULL) {
        fprintf(stderr, "ERROR: jobs takes no arguments\n");
        return;
    }

    // Print all jobs that are running or suspended
    for (int i = 0; i < processes.job_count; i++) {

        // get the job status 
        job_status s = processes.jobs[i].status;
        
        // if running or suspended, print job info
        // [id] (pid)  status  cmd
        if (s == running || s == suspended) {      
            printf("[%d] (%d)  %s  %s\n",       
                   processes.jobs[i].job_id, processes.jobs[i].pid,
                   job_status_strings(s), processes.jobs[i].cmd);
        }
    }
}

// bg_cmd: resumes one or more suspended jobs in the background
void bg_cmd(const char **toks) {

    // bg needs at least one argument
    if (toks[1] == NULL) {
        fprintf(stderr, "ERROR: bg needs some arguments\n");
        return;
    }

    // validate argument and resume the corresponding job in the background
    for (int j = 1; toks[j] != NULL; j++) {
        bool found = false;
        pid_t bg_pid = 0;


        // Check if argument is a valid job ID or PID
        if (!can_parse_as_int(toks[j])) {
            fprintf(stderr, "ERROR: bad argument for bg: %s\n", toks[j]);
            continue;
        }

        // argurment is a job ID (e.g., %1)
        if (toks[j][0] == '%') {

            // convert string after '%' to int for job ID
            int job_id = atoi(&toks[j][1]); 

            // check jobID is valid 
            // atoi returns 0 if the string cannot be parsed as an int
            // so if job_id is 0, it is an invalid job ID
            if (job_id == 0) {
                fprintf(stderr, "ERROR: bad argument for bg: %s\n", toks[j]);
                continue;
            }

            // find job with ID and suspended status
            for (int i = 0; i < processes.job_count; i++) {
                if (processes.jobs[i].job_id == job_id &&
                    processes.jobs[i].status == suspended) {
                    bg_pid = processes.jobs[i].pid;
                    found = true;
                    break;
                }
            }
            
            // if no job found with jobID and suspended status, print error message
            if (!found) fprintf(stderr, "ERROR: no job %d\n", job_id);
        } 
        
        // argument is a PID (e.g., 12345)
        else {
            
            // convert string to int for PID
            pid_t pid = atoi(toks[j]);

            // find job with PID and suspended status
            for (int i = 0; i < processes.job_count; i++) {
                if (processes.jobs[i].pid == pid &&
                    processes.jobs[i].status == suspended) {
                    bg_pid = pid;
                    found = true;
                    break;
                }
            }

            // if no job found with PID and suspended status, print error message
            if (!found) fprintf(stderr, "ERROR: no PID %d\n", pid);
        }

        // if a job is found 
        // resume the job in the bg by sending SIGCONT 
        // update the job status to continued
        if (found && bg_pid > 0) {

            if (kill(bg_pid, SIGCONT) == -1) {
                perror("ERROR: failed to resume job");
            } 
        }
    }
}

// fg_cmd: moves a background or suspended job to the foreground
void fg_cmd(const char **toks) {

    // fg needs exactly one argument
    if (toks[1] == NULL || toks[2] != NULL) {
        fprintf(stderr, "ERROR: fg needs exactly one argument\n");
        return;
    }

    bool found = false;
    pid_t fg_pid = 0;

    // check if argument is a valid job ID or PID
    if (!can_parse_as_int(toks[1])) {
        fprintf(stderr, "ERROR: bad argument for fg: %s\n", toks[1]);
        return;
    }

    // argument is a job ID (e.g., %1)
    if (toks[1][0] == '%') {
       
        // convert string after '%' to int for job ID
        int job_id = atoi(&toks[1][1]);

        // check jobID is valid
        if (job_id == 0) {
            fprintf(stderr, "ERROR: bad argument for fg: %s\n", toks[1]);
            return;
        }

        // find job with this job ID
        for (int i = 0; i < processes.job_count; i++) {
            if (processes.jobs[i].job_id == job_id) {
                fg_pid = processes.jobs[i].pid;
                found = true;
                break;
            }
        }
        if (!found) fprintf(stderr, "ERROR: no job %d\n", job_id);
    } 
    
    // argument is a PID (e.g., 12345)
    else {
        
        // convert string to int for PID
        pid_t pid = atoi(toks[1]);

        // find job that is running or suspended with PID 
        for (int i = 0; i < processes.job_count; i++) {
            if (processes.jobs[i].pid == pid && 
                (processes.jobs[i].status == running || 
                processes.jobs[i].status == suspended)) {

                    // set the PID of fg job
                    fg_pid = pid;
                    found = true;
                    break;
            }
        }
        if (!found) fprintf(stderr, "ERROR: no PID %d\n", pid);
    }


    // note: we need to wait because when we move a job to the fg, 
    // the shell should not accept new user input until the fg job is no longer running
    // (i.e., it finishes, is killed, or is suspended).
    if (found && fg_pid > 0) {
        
        // move the job to the foreground
        processes.fg = fg_pid; 

        // Send SIGCONT to resume the job if it was suspended
        kill(fg_pid, SIGCONT);

        // wait for the job to finish, be killed, or be suspended
        while (processes.fg != -1) {
            usleep(1000);
        }
    }
}


// nuke_cmd: sends SIGKILL to all active jobs or specified jobs
void nuke_cmd(const char **toks) {


    // if no arguments, nuke all running or suspended jobs
    if (toks[1] == NULL) {
        for (int i = 0; i < processes.job_count; i++) {
            if (processes.jobs[i].status == running ||
                processes.jobs[i].status == suspended) {
                kill(processes.jobs[i].pid, SIGKILL); // Send SIGKILL
            }
        }
        return;
    }

    // Otherwise, nuke each specified job
    for (int j = 1; toks[j] != NULL; j++) {
        bool found = false;

        // check if argument is a valid job ID or PID
        if (!can_parse_as_int(toks[j])) {
            fprintf(stderr, "ERROR: bad argument for nuke: %s\n", toks[j]);
            continue;
        }

        // if argument is a job ID (e.g., %1)
        if (toks[j][0] == '%') {

            // convert string after '%' to int for job ID
            int job_id = atoi(&toks[j][1]);

            // check jobID is valid
            if (job_id == 0) {
                fprintf(stderr, "ERROR: bad argument for nuke: %s\n", toks[j]);
                continue;
            }

            // find job with this job ID
            for (int i = 0; i < processes.job_count; i++) {
                if (processes.jobs[i].job_id == job_id) {
                    found = true;

                    // if the job is running or suspended, send SIGKILL to nuke it
                    if (processes.jobs[i].status == running ||
                        processes.jobs[i].status == suspended) {
                        kill(processes.jobs[i].pid, SIGKILL); // Send SIGKILL
                    } else {
                        fprintf(stderr, "ERROR: no job %d\n", job_id);
                    }
                    break;
                }
            }
            if (!found) fprintf(stderr, "ERROR: no job %d\n", job_id);
        } 
        
        // if argument is a PID (e.g., 12345)
        else {
            
            // check PID is valid
            pid_t pid = atoi(toks[j]);

            // find job with this PID
            for (int i = 0; i < processes.job_count; i++) {
                if (processes.jobs[i].pid == pid) {
                    found = true;

                    // if the job is running or suspended, send SIGKILL to nuke it
                    if (processes.jobs[i].status == running ||
                        processes.jobs[i].status == suspended) {
                        kill(pid, SIGKILL); // Send SIGKILL
                    } else {
                        fprintf(stderr, "ERROR: no PID %d\n", pid);
                    }
                    break;
                }
            }
            if (!found) fprintf(stderr, "ERROR: no PID %d\n", pid);
        }
    }
}

// spawn_processes: forks a child process and execs the given external command
void spawn_processes(const char **toks, bool bg) {

    // count active jobs (running + suspended)
    int active = 0;
    
    for (int i = 0; i < processes.job_count; i++) {
        if (processes.jobs[i].status == running || 
            processes.jobs[i].status == suspended) 

            // if job is active, increment count
            active++;
    }

    // check if too many active jobs
    // print error and return
    if (active >= 32) {
        fprintf(stderr, "ERROR: too many jobs\n");
        return;
    }

    // block signals before fork to avoid race conditions
    block_signals();

    // fork a child process to run the external command
    pid_t pid = fork();

    // check if fork failed PID < 0, print error and return
    if (pid < 0) {
        // Fork failed
        fprintf(stderr, "ERROR: cannot run %s\n", toks[0]);
        unblock_signals();
        return;
    }

    // if fork succeeded, check if we are in the child or parent process
    if (pid == 0) {

        // Child process: set new process group, close stdin, unblock signals, exec

        setpgid(0, 0);                         // put child process in a new process group 

        close(STDIN_FILENO);                   // Close stdin in the child processs 

        unblock_signals();                     // Unblock signals in child process
                                               // so it can recieve signals from terminal.)

        execvp(toks[0], (char *const *)toks);  // replace the child process program (eg. sleep)
                                               // execvp searches the PATH of the program 

        fprintf(stderr, "ERROR: cannot run %s\n", toks[0]);   //  returns on failure
        exit(EXIT_FAILURE);
    }

    // Parent process: set process group for child
    setpgid(pid, pid);

    // background job
    if (bg) {

        // add to job table
        add_newjob(pid, toks[0], running);

        // no foreground job since bg, so set fg to -1
        processes.fg = -1;

        // unblock signals and let SIGCHLD handler 
        // find the job and update status
        unblock_signals();
    } 
    
    // foreground job
    else {
        
        // add job 
        add_newjob(pid, toks[0], running);

        // set the foreground job PID
        processes.fg = pid;

        // let SIGCHLD handler find job and update status
        unblock_signals();

        // wait until sigchld handler sets fg = -1
        while (processes.fg != -1) {
            usleep(1000);
        }

    }
}


// given functions ----------------------------------------------------------------------
 
    
// eval: dispatches a single parsed command to the appropriate handler
void eval(const char **toks, bool bg) {
    assert(toks);
    if (*toks == NULL) return;

    if (strcmp(toks[0], "quit") == 0) {
        if (toks[1] != NULL) {
            const char *msg = "ERROR: quit takes no arguments\n";
            write(STDERR_FILENO, msg, strlen(msg));
        } else {
            exit(0);
        }
    }
    // implement jobs cmd
    else if (strcmp(toks[0], "jobs") == 0) {
        jobs_cmd(toks);
    }
    // implement nuke cmd
    else if (strcmp(toks[0], "nuke") == 0) {
        nuke_cmd(toks);
    }
    // implement fg cmd
    else if (strcmp(toks[0], "fg") == 0) {
        fg_cmd(toks);
    }
    // implement bg cmd
    else if (strcmp(toks[0], "bg") == 0) {
        bg_cmd(toks);
    }
    // fork and exec
    else {   
        spawn_processes(toks, bg);
    }
}

// parse_and_eval: tokenizes a raw input line and evaluates each command
void parse_and_eval(char *s) {
    assert(s);
    const char *toks[MAXLINE+1];

    while (*s != '\0') {
        bool end = false;
        bool bg = false;
        int t = 0;

        while (*s != '\0' && !end) {
            // skip whitespace
            while (*s == '\n' || *s == '\t' || *s == ' ') ++s;
            // add token if not a command separator
            if (*s != ';' && *s != '&' && *s != '\0')
                toks[t++] = s;
            // advance to next separator
            while (strchr("&;\n\t ", *s) == NULL) ++s;
            switch (*s) {
                case '&':
                    bg = true;
                    end = true;
                    break;
                case ';':
                    end = true;
                    break;
            }
            if (*s) *s++ = '\0';
        }
        toks[t] = NULL;
        eval(toks, bg);
    }
}


// running = green, suspended = yellow, killed = red, finished = dim
const char *status_color(job_status s) {
    switch(s) {
        case running:   return "\033[32m";          // green
        case suspended: return "\033[33m";          // yellow
        case killed:    return "\033[31m";          // red
        case killed_core_dumped: return "\033[31m"; // red
        case finished:  return "\033[2m";           // dim
        default:        return "\033[0m";
    }
}

// print at the start of repl()
void print_banner() {
    printf(
        "\n\033[32m"   
        " ██████╗██████╗  █████╗ ███████╗██╗  ██╗\n"
        "██╔════╝██╔══██╗██╔══██╗██╔════╝██║  ██║\n"
        "██║     ██████╔╝███████║███████╗███████║\n"
        "██║     ██╔══██╗██╔══██║╚════██║██╔══██║\n"
        "╚██████╗██║  ██║██║  ██║███████║██║  ██║\n"
        " ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝\033[0m\n\n"
        "\033\n"
        "\033[36mcrash shell 1.0.0\033[0m\n"   
        "\033[2minteractive unix shell\033[0m\n"
        "\033\n\n"
    );
}

// writes the shell prompt to stdout
void prompt() {
    const char *p =
    "\033[32muser\033[0m"       // green  "user"
    "\033[2m@\033[0m"           // dim    "@"
    "\033[32mcrash\033[0m"      // green "crash"
    "\033[2m:\033[0m"           // dim    ":"
    "\033[34m~/projects\033[0m" // blue  path
    "\033[2m $ \033[0m";        // dim "$"
    write(STDOUT_FILENO, p, strlen(p));
}

// repl: main read-eval-print loop
int repl() {
    char *buf = NULL;
    size_t len = 0;
    while (prompt(), getline(&buf, &len, stdin) != -1) {
        parse_and_eval(buf);
    }

    if (buf != NULL) free(buf);
    if (ferror(stdin)) {
        perror("ERROR");
        return 1;
    }
    return 0;
}

// main: entry point
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    install_sig_handlers();
    // initialize to 0
    memset(&processes, 0, sizeof(shell));
    // restore fg to -1
    processes.fg = -1;
    print_banner();
    return repl();
}