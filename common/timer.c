#include "timer.h"

#include "memorymap.h"

static int32 timer_cycles;
static BOOL  timer_activated;

void timer_reset(void)
{
    timer_cycles = 0;
    timer_activated = FALSE;
}

void timer_write(uint8 data)
{
    uint32 d = data ? data : 0x100; // Dancing Block. d = data; ???
    if ((memorymap_getRegisters()[BANK] >> 4) & 1) {
        timer_cycles = d * 0x4000; // Bubble World, Eagle Plan...
    }
    else {
        timer_cycles = d * 0x100;
    }
    timer_activated = TRUE;
}

void timer_exec(uint32 cycles)
{
    if (timer_activated) {
        timer_cycles -= cycles;

        if (timer_cycles <= 0) {
            timer_activated = FALSE;
            memorymap_set_timer_shot();
        }
    }
}

void timer_save_state(FILE *fp)
{
    WRITE_int32(timer_cycles, fp);
    WRITE_BOOL(timer_activated, fp);
}

void timer_load_state(FILE *fp)
{
    READ_int32(timer_cycles, fp);
    READ_BOOL(timer_activated, fp);
}
