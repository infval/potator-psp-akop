#ifndef __CONTROLS_H__
#define __CONTROLS_H__

#include "supervision.h"

void controls_init();
void controls_done();
void controls_reset();
void controls_write(uint32 addr, uint8 data);
uint8 controls_read(uint32 addr);
bool controls_update(void);

#endif
