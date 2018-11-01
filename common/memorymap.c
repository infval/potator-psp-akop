#include "memorymap.h"

#include "controls.h"
#include "sound.h"
#include "timer.h"
#include "./m6502/m6502.h"

#include <stdlib.h>
#include <string.h>

static uint8 *lowerRam;
static uint8 *upperRam;
static uint8 *regs;
static const uint8 *programRom;
static const uint8 *lowerRomBank;
static const uint8 *upperRomBank;

static uint32 programRomSize;

static BOOL dma_finished = FALSE;
static BOOL timer_shot   = FALSE;

static void check_irq(void)
{
    BOOL irq = (timer_shot && ((regs[SV_BANK] >> 1) & 1))
          || (dma_finished && ((regs[SV_BANK] >> 2) & 1));

    void m6502_set_irq_line(BOOL); // watara.c
    m6502_set_irq_line(irq);
}

void memorymap_set_dma_finished(void)
{
    dma_finished = TRUE;
    check_irq();
}

void memorymap_set_timer_shot(void)
{
    timer_shot = TRUE;
    check_irq();
}

void memorymap_init(void)
{
    lowerRam = (uint8*)malloc(0x2000);
    upperRam = (uint8*)malloc(0x2000);
    regs     = (uint8*)malloc(0x2000);
}

void memorymap_reset(void)
{
    lowerRomBank = programRom + 0x0000;
    //  size -> upperRomBank:
    //  16KB ->   0KB (min in theory)
    //  32KB ->  16KB (min in practice)
    //  48KB ->  32KB
    //  64KB ->  48KB (max in practice)
    //  80KB ->  64KB
    //  96KB ->  80KB
    // 112KB ->  96KB
    // 128KB -> 112KB (max in theory)
    upperRomBank = programRom + (programRomSize - 0x4000);

    memset(lowerRam, 0x00, 0x2000);
    memset(upperRam, 0x00, 0x2000);
    memset(regs,     0x00, 0x2000);

    dma_finished = FALSE;
    timer_shot   = FALSE;
}

void memorymap_done(void)
{
    free(lowerRam); lowerRam = NULL;
    free(upperRam); upperRam = NULL;
    free(regs);     regs     = NULL;
}

uint8 memorymap_registers_read(uint32 Addr)
{
    uint8 data = regs[Addr & 0x1fff];
    switch (Addr & 0x1fff) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
            break;
        case 0x20:
            return controls_read();
        case 0x21:
            data &= ~0xf;
            data |= regs[0x22] & 0xf;
            break;
        case 0x24:
            timer_shot = FALSE;
            check_irq();
            break;
        case 0x25:
            dma_finished = FALSE;
            check_irq();
            break;
        case 0x27:
            data &= ~3;
            if (timer_shot) {
                data |= 1;
            }
            if (dma_finished) {
                data |= 2;
            }
            break;
    }
    return data;
}

void memorymap_registers_write(uint32 Addr, uint8 Value)
{
    regs[Addr & 0x1fff] = Value;
    switch (Addr & 0x1fff) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
            break;
        case 0x23:
            timer_write(Value);
            break;
        case 0x26:
            lowerRomBank = programRom + ((Value & 0xe0) << 9) % programRomSize;
            check_irq();
            return;
        case 0x10: case 0x11: case 0x12: case 0x13:
        case 0x14: case 0x15: case 0x16: case 0x17:
            sound_soundport_w(((Addr & 0x4) >> 2), Addr & 3, Value);
            break;
        case 0x28:
        case 0x29:
        case 0x2a:
            sound_noise_w(Addr & 0x07, Value);
            break;
        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x1b:
        case 0x1c:
            sound_sounddma_w(Addr & 0x07, Value);
            break;
    }
}

void Wr6502(register word Addr, register byte Value)
{
    Addr &= 0xffff;
    switch (Addr >> 12) {
        case 0x0:
        case 0x1:
            lowerRam[Addr] = Value;
            return;
        case 0x2:
        case 0x3:
            memorymap_registers_write(Addr, Value);
            return;
        case 0x4:
        case 0x5:
            upperRam[Addr & 0x1fff] = Value;
            return;
    }
}

byte Rd6502(register word Addr)
{
    Addr &= 0xffff;
    switch (Addr >> 12) {
        case 0x0:
        case 0x1:
            return lowerRam[Addr];
        case 0x2:
        case 0x3:
            return memorymap_registers_read(Addr);
        case 0x4:
        case 0x5:
            return upperRam[Addr & 0x1fff];
        case 0x6:
        case 0x7:
            return programRom[Addr & 0x1fff];
        case 0x8:
        case 0x9:
        case 0xa:
        case 0xb:
            return lowerRomBank[Addr & 0x3fff];
        case 0xc:
        case 0xd:
        case 0xe:
        case 0xf:
            return upperRomBank[Addr & 0x3fff];
    }
    return 0xff;
}

BOOL memorymap_load(const uint8 *rom, uint32 size)
{
    if ((size & 0x3fff) || size == 0 || rom == NULL) {
        return FALSE;
    }
    programRomSize = size;
    programRom = rom;
    return TRUE;
}

void memorymap_save_state(FILE *fp)
{
    uint8 ibank = 0;
    fwrite(regs,     0x2000, 1, fp);
    fwrite(lowerRam, 0x2000, 1, fp);
    fwrite(upperRam, 0x2000, 1, fp);

    ibank = (lowerRomBank - programRom) / 0x4000;
    fwrite(&ibank, sizeof(ibank), 1, fp);

    fwrite(&dma_finished, sizeof(dma_finished), 1, fp);
    fwrite(&timer_shot,   sizeof(timer_shot),   1, fp);
}

void memorymap_load_state(FILE *fp)
{
    uint8 ibank = 0;
    fread(regs,     0x2000, 1, fp);
    fread(lowerRam, 0x2000, 1, fp);
    fread(upperRam, 0x2000, 1, fp);

    fread(&ibank, sizeof(ibank), 1, fp);
    lowerRomBank = programRom + ibank * 0x4000;

    fread(&dma_finished, sizeof(dma_finished), 1, fp);
    fread(&timer_shot,   sizeof(timer_shot),   1, fp);
}

uint8 *memorymap_getLowerRamPointer(void)
{
    return lowerRam;
}

uint8 *memorymap_getUpperRamPointer(void)
{
    return upperRam;
}

uint8 *memorymap_getRegisters(void)
{
    return regs;
}

const uint8 *memorymap_getRomPointer(void)
{
    return programRom;
}

const uint8 *memorymap_getLowerRomBank(void)
{
    return lowerRomBank;
}

const uint8 *memorymap_getUpperRomBank(void)
{
    return upperRomBank;
}
