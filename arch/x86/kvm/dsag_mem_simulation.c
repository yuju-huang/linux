#ifdef CONFIG_KVM_DSAG_MEM_SIMULATION

#include <linux/hashtable.h>
#include <linux/kvm_host.h>
#include <linux/slab.h>

#include "dsag_mem_simulation.h"
#include "mmu.h"

/*
 * External APIs
 */
void dsag_sim_init(struct kvm *kvm)
{
    hash_init(kvm->dsag_mem_hash);
    kvm->dasg_mem_node_num = 0;
    return;
}

int dsag_mem_simulation(struct kvm *kvm, gfn_t gfn, u64 *sptep)
{
    const bool is_in_dsag_mem = in_dsag_mem(kvm, gfn);

    if (!is_in_dsag_mem) {
        if (record_dsag_mem(kvm, gfn, LOCAL_MEM)) {
            return RET_PF_RETRY;
        }

        // Swap out a local page.
        // dsag_swap_out_local_page(kvm);
    } else {
        // Page in remote region?
        //  - Yes: swap in.
        //  - No : Possibility - page is swapped to disk? [WARNING]
    }
    return 0;
}

/*
 * Node operations
 */
bool in_dsag_mem(struct kvm *kvm, gpa_t gpa)
{
    struct dsag_mem_node *node;
    hash_for_each_possible(kvm->dsag_mem_hash, node, hnode, gpa) {
        printk(KERN_INFO "in_dsag_mem: true\n");
        return true;
    }
    printk(KERN_INFO "in_dsag_mem: false\n");
    return false;
}

int record_dsag_mem(struct kvm *kvm, gpa_t gpa, enum dsag_mem_type mem_type) {
    struct dsag_mem_node *node = kmalloc(sizeof(struct dsag_mem_node),
                                         GFP_KERNEL);
    if (!node) return -1;

    node->gpa = gpa;
    node->mem_type = mem_type;

    hash_add(kvm->dsag_mem_hash, &node->hnode, gpa);
    ++kvm->dasg_mem_node_num;
    printk(KERN_INFO "dasg_mem_node_num=%d\n", kvm->dasg_mem_node_num);
    return 0;
}

/*
 * Page operations
 */
int dsag_swap_out_local_page(struct kvm *kvm);


#endif  // CONFIG_KVM_DSAG_MEM_SIMULATION
