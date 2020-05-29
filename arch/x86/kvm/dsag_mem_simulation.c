#ifdef CONFIG_KVM_DSAG_MEM_SIMULATION

#include <asm/vmx.h>
#include <linux/delay.h>
#include <linux/hashtable.h>
#include <linux/kthread.h>
#include <linux/kvm_host.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/swap.h>

#include "dsag_mem_simulation.h"
#include "mmu.h"

#define MB (1ULL << 20)

#define RET_SUCC 0
#define RET_FAIL -1

#define LOW_MEM_THRESHOLD 10
#define FREE_POOL_WATERMARK 10000

extern int isolate_lru_page(struct page *page);

static inline bool valid_to_swap_out(struct dsag_mem_node *node)
{
    // Only swap the page whose level is 1.
    return (node && node->sptep &&
            (node->mem_type == LOCAL_MEM) && (node->pfn != KVM_PFN_NOSLOT) &&
            (node->level == 1));
}

// Return a node to be swapped out from the dsag_mem's mem_list.
static struct dsag_mem_node* find_victim(const struct dsag_local_mem_t* dsag_mem)
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

static int dsag_swap_out_local_page(void *data)
{
    struct kvm* kvm = (struct kvm*)data;
    struct dsag_mem_node *victim;
    unsigned long flags;
    struct page* page;
    struct list_head* pos;
    struct dsag_local_mem_t* dsag_mem = &kvm->dsag_local_mem;
    unsigned long nr_reclaimed = 0;
    unsigned long num_reclaim = 0;
    uint32_t num_free_pages = 0;
    uint32_t num_swap_out = 0;
    uint32_t num_victim = 0;
    size_t i;

    while (1) {
        spin_lock_irqsave(&dsag_mem->free_page_lock, flags);
        num_free_pages = dsag_mem->num_free_pages;
        spin_unlock_irqrestore(&dsag_mem->free_page_lock, flags);

        if (num_free_pages >= FREE_POOL_WATERMARK) {
            // sleep
            set_current_state(TASK_INTERRUPTIBLE);
            schedule();
            set_current_state(TASK_RUNNING);
            continue;
        } else {
            spin_lock_irqsave(&dsag_mem->reclaim_list_lock, flags);
            num_reclaim = dsag_mem->num_reclaim;
            spin_unlock_irqrestore(&dsag_mem->reclaim_list_lock, flags);

            num_swap_out = FREE_POOL_WATERMARK - num_free_pages;
            num_victim = (num_reclaim > num_swap_out) ? 0 : (num_swap_out - num_reclaim);
            BUG_ON(num_swap_out < 0);
            printk("%s1: num_swap_out=%d, num_reclaim=%d, num_victim=%d, num_free_pages=%d\n", __func__, num_swap_out, num_reclaim, num_victim, num_free_pages);
            dsag_printk(KERN_DEBUG, "%s: num_swap_out=%d, num_free_pages=%d\n", __func__, num_swap_out, num_free_pages);

            // Find victims.
            LIST_HEAD(page_list);
            for (i = 0; i < num_victim; ++i) {
                victim = find_victim(dsag_mem);
                if (victim && victim->sptep) {
                    page = pfn_to_page(victim->pfn);
                    BUG_ON(!page);

                    // If the page is active, clear it since the reclaim_pages only accept inactive pages.
                    ClearPageActive(page);
                    ClearPageReferenced(page);
                    // TODO: dig more into shrink_active_list for calling get_page.
                    get_page(page);
                    ClearPageLRU(page);

                    BUG_ON(&page->lru == LIST_POISON1); 
                    BUG_ON(&page->lru == LIST_POISON2); 
                    BUG_ON(&page->lru == NULL);
                    BUG_ON(&(page->lru.prev) == LIST_POISON1); 
                    BUG_ON(&(page->lru.prev) == LIST_POISON2); 
                    BUG_ON(&(page->lru.prev) == NULL);
                    BUG_ON(&(page->lru.next) == LIST_POISON1); 
                    BUG_ON(&(page->lru.next) == LIST_POISON2); 
                    BUG_ON(&(page->lru.next) == NULL);
                    if (list_empty(&page->lru))
                        list_add(&page->lru, &page_list);
                    else
                       list_move(&page->lru, &page_list);

                    // Ready to swap out.
                    victim->mem_type = REMOTE_MEM;
                } else {
                    break;
                }
            }

            if (i == 0) {
                dsag_printk(KERN_ERR, "Error: no pte found to be swapped out\n");
                continue;
            }

            // TODO: Need lock if there is only one swapper thread?
            spin_lock_irqsave(&dsag_mem->reclaim_list_lock, flags);
            list_for_each(pos, &dsag_mem->reclaim_list) {
                page = list_entry(pos, struct page, lru);
                ClearPageActive(page);
                ClearPageReferenced(page);
                ClearPageLRU(page); 
            }
            list_splice_tail(&page_list, &dsag_mem->reclaim_list);
            dsag_mem->num_reclaim += i;
            num_reclaim = dsag_mem->num_reclaim;
            spin_unlock_irqrestore(&dsag_mem->reclaim_list_lock, flags);
            printk("%s2: num_swap_out=%d, num_reclaim=%d, num_free_pages=%d\n", __func__, num_swap_out, num_reclaim, num_free_pages);

            // Update num_reclaim.
            nr_reclaimed = reclaim_pages(&dsag_mem->reclaim_list, num_reclaim);
            printk("%s: done reclaim_pages, nr_reclaimed=%ld, num_reclaim=%ld\n", __func__, nr_reclaimed, dsag_mem->num_reclaim);
            spin_lock_irqsave(&dsag_mem->reclaim_list_lock, flags);
            dsag_mem->num_reclaim -= nr_reclaimed;
            BUG_ON(dsag_mem->num_reclaim < 0);
            spin_unlock_irqrestore(&dsag_mem->reclaim_list_lock, flags);

            // Update num_free_pages.
            spin_lock_irqsave(&dsag_mem->free_page_lock, flags);
            dsag_mem->num_free_pages += nr_reclaimed;
            spin_unlock_irqrestore(&dsag_mem->free_page_lock, flags);
        }
    }
    return 0;
}

