/*
 * Header file for disaggregated architecture simulation.
 */
#ifndef __DSAG_MEM_SIMULATE_H
#define __DSAG_MEM_SIMULATE_H

#ifdef CONFIG_KVM_DSAG_MEM_SIMULATION

#define DSAG_MEM_HASH_BITS 20

void dsag_sim_init(struct kvm *kvm, int local_mem_size, int network_delay);

struct dsag_local_mem_t {
    DECLARE_HASHTABLE(local_mem_list, DSAG_MEM_HASH_BITS);                        
    struct task_struct* swapper_thread;
    uint32_t num_total_pages;
    uint32_t num_free_pages;
    spinlock_t free_page_lock;
};

#endif  // CONFIG_KVM_DSAG_MEM_SIMULATION
#endif  // __DSAG_MEM_SIMULATE_H
