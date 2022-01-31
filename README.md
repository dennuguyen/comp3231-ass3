# Assignment 3

## Due Date

10% Bonus: **18 April 1600**

Final: **23 April 1600**

## Research

### Page Tables

- A frame is a small chunk of physical memory.
- A page is a small chunk of virtual memory.
- A page table translates virtual addresses to physical addresses.
- A page table is an array of page table entries.
    - Page table index are page numbers.
    - Page table entries are `30-bit numbers` with the following fields:
        - `global`: 1 bit; indicates if PID bits in TLB can be ignored.
        - `valid`: 1 bit; indicates valid mapping for the page.
        - `dirty`: 1 bit; indicates page is being written to. If 0 then page is only accessible for reading.
        - `nocache`: 1 bit; unused in sys161.
        - `asid`: 6 bits; context or address space ID.
        - `frame number`: 20 bits; physical frame number

- 3 level page table
    - `uint32_t ***pgtable;`
    - first level indexed using 8 MSB of page number.
    - second level indexed using next 6 bits of page number.
    - third level indexed using LSB of page number.
    - e.g. if page number is `0b00000001 000010 000011` then to get the frame number `pgtable[1][2][3]`.
    - newly allocated frames initialised to `0`.

### Address Space

- Address space is a data structure associated with the virtual memory space of a process.
- Address space is a per-process data structure where the page table is also implicitly per-process.
- No need to store physical addresses in address space if we have a page table.
- Regions split up the address space into segments and allow protection of data.

```
struct region {
    vaddr_t vaddr;
    size_t sz;
    int readable;
    int writeable;
    int executable;
    struct region *next;
};

struct addrspace {
    struct region* regions;
    uint32_t ***pgtable;
};
```

- `alloc_kpage()` -> `kmalloc()`
- `free_kpage()` -> `kfree()`

To convert physical address to use as frame: `KVADDR_TO_PADDR(vaddr)`

- `as_create()`
    - allocate data structure used to keep track of an address space
    - calls `proc_getas()`

- `as_destroy()`
    - deallocate book keeping and page tables

- `as_copy()`
    - allocates a new address space
    - adds all the same regions as source
    - for each mapped page in source
        - allocate a frame in destination
        - copy contents from source frame to destination frame
        - add page table entry for destination

- `as_activate()`
    - flush TLB

- `as_deactivate()`
    - flush TLB

### Address Translation

- Kernel Address Space
    - kseg0
        - 512 MB
        - 

### TLB

- Application uses TLB to keep track of physical and virtual memory mapping.
- On TLB entry miss, follow the VM fault routine.
- entry_hi: match page number and ASID.
- entry_lo: contains frame number and permissions.

```
// TLB insert based on entry_hi and entry_lo
spl = splhigh();
tlb_random(entry_hi, entry_lo); // TLB refill
splx(spl);
```

### Hints
1. Review how the specified page table works from the lectures, and understand its relationship with the TLB.
1. Review the assignment specification and its relationship with the supplied code.
dumbvm is not longer compiled into the OS/161 kernel for this assignment (kern/arch/mips/vm/dumbvm.c), but you can review it as an example implementation within the interface/framework you will be working within.
1. Work out a basic design for your page table implementation.
1. Modify kern/vm/vm.c to insert , lookup, and update page table entries, and keep the TLB consistent with the page table.
1. Implement the TLB exception handler vm_fault() in vm.c using your page table.
1. Implement the functions in kern/vm/addrspace.c that are required for basic functionality (e.g. as_create(), as_prepare_load(), etc.). Allocating user pages in as_define_region() may also simplify your assignment, however good solution allocate pages in vm_fault().
1. Test and debug this. Use the debugger!

Note: Interrupts should be disabled when writing to the TLB, see dumbvm for an example. Otherwise, unexpected concurrency issues can occur.

as_activate() and as_deactivate() can be copied from dumbvm.

## Assumptions

- Can ignore ASID field -> load 0 for all address spaces.
- Can ignore demand paging.
- Don't use `kprintf()` for `vm_fault()`.

