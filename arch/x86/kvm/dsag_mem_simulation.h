/*
 * Header file for disaggregated architecture simulation.
 */
#ifndef __KVM_X86_DSAG_MEM_SIMULATE_H
#define __KVM_X86_DSAG_MEM_SIMULATE_H

#ifdef CONFIG_KVM_DSAG_MEM_SIMULATION

#include <linux/types.h>
#include <linux/kvm_types.h>

#define DEBUG 1
// #define HAL 0
#if (DEBUG && HALT)
#define dsag_printk(type, x...) do { \
    pr_debug("[DSAG] " x); \
    if ((type) == KERN_ERR) BUG_ON(true); \
} while (0)
#elif DEBUG
#define dsag_printk(type, x...) do { \
    pr_debug("%s-", #type); \
    pr_debug("[DSAG] " x); \
} while (0)
#else
#define dsag_printk(type, x...)
#endif

enum dsag_mem_type {
    LOCAL_MEM,
    REMOTE_MEM,
};

struct dsag_mem_node {
    u64 *sptep;
    kvm_pfn_t pfn;
    gfn_t gfn;
    int level;
    enum dsag_mem_type mem_type;
    struct hlist_node hnode;
};

// Simulate the disaggregated memory architecture.
// Return 0 indicates success; otherwise a failure.
int dsag_mem_simulation(struct kvm *kvm, kvm_pfn_t pfn, gfn_t gfn, u64 *sptep, int level);

/*
 * Node operations
 */
struct dsag_mem_node* find_dsag_node(struct kvm *kvm, u64 *sptep);
int record_dsag_node(struct kvm *kvm, kvm_pfn_t pfn, u64 *sptep, gfn_t gfn, int level, enum dsag_mem_type mem_type);
int delete_dsag_node(struct kvm *kvm, u64 *sptep);

/*
 * Page operations
 */
int dsag_swap_out_local_page(struct kvm *kvm);
int dsag_swap_in_remote_page(struct kvm *kvm, struct dsag_mem_node *node);

#endif  // CONFIG_KVM_DSAG_MEM_SIMULATION
#endif  // __KVM_X86_DSAG_MEM_SIMULATE_H
