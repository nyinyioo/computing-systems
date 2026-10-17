#include "vm.h"
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define PAGE_SIZE     4096              // 4KB page size
#define PTE_SIZE      4                 // PTE is 4 bytes
#define FREE_LIST_END 0xFFFFFFFFu       // sentinel value for end of free list (invalid physical address)

// PTE bits
#define PTE_VALID      (1u << 0)        // valid bit
#define PTE_PRESENT    (1u << 1)        // present bit
#define PTE_READABLE   (1u << 2)        // readable bit
#define PTE_WRITABLE   (1u << 3)        // writable bit
#define PTE_EXECUTABLE (1u << 4)        // executable bit
#define PTE_USER       (1u << 5)        // user bit
#define PTE_ACCESSED   (1u << 6)        // accessed bit

// virtual address splitting
#define VA_L1(va)     (((va) >> 22) & 0x3FFu)  // bits 31:22 = L1 index
#define VA_L2(va)     (((va) >> 12) & 0x3FFu)  // bits 21:12 = L2 index
#define VA_OFFSET(va) ((va) & 0xFFFu)          // bits 11:0  = page offset

// page 0 metadata offset       // physical address in page 0 where the allocator stores the: 
#define OFF_FREE_HEAD    24     // - first free page in the free list.
#define OFF_SWAP_HEAD    28     // - first free swap page in the free list.
#define OFF_SWAP_FILE    32     // - physical address of the swap file pointer
#define OFF_ASID_TABLE   40     // - ASID table: each ASID maps to an L1 page table address
                        
#define SWAP_IO_ERR (FREE_LIST_END - 1)

/*      
    Implements a simple virtual memory with two-level page table. 

    Our VM config is 4GB (32-bit address space) with 4KB page sizes, 
    so we have 1024 PTE entries in each level of the page table.

    Each page table entry (PTE) is 4 bytes and has the following format:

        bits [31:12] = (ppn) physical page number
        bits [11:7]  = (reserved) reserved, must be zero
        bits    [6]  = (accessed) indicates this page was used for translation
        bits    [5]  = (user) indicates user-level accesses permitted (otherwise kernel-level only)
        bits    [4]  = (executable) indicates that instruction fetches from this page are permitted
        bits    [3]  = (writable) indicates writes to this page are permitted
        bits    [2]  = (readable) indicates  reads from this page are permitted
        bits    [1]  = (present) indicates this page is resident in physical memory
        bits    [0]  = (valid) indicates this page is mapped in the current virtual address space 
                       (and may or may not be resident)

    ASIDs and address spaces:

        The ASID table is stored in physical page 0, with each ASID mapping to
        the physical address of each processes L1 page table.
        The system supports up to 512 concurrent address spaces.

    The VM supports the following operations: 

        - vm_init: initialize the VM and free list
        - vm_translate: translate a virtual address to a physical address with permission checks
        - vm_new_addr_space: create a new address space for an ASID and return its L1 page table address
        - vm_map_page: map a virtual page to a physical page with permissions
        - vm_destroy_addr_space: destroy an address space and free all associated pages
        - vm_unmap_page: unmap a virtual page and free its physical page

    Swapping:

        Case 1. page allocation failure: physical memory is full, a present page is 
            evicted to swap to make room for a new page.

        Case 2. page fault: present=0 during translate/map/unmap,
            the page is swapped back into physical memory before continuing.

        - Swap space is at least 2 pages and no more than 67,108,864 pages.

        - Swap file pointer (FILE*) is stored in page 0 at offset OFF_SWAP_FILE.

        - Swap free list head (slot index) stored in page 0 at offset OFF_SWAP_HEAD.
        - Each free slot's first 4 bytes in the FILE = next free slot index.

        Eviction policy: 
            first present data page found in the current process's page table.

        PTE encoding:
            present=1  bits[31:12] = PPN         (physical page number)
            present=0  bits[31:6]  = swap slot   (swap file index)
                       bits[5:2]   = permissions (no change)
                       bit[0]      = valid=1     (no change)
*/


