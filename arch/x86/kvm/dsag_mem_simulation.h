/*
 * Header file for disaggregated architecture simulation.
 */
#ifndef __KVM_X86_DSAG_MEM_SIMULATE_H
#define __KVM_X86_DSAG_MEM_SIMULATE_H

#ifdef CONFIG_KVM_DSAG_MEM_SIMULATION

#include <linux/types.h>
#include <linux/kvm_types.h>

enum dsag_mem_type {
    LOCAL_MEM,
    REMOTE_MEM,
};

struct dsag_mem_node {
    u64 *sptep;
    enum dsag_mem_type mem_type;
    struct hlist_node hnode;
};

/*
 * External APIs
 */
void dsag_sim_init(struct kvm *kvm);

// Simulate the disaggregated memory architecture.
// Return 0 indicates success; otherwise a failure.
int dsag_mem_simulation(struct kvm *kvm, gfn_t gfn, u64 *sptep);

/*
 * Node operations
 */
struct dsag_mem_node* find_dsag_node(struct kvm *kvm, u64 *sptep);
int record_dsag_mem(struct kvm *kvm, u64 *sptep, enum dsag_mem_type mem_type);
void update_dsag_mem(struct kvm *kvm, gfn_t gfn, enum dsag_mem_type mem_type);

/*
 * Page operations
 */
void dsag_swap_out_local_page(struct kvm *kvm);
void dsag_swap_in_remote_page(struct kvm *kvm, struct dsag_mem_node *node);

#endif  // CONFIG_KVM_DSAG_MEM_SIMULATION
#endif  // __KVM_X86_DSAG_MEM_SIMULATE_H
