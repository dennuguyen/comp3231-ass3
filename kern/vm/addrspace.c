/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *    The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

struct region *init_region(vaddr_t vaddr,
                           size_t memsize,
                           int cur_perm,
                           int old_perm) {
    struct region *r;

    r = NULL;
    r = (struct region*)kmalloc(sizeof(struct region));
    if (r == NULL) {
        return NULL;
    }

    r->vaddr = vaddr;
    r->memsize = memsize;
    r->cur_perm = cur_perm;
    r->old_perm = old_perm;
    r->next = NULL;

    return r;
}

/**
 * Copies an old region to a new region and returns its pointer.
 * 
 * Note this does not copy over the next pointer. Next must be updated manually
 * outside this function.
 */
struct region *copy_region(struct region* old_r) {
    return init_region(old_r->vaddr,
                       old_r->memsize,
                       old_r->cur_perm,
                       old_r->old_perm);
}

/**
 * Adds regions to the end of the linked list.
 */
void add_region(struct addrspace *as, struct region *r) {
    struct region *cur;

    cur = as->regions; // Head of list.

    // Head of linked list is NULL.
    if (cur == NULL) {
        as->regions = r;
        return;
    }

    // Iterate linked list.
    while (cur->next != NULL) {
        cur = cur->next;
    }

    // Add region to end of linked list.
    cur->next = r;
}

/**
 * Removes a region anywhere within the linked list.
 */
void remove_region(struct addrspace *as, struct region *r) {
    struct region *cur;
    struct region *prv;

    cur = as->regions; // Head of list.
    prv = NULL;

    // Empty list.
    if (cur == NULL) {
        return;
    }

    // Find region.
    while (cur != NULL && cur != r) {
        prv = cur;
        cur = cur->next;
    }

    // Reached end of list and did not find region.
    if (cur == NULL) {
        return;
    }

    // Relink the list.
    if (prv == NULL) {
        as->regions = cur->next; // empty list case
    } else {
        prv->next = cur->next;
    }

    // Remove region.
    kfree(cur);
    cur = NULL;
}

/**
 * Free all regions in the address space.
 */
void free_regions(struct addrspace *as) {
    struct region *cur;
    struct region *tmp;

    cur = as->regions; // start of linked list
    while (cur != NULL) {
        tmp = cur;
        cur = cur->next;
        kfree(tmp);
        tmp = NULL;
    }
}

/**
 * search_region() returns a region from the address space if there exists a
 * region that is a superset of the given vaddr and memsize.
 * 
 * Returns NULL if region is not found. This also indicates that a region
 * allocation will be valid.
 */
struct region *search_region(struct addrspace *as,
                                    vaddr_t vaddr,
                                    size_t memsize) {
    struct region *cur;

    cur = as->regions; // start of linked list
    while (cur != NULL) {

        // Check if cur's vaddr and memsize is a superset of the given vaddr
        // and memsize.
        if (vaddr >= cur->vaddr &&
            ((vaddr + memsize) <= (cur->vaddr + cur->memsize))) {
            return cur;
        }
        cur = cur->next;
    }

    return NULL; // could not find region
}

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

struct addrspace *as_create(void) {
    struct addrspace *as;
    int i;

    // Memory allocate the address space.
    as = NULL;
    as = kmalloc(sizeof(struct addrspace));
    if (as == NULL) {
        goto cleanupA;
    }

    // Memory allocate the page table for the address space.
    as->pgtable = NULL;
    as->pgtable = (paddr_t ***)kmalloc(PG_SIZE_0 * sizeof(paddr_t));
    if (as->pgtable == NULL) {
        goto cleanupB;
    }

    // Initialise first level of page table.
    for (i = 0; i < PG_SIZE_0; i++) {
        as->pgtable[i] = NULL; // Zero-fill the first page.
    }

    // Memory allocation will come as needed.
    as->regions = NULL;

    return as;

cleanupB:
    kfree(as);
    as = NULL;

cleanupA:
    return NULL;
}

