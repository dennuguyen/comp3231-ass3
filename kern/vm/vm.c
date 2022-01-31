#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <proc.h>
#include <vm.h>
#include <spl.h>
#include <current.h>

int vm_allocpte1(struct addrspace *as, paddr_t paddr) {
    int idx0;
    int i;

    // Get page table index.
    idx0 = PG_IDX0(paddr);

    // Malloc 1st level page table entry.
    as->pgtable[idx0] = (paddr_t **)kmalloc(PG_SIZE_1 * sizeof(paddr_t));
    if (as->pgtable[idx0] == 0) {
        return ENOMEM;
    }

    // Initialise 1st level page table entry.
    memset(as->pgtable[idx0], 0, PG_SIZE_1 * sizeof(paddr_t));

    // Zero-fill 2nd level page table entry.
    for (i = 0; i < PG_SIZE_1; i++) {
        as->pgtable[idx0][i] = NULL;
    }

    return 0;
}

int vm_allocpte2(struct addrspace *as, paddr_t paddr) {
    int idx0;
    int idx1;
    int i;

    // Get page table index.
    idx0 = PG_IDX0(paddr);
    idx1 = PG_IDX1(paddr);

    // Malloc 2nd level page table entry.
    as->pgtable[idx0][idx1] = (paddr_t *)kmalloc(PG_SIZE_2 * sizeof(paddr_t));
    if (as->pgtable[idx0][idx1] == 0) {
        return ENOMEM;
    }

    // Initialise 2nd level page table entry.
    memset(as->pgtable[idx0][idx1], 0, PG_SIZE_2 * sizeof(paddr_t));

    // Zero-fill 3rd level page table entry.
    for (i = 0; i < PG_SIZE_2; i++) {
        as->pgtable[idx0][idx1][i] = 0;
    }

    return 0;
}

int vm_allocpte3(struct addrspace *as, paddr_t paddr, int perm) {
    vaddr_t vaddr;
    paddr_t pfn;   // Page frame number.
    int idx0;
    int idx1;
    int idx2;

    // Get page table index.
    idx0 = PG_IDX0(paddr);
    idx1 = PG_IDX1(paddr);
    idx2 = PG_IDX2(paddr);

    // Allocate frame/physical address.
    vaddr = alloc_kpages(1);
    if (vaddr == 0) {
        return ENOMEM;
    }
    
    // Get page frame number.
    pfn = KVADDR_TO_PADDR(vaddr);
    
    // Zero-fill 3rd level page table entry.
    bzero((void *)PADDR_TO_KVADDR(pfn), PAGE_SIZE);

    // Assign 3rd level page table entry.
    as->pgtable[idx0][idx1][idx2] = (pfn & PAGE_FRAME) | GET_DIRTY_BIT(perm) |
        GET_VALID_BIT(perm);

    return 0;
}


void vm_bootstrap(void) {
    /* Initialise any global components of your VM sub-system here.  
     *  
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
}

/**
 * 
 */
int vm_fault(int faulttype, vaddr_t faultaddress) {
    struct addrspace *as;
    struct region *r;
    paddr_t **pte1;
    paddr_t *pte2;
    paddr_t pte3;
    paddr_t paddr;
    int spl;
    int entry_hi;
    int entry_lo;
    int result;
    int allocated_pte1_flag;
    int allocated_pte2_flag;

    // Set flags to false.
    allocated_pte1_flag = 0;
    allocated_pte2_flag = 0;

    // Sanity check curproc.
    if (curproc == NULL) {
        return EFAULT;
    }

    // Sanity check address space.
    as = proc_getas();
    if (as == NULL) {
        return EFAULT;
    }

    // Sanity check address space's regions.
    if (as->regions == NULL) {
        return EFAULT;
    }

    // Sanity check address space's page table.
    if (as->pgtable == NULL) {
        return EFAULT;
    }

    // Check faulttype is valid.
    switch (faulttype) {
        case VM_FAULT_READ:
            break;
        case VM_FAULT_WRITE:
            break;
        case VM_FAULT_READONLY:
            return EFAULT;
        default:
            return EINVAL;
    }

    // Get physical address.
    paddr = KVADDR_TO_PADDR(faultaddress);

    // Allocate 1st level page table entry if root entry is NULL.
    pte1 = as->pgtable[PG_IDX0(paddr)];
    if (pte1 == NULL) {
        result = vm_allocpte1(as, paddr);
        if (result != 0) {
            goto cleanupA;
        }

        allocated_pte1_flag = 1;
    }

    // Allocate 2nd level page table entry if entry was not found.
    pte2 = as->pgtable[PG_IDX0(paddr)][PG_IDX1(paddr)];
    if (pte2 == NULL) {
        result = vm_allocpte2(as, paddr);
        if (result != 0) {
            goto cleanupB;
        }

        allocated_pte2_flag = 1;
    }

    // Allocate 3rd level page table entry.
    pte3 = as->pgtable[PG_IDX0(paddr)][PG_IDX1(paddr)][PG_IDX2(paddr)];
    if (pte3 == 0) {
        r = search_region(as, faultaddress, 0);
        if (r == NULL) {
            result = EFAULT;
            goto cleanupC;
        }

        result = vm_allocpte3(as, paddr, r->cur_perm);
        if (result != 0) {
            goto cleanupC;
        }
    }

    // Get entry high and entry low.
    entry_hi = faultaddress & PAGE_FRAME;
    entry_lo = as->pgtable[PG_IDX0(paddr)][PG_IDX1(paddr)][PG_IDX2(paddr)];

    // Add pagetable entry randomly to the TLB.
    spl = splhigh();
    tlb_random(entry_hi, entry_lo);
    splx(spl);

    // Success
    result = 0;
    goto cleanupA;

cleanupC:
    // Undo pte2 memory allocation after failure.
    if (allocated_pte2_flag == 1 && pte2 != NULL) {
        kfree(pte2);
        pte2 = NULL;
    }

cleanupB:
    // Undo pte1 memory allocation after failure.
    if (allocated_pte1_flag == 1 && pte1 != NULL) {
        kfree(pte1);
        pte1 = NULL;
    }

cleanupA:
    return result;
}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void vm_tlbshootdown(const struct tlbshootdown *ts) {
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}

/**
 * TLB is flushed to protect process memory from access by other processes.
 * 
 * TLB is flushed by writing invalid data to TLB.
 * 
 * Uses NUM_TLB, tlb_write(), TLBHI_INVALID(), and TLBLO_INVALID from tlb.h.
 */
void vm_tlbflush(void) {
    int i;
    int spl;

    spl = splhigh();
    for (i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    splx(spl);
}
