/*
 * Header file for disaggregated architecture simulation.
 */
#ifndef __DSAG_MEM_SIMULATE_H
#define __DSAG_MEM_SIMULATE_H

#ifdef CONFIG_KVM_DSAG_MEM_SIMULATION

#define DSAG_LOCAL_MEMORY_SIZE (1UL << 32)
#define DSAG_LOCAL_MEMORY_PAGE_NUM  (DSAG_LOCAL_MEMORY_SIZE / PAGE_SIZE)
#define DSAG_MEM_HASH_BITS 8
#define DSAG_MEM_HASH_SIZE (1UL << 20)

#endif  // CONFIG_KVM_DSAG_MEM_SIMULATION
#endif  // __DSAG_MEM_SIMULATE_H
