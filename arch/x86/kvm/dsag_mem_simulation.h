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
    pte_t pte;
    gpa_t gpa;
    enum dsag_mem_type mem_type;
    struct hlist_node hnode;
};

/*
 * Global initilialization.
 */
void dsag_sim_init(struct kvm *kvm);

/*
 * Per request operations.
 */
bool in_dsag_mem(struct kvm *kvm, gpa_t gpa);
int record_dsag_mem(struct kvm *kvm, gpa_t gpa, enum dsag_mem_type mem_type);
void update_dsag_mem(struct kvm *kvm, gpa_t gpa, enum dsag_mem_type mem_type);

#endif  // CONFIG_KVM_DSAG_MEM_SIMULATION
#endif  // __KVM_X86_DSAG_MEM_SIMULATE_H
