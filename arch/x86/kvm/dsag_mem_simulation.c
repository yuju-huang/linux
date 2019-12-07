#ifdef CONFIG_KVM_DSAG_MEM_SIMULATION

#include <linux/hashtable.h>

#include "dsag_mem_simulation.h"

static DEFINE_HASHTABLE(dsag_mem_hash, DSAG_MEM_HASH_BITS);

#endif  // CONFIG_KVM_DSAG_MEM_SIMULATION