// helper functions -------------------------------------------------------------------------------------


// read a little-endian uint32 from physmem at byte offset addr
// args: physmem = pointer to physical memory, addr = byte offset in physmem
static uint32_t read_pte_from_physmem(void *physmem, uint32_t addr) {

    // assert physmem is not NULL
    assert(physmem != NULL);

    // physmen byte-addressed array at offset addr
    uint8_t *bytes = (uint8_t *)physmem + addr;    

    // returns a 4 byte little-endian uint32 
    return (uint32_t)bytes[0]              
         | ((uint32_t)bytes[1] << 8)       
         | ((uint32_t)bytes[2] << 16)
         | ((uint32_t)bytes[3] << 24);
}

// write a little-endian uint32 to physmem at byte offset addr
// args: physmem = pointer to physical memory, addr = byte offset in physmem, value = uint32 value to write
static void write_uint32_to_physmem(void *physmem, uint32_t addr, uint32_t value) {

    // assert physmem is not NULL
    assert(physmem != NULL);

    // physmen byte-addressed array at offset addr 
    uint8_t *bytes = (uint8_t *)physmem + addr;

    // write 4 bytes little-endian uint32
    bytes[0] = value & 0xFF;
    bytes[1] = (value >> 8)  & 0xFF;
    bytes[2] = (value >> 16) & 0xFF;
    bytes[3] = (value >> 24) & 0xFF;
}

// removes the head page from the free list and return the next free page
// return FREE_LIST_END if empty 
static paddr_t alloc_page(void *physmem) {

	// read current head from free list
	paddr_t head = read_pte_from_physmem(physmem, OFF_FREE_HEAD);

	// if list is empty, return ptr
	if (head == FREE_LIST_END) {return FREE_LIST_END;}

	// read what head ptr points to ~ (next free page)
	paddr_t next = read_pte_from_physmem(physmem, head);

	// update off_free_head to point to next page in free list
	write_uint32_to_physmem(physmem, OFF_FREE_HEAD, next);

	// memset the allocated page to 0 before returning it
	memset((uint8_t *)physmem + head, 0, PAGE_SIZE);

	return head;
}
	

// returns free page by inserting at the head
// caller must ensure page being freed is not already in free list
static void free_page(void *physmem, paddr_t page){

    // assert valid page 
    assert (page != FREE_LIST_END);

    // save the head of the free list
    paddr_t prev_head = read_pte_from_physmem(physmem, OFF_FREE_HEAD);

    // store the head in the freed page's first 4 bytes
    write_uint32_to_physmem(physmem, page, prev_head);

    // update OFF_FREE_HEAD to point to the freed page
    write_uint32_to_physmem(physmem, OFF_FREE_HEAD, page);
}

// helper to get swap file pointer from vm handle
static FILE *get_swap(void *vm) {
    FILE **swap_ptr = (FILE **)((uint8_t *)vm + OFF_SWAP_FILE);
    return *swap_ptr;
}


// allocates a new swap slot
// returns the slot index or FREE_LIST_END if no free swap slots remain
static uint32_t alloc_swap_slot(void *vm, FILE *swap) {

    // read current head from swap free list
    uint32_t head = read_pte_from_physmem(vm, OFF_SWAP_HEAD);

    // if list is empty, return ptr
    if (head == FREE_LIST_END) {return FREE_LIST_END;}

    // read next free slot from the FILE
    uint32_t next;
    if (fseek(swap, (long)(head * PAGE_SIZE), SEEK_SET) != 0) return SWAP_IO_ERR;
    if (fread(&next, sizeof(uint32_t), 1, swap) != 1) return SWAP_IO_ERR;

    // update OFF_SWAP_HEAD in page 0 to point to next
    write_uint32_to_physmem(vm, OFF_SWAP_HEAD, next);

    return head;
}

