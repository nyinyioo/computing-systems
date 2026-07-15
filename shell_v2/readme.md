```
██████╗██████╗  █████╗ ███████╗██╗  ██╗    ██╗   ██╗██████╗ 
██╔════╝██╔══██╗██╔══██╗██╔════╝██║  ██║    ██║   ██║╚════██╗
██║     ██████╔╝███████║███████╗███████║    ██║   ██║ █████╔╝
██║     ██╔══██╗██╔══██║╚════██║██╔══██║    ╚██╗ ██╔╝██╔═══╝ 
╚██████╗██║  ██║██║  ██║███████║██║  ██║     ╚████╔╝ ███████╗
 ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝      ╚═══╝  ╚══════╝
```

### A Unix shell 

with custom memory management  — no libc malloc, no kernel VM

---

### How It Works

```
┌─────────────────────────────────────────────┐
│                  crash shell                │
│         job control · signals · repl        │
└──────────────────┬──────────────────────────┘
                   │ spawns processes
                   ▼
┌─────────────────────────────────────────────┐
│              virtual memory                 │
│    address spaces · page tables · mmap      │
└──────────────────┬──────────────────────────┘
                   │ map pages into heap
                   ▼
┌─────────────────────────────────────────────┐
│                 allocator                   │
│         malloc · free · coalescing          │
└─────────────────────────────────────────────┘
```

When crash spawns a process:
1. **VM** creates a new address space (`vm_new_addr_space`) and maps in pages
2. **Allocator** manages the heap within those pages
3. **Shell** tracks the job, handles signals, reaps it on exit
4. **VM** destroys the address space (`vm_destroy_addr_space`) and reclaims all pages


---

### Setup


```bash
# 1. Clone the repository
git clone <repo-url>
cd os-shell

# 2. Build everything
make

# 3. Run
./crash

# 4. Clean
make clean
```
---
