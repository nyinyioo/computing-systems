```
 █████╗ ██╗      ██████╗  ██████╗
██╔══██╗██║     ██╔═══██╗██╔════╝
███████║██║     ██║   ██║██║
██╔══██║██║     ██║   ██║██║
██║  ██║███████╗╚██████╔╝╚██████╗
╚═╝  ╚═╝╚══════╝ ╚═════╝  ╚═════╝
```

### A heap allocator that supports

- `malloc` / `free` / `realloc`
- `Implicit Free List` / `First Fit` / `Coalescing` 

---

### How It Works

The heap is a flat slab of memory split into **blocks**. Each block follows the specification below: 

Boundary tags: 4-byte header/footer per block, size in bits [31:3], alloc in bits [2:0]

```
 ┌─────────────────────────────────────┐
 │  header (4 bytes)                   │
 │  [ size (31 bits) |  alloc (1 bit)] │
 ├─────────────────────────────────────┤
 │  allocated/free space               │
 │  (addr malloc returns)              │
 ├─────────────────────────────────────┤
 │  footer (4 bytes)                   │
 └─────────────────────────────────────┘
```

**Implicit Free list + First Fit**

Boundary tags are used to traverse the heap, checking each block's allocation bit to determine whether it is allocated or free. Since free blocks are not explicitly linked, the allocator traverses the entire heap and selects the first free block that fits the allocation request.

**Coalescing**

When a block is freed, adjacent free blocks are checked and coalesced into a single larger block to reduce fragmentation.

```
before free:   [used][FREE][used][FREE][used]
                             ↑
                          free()
                          

after free:    [used][FREE         ][used]
                      ↑ 
                    coalesced
```

---

### API

```c
// initialize the allocator with a fixed memory region
void alloc_init(void *heap, size_t size);

// allocate size bytes
void *alloc_malloc(size_t size);

// free a previously allocated block
void alloc_free(void *ptr);

// resize an allocation
void *alloc_realloc(void *ptr, size_t size);
```

---

### Setup

```bash
# 1. Clone the repository
git clone <repo-url>
cd allocator

# 2. Build
make

# 3. Run tests
make memcheck

# 4. Clean
make clean
```