// return bool for success
static bool free_swap_slot(void *vm, FILE *swap, uint32_t slot){

    // assert valid slot 
    assert (slot != FREE_LIST_END);

    // save the head of the swap free list
    uint32_t prev_head = read_pte_from_physmem(vm, OFF_SWAP_HEAD);

    // store the head in the freed slot's first 4 bytes in the swap file
    if (fseek(swap, (long)(slot * PAGE_SIZE), SEEK_SET) != 0) return false;
    if (fwrite(&prev_head, sizeof(uint32_t), 1, swap) != 1) return false;

    // update OFF_SWAP_HEAD to point to the freed slot
    write_uint32_to_physmem(vm, OFF_SWAP_HEAD, slot);

    return true;
}

// swaps out a page from physical memory to the swap file
// and returns the freed physical page address
static paddr_t swap_out(void *vm, FILE *swap, paddr_t pt) {

    //a. traverse L1 table
    for (int i = 0; i < 1024; i++) {
        paddr_t l1_pte_addr = pt + i * sizeof(uint32_t);
        uint32_t l1_pte = read_pte_from_physmem(vm, l1_pte_addr);
        if (!(l1_pte & PTE_VALID)) continue;
        if (!(l1_pte & PTE_PRESENT)) continue;
        // if valid and present, get L2 table address from L1 PTE
        paddr_t l2_table = (l1_pte >> 12) << 12;
        
        // b. traverse L2 table
        for (int j = 0; j < 1024; j++) {
            paddr_t l2_pte_addr = l2_table + j * sizeof(uint32_t);
            uint32_t l2_pte = read_pte_from_physmem(vm, l2_pte_addr);
            if (!(l2_pte & PTE_VALID)) continue;
            if (!(l2_pte & PTE_PRESENT)) continue;

            // c. find evict page
            paddr_t evict_page = (l2_pte >> 12) << 12;

            // d. allocate a swap slot
            uint32_t slot = alloc_swap_slot(vm, swap);
            if (slot == SWAP_IO_ERR) return SWAP_IO_ERR;
            if  (slot == FREE_LIST_END) return FREE_LIST_END;

            // e. write evict page to swap file
            if (fseek(swap, (long)(slot * PAGE_SIZE), SEEK_SET) != 0) return SWAP_IO_ERR;
            if (fwrite((uint8_t *)vm + evict_page, 1, PAGE_SIZE, swap) != PAGE_SIZE) return SWAP_IO_ERR;

            // f. update L2 PTE to indicate SWAP
            uint32_t perm_bits = l2_pte & 0x3C;
            uint32_t swap_pte = ((slot & 0x3FFFFFFu) << 6) | perm_bits | PTE_VALID;
            write_uint32_to_physmem(vm, l2_pte_addr, swap_pte);

            // g. free the physical page
            free_page(vm, evict_page);

            // h. return freed physical page address
            return evict_page;
        }
    }

    // no pages to evict
    return FREE_LIST_END;
}


// swaps in a page from the swap file to physical memory and updates the PTE
static vm_status_t swap_in(void *vm, FILE *swap, paddr_t pt, paddr_t pte_addr) {

    // a. read swap slot from PTE bits[31:6]
    uint32_t swap_pte = read_pte_from_physmem(vm, pte_addr);
    uint32_t slot = swap_pte >> 6;

    // b. allocate physical page, evict if RAM full
    paddr_t phys_page = alloc_page(vm);
    if (phys_page == FREE_LIST_END) {
        paddr_t evicted = swap_out(vm, swap, pt);
        if (evicted == SWAP_IO_ERR)   {return VM_BAD_IO;}
        if (evicted == FREE_LIST_END) {return VM_OUT_OF_MEM;}
        phys_page = alloc_page(vm);
        if (phys_page == FREE_LIST_END) {return VM_OUT_OF_MEM;}
    }

    // c. read page from swap file into physical page
    if (fseek(swap, (long)(slot * PAGE_SIZE), SEEK_SET) != 0) return VM_BAD_IO;
    if (fread((uint8_t *)vm + phys_page, 1, PAGE_SIZE, swap) != PAGE_SIZE) return VM_BAD_IO;

    // d. restore PTE 
    uint32_t perm_bits = swap_pte & 0x3C;
    uint32_t pte = ((phys_page >> 12) << 12) | perm_bits | PTE_VALID | PTE_PRESENT;
    write_uint32_to_physmem(vm, pte_addr, pte);

    // e. free the swap slot
    if (!free_swap_slot(vm, swap, slot)) {return VM_BAD_IO;}

    return VM_OK;
}

