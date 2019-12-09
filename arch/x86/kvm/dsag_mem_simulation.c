#ifdef CONFIG_KVM_DSAG_MEM_SIMULATION

#include <asm/vmx.h>
#include <linux/hashtable.h>
#include <linux/kvm_host.h>
#include <linux/random.h>
#include <linux/slab.h>

#include "dsag_mem_simulation.h"
#include "mmu.h"

/*
 * External APIs
 */
void dsag_sim_init(struct kvm *kvm)
{
    hash_init(kvm->dsag_mem_hash);
    kvm->dsag_mem_node_num = 0;
    return;
}

int dsag_mem_simulation(struct kvm *kvm, gfn_t gfn, u64 *sptep)
{
    struct dsag_mem_node *node = find_dsag_node(kvm, sptep);
    if (!sptep) {
        printk(KERN_ERR "[DSAG] Error: sptep null in %s\n", __func__);
        return RET_PF_RETRY;
    }

    if (!node) {
        printk(KERN_DEBUG "[DSAG] node not exist\n");

        if (record_dsag_mem(kvm, gfn, LOCAL_MEM)) {
            return RET_PF_RETRY;
        }

        // Swap out a local page if local region is full.
        if (kvm->dsag_mem_node_num > DSAG_LOCAL_MEMORY_PAGE_NUM)
            dsag_swap_out_local_page(kvm);
    } else {
        printk(KERN_DEBUG "[DSAG] node exist, mem_type=%d\n", node->mem_type);

        // Page in remote region?
        //  - Yes: swap in.
        //  - No : Possibility - page is swapped to disk? [WARNING]
        if (node->mem_type == LOCAL_MEM) {
            printk(KERN_WARNING "[DSAG] Error: fault should not happen for a local page access\n");
            // TODO: handle a local page is swapped to disk.
        } else {
            dsag_swap_in_remote_page(kvm, node);
        }
    }
    return 0;
}

/*
 * Node operations
 */
struct dsag_mem_node* find_dsag_node(struct kvm *kvm, u64 *sptep)
{
    struct dsag_mem_node *node;
    hash_for_each_possible(kvm->dsag_mem_hash, node, hnode, sptep) {
        if (node->sptep == sptep)
            return node;
    }
    return NULL;
}

int record_dsag_mem(struct kvm *kvm, u64 *sptep, enum dsag_mem_type mem_type) {
    struct dsag_mem_node *node = kmalloc(sizeof(struct dsag_mem_node),
                                         GFP_KERNEL);
    if (!node) return -1;

    node->sptep = sptep;
    node->mem_type = mem_type;

    hash_add(kvm->dsag_mem_hash, &node->hnode, sptep);
    ++kvm->dsag_mem_node_num;
    printk(KERN_DEBUG "[DSAG] %s:sptep=0x%llx, dsag_mem_node_num=%d\n", __func__, sptep, kvm->dsag_mem_node_num);
    return 0;
}

/*
 * Page operations
 */
void dsag_swap_out_local_page(struct kvm *kvm)
{
    int bkt;
    gfn_t victim_key;
    struct dsag_mem_node *node, *victim_node;

    // Find a victim.
    // TODO: Introduce a proper mechanism. Current design simply finds a random
    //       node to be a victim.
    get_random_bytes(&victim_key, sizeof(victim_key));
    hash_for_each_from(kvm->dsag_mem_hash, bkt, victim_key, node, hnode) {
        victim_node = node;
    }

    if (!node) {
        // Circle back
        hash_for_each(kvm->dsag_mem_hash, bkt, node, hnode) {
            victim_node = node;
        }
    }

    if (!node || !node->sptep) {
        printk(KERN_ERR "[DSAG] Error: no pte found to be swapped out\n");
        return;
    }

    // Update vimcit's PTE to not-present and memory type.
    printk(KERN_DEBUG "[DSAG] swap out, ori pte=0x%llx\n", *node->sptep);
    *node->sptep &= ~VMX_EPT_RWX_MASK;
    node->mem_type = REMOTE_MEM;
    printk(KERN_DEBUG "[DSAG] swap out, after pte=0x%llx\n", *node->sptep);

    // TODO: Add network delay.
    return;
}

void dsag_swap_in_remote_page(struct kvm *kvm, struct dsag_mem_node *node)
{
    if (!node || node->mem_type == LOCAL_MEM) {
        printk(KERN_ERR "[DSAG] Error: the node to be swapped in should be in remote region\n");
        return;
    }

    if (kvm->dsag_mem_node_num <= DSAG_LOCAL_MEMORY_PAGE_NUM) {
        printk(KERN_ERR "[DSAG] Error: swap in process should be called only if local region is full but dsag_mem_node_num=%d\n", kvm->dsag_mem_node_num);
    } else {
        // Swap out a local page first.
        dsag_swap_out_local_page(kvm);
    }

    // The __direct_map has already handled the PTE settings.
/*
    // TODO: should cache original access permission.
    // TODO: this should be already updated by original handler?
    printk(KERN_DEBUG "[DSAG] swap in, ori pte=0x%llx\n", *node->sptep);
    *node->sptep |= VMX_EPT_RWX_MASK;
    printk(KERN_DEBUG "[DSAG] swap in, after pte=0x%llx\n", *node->sptep);
*/

    // TODO: Add network delay.
    return;
}


#endif  // CONFIG_KVM_DSAG_MEM_SIMULATION
