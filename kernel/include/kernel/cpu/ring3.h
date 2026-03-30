#ifndef RING3_H_
#define RING3_H_

#include <stdint.h>

extern void ring3_enter(uint32_t user_eip, uint32_t user_esp);

#endif /* !RING3_H_ */