// VM functions -------------------------------------------------------------------------------------

// VM INIT: initialize the free list and return a handle to vm 
void *vm_init(void *physmem, size_t num_phys_pages, FILE *swap, size_t num_swap_pages) {
 
    // zero out page 0
    memset(physmem, 0, PAGE_SIZE);

    // store swap file pointer in page 0
    FILE **swap_ptr = (FILE **)((uint8_t *)physmem + OFF_SWAP_FILE);
    *swap_ptr = swap;

    // build free list: pages 1..N-1 linked together
    // each free page's first 4 bytes = paddr of next free page
    if (num_phys_pages > 1) {

        // page 0 reserved for metadata, so start free list at page 1
        write_uint32_to_physmem(physmem, OFF_FREE_HEAD, PAGE_SIZE);
        
        // link page i -> page i+1
        for (size_t i = 1; i < num_phys_pages - 1; i++){
            write_uint32_to_physmem(physmem, i * PAGE_SIZE, (i + 1) * PAGE_SIZE);
        }

        // last page points to end of list
        write_uint32_to_physmem(physmem, (num_phys_pages - 1) * PAGE_SIZE, FREE_LIST_END);
    } 
    
    // no free pages
    else {
        write_uint32_to_physmem(physmem, OFF_FREE_HEAD, FREE_LIST_END);
    }


    // build swap free list: 0 .. M-1
    // each swap page's first 4 bytes = index of next free swap page
    if ( swap != NULL && num_swap_pages > 0) {
        
        // write first slot index to 0, not physical address
        write_uint32_to_physmem(physmem, OFF_SWAP_HEAD, 0);

        // link slot i -> slot i+1, written into SWAP file
        for (size_t i = 0; i < num_swap_pages - 1; i++) {
            uint32_t next = (uint32_t)(i + 1);
            if (fseek(swap, (long)(i * PAGE_SIZE), SEEK_SET) != 0) {return NULL;}
            if (fwrite(&next, sizeof(uint32_t), 1, swap) != 1) {return NULL;}
        }

        // last swap page points to end of list
        uint32_t end = FREE_LIST_END;
        if (fseek(swap, (long)((num_swap_pages - 1) * PAGE_SIZE), SEEK_SET) != 0) {return NULL;}
        if (fwrite(&end, sizeof(uint32_t), 1, swap) != 1) {return NULL;}
    }

    // no swap space
    else {
        write_uint32_to_physmem(physmem, OFF_SWAP_HEAD, FREE_LIST_END);
    }

    // return vm handle 
    return physmem;
}


