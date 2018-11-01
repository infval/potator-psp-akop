#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "supervision.h"

#include "controls.h"
#include "gpu.h"
#include "memorymap.h"
#include "sound.h"
#include "timer.h"
#include "./m6502/m6502.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static M6502 m6502_registers;
static BOOL irq = FALSE;

void m6502_set_irq_line(BOOL assertLine)
{
    m6502_registers.IRequest = assertLine ? INT_IRQ : INT_NONE;
    irq = assertLine;
}

byte Loop6502(register M6502 *R)
{
    if (irq) {
        irq = FALSE;
        return INT_IRQ;
    }
    return INT_QUIT;
}

void supervision_init(void)
{
    gpu_init();
    memorymap_init();
    // 256 * 256 -- 1 frame (61 FPS)
    m6502_registers.IPeriod = 256;
}

void supervision_reset(void)
{
    controls_reset();
    gpu_reset();
    memorymap_reset();
    sound_reset();
    timer_reset();

    Reset6502(&m6502_registers);
    irq = FALSE;
}

void supervision_done(void)
{
    gpu_done();
    memorymap_done();
}

BOOL supervision_load(const uint8 *rom, uint32 romSize)
{
    if (!memorymap_load(rom, romSize)) {
        return FALSE;
    }
    supervision_reset();
    return TRUE;
}

void supervision_exec(uint16 *backbuffer)
{
    supervision_exec_ex(backbuffer, SV_W);
}

void supervision_exec_ex(uint16 *backbuffer, int16 backbufferWidth)
{
    uint32 i, scan;
    uint8 *regs = memorymap_getRegisters();
    uint8 startx, endx;

    // Number of iterations = 256 * 256 / m6502_registers.IPeriod
    for (i = 0; i < 256; i++) {
        Run6502(&m6502_registers);
        timer_exec(m6502_registers.IPeriod);
    }

    //if (!((regs[SV_BANK] >> 3) & 1)) { printf("LCD off\n"); }
    scan   = regs[SV_XPOS] / 4 + regs[SV_YPOS] * 0x30;
    startx = regs[SV_XPOS] & 3;      //3 - (regs[SV_XPOS] & 3);
    endx = (regs[SV_XSIZE] | 3) - 3; //min(163, (regs[SV_XSIZE] | 3));
    if (endx > SV_W) endx = SV_W;

    for (i = 0; i < SV_H; i++) {
        gpu_render_scanline(scan, backbuffer, startx, endx);
        backbuffer += backbufferWidth;
        scan += 0x30;
        if (scan >= 0x1fe0)
            scan = 0; // SSSnake
    }

    if (Rd6502(0x2026) & 0x01)
        Int6502(&m6502_registers, INT_NMI);

    sound_decrement();
}

void supervision_set_map_func(SV_MapRGBFunc func)
{
    gpu_set_map_func(func);
}

void supervision_set_color_scheme(int colorScheme)
{
    gpu_set_color_scheme(colorScheme);
}

void supervision_set_ghosting(int frameCount)
{
    gpu_set_ghosting(frameCount);
}

void supervision_set_input(uint8 data)
{
    controls_state_write(data);
}

void supervision_update_sound(uint8 *stream, int len)
{
    sound_stream_update(stream, len);
}

static void get_state_path(const char *statePath, int8 id, char **newPath)
{
    if (id < 0) {
        *newPath = (char*)statePath;
    }
    else {
        size_t newPathLen;
        newPathLen = strlen(statePath);
        *newPath = (char *)malloc(newPathLen + 8 + 1); // strlen("XXX.svst") + 1
        strcpy(*newPath, statePath);
        sprintf(*newPath + newPathLen, "%d.svst", id);
    }
}

BOOL supervision_save_state(const char *statePath, int8 id)
{
    FILE *fp;
    char *newPath;

    get_state_path(statePath, id, &newPath);
    fp = fopen(newPath, "wb");
    if (id >= 0)
        free(newPath);
    if (fp) {
        fwrite(&m6502_registers, sizeof(m6502_registers), 1, fp);
        fwrite(&irq, sizeof(irq), 1, fp);

        memorymap_save_state(fp);
        sound_save_state(fp);
        timer_save_state(fp);

        fflush(fp);
        fclose(fp);
    }
    else {
        return FALSE;
    }
    return TRUE;
}

BOOL supervision_load_state(const char *statePath, int8 id)
{
    FILE *fp;
    char *newPath;

    get_state_path(statePath, id, &newPath);
    fp = fopen(newPath, "rb");
    if (id >= 0)
        free(newPath);
    if (fp) {
        sound_reset();

        fread(&m6502_registers, sizeof(m6502_registers), 1, fp);
        fread(&irq, sizeof(irq), 1, fp);

        memorymap_load_state(fp);
        sound_load_state(fp);
        timer_load_state(fp);

        fclose(fp);
    }
    else {
        return FALSE;
    }
    return TRUE;
}
