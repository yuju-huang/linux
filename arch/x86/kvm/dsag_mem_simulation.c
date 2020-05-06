#ifdef CONFIG_KVM_DSAG_MEM_SIMULATION

#include <asm/vmx.h>
#include <linux/delay.h>
#include <linux/hashtable.h>
#include <linux/kvm_host.h>
#include <linux/random.h>
#include <linux/slab.h>

#include "dsag_mem_simulation.h"
#include "mmu.h"

#define MB (1ULL << 20)

#define RET_SUCC 0
#define RET_FAIL -1

#define LOW_MEM_THRESHOLD 10
#define FREE_POOL_SIZE 100

/*
 * External APIs
 */
void dsag_sim_init(struct kvm *kvm, int local_mem_size, int network_delay)
{
    struct dsag_local_mem_t* dsag_mem = &kvm->dsag_local_mem;
    const unsigned long size_in_byte = local_mem_size * MB;

    hash_init(dsag_mem->local_mem_list);
    dsag_mem->num_total_pages = size_in_byte / PAGE_SIZE;
    dsag_mem->num_free_pages = dsag_mem->num_total_pages;
    spin_lock_init(&dsag_mem->free_page_lock);

    // TODO: register this kvm to swapper thread.
    dsag_printk(KERN_DEBUG, "%s, local_mem_size=%d, num_total_pages=%d\n", __func__, local_mem_size, dsag_mem->num_total_pages);
    return;
}

static void check_free_page(struct kvm* kvm) {
    uint32_t num_free_pages;
    unsigned long flags;
    struct dsag_local_mem_t* dsag_mem = &kvm->dsag_local_mem;

    while (1) {
        spin_lock_irqsave(&dsag_mem->free_page_lock, flags);
        num_free_pages = dsag_mem->num_free_pages;
        spin_unlock_irqrestore(&dsag_mem->free_page_lock, flags);
        if (num_free_pages == 0) {
            dsag_printk(KERN_DEBUG, "%s, do blocking swap out\n", __func__);
            dsag_swap_out_local_page(kvm, LOW_MEM_THRESHOLD);
        } else {
            break;
        }
    }
    return;
}

int dsag_mem_simulation(struct kvm *kvm, kvm_pfn_t pfn, gfn_t gfn, u64 *sptep, int level)
{
    unsigned long flags;
    struct dsag_mem_node *node;
    struct dsag_local_mem_t* dsag_mem = &kvm->dsag_local_mem;

    if (!sptep) {
        dsag_printk(KERN_ERR, "Error: sptep null in %s\n", __func__);
        return RET_PF_RETRY;
    }

    dsag_printk(KERN_DEBUG, "%s: gfn=0x%llx, sptep=0x%lx\n", __func__, gfn, (uintptr_t)sptep);

    check_free_page(kvm);

    node = find_dsag_node(kvm, sptep);
    if (!node) {
        // If the node does not exist, it must be newly allocated, record it.
        dsag_printk(KERN_DEBUG, "node not exist\n");

        if (record_dsag_node(kvm, pfn, sptep, gfn, level, LOCAL_MEM)!= RET_SUCC) {
            return RET_PF_RETRY;
        }

        spin_lock_irqsave(&dsag_mem->free_page_lock, flags);
        --dsag_mem->num_free_pages;
        BUG_ON(dsag_mem->num_free_pages < 0);
        spin_unlock_irqrestore(&dsag_mem->free_page_lock, flags);
    } else {
        dsag_printk(KERN_DEBUG, "node exist, sptep=0x%lx, mem_type=%d\n", (uintptr_t)node->sptep, node->mem_type);

        // Page in remote region?
        //  - Yes: it should be swapped in the EPT page fault handler.
        //  - No : ignore.
        if (node->mem_type == REMOTE_MEM) {
            node->mem_type = LOCAL_MEM;
        }
    }

    dsag_printk(KERN_DEBUG, "%s end: dsag_local_mem_node_num=%d\n", __func__, dsag_mem->num_free_pages);
    return 0;
}

/*
 * Node operations
 */
struct dsag_mem_node* find_dsag_node(struct kvm *kvm, u64 *sptep)
{
    struct dsag_mem_node *node;
    hash_for_each_possible(kvm->dsag_local_mem.local_mem_list, node, hnode,
                           (uintptr_t)sptep) {
        dsag_printk(KERN_DEBUG, "%s: sptep=0x%lx, node->sptep=0x%lx\n", __func__, (uintptr_t)sptep, (uintptr_t)node->sptep);
        if (node->sptep == sptep)
            return node;
    }
    return NULL;
}

