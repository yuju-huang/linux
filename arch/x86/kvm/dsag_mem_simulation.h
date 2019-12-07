/*
 * Header file for disaggregated architecture simulation.
 */
#ifndef __KVM_X86_DSAG_MEM_SIMULATE_H
#define __KVM_X86_DSAG_MEM_SIMULATE_H

#ifdef CONFIG_KVM_DSAG_MEM_SIMULATION

#include <linux/types.h>
#include <linux/kvm_types.h>

#define DSAG_LOCAL_MEMORY_SIZE 1UL << 32
#define DSAG_LOCAL_MEMORY_PAGE_NUM  (DSAG_LOCAL_MEMORY_SIZE / PAGE_SIZE)
#define DSAG_MEM_HASH_BITS 20

struct dsag_mem_node {
    pte_t pte;
    bool is_remote;
    struct hlist_node hnode;
};

enum dsag_mem_type {
    LOCAL_MEM,
    REMOTE_MEM,
};

bool in_dsag_mem(gpa_t gpa);
int record_dsag_mem(gpa_t gpa);
void update_dsag_mem(gpa_t gpa, enum dsag_mem_type mem_type);

#endif  // CONFIG_KVM_DSAG_MEM_SIMULATION
#endif  // __KVM_X86_DSAG_MEM_SIMULATE_H