//VM TRANSLATE: translates a virtual address to a physical address
vm_result_t vm_translate(void *vm, paddr_t pt, vaddr_t addr, access_type_t access, bool user) {
    FILE *swap = get_swap(vm);

    // L1 lookup: use bits 31:22 to index into L1 page table at pt
    uint32_t top_level_index    = VA_L1(addr);
    uint32_t bottom_level_index = VA_L2(addr);
    uint32_t offset             = VA_OFFSET(addr);

    // read L1 PTE
    paddr_t  top_level_pte_addr = pt + top_level_index * sizeof(uint32_t);
    uint32_t top_level_pte      = read_pte_from_physmem(vm, top_level_pte_addr);

    // if not valid, bad address
    if (!(top_level_pte & PTE_VALID))   { return (vm_result_t){VM_BAD_ADDR, 0}; }

    // if not valid and present, L1 swap in
    if (!(top_level_pte & PTE_PRESENT)) { 
        vm_status_t s = swap_in(vm, swap, pt, top_level_pte_addr);
        if (s != VM_OK) { return (vm_result_t){s, 0};}
        
        // after swap in, read the L1 PTE again to get updated bits and ppn
        top_level_pte = read_pte_from_physmem(vm, top_level_pte_addr);
    }

    // L2 lookup: extract ppn from L1 PTE to find L2 table base
    paddr_t  bottom_level_table_addr = (top_level_pte >> 12) << 12;
    paddr_t  bottom_level_pte_addr   = bottom_level_table_addr + bottom_level_index * sizeof(uint32_t);
    uint32_t bottom_level_pte        = read_pte_from_physmem(vm, bottom_level_pte_addr);

    // if not valid or not present, bad address
    if (!(bottom_level_pte & PTE_VALID))   { return (vm_result_t){VM_BAD_ADDR, 0}; }

    // if not valid and present, L2 swap in
    if (!(bottom_level_pte & PTE_PRESENT)) { 
        vm_status_t s = swap_in(vm, swap, pt, bottom_level_pte_addr);
        if (s != VM_OK) { return (vm_result_t){s, 0};}
        
        // after swap in, read the L2 PTE again to get updated bits and ppn
        bottom_level_pte = read_pte_from_physmem(vm, bottom_level_pte_addr);
    }

    // permission checks
    bool permission_denied = false;

    bool pte_readable   = bottom_level_pte & PTE_READABLE;
    bool pte_writable   = bottom_level_pte & PTE_WRITABLE;
    bool pte_executable = bottom_level_pte & PTE_EXECUTABLE;
    bool pte_user       = bottom_level_pte & PTE_USER;

    if (access == VM_READ  && !pte_readable)   { permission_denied = true; }
    if (access == VM_WRITE && !pte_writable)   { permission_denied = true; }
    if (access == VM_EXEC  && !pte_executable) { permission_denied = true; }
    if (user               && !pte_user)       { permission_denied = true; }

    if (permission_denied) return (vm_result_t){VM_BAD_PERM, 0};

    // set accessed bit
    if (!(bottom_level_pte & PTE_ACCESSED)) {
        write_uint32_to_physmem(vm, bottom_level_pte_addr, bottom_level_pte | PTE_ACCESSED);
    } 

    // final physical address = ppn * 4096 + offset
    paddr_t final_physical_addr = ((bottom_level_pte >> 12) << 12) + offset;
    return (vm_result_t){VM_OK, final_physical_addr};
}


// VM_NEW_ADDR_SPACE: creates a new address space for the given ASID 
// and returns a handle to the new address space (ie. addr of L1 page table)
vm_result_t vm_new_addr_space(void *vm, asid_t asid) {

    FILE *swap = get_swap(vm);

    // a. get asid slot in page 0 
    paddr_t asid_slot = OFF_ASID_TABLE + asid * sizeof(uint32_t);

    // b. allocate one page for L1 table
    paddr_t l1 = alloc_page(vm);

    // if alloc fails, swap out a page and try alloc again
    if (l1 == FREE_LIST_END) {

        if (swap == NULL) return (vm_result_t){VM_OUT_OF_MEM, 0};
        
        // iterate through ASID table 
        // to find an active address space to evict a page from
        for (int i = 0; i < 512; i++) {
            paddr_t other_pt = read_pte_from_physmem(vm, OFF_ASID_TABLE + i * sizeof(uint32_t));
            if (other_pt == 0) continue;
            l1 = swap_out(vm, swap, other_pt);
            if (l1 == SWAP_IO_ERR) return (vm_result_t){VM_BAD_IO, 0};
            if (l1 != FREE_LIST_END) break;
        }
        if (l1 == FREE_LIST_END) return (vm_result_t){VM_OUT_OF_MEM, 0};
    }

    // c. store L1 page table address in asid table
    write_uint32_to_physmem(vm, asid_slot, l1);

    return (vm_result_t){VM_OK, l1};
}
	
