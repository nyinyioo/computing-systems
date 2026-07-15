```
 ██████╗██████╗  █████╗ ███████╗██╗  ██╗
██╔════╝██╔══██╗██╔══██╗██╔════╝██║  ██║
██║     ██████╔╝███████║███████╗███████║
██║     ██╔══██╗██╔══██║╚════██║██╔══██║
╚██████╗██║  ██║██║  ██║███████║██║  ██║
 ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝
```


###  A lightweight Unix shell that supports


- `Foreground and background processes` 
- `Signal handling` 
- `Process management`

--- 

### How It Works

> The shell runs a **read-eval-print loop** (`repl`) which prints a prompt such as `crash`.
>
> Commands are read with `getline` and parsed by `parse_and_eval` using whitespace and `;` / `&` separators.
>
> A trailing `&` runs a command as a background job.
>
> Each command is handled by `eval`. Built-in commands (`jobs`, `bg`, `fg`, `nuke`, `quit`) run inside the shell, while other commands are started with `fork` and `execvp`.
>
> The child process creates a new process group with `setpgid` and runs the program. The parent process stores jobs in a table with a maximum of 32 active jobs.
>
> Foreground jobs block the shell until they finish or change state, while background jobs immediately return control to the prompt.
>
> The `SIGCHLD` handler keeps job states updated using `waitpid` with `WNOHANG`, `WUNTRACED`, and `WCONTINUED`. Signals are blocked during `fork` to prevent race conditions before the job is added to the table.
>
> The shell forwards terminal signals such as `SIGINT` (`Ctrl+C`), `SIGTSTP` (`Ctrl+Z`), `SIGQUIT` (`Ctrl+\`), and `SIGCONT` to the current foreground job instead of handling them itself.
--- 

### API

Jobs are referenced as **job ID** prefixed with `%` (e.g. `%1`) or **PID** (e.g. `12345`).

Job status output has the form `[id] (pid)  status  cmd`

where `status` is one of `running`, `suspended`, `finished`, `killed`, `killed (core dumped)`, `continued`, or `failed`.

```bash
# run an external program in the foreground
<program> [args...]

# run an external program in the background
<program> [args...] &

# list all running or suspended jobs
jobs

# resume suspended job(s) in the background (SIGCONT)
bg <job|pid> [job|pid...]

# move a job to the foreground and wait for it
fg <job|pid>

# kill all active jobs (SIGKILL)
nuke [job|pid...]

# exit the shell
quit
```



---

### Setup


```bash
# 1. Clone the repository
git clone <repo-url>
cd shell

# 2. Build the project
make

# 3. Run the shell
./crash

# 4. Clean build files
make clean
```
---




