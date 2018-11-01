#ifndef __TIMER_H__
#define __TIMER_H__

#include "types.h"

#include <stdio.h>

void timer_reset(void);
void timer_write(uint8 data);
void timer_exec(uint32 cycles);

void timer_save_state(FILE *fp);
void timer_load_state(FILE *fp);

#endif