int as_copy(struct addrspace *old_as, struct addrspace **ret) {
    struct addrspace *new_as;
    struct region *r_cur;
    struct region *r_prv;
    paddr_t paddr;
    int perm;
    int i;
    int j;
    int k;
    int result;

    new_as = as_create();
    if (new_as == NULL) {
        result = ENOMEM;
        goto cleanupA;
    }

    // Copy regions.
    r_prv = old_as->regions; // Old head of regions linked list.
    while (r_prv != NULL) {

        // Make copy of old region.
        r_cur = copy_region(r_prv);
        if (r_cur == NULL) {
            result = ENOMEM;
            goto cleanupB;
        }

        // Add copied region to new address space.
        add_region(new_as, r_cur); // Use this function because we're lazy and want to be safe.

        // Next region to copy.
        r_prv = r_prv->next;
    }

    // Copy page table
    for (i = 0; i < PG_SIZE_0; i++) {
        if (old_as->pgtable[i] != NULL) {
            for (j = 0; j < PG_SIZE_1; j++) {
                if (old_as->pgtable[i][j] != NULL) {
                    for (k = 0; k < PG_SIZE_2; k++) {
                        if (old_as->pgtable[i][j][k] != 0) {

                            // Get paddr from old page table.
                            paddr = old_as->pgtable[i][j][k];

                            // Get region permissions for paddr.
                            perm = GET_WRITE_BIT(paddr) | GET_READ_BIT(paddr);

                            // Malloc required page table entry.
                            result = vm_allocpte1(new_as, paddr);
                            if (result != 0) {
                                goto cleanupB;
                            }

                            result = vm_allocpte2(new_as, paddr);
                            if (result != 0) {
                                goto cleanupC;
                            }

                            result = vm_allocpte3(new_as, paddr, perm);
                            if (result != 0) {
                                goto cleanupD;
                            }
                        }
                    }
                }
            }
        }
    }

    *ret = new_as; // Return the pointer to the copied address space.
    result = 0;
    goto cleanupA;

cleanupD:
    kfree(new_as->pgtable[i][j]);
    old_as->pgtable[i][j] = NULL;

cleanupC:
    kfree(new_as->pgtable[i]);
    old_as->pgtable[i] = NULL;

cleanupB:
    as_destroy(new_as);

cleanupA:
    return result;
}

void as_destroy(struct addrspace *as) {
    int i;
    int j;
    int k;

    // Free regions
    free_regions(as);

    // Free page table
    for (i = 0; i < PG_SIZE_0; i++) {
        if (as->pgtable[i] != NULL) {
            for (j = 0; j < PG_SIZE_1; j++) {
                if (as->pgtable[i][j] != NULL) {
                    for (k = 0; k < PG_SIZE_2; k++) {
                        if (as->pgtable[i][j][k] != 0) {
                            free_kpages(PADDR_TO_KVADDR(as->pgtable[i][j][k] & PAGE_FRAME));
                        }
                    }
                }
                kfree(as->pgtable[i][j]);
                as->pgtable[i][j] = NULL;
            }
            kfree(as->pgtable[i]);
            as->pgtable[i] = NULL;
        }
    }
    kfree(as->pgtable);
    as->pgtable = NULL;

    // Free address space
    kfree(as);
    as = NULL;
}

void as_activate(void) {
    struct addrspace *as;

    as = proc_getas();
    if (as == NULL) {
        /*
        * Kernel thread without an address space; leave the
        * prior address space in place.
        */
        return;
    }

    vm_tlbflush();
}

void as_deactivate(void) {
    vm_tlbflush();
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int as_define_region(struct addrspace *as,
                     vaddr_t vaddr,
                     size_t memsize,
                     int readable,
                     int writeable,
                     int executable) {

    struct region *r;
    int cur_perm;

    // Check if address space is valid.
    if (as == NULL) {
        return EFAULT;
    }

    // Check if given vaddr and memsize is invalid.
    if (search_region(as, vaddr, memsize) != NULL) {
        return ENOMEM;
    }

    // Align region
    memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
    vaddr &= PAGE_FRAME;
    memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

    // Initialise the region.
    cur_perm = readable | writeable | executable;
    r = init_region(vaddr, memsize, cur_perm, cur_perm);
    if (r == NULL) {
        return ENOMEM;
    }

    // Add region to the linked list.
    add_region(as, r);

    return 0;
}

int as_prepare_load(struct addrspace *as) {
    struct region *r;
    int perm;

    r = as->regions;
    if (r == NULL) {
        return EFAULT;
    }
    
    perm = R_RD | R_WR | R_EX;

    while (r != NULL) {
        r->old_perm = r->cur_perm;
        r->cur_perm = perm;
        r = r->next;
    }

    return 0;
}

int as_complete_load(struct addrspace *as) {
    struct region *r;
    int spl;

    r = as->regions;
    if (r == NULL) {
        return EFAULT;
    }

    while (r != NULL) {
        r->cur_perm = r->old_perm;
        r = r->next;
    }

    spl = splhigh();
    vm_tlbflush();
    splx(spl);

    return 0;
}

int as_define_stack(struct addrspace *as, vaddr_t *stackptr) {
    int result;
    
    // A stack is a region.
    result = as_define_region(as, USERSTACK - USERSTACKSIZE, USERSTACKSIZE,
        R_RD, R_WR, 0);
    if (result != 0) {
        return result;
    }

    // Initial user-level stack pointer.
    *stackptr = USERSTACK;

    return 0;
}