// VM_MAP_PAGE: maps a virtual page to a physical page with the given permissions
vm_status_t vm_map_page(void *vm, paddr_t pt, vaddr_t addr, bool user, bool exec, bool write, bool read) {

    FILE *swap = get_swap(vm);

    // a. split virtual address
    uint32_t l1_idx = VA_L1(addr); 
    uint32_t l2_idx = VA_L2(addr);

    // b. read L1 PTE to check if L2 table exists
    paddr_t l1_pte_addr = pt + l1_idx * sizeof(uint32_t);
    uint32_t l1_pte = read_pte_from_physmem(vm, l1_pte_addr);

    // c. if L1 PTE not valid, allocate L2 table
    paddr_t l2_table = 0;     
    bool l2_allocated = false;

    if (!(l1_pte & PTE_VALID)) {

        // if no L2 table yet, allocate one
        l2_table = alloc_page(vm);

        // if RAM full and swap available
        // evict a page and try alloc again
        if (l2_table == FREE_LIST_END) {
            if (swap == NULL) return VM_OUT_OF_MEM;
            l2_table = swap_out(vm, swap, pt);
            if (l2_table == SWAP_IO_ERR)   {return VM_BAD_IO;}
            if (l2_table == FREE_LIST_END) {return VM_OUT_OF_MEM;}
        }
    
        l2_allocated = true;
    }

    // if l2 table exists, get its address from L1 PTE
    else {
        l2_table = (l1_pte >> 12) * PAGE_SIZE;
    }

    // d. read L2 PTE
    paddr_t l2_pte_addr = l2_table + l2_idx * sizeof(uint32_t);
    uint32_t l2_pte = read_pte_from_physmem(vm, l2_pte_addr);

    // e. if L2 PTE valid, page mapped, return duplicate 
    if (l2_pte & PTE_VALID) {
        if (l2_allocated) {free_page(vm, l2_table);}
        return VM_DUPLICATE;
    }

    // f. allocate data page for the process
    paddr_t data_page = alloc_page(vm);
    
    // if RAM full and swap available
    // evict a page and try alloc again
    if (data_page == FREE_LIST_END) {

        if (swap == NULL) {                       
            if (l2_allocated) {free_page(vm, l2_table);}
            return VM_OUT_OF_MEM;
        }
        
        paddr_t evicted = swap_out(vm, swap, pt);

        if (evicted == SWAP_IO_ERR) {
            if (l2_allocated) free_page(vm, l2_table);
            return VM_BAD_IO;
        }

        if (evicted == FREE_LIST_END) {
            if (l2_allocated) free_page(vm, l2_table);
            return VM_OUT_OF_MEM;
        }

        data_page = alloc_page(vm);
        if (data_page == FREE_LIST_END) {
            if (l2_allocated) free_page(vm, l2_table);
            return VM_OUT_OF_MEM;
        }

    }

    // g. write L2 PTE set PPN, valid, present, and permission bits
    uint32_t new_l2_pte = (data_page / PAGE_SIZE) << 12
                        | PTE_VALID | PTE_PRESENT
                        | (read  ? PTE_READABLE   : 0)
                        | (write ? PTE_WRITABLE   : 0)
                        | (exec  ? PTE_EXECUTABLE : 0)
                        | (user  ? PTE_USER       : 0);
                        
    write_uint32_to_physmem(vm, l2_pte_addr, new_l2_pte);
   
    // h. write L1 PTE to point towards L2 table 
    if (l2_allocated) {
        uint32_t new_l1_pte = (l2_table / PAGE_SIZE) << 12
                            | PTE_VALID | PTE_PRESENT;
                            
        write_uint32_to_physmem(vm, l1_pte_addr, new_l1_pte);
    }
    return VM_OK;
}

