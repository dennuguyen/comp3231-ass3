/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
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

#ifndef _VM_H_
#define _VM_H_

#include <machine/vm.h>
#include <machine/tlb.h>
#include <addrspace.h>

struct addrspace;

// Page number masks
#define PG_IDX0(pg) (pg >> 24)       // mask to get first level from page number
#define PG_IDX1(pg) (pg << 8 >> 26)  // mask to get second level from page number
#define PG_IDX2(pg) (pg << 14 >> 26) // mask to get third level from page number
#define PG_SIZE_0   256              // number of pages in first level
#define PG_SIZE_1   64               // number of pages in second level
#define PG_SIZE_2   64               // number of pages in third level

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/

// Region permissions (RWX)
#define R_RD 0b100
#define R_WR 0b010
#define R_EX 0b001

// Region permissions to page permissions
#define GET_DIRTY_BIT(perm) (((perm & R_WR) == R_WR) * TLBLO_DIRTY)
#define GET_VALID_BIT(perm) ((((perm & R_RD) == R_RD) | ((perm & R_EX) == R_EX)) * TLBLO_VALID)

// Page permissions to region permissions
#define GET_WRITE_BIT(paddr) (((paddr & TLBLO_DIRTY) == TLBLO_DIRTY) * R_WR)
#define GET_READ_BIT(paddr)  (((paddr & TLBLO_VALID) == TLBLO_VALID) * R_RD) // This is also used for the R_EX bit

// Allocate/free kernel heap pages (called by kmalloc/kfree)
vaddr_t alloc_kpages(unsigned npages);
void free_kpages(vaddr_t addr);

/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *);

// TLB shluld be flushed to protect process memory after a context switch.
void vm_tlbflush(void);

/* Page table functions */
int vm_allocpte1(struct addrspace *as, paddr_t paddr);
int vm_allocpte2(struct addrspace *as, paddr_t paddr);
int vm_allocpte3(struct addrspace *as, paddr_t paddr, int perm);


#endif /* _VM_H_ */