/*
 * External APIs
 */
void dsag_sim_init(struct kvm *kvm, int local_mem_size, int network_delay)
{
    struct dsag_local_mem_t* dsag_mem = &kvm->dsag_local_mem;
    const unsigned long size_in_byte = local_mem_size * MB;

    // TODO: Only initialize once. Here is simply a workaround.
    if (dsag_mem->swapper_thread) return;

    hash_init(dsag_mem->local_mem_list);
    dsag_mem->num_total_pages = size_in_byte / PAGE_SIZE;
    dsag_mem->num_free_pages = dsag_mem->num_total_pages;
    spin_lock_init(&dsag_mem->free_page_lock);

    // TODO: how to destroy thread.
    dsag_mem->swapper_thread = kthread_create(dsag_swap_out_local_page, kvm, "dsag_local_mem_swapper");
    INIT_LIST_HEAD(&dsag_mem->reclaim_list);
    dsag_mem->num_reclaim = 0;
    spin_lock_init(&dsag_mem->reclaim_list_lock);

    // TODO: register this kvm to swapper thread.
    dsag_printk(KERN_DEBUG, "%s, local_mem_size=%d, num_total_pages=%d\n", __func__, local_mem_size, dsag_mem->num_total_pages);
    return;
}

#if 0
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
#endif

int dsag_mem_simulation(struct kvm *kvm, kvm_pfn_t pfn, gfn_t gfn, u64 *sptep, int level)
{
    unsigned long flags;
    uint32_t num_free_pages;
    struct dsag_mem_node *node;
    struct dsag_local_mem_t* dsag_mem = &kvm->dsag_local_mem;

    if (!sptep) {
        dsag_printk(KERN_ERR, "Error: sptep null in %s\n", __func__);
        return RET_PF_RETRY;
    }

    dsag_printk(KERN_DEBUG, "%s: gfn=0x%llx, sptep=0x%lx\n", __func__, gfn, (uintptr_t)sptep);

    spin_lock_irqsave(&dsag_mem->free_page_lock, flags);
    num_free_pages = dsag_mem->num_free_pages;
    spin_unlock_irqrestore(&dsag_mem->free_page_lock, flags);
    if (num_free_pages == 0) {
        BUG_ON(!dsag_mem->swapper_thread);
        wake_up_process(dsag_mem->swapper_thread);
        return RET_PF_RETRY;
    }

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
            dsag_printk(KERN_DEBUG, "%s: node sptep=0x%lx is swaped in\n", __func__, (uintptr_t)node->sptep);
            node->mem_type = LOCAL_MEM;

            spin_lock_irqsave(&dsag_mem->free_page_lock, flags);
            --dsag_mem->num_free_pages;
            BUG_ON(dsag_mem->num_free_pages < 0);
            spin_unlock_irqrestore(&dsag_mem->free_page_lock, flags);
        }
    }

    spin_lock_irqsave(&dsag_mem->free_page_lock, flags);
    num_free_pages = dsag_mem->num_free_pages;
    dsag_printk(KERN_DEBUG, "%s end: num_free_pages=%d\n", __func__, num_free_pages);
    spin_unlock_irqrestore(&dsag_mem->free_page_lock, flags);
    // TODO: may use the old num_free_pages
    BUG_ON(!dsag_mem->swapper_thread);
    if (num_free_pages < FREE_POOL_WATERMARK)
        wake_up_process(dsag_mem->swapper_thread);

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

#endif  // CONFIG_KVM_DSAG_MEM_SIMULATION
