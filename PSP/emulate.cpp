#include <stdio.h>
#include <psptypes.h>
#include <psprtc.h>
#include <pspkernel.h>
#include <pspgu.h>
#include <string.h>

#include "pl_psp.h"
#include "ctrl.h"
#include "video.h"
#include "pl_snd.h"
#include "pl_perf.h"

#include "supervision.h"
#include "controls.h"

#include "emulate.h"

PspImage *Screen;

extern uint8 controls_state;

extern EmulatorOptions Options;
extern const u64 ButtonMask[];
extern const int ButtonMapId[];
extern struct ButtonConfig ActiveConfig;

static int ScreenX, ScreenY, ScreenW, ScreenH;
static int ClearScreen;
static int TicksPerUpdate;
static u32 TicksPerSecond;
static u64 LastTick;
static u64 CurrentTick;
static int Frame;

static pl_perf_counter FpsCounter;

static int  ParseInput();
static void RenderVideo();
static void psp_audio_callback(pl_snd_sample* buf,
                               unsigned int samples,
                               void *userdata);

int InitEmulator()
{
	supervision_init(); //Init the emulator

  /* Initialize screen buffer */
  if (!(Screen = pspImageCreateVram(256, 160, PSP_IMAGE_16BPP)))
    return 0;

  Screen->Viewport.Width = 160;

  pl_snd_set_callback(0, psp_audio_callback, NULL);

  return 1;
}

void RunEmulator()
{
  pspImageClear(Screen, 0);

  /* Recompute screen size/position */
  float ratio;
  switch (Options.DisplayMode)
  {
  default:
  case DISPLAY_MODE_UNSCALED:
    ScreenW = Screen->Viewport.Width;
    ScreenH = Screen->Viewport.Height;
    break;
  case DISPLAY_MODE_FIT_HEIGHT:
    ratio = (float)SCR_HEIGHT / (float)Screen->Viewport.Height;
    ScreenW = (float)Screen->Viewport.Width * ratio - 2;
    ScreenH = SCR_HEIGHT;
    break;
  case DISPLAY_MODE_FILL_SCREEN:
    ScreenW = SCR_WIDTH - 3;
    ScreenH = SCR_HEIGHT;
    break;
  }

  ScreenX = (SCR_WIDTH / 2) - (ScreenW / 2);
  ScreenY = (SCR_HEIGHT / 2) - (ScreenH / 2);

  /* Init performance counter */
  pl_perf_init_counter(&FpsCounter);

  /* Recompute update frequency */
  TicksPerSecond = sceRtcGetTickResolution();
  if (Options.UpdateFreq)
  {
    TicksPerUpdate = TicksPerSecond
      / (Options.UpdateFreq / (Options.Frameskip + 1));
    sceRtcGetCurrentTick(&LastTick);
  }
  Frame = 0;
  ClearScreen = 1;

  /* Resume sound */
  pl_snd_resume(0);

  /* Wait for V. refresh */
  pspVideoWaitVSync();

  /* Main emulation loop */
  while (!ExitPSP)
  {
    /* Check input */
    if (ParseInput()) break;

    supervision_exec((int16*)Screen->Pixels, 1, Screen->Width);

    /* Run the system emulation for a frame */
    if (++Frame > Options.Frameskip)
    {
      RenderVideo();
      Frame = 0;
    }
  }

  /* Stop sound */
  pl_snd_pause(0);
}

void TrashEmulator()
{
	supervision_done(); //shuts down the system

  if (Screen)
    pspImageDestroy(Screen);
}

static int ParseInput()
{
  /* Reset input */
	controls_state = 0;

  static SceCtrlData pad;
  static int autofire_status = 0;

  /* Check the input */
  if (pspCtrlPollControls(&pad))
  {
    if (--autofire_status < 0)
      autofire_status = Options.AutoFire;

    /* Parse input */
    int i, on, code;
    for (i = 0; ButtonMapId[i] >= 0; i++)
    {
      code = ActiveConfig.ButtonMap[ButtonMapId[i]];
      on = (pad.Buttons & ButtonMask[i]) == ButtonMask[i];

      /* Check to see if a button set is pressed. If so, unset it, so it */
      /* doesn't trigger any other combination presses. */
      if (on) pad.Buttons &= ~ButtonMask[i];

      if (code & AFI)
      {
        if (on && (autofire_status == 0)) 
          controls_state |= CODE_MASK(code);
      }
      else if (code & JOY)
      {
        if (on) controls_state |= CODE_MASK(code);
      }
      else if (code & SPC)
      {
        switch (CODE_MASK(code))
        {
        case SPC_MENU:
          if (on) return 1;
          break;
        }
      }
    }
  }

  return 0;
}

static void RenderVideo()
{
  /* Update the display */
  pspVideoBegin();

  /* Clear the buffer first, if necessary */
  if (ClearScreen >= 0)
  {
    ClearScreen--;
    pspVideoClearScreen();
  }

  /* Draw the screen */
  sceGuDisable(GU_BLEND);
  pspVideoPutImage(Screen, ScreenX, ScreenY, ScreenW, ScreenH);
  sceGuEnable(GU_BLEND);

  /* Show FPS counter */
  if (Options.ShowFps)
  {
    static char fps_display[32];
    sprintf(fps_display, " %3.02f", pl_perf_update_counter(&FpsCounter));

    int width = pspFontGetTextWidth(&PspStockFont, fps_display);
    int height = pspFontGetLineHeight(&PspStockFont);

    pspVideoFillRect(SCR_WIDTH - width, 0, SCR_WIDTH, height, PSP_COLOR_BLACK);
    pspVideoPrint(&PspStockFont, SCR_WIDTH - width, 0, fps_display, PSP_COLOR_WHITE);
  }

  pspVideoEnd();

  /* Wait if needed */
  if (Options.UpdateFreq)
  {
    do { sceRtcGetCurrentTick(&CurrentTick); }
    while (CurrentTick - LastTick < TicksPerUpdate);
    LastTick = CurrentTick;
  }

  /* Wait for VSync signal */
  if (Options.VSync) 
    pspVideoWaitVSync();

  /* Swap buffers */
  pspVideoSwapBuffers();
}

static void psp_audio_callback(pl_snd_sample* buf,
                               unsigned int samples,
                               void *userdata)
{
  int length = samples << 1; /* 2 bits per sample */

  /* Render silence for now */
  memset(buf, 0, length);
}

