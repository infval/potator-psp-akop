#include "gpu.h"

#include "memorymap.h"

#include <stdlib.h>
#include <string.h>

// RGB555 or RGBA5551
#define RGB555(R,G,B) ((((int)(B))<<10)|(((int)(G))<<5)|(((int)(R)))|(1<<15))

static uint16 rgb555(uint8 r, uint8 g, uint8 b)
{
    return RGB555(r >> 3, g >> 3, b >> 3);
}

static SV_MapRGBFunc mapRGB = rgb555;

const static uint8 palettes[SV_COLOR_SCHEME_COUNT][12] = {
{
    252, 252, 252,
    168, 168, 168,
     84,  84,  84,
      0,   0,   0,
},
{
    252, 154,   0,
    168, 102,   0,
     84,  51,   0,
      0,   0,   0,
},
{
     50, 227,  50,
     34, 151,  34,
     17,  76,  17,
      0,   0,   0,
},
{
      0, 154, 252,
      0, 102, 168,
      0,  51,  84,
      0,   0,   0,
},
{
    224, 248, 208,
    136, 192, 112,
     52, 104,  86,
      8,  24,  32,
},
{
    0x7b, 0xc7, 0x7b,
    0x52, 0xa6, 0x8c,
    0x2e, 0x62, 0x60,
    0x0d, 0x32, 0x2e,
},
};

static uint16 *palette;
static int paletteIndex;

#define SB_MAX (SV_GHOSTING_MAX + 1)
static int ghostCount = 0;
static uint8 *screenBuffers[SB_MAX];
static uint8 screenBufferInnerX[SB_MAX];

static void add_ghosting(uint32 scanline, uint16 *backbuffer, uint8 start_x, uint8 end_x);

void gpu_init(void)
{
    palette = (uint16*)malloc(4 * sizeof(int16));
}

void gpu_reset(void)
{
    gpu_set_map_func(NULL);
    gpu_set_color_scheme(SV_COLOR_SCHEME_DEFAULT);
    gpu_set_ghosting(0);
}

void gpu_done(void)
{
    free(palette); palette = NULL;
    gpu_set_ghosting(0);
}

void gpu_set_map_func(SV_MapRGBFunc func)
{
    mapRGB = func;
    if (mapRGB == NULL) {
        mapRGB = rgb555;
    }
}

void gpu_set_color_scheme(int colorScheme)
{
    int i;
    if (colorScheme < 0 || colorScheme >= SV_COLOR_SCHEME_COUNT) {
        return;
    }
    for (i = 0; i < 4; i++) {
        palette[i] = mapRGB(palettes[colorScheme][i * 3 + 0],
                            palettes[colorScheme][i * 3 + 1],
                            palettes[colorScheme][i * 3 + 2]);
    }
    paletteIndex = colorScheme;
}

// Faster but it's not accurate
//void gpu_render_scanline(uint32 scanline, uint16 *backbuffer)
//{
//    uint8 *vram_line = memorymap_getUpperRamPointer() + scanline;
//    uint8 x;
//
//    for (x = 0; x < SV_W; x += 4) {
//        uint8 b = *(vram_line++);
//        backbuffer[0] = palette[((b >> 0) & 0x03)];
//        backbuffer[1] = palette[((b >> 2) & 0x03)];
//        backbuffer[2] = palette[((b >> 4) & 0x03)];
//        backbuffer[3] = palette[((b >> 6) & 0x03)];
//        backbuffer += 4;
//    }
//}

void gpu_render_scanline(uint32 scanline, uint16 *backbuffer, uint8 innerx, uint8 size)
{
    uint8 *vram_line = memorymap_getUpperRamPointer() + scanline;
    uint8 x, j = innerx, b = 0;

    // #1
    if (j & 3) {
        b = *vram_line++;
        b >>= (j & 3) * 2;
    }
    for (x = 0; x < size; x++, j++) {
        if (!(j & 3)) {
            b = *(vram_line++);
        }
        backbuffer[x] = palette[b & 3];
        b >>= 2;
    }
    // #2 Slow
    /*for (x = 0; x < size; x++, j++) {
        b = vram_line[j >> 2];
        backbuffer[x] = palette[(b >> ((j & 3) * 2)) & 3];
    }*/

    if (ghostCount != 0) {
        add_ghosting(scanline, backbuffer, innerx, size);
    }
}

void gpu_set_ghosting(int frameCount)
{
    int i;
    if (frameCount < 0)
        ghostCount = 0;
    else if (frameCount > SV_GHOSTING_MAX)
        ghostCount = SV_GHOSTING_MAX;
    else
        ghostCount = frameCount;

    if (ghostCount != 0) {
        if (screenBuffers[0] == NULL) {
            for (i = 0; i < SB_MAX; i++) {
                screenBuffers[i] = malloc(SV_H * SV_W / 4);
            }
        }
        for (i = 0; i < SB_MAX; i++) {
            memset(screenBuffers[i], 0, SV_H * SV_W / 4);
        }
    }
    else {
        for (i = 0; i < SB_MAX; i++) {
            free(screenBuffers[i]);
            screenBuffers[i] = NULL;
        }
    }
}

static void add_ghosting(uint32 scanline, uint16 *backbuffer, uint8 innerx, uint8 size)
{
    static int curSB = 0;
    static int lineCount = 0;

    uint8 *vram_line = memorymap_getUpperRamPointer() + scanline;
    uint8 x, i, j;

    screenBufferInnerX[curSB] = innerx;
    memset(screenBuffers[curSB] + lineCount * SV_W / 4, 0, SV_W / 4);
    for (j = innerx, x = 0; x < size; x++, j++) {
        uint8 b = vram_line[j >> 2];
        uint8 innerInd = (j & 3) * 2;
        uint8 c = (b >> innerInd) & 3;
        int pixInd = (x + lineCount * SV_W) / 4;
        if (c == 0) {
            for (i = 0; i < ghostCount; i++) {
                uint8 sbInd = (curSB + (SB_MAX - 1) - i) % SB_MAX;
                innerInd = ((screenBufferInnerX[sbInd] + x) & 3) * 2;
                c = (screenBuffers[sbInd][pixInd] >> innerInd) & 3;
                if (c != 0) {
#if 0
                    backbuffer[x] = palette[3 - 3 * i / ghostCount];
#else
                    uint8 r = palettes[paletteIndex][c * 3 + 0];
                    uint8 g = palettes[paletteIndex][c * 3 + 1];
                    uint8 b = palettes[paletteIndex][c * 3 + 2];
                    r =  r + (palettes[paletteIndex][0] - r) * i / ghostCount;
                    g =  g + (palettes[paletteIndex][1] - g) * i / ghostCount;
                    b =  b + (palettes[paletteIndex][2] - b) * i / ghostCount;
                    backbuffer[x] = mapRGB(r, g, b);
#endif
                    break;
                }
            }
        }
        else {
            screenBuffers[curSB][pixInd] |= c << innerInd;
        }
    }

    if (lineCount == SV_H - 1) {
        curSB = (curSB + 1) % SB_MAX;
    }
    lineCount = (lineCount + 1) % SV_H;
}
