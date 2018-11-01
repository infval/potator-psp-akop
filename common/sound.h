#ifndef __SOUND_H__
#define __SOUND_H__

#include "types.h"

#include <stdio.h>

void sound_reset(void);
/*!
 * Generate U8 (0 - 127), 2 channels.
 * \param len in bytes.
 */
void sound_stream_update(uint8 *stream, int len);
void sound_decrement(void);
void sound_soundport_w(int which, int offset, int data);
void sound_sounddma_w(int offset, int data);
void sound_noise_w(int offset, int data);

void sound_save_state(FILE *fp);
void sound_load_state(FILE *fp);

#endif
