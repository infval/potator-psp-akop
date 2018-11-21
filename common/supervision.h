/**
 * \file supervision.h
 */

#ifndef __SUPERVISION_H__
#define __SUPERVISION_H__

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SV_CORE_VERSION 0x01000002U
#define SV_CORE_VERSION_MAJOR ((SV_CORE_VERSION >> 24) & 0xFF)
#define SV_CORE_VERSION_MINOR ((SV_CORE_VERSION >> 12) & 0xFFF)
#define SV_CORE_VERSION_PATCH ((SV_CORE_VERSION >>  0) & 0xFFF)

/*! Screen width.  */
#define SV_W 160
/*! Screen height. */
#define SV_H 160
/*!
 * \sa supervision_set_map_func()
 */
typedef uint16 (*SV_MapRGBFunc)(uint8 r, uint8 g, uint8 b);
/*!
 * \sa supervision_set_color_scheme()
 */
enum SV_COLOR {
      SV_COLOR_SCHEME_DEFAULT
    , SV_COLOR_SCHEME_AMBER
    , SV_COLOR_SCHEME_GREEN
    , SV_COLOR_SCHEME_BLUE
    , SV_COLOR_SCHEME_BGB
    , SV_COLOR_SCHEME_WATAROO

    , SV_COLOR_SCHEME_COUNT
};
/*!
 * \sa supervision_set_ghosting()
 */
#define SV_GHOSTING_MAX 8
 /*!
  * \sa supervision_update_sound()
  */
#define SV_SAMPLE_RATE 44100

void supervision_init(void);
void supervision_reset(void);
void supervision_done(void);
/*!
 * \return TRUE - success, FALSE - error
 */
BOOL supervision_load(const uint8 *rom, uint32 romSize);
void supervision_exec(uint16 *backbuffer);
void supervision_exec_ex(uint16 *backbuffer, int16 backbufferWidth);

/*!
 * \param data Bits 0-7: Right, Left, Down, Up, B, A, Select, Start.
 */
void supervision_set_input(uint8 data);
/*!
 * \param func Default: RGB888 -> RGB555 (RGBA5551), R - least significant.
 */
void supervision_set_map_func(SV_MapRGBFunc func);
/*!
 * \param colorSheme in range [0, SV_COLOR_SCHEME_COUNT - 1] or SV_COLOR_* constants.
 * \sa SV_COLOR
 */
void supervision_set_color_scheme(int colorScheme);
/*!
 * Add ghosting (blur). It reduces flickering.
 * \param frameCount in range [0, SV_GHOSTING_MAX]. 0 - disable.
 */
void supervision_set_ghosting(int frameCount);
/*!
 * Generate U8 (0 - 45), 2 channels.
 * \param len in bytes.
 */
void supervision_update_sound(uint8 *stream, uint32 len);

/*!
 * Save state to '{statePath}{id}.svst' if id >= 0, otherwise '{statePath}'.
 * \return TRUE - success, FALSE - error
 */
BOOL supervision_save_state(const char *statePath, int8 id);
/*!
 * Load state from '{statePath}{id}.svst' if id >= 0, otherwise '{statePath}'.
 * \return TRUE - success, FALSE - error
 */
BOOL supervision_load_state(const char *statePath, int8 id);

#ifdef __cplusplus
}
#endif

#endif