// VM_DESTROY_ADDR_SPACE: destroys the address space for the given ASID
// and frees all associated pages (L1, L2, data)
vm_status_t vm_destroy_addr_space(void *vm, asid_t asid) {
    FILE *swap = get_swap(vm);

    //a. look up pt for this asid
    paddr_t asid_slot = OFF_ASID_TABLE + asid * sizeof(uint32_t); 
    paddr_t pt = read_pte_from_physmem(vm, asid_slot);            
    if (pt == 0) {return VM_BAD_ADDR;}

    
    //b. traverse L1 table
    for (int i = 0; i < 1024; i++) {
        paddr_t l1_pte_addr = pt + i * sizeof(uint32_t);
        uint32_t l1_pte = read_pte_from_physmem(vm, l1_pte_addr);


        // if not valid, skip to next entry
        if (!(l1_pte & PTE_VALID)) {continue; }  

        // if valid, get L2 table address from L1 PTE                   
        paddr_t l2_table = (l1_pte >> 12) * PAGE_SIZE;    
        
        //c. traverse L2 table    
        for (int j = 0; j < 1024; j++) {
            paddr_t l2_pte_addr = l2_table + j * sizeof(uint32_t);
            uint32_t l2_pte = read_pte_from_physmem(vm, l2_pte_addr);

            // skip invalid entries first 
            if (!(l2_pte & PTE_VALID)) {continue; }

            // check if page is present 
            if (l2_pte & PTE_PRESENT) {
                
                // free physical page
                free_page(vm, (l2_pte >> 12) * PAGE_SIZE);
            } 
            // if not present, free swap slot
            else {
                uint32_t slot = l2_pte >> 6;
                if (!free_swap_slot(vm, swap, slot)) {return VM_BAD_IO;}
            }
        }

        //d. free L2 table
        free_page(vm, l2_table);
    }                                                              
    //e. free L1 table
    free_page(vm, pt);

    //f. clear asid slot
    write_uint32_to_physmem(vm, asid_slot, 0);                    

    return VM_OK;
}

// VM_UNMAP_PAGE: unmaps a virtual page from its physical page
vm_status_t vm_unmap_page(void *vm, paddr_t pt, vaddr_t addr) {
    FILE *swap = get_swap(vm);

    // a. split virtual address
    uint32_t l1_idx = VA_L1(addr);
    uint32_t l2_idx = VA_L2(addr);
    
    //b. read l1 pte 
    paddr_t l1_pte_addr = pt + l1_idx * sizeof(uint32_t);
    uint32_t l1_pte = read_pte_from_physmem(vm,l1_pte_addr);
    if(!(l1_pte & PTE_VALID)) {return VM_BAD_ADDR;}
    
    //c. get L2 table address from L1 PTE
    paddr_t l2_table = (l1_pte >> 12) * PAGE_SIZE;
    
    //d. read L2 PTE
    paddr_t l2_pte_addr = l2_table + l2_idx * sizeof(uint32_t);
    uint32_t l2_pte = read_pte_from_physmem(vm, l2_pte_addr);

    // skip invalid entries first
    if (!(l2_pte & PTE_VALID)) { return VM_BAD_ADDR; }
    

    //e. free the data page
    if (l2_pte & PTE_PRESENT) {
        paddr_t data_page = (l2_pte >> 12) * PAGE_SIZE;
        // free physical page
        free_page(vm, data_page);
    } 
    // if not present, free swap slot
    else {
        uint32_t slot = l2_pte >> 6;
         if (!free_swap_slot(vm, swap, slot)) {return VM_BAD_IO;}
    }

    //f. clear L2 PTE
    write_uint32_to_physmem(vm, l2_pte_addr, 0);

    //g. check if L2 table is empty
    bool l2_empty = true;
    for (int i = 0; i < 1024; i++) {
        uint32_t pte = read_pte_from_physmem(vm, l2_table + i * sizeof(uint32_t));
        if (pte & PTE_VALID) { l2_empty = false; break; }
    }

    //h. if L2 empty, free and clear L1 PTE
    if (l2_empty) {
        free_page(vm, l2_table);
        write_uint32_to_physmem(vm, l1_pte_addr, 0);
    }

    return VM_OK;
}


