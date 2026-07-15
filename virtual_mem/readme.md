```
 ██╗   ██╗███╗   ███╗███████╗███╗   ███╗
 ██║   ██║████╗ ████║██╔════╝████╗ ████║
 ██║   ██║██╔████╔██║█████╗  ██╔████╔██║
 ╚██╗ ██╔╝██║╚██╔╝██║██╔══╝  ██║╚██╔╝██║
  ╚████╔╝ ██║ ╚═╝ ██║███████╗██║ ╚═╝ ██║
   ╚═══╝  ╚═╝     ╚═╝╚══════╝╚═╝     ╚═╝
```

### A two-level virtual memory system that supports

- Permission bits: read, write, execute, user
- Two-level page table walks (L1 → L2)
- Physical page allocation via free list
- Address space creation and destruction per process
- Swap pages to disk

---

### How It Works

Every process gets its own **address space** — an isolated view of memory backed by physical pages. Virtual addresses are split into three parts:

```
 31        22 21        12 11          0
 ┌──────────┬────────────┬─────────────┐
 │  L1 idx  │   L2 idx   │   offset    │
 │  10 bits │   10 bits  │   12 bits   │
 └──────────┴────────────┴─────────────┘
```

A virtual address walks two page tables to find the physical address:

```
pt (L1 table)
 └─[L1 idx]──→ L2 table
                └─[L2 idx]──→ physical page
                               └─[offset]──→ final byte
```

---

### API

```c
// initialize physical memory and free list
void *vm_init(void *physmem, size_t num_phys_pages, FILE *swap, size_t num_swap_pages);

// translate a virtual address to a physical address
vm_result_t vm_translate(void *vm, paddr_t pt, vaddr_t addr, access_type_t access, bool user);

// create a new address space for a process
vm_result_t vm_new_addr_space(void *vm, asid_t asid);

// map a virtual page into an address space
vm_status_t vm_map_page(void *vm, paddr_t pt, vaddr_t addr, bool user, bool exec, bool write, bool read);

// unmap a virtual page and free physical memory
vm_status_t vm_unmap_page(void *vm, paddr_t pt, vaddr_t addr);

// destroy an address space and reclaim all pages
vm_status_t vm_destroy_addr_space(void *vm, asid_t asid);
```

---

### Setup

```bash
# 1. Clone the repository
git clone <repo-url>
cd virtual_mem

# 2. Build
make

# 3. Run tests
make memcheck

# 4. Clean
make clean
```
---