int record_dsag_node(struct kvm *kvm, kvm_pfn_t pfn, u64 *sptep, gfn_t gfn, int level, enum dsag_mem_type mem_type) {
    struct dsag_local_mem_t* dsag_mem = &kvm->dsag_local_mem;
    struct dsag_mem_node *node = kmalloc(sizeof(struct dsag_mem_node),
                                         GFP_KERNEL);
    if (!node) return RET_FAIL;

    node->sptep = sptep;
    node->pfn = pfn;
    node->gfn = gfn;
    node->level = level;
    node->mem_type = mem_type;

    hash_add(dsag_mem->local_mem_list, &node->hnode, (uintptr_t)sptep);
    dsag_printk(KERN_DEBUG, "%s: sptep=0x%lx, hash=%d, dsag_local_mem_node_num=%d\n", __func__, (uintptr_t)sptep, hash_min((uintptr_t)sptep, HASH_BITS(dsag_mem->local_mem_list)), dsag_mem->num_free_pages);
    return 0;
}

int delete_dsag_node(struct kvm *kvm, u64 *sptep) {
    unsigned long flags;
    struct dsag_mem_node *node;
    struct dsag_local_mem_t* dsag_mem = &kvm->dsag_local_mem;

    if (!sptep) {
        dsag_printk(KERN_ERR, "Error: sptep null in %s\n", __func__);
        return RET_FAIL;
    }

    node = find_dsag_node(kvm, sptep);
    if (!node) {
        dsag_printk(KERN_ERR, "Error in %s: delete a non-existent node 0x%lx\n", __func__, (uintptr_t)sptep);
        return RET_FAIL;
    }

    hash_del(&node->hnode);
    if (node->mem_type == LOCAL_MEM) {
        spin_lock_irqsave(&dsag_mem->free_page_lock, flags);
        ++dsag_mem->num_free_pages;
        spin_unlock_irqrestore(&dsag_mem->free_page_lock, flags);
    }
    kfree(node);
    dsag_printk(KERN_DEBUG, "%s:delete sptep=0x%lx, num_free_pages=%d\n", __func__, (uintptr_t)sptep, dsag_mem->num_free_pages);
    return RET_SUCC;
}

/*
 * Page operations
 */
static inline bool valid_to_swap_out(struct dsag_mem_node *node)
{
    // Only swap the page whose level is 1.
    return (node && node->sptep &&
            (node->mem_type == LOCAL_MEM) && (node->pfn != KVM_PFN_NOSLOT) &&
            (node->level == 1));
}

// Return a node to be swapped out from the dsag_mem's mem_list.
struct dsag_mem_node* find_victim(const struct dsag_local_mem_t* dsag_mem)
{
    int bkt;
    u64 victim_key;
    struct dsag_mem_node *node, *victim_node = NULL;

    // TODO: Introduce a proper mechanism. Current design simply finds a random
    //       node as a victim.

    get_random_bytes(&victim_key, sizeof(victim_key));
    dsag_printk(KERN_DEBUG, "%s: victim_key=0x%llx\n", __func__, victim_key);
    hash_for_each_from(dsag_mem->local_mem_list, bkt, victim_key, node, hnode) {
        dsag_printk(KERN_DEBUG, "bkt=%d, HASH_SIZE(name)=%ld, node->sptep=0x%lx\n", bkt, HASH_SIZE(dsag_mem->local_mem_list), (uintptr_t)node->sptep);
        if (valid_to_swap_out(node)) {
            victim_node = node;
            break;
        }
    }

    if (!victim_node) {
        // Circle back.
        hash_for_each(dsag_mem->local_mem_list, bkt, node, hnode) {
        if (valid_to_swap_out(node)) {
                victim_node = node;
                break;
            }
        }
    }
    return victim_node;
}

// Return the number of pages that are swapped out.
int dsag_swap_out_local_page(struct kvm *kvm, size_t num_swap_out)
{
    size_t i;
    unsigned long flags;
    struct dsag_local_mem_t* dsag_mem = &kvm->dsag_local_mem;
    struct dsag_mem_node *victim, **victims;
    victims = kmalloc(num_swap_out * sizeof(struct dsag_mem_node*), GFP_KERNEL);

    // Find victims.
    for (i = 0; i < num_swap_out; ++i) {
        victim = find_victim(dsag_mem);
        if (victim && victim->sptep) {
            victim->mem_type = REMOTE_MEM;
            victims[i] = victim;
        } else {
            break;
        }
    }

    if (i == 0) {
        dsag_printk(KERN_ERR, "Error: no pte found to be swapped out\n");
        return RET_FAIL;
    }

    // TODO: Call swap function.
    
    // Update num_free_pages.
    spin_lock_irqsave(&dsag_mem->free_page_lock, flags);
    dsag_mem->num_free_pages += i;
    spin_unlock_irqrestore(&dsag_mem->free_page_lock, flags);

    return i;
}

#endif  // CONFIG_KVM_DSAG_MEM_SIMULATION
