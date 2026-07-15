### Sample Trace

- **32-bit** virtual addresses 
- **2-level** page table (L1 index → L2 index → PPN)
- **More PTE bits** (readable, writable, executable, user, accessed)

VPN (32 bit addresses) 

```c
31         22 21        12 11               0  
+------------+------------+------------------+
|  L1 index  |  L2 index  |     offset       |
|  10 bits   |  10 bits   |    12 bits       |
|  top 10    |  next 10   |  bottom 3 hex    |
+------------+------------+------------------+
```

PPN ( 32 bit addresses) 

```c
31                 12 11                          0        
+--------------------+---------------------------+
|        PPN         |          offset           |
|      20 bits       |          12 bits          |
|    (top 5 hex)     |       (bottom 3 hex)      |
+--------------------+---------------------------+
```

PTE (lab 2)

```c
31        12 11    7   6   5   4   3   2   1   0
┌───────────┬───────┬───┬───┬───┬───┬───┬───┬───┐
│    PPN    │  res  │ACC│USR│EXE│WRT│ RD│PRE│VAL│
│ (20 bits) │       │   │   │   │   │   │   │   │
└───────────┴───────┴───┴───┴───┴───┴───┴───┴───┘
```

### TRACE

---

1. split the address into L1 index / L2 index / offset
2. look up the L1 table → find the L2 table
3. look up the L2 table → find the PPN (check valid + present)
4. check the permission bits
5. result → physical address or fault

### EX:  0xABCDE123

**1. Split the address**

```c
VA       = 0xABCDE123
L1 index = (VA >> 22) & 0x3FF = 0x2AF   // VA_L1(va)
L2 index = (VA >> 12) & 0x3FF = 0x0DE   // VA_L2(va)
offset   =  VA        & 0xFFF = 0x123   // VA_OFFSET(va)
```

**2. Look up L1 table**

```c
each ASID has its own L1 page table (1024 entries)
go to entry [0x2AF] in L1 table

PTE at L1[0x2AF]:
  valid = 1  → L2 table exists, get its PPN
  valid = 0  → segfault, no L2 table at all
```

**3. Look up L2 table**

```c
use PPN from L1 entry to find L2 table in physical memory
go to entry [0x0DE] in that L2 table

PTE at L2[0x0DE]:
  valid = 0          → segfault
  valid = 1, present = 0  → page fault
  valid = 1, present = 1  → success!
```

**4. Check permission bits**

```
read access?   → check RD  bit
write access?  → check WRT bit
fetch access?  → check EXE bit
user access?   → check USR bit
any fail       → segfault (permission denied)
```

**5. Result**

```c
PPN    = top 20 bits of L2 PTE
offset = 0x123  (unchanged)

physical address = (PPN << 12) | offset
```