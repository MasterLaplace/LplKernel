#ifndef NUMA_POLICY_H_
#define NUMA_POLICY_H_

#include <stdint.h>

#define NUMA_POLICY_MAX_NODES 8u

typedef struct {
    uint32_t node_id;
    uint32_t memory_ranges;
    uint32_t cpus_bound;
} NumaNodeInfo_t;

extern void numa_policy_initialize(void);
extern uint32_t numa_policy_get_node_for_slot(uint32_t slot);
extern uint32_t numa_policy_get_local_node(void);
extern uint32_t numa_policy_get_node_count(void);
extern uint32_t numa_policy_get_node_for_address(uint32_t phys_addr);

#endif /* !NUMA_POLICY_H_ */
