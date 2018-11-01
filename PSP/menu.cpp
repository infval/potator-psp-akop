#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <psptypes.h>
#include <psprtc.h>
#include <pspgu.h>
#include <pspkernel.h>

#include "ui.h"
#include "ctrl.h"
#include "pl_psp.h"
#include "pl_ini.h"
#include "pl_file.h"
#include "pl_util.h"
#include "image.h"

#include "supervision.h"
#include "unzip.h"

#include "menu.h"
#include "emulate.h"

#define TAB_QUICKLOAD 0
#define TAB_STATE     1
#define TAB_CONTROL   2
#define TAB_OPTION    3
#define TAB_SYSTEM    4
#define TAB_ABOUT     5
#define TAB_MAX       TAB_SYSTEM

#define OPTION_DISPLAY_MODE 0x01
#define OPTION_SYNC_FREQ    0x02
#define OPTION_FRAMESKIP    0x03
#define OPTION_VSYNC        0x04
#define OPTION_CLOCK_FREQ   0x05
#define OPTION_SHOW_FPS     0x06
#define OPTION_CONTROL_MODE 0x07
#define OPTION_ANIMATE      0x08
#define OPTION_AUTOFIRE     0x09

#define SYSTEM_SCRNSHOT     0x11
#define SYSTEM_RESET        0x12
#define SYSTEM_COLORS       0x13

/* Tab labels */
static const char *TabLabel[] = 
{
  "Game",
  "Save/Load",
  "Controls",
  "Options",
  "System",
  "About"
};

extern PspImage *Screen;

EmulatorOptions Options;

static uint8 *rom_buffer;
static int TabIndex;
static int ResumeEmulation;
static PspImage *Background;
static PspImage *NoSaveIcon;

static const char *QuickloadFilter[] = { "ZIP", "SV", '\0' },
  PresentSlotText[] = "\026\244\020 Save\t\026\001\020 Load\t\026\243\020 Delete",
  EmptySlotText[] = "\026\244\020 Save",
  ControlHelpText[] = "\026\250\020 Change mapping\t\026\001\020 Save to \271\t\026\243\020 Load defaults";

pl_file_path CurrentGame = "",
             GamePath = "",
             SaveStatePath,
             ScreenshotPath;

#define SET_AS_CURRENT_GAME(filename) \
  strncpy(CurrentGame, filename, sizeof(CurrentGame) - 1)
#define CURRENT_GAME (CurrentGame)
#define GAME_LOADED (CurrentGame[0] != '\0')

static int psp_load_rom(const char *filename);
static int SaveOptions();
static void LoadOptions();

static void DisplayStateTab();

static PspImage* LoadStateIcon(const char *path);
static int LoadState(const char *path);
static PspImage* SaveState(const char *path, PspImage *icon);

static void InitButtonConfig();
static int LoadButtonConfig();
static int SaveButtonConfig();

static int OnMenuItemChanged(const struct PspUiMenu *uimenu, pl_menu_item* item, 
  const pl_menu_option* option);
static int OnMenuOk(const void *uimenu, const void* sel_item);
static int OnMenuButtonPress(const struct PspUiMenu *uimenu, 
  pl_menu_item* sel_item, u32 button_mask);

static int OnSplashButtonPress(const struct PspUiSplash *splash, 
  u32 button_mask);
static void OnSplashRender(const void *uiobject, const void *null);

static int OnGenericCancel(const void *uiobject, const void *param);
static void OnGenericRender(const void *uiobject, const void *item_obj);
static int OnGenericButtonPress(const PspUiFileBrowser *browser, 
  const char *path, u32 button_mask);

static int OnSaveStateOk(const void *gallery, const void *item);
static int OnSaveStateButtonPress(const PspUiGallery *gallery, 
  pl_menu_item* item, u32 button_mask);

static int OnQuickloadOk(const void *browser, const void *path);

static void OnSystemRender(const void *uiobject, const void *item_obj);

/* Menu options */
PL_MENU_OPTIONS_BEGIN(ToggleOptions)
  PL_MENU_OPTION("Disabled", 0)
  PL_MENU_OPTION("Enabled",  1)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(ScreenSizeOptions)
  PL_MENU_OPTION("Actual size",              DISPLAY_MODE_UNSCALED)
  PL_MENU_OPTION("4:3 scaled (fit height)",  DISPLAY_MODE_FIT_HEIGHT)
  PL_MENU_OPTION("16:9 scaled (fit screen)", DISPLAY_MODE_FILL_SCREEN)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(FrameLimitOptions)
  PL_MENU_OPTION("Disabled",      0)
  PL_MENU_OPTION("60 fps (NTSC)", 60)
PL_MENU_OPTIONS_END
// PL_MENU_OPTIONS_BEGIN(FrameSkipOptions)
//   PL_MENU_OPTION("No skipping",   0)
//   PL_MENU_OPTION("Skip 1 frame",  1)
//   PL_MENU_OPTION("Skip 2 frames", 2)
//   PL_MENU_OPTION("Skip 3 frames", 3)
//   PL_MENU_OPTION("Skip 4 frames", 4)
//   PL_MENU_OPTION("Skip 5 frames", 5)
// PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(PspClockFreqOptions)
  PL_MENU_OPTION("222 MHz", 222)
  PL_MENU_OPTION("266 MHz", 266)
  PL_MENU_OPTION("300 MHz", 300)
  PL_MENU_OPTION("333 MHz", 333)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(AutofireOptions)
  PL_MENU_OPTION("Once every 3 frames", 2)
  PL_MENU_OPTION("Once every 10 frames", 9)
  PL_MENU_OPTION("Once every 30 frames", 29)
  PL_MENU_OPTION("Once every 60 frames", 59)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(ControlModeOptions)
  PL_MENU_OPTION("\026\242\020 cancels, \026\241\020 confirms (US)",    0)
  PL_MENU_OPTION("\026\241\020 cancels, \026\242\020 confirms (Japan)", 1)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(ColorSchemeOptions)
  PL_MENU_OPTION("Default",       SV_COLOR_SCHEME_DEFAULT)
  PL_MENU_OPTION("Amber",         SV_COLOR_SCHEME_AMBER)
  PL_MENU_OPTION("Green",         SV_COLOR_SCHEME_GREEN)
  PL_MENU_OPTION("Blue",          SV_COLOR_SCHEME_BLUE)
  PL_MENU_OPTION("BGB (GameBoy)", SV_COLOR_SCHEME_BGB)
  PL_MENU_OPTION("TV link (?)",   SV_COLOR_SCHEME_YOUTUBE)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(ButtonMapOptions)
  /* Unmapped */
  PL_MENU_OPTION("None", 0)
  /* Special */
  PL_MENU_OPTION("Special: Open Menu", SPC|SPC_MENU)
  /* Joystick */
  PL_MENU_OPTION("Up",       JOY|0x08)
  PL_MENU_OPTION("Down",     JOY|0x04)
  PL_MENU_OPTION("Left",     JOY|0x02)
  PL_MENU_OPTION("Right",    JOY|0x01)
  PL_MENU_OPTION("Button A", JOY|0x20)
  PL_MENU_OPTION("Button B", JOY|0x10)
  PL_MENU_OPTION("Button A (autofire)", AFI|0x20)
  PL_MENU_OPTION("Button B (autofire)", AFI|0x10)
  PL_MENU_OPTION("Select",   JOY|0x40)
  PL_MENU_OPTION("Start",    JOY|0x80)
PL_MENU_OPTIONS_END

/* Menu items */
PL_MENU_ITEMS_BEGIN(OptionMenuDef)
  PL_MENU_HEADER("Video")
  PL_MENU_ITEM("Screen size", OPTION_DISPLAY_MODE, ScreenSizeOptions,
               "\026\250\020 Change screen size")
  PL_MENU_HEADER("Input")
  PL_MENU_ITEM("Rate of autofire", OPTION_AUTOFIRE, AutofireOptions, 
               "\026\250\020 Adjust rate of autofire")
  PL_MENU_HEADER("Performance")
  PL_MENU_ITEM("Frame limiter", OPTION_SYNC_FREQ, FrameLimitOptions,
               "\026\250\020 Change screen update frequency")
//  PL_MENU_ITEM("Frame skipping", OPTION_FRAMESKIP, FrameSkipOptions,
//               "\026\250\020 Change number of frames skipped per update")
  PL_MENU_ITEM("VSync", OPTION_VSYNC, ToggleOptions,
               "\026\250\020 Enable to reduce tearing; disable to increase speed")
  PL_MENU_ITEM("PSP clock frequency", OPTION_CLOCK_FREQ, PspClockFreqOptions,
               "\026\250\020 Larger values: faster emulation, faster battery depletion (default: 222MHz)")
  PL_MENU_ITEM("Show FPS counter",    OPTION_SHOW_FPS, ToggleOptions,
               "\026\250\020 Show/hide the frames-per-second counter")
  PL_MENU_HEADER("Menu")
  PL_MENU_ITEM("Button mode", OPTION_CONTROL_MODE, ControlModeOptions,
               "\026\250\020 Change OK and Cancel button mapping")
  PL_MENU_ITEM("Animations", OPTION_ANIMATE, ToggleOptions, 
               "\026\250\020 Enable/disable in-menu animations")
PL_MENU_ITEMS_END
PL_MENU_ITEMS_BEGIN(ControlMenuDef)
    PL_MENU_ITEM(PSP_CHAR_ANALUP, MAP_ANALOG_UP, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_ANALDOWN, MAP_ANALOG_DOWN, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_ANALLEFT, MAP_ANALOG_LEFT, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_ANALRIGHT, MAP_ANALOG_RIGHT, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_UP, MAP_BUTTON_UP, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_DOWN, MAP_BUTTON_DOWN, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_LEFT, MAP_BUTTON_LEFT, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_RIGHT, MAP_BUTTON_RIGHT, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_SQUARE, MAP_BUTTON_SQUARE, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_CROSS, MAP_BUTTON_CROSS, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_CIRCLE, MAP_BUTTON_CIRCLE, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_TRIANGLE, MAP_BUTTON_TRIANGLE, ButtonMapOptions, 
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_LTRIGGER, MAP_BUTTON_LTRIGGER, ButtonMapOptions, 
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_RTRIGGER, MAP_BUTTON_RTRIGGER, ButtonMapOptions, 
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_SELECT, MAP_BUTTON_SELECT, ButtonMapOptions, 
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_START, MAP_BUTTON_START, ButtonMapOptions,
      ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_LTRIGGER"+"PSP_CHAR_RTRIGGER, 
      MAP_BUTTON_LRTRIGGERS, ButtonMapOptions, ControlHelpText)
    PL_MENU_ITEM(PSP_CHAR_START"+"PSP_CHAR_SELECT,
      MAP_BUTTON_STARTSELECT, ButtonMapOptions, ControlHelpText)
PL_MENU_ITEMS_END
PL_MENU_ITEMS_BEGIN(SystemMenuDef)
  PL_MENU_HEADER("Video")
  PL_MENU_ITEM("Color scheme", SYSTEM_COLORS, ColorSchemeOptions, 
    "\026\250\020 Select color scheme")
  PL_MENU_HEADER("System")
  PL_MENU_ITEM("Reset", SYSTEM_RESET, NULL, "\026\001\020 Reset")
  PL_MENU_ITEM("Save screenshot",  SYSTEM_SCRNSHOT, NULL,
    "\026\001\020 Save screenshot")
PL_MENU_ITEMS_END

PspUiSplash SplashScreen =
{
  OnSplashRender,
  OnGenericCancel,
  OnSplashButtonPress,
  NULL
};

PspUiGallery SaveStateGallery = 
{
  OnGenericRender,             /* OnRender() */
  OnSaveStateOk,               /* OnOk() */
  OnGenericCancel,             /* OnCancel() */
  OnSaveStateButtonPress,      /* OnButtonPress() */
  NULL                         /* Userdata */
};

PspUiMenu OptionUiMenu =
{
  OnGenericRender,       /* OnRender() */
  OnMenuOk,              /* OnOk() */
  OnGenericCancel,       /* OnCancel() */
  OnMenuButtonPress,     /* OnButtonPress() */
  OnMenuItemChanged,     /* OnItemChanged() */
};

PspUiMenu ControlUiMenu =
{
  OnGenericRender,       /* OnRender() */
  OnMenuOk,              /* OnOk() */
  OnGenericCancel,       /* OnCancel() */
  OnMenuButtonPress,     /* OnButtonPress() */
  OnMenuItemChanged,     /* OnItemChanged() */
};

PspUiFileBrowser QuickloadBrowser = 
{
  OnGenericRender,
  OnQuickloadOk,
  OnGenericCancel,
  OnGenericButtonPress,
  QuickloadFilter,
  0
};

PspUiMenu SystemUiMenu =
{
  OnSystemRender,        /* OnRender() */
  OnMenuOk,              /* OnOk() */
  OnGenericCancel,       /* OnCancel() */
  OnMenuButtonPress,     /* OnButtonPress() */
  OnMenuItemChanged,     /* OnItemChanged() */
};

/* Game configuration (includes button maps) */
struct ButtonConfig ActiveConfig;

/* Default configuration */
struct ButtonConfig DefaultConfig =
{
  {
    JOY|0x08,  /* Analog Up    */
    JOY|0x04,  /* Analog Down  */
    JOY|0x02,  /* Analog Left  */
    JOY|0x01,  /* Analog Right */
    JOY|0x08,  /* D-pad Up     */
    JOY|0x04,  /* D-pad Down   */
    JOY|0x02,  /* D-pad Left   */
    JOY|0x01,  /* D-pad Right  */
    0,         /* Square       */
    JOY|0x20,  /* Cross        */
    JOY|0x10,  /* Circle       */
    0,         /* Triangle     */
    0,         /* L Trigger    */
    0,         /* R Trigger    */
    JOY|0x40,  /* Select       */
    JOY|0x80,  /* Start        */
    SPC|SPC_MENU, /* L+R Triggers */
    0,            /* Start+Select */
  }
};

/* Button masks */
u64 ButtonMask[] = 
{
  PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER, 
  PSP_CTRL_START    | PSP_CTRL_SELECT,
  PSP_CTRL_ANALUP,    PSP_CTRL_ANALDOWN,
  PSP_CTRL_ANALLEFT,  PSP_CTRL_ANALRIGHT,
  PSP_CTRL_UP,        PSP_CTRL_DOWN,
  PSP_CTRL_LEFT,      PSP_CTRL_RIGHT,
  PSP_CTRL_SQUARE,    PSP_CTRL_CROSS,
  PSP_CTRL_CIRCLE,    PSP_CTRL_TRIANGLE,
  PSP_CTRL_LTRIGGER,  PSP_CTRL_RTRIGGER,
  PSP_CTRL_SELECT,    PSP_CTRL_START,
  0 /* End */
};

/* Button map ID's */
int ButtonMapId[] = 
{
  MAP_BUTTON_LRTRIGGERS, 
  MAP_BUTTON_STARTSELECT,
  MAP_ANALOG_UP,       MAP_ANALOG_DOWN,
  MAP_ANALOG_LEFT,     MAP_ANALOG_RIGHT,
  MAP_BUTTON_UP,       MAP_BUTTON_DOWN,
  MAP_BUTTON_LEFT,     MAP_BUTTON_RIGHT,
  MAP_BUTTON_SQUARE,   MAP_BUTTON_CROSS,
  MAP_BUTTON_CIRCLE,   MAP_BUTTON_TRIANGLE,
  MAP_BUTTON_LTRIGGER, MAP_BUTTON_RTRIGGER,
  MAP_BUTTON_SELECT,   MAP_BUTTON_START,
  -1 /* End */
};

int InitMenu()
{
  /* Reset variables */
  TabIndex = TAB_ABOUT;
  Background = NULL;
  rom_buffer = NULL;

  /* Initialize paths */
  sprintf(SaveStatePath, "%sstates", pl_psp_get_app_directory());
  sceIoMkdir(SaveStatePath, 0777);
  sprintf(SaveStatePath, "%sstates/", pl_psp_get_app_directory());
  sprintf(ScreenshotPath, "ms0:/PSP/PHOTO/%s/", PSP_APP_NAME);
  sprintf(GamePath, "%s", pl_psp_get_app_directory());

  /* Initialize options */
  LoadOptions();

  if (!InitEmulator())
    return 0;

  /* Load the background image */
  Background = pspImageLoadPng("background.png");

  /* Init NoSaveState icon image */
  NoSaveIcon = pspImageCreate(80, 80, PSP_IMAGE_16BPP);
  pspImageClear(NoSaveIcon, RGB(0x1a,0x44,0x44));

  /* Initialize state menu */
  int i;
  pl_menu_item *item;
  for (i = 0; i < 10; i++)
  {
    item = pl_menu_append_item(&SaveStateGallery.Menu, i, NULL);
    pl_menu_set_item_help_text(item, EmptySlotText);
  }

  /* Initialize menus */
  pl_menu_create(&SystemUiMenu.Menu, SystemMenuDef);
  pl_menu_create(&OptionUiMenu.Menu, OptionMenuDef);
  pl_menu_create(&ControlUiMenu.Menu, ControlMenuDef);

  /* Load default configuration */
  LoadButtonConfig();

  /* Initialize UI components */
  UiMetric.Background = Background;
  UiMetric.Font = &PspStockFont;
  UiMetric.Left = 8;
  UiMetric.Top = 24;
  UiMetric.Right = 472;
  UiMetric.Bottom = 250;
  UiMetric.OkButton = (!Options.ControlMode) ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
  UiMetric.CancelButton = (!Options.ControlMode) ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
  UiMetric.ScrollbarColor = PSP_COLOR_GRAY;
  UiMetric.ScrollbarBgColor = COLOR(0,0,0,0x22);
  UiMetric.ScrollbarWidth = 10;
  UiMetric.TextColor = PSP_COLOR_GRAY;
  UiMetric.SelectedColor = PSP_COLOR_YELLOW;
  UiMetric.SelectedBgColor = COLOR(0xff,0xff,0xff,0x88);
  UiMetric.StatusBarColor = PSP_COLOR_WHITE;
  UiMetric.BrowserFileColor = PSP_COLOR_GRAY;
  UiMetric.BrowserDirectoryColor = PSP_COLOR_YELLOW;
  UiMetric.GalleryIconsPerRow = 5;
  UiMetric.GalleryIconMarginWidth = 16;
  UiMetric.MenuItemMargin = 20;
  UiMetric.MenuSelOptionBg = PSP_COLOR_BLACK;
  UiMetric.MenuOptionBoxColor = PSP_COLOR_GRAY;
  UiMetric.MenuOptionBoxBg = COLOR(0x1a,0x44,0x44,0xbb);
  UiMetric.MenuDecorColor = PSP_COLOR_YELLOW;
  UiMetric.DialogFogColor = COLOR(0, 0, 0, 88);
  UiMetric.TitlePadding = 4;
  UiMetric.TitleColor = PSP_COLOR_WHITE;
  UiMetric.MenuFps = 30;
  UiMetric.TabBgColor = COLOR(0x58,0xe8,0xe8,0xff); 
  UiMetric.BrowserScreenshotPath = ScreenshotPath;
  UiMetric.BrowserScreenshotDelay = 30;

  return 1;
}

void DisplayMenu()
{
  int i;
  pl_menu_item *item;

  /* Menu loop */
  do
  {
    ResumeEmulation = 0;

    /* Set normal clock frequency */
    pl_psp_set_clock_freq(222);
    /* Set buttons to autorepeat */
    pspCtrlSetPollingMode(PSP_CTRL_AUTOREPEAT);

    do
    {
      /* Display appropriate tab */
      switch (TabIndex)
      {
      case TAB_STATE:
        DisplayStateTab();
        break;
      case TAB_CONTROL:
        /* Load current button mappings */
        for (item = ControlUiMenu.Menu.items, i = 0; item; item = item->next, i++)
          pl_menu_select_option_by_value(item, (void*)ActiveConfig.ButtonMap[i]);
        pspUiOpenMenu(&ControlUiMenu, NULL);
        break;
      case TAB_QUICKLOAD:
        pspUiOpenBrowser(&QuickloadBrowser,
                        (GAME_LOADED) ? CURRENT_GAME : GamePath);
        break;
      case TAB_SYSTEM:
        item = pl_menu_find_item_by_id(&SystemUiMenu.Menu, SYSTEM_COLORS);
        pl_menu_select_option_by_value(item, (void*)Options.ColorScheme);

        pspUiOpenMenu(&SystemUiMenu, NULL);
        break;
      case TAB_OPTION:
        /* Init menu options */
        item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_DISPLAY_MODE);
        pl_menu_select_option_by_value(item, (void*)Options.DisplayMode);
        item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_SYNC_FREQ);
        pl_menu_select_option_by_value(item, (void*)Options.UpdateFreq);
//        item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_FRAMESKIP);
//        pl_menu_select_option_by_value(item, (void*)(int)Options.Frameskip);
        item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_VSYNC);
        pl_menu_select_option_by_value(item, (void*)Options.VSync);
        item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_CLOCK_FREQ);
        pl_menu_select_option_by_value(item, (void*)Options.ClockFreq);
        item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_SHOW_FPS);
        pl_menu_select_option_by_value(item, (void*)Options.ShowFps);
        item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_CONTROL_MODE);
        pl_menu_select_option_by_value(item, (void*)Options.ControlMode);
        item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_ANIMATE);
        pl_menu_select_option_by_value(item, (void*)UiMetric.Animate);
        item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_AUTOFIRE);
        pl_menu_select_option_by_value(item, (void*)Options.AutoFire);
        pspUiOpenMenu(&OptionUiMenu, NULL);
        break;
      case TAB_ABOUT:
        pspUiSplashScreen(&SplashScreen);
        break;
      }
    } while (!ExitPSP && !ResumeEmulation);

    if (!ExitPSP)
    {
      /* Set clock frequency during emulation */
      pl_psp_set_clock_freq(Options.ClockFreq);
      /* Set buttons to normal mode */
      pspCtrlSetPollingMode(PSP_CTRL_NORMAL);

      /* Resume emulation */
      if (ResumeEmulation)
      {
        supervision_set_color_scheme(Options.ColorScheme);

        if (UiMetric.Animate) pspUiFadeout();
        RunEmulator();
        if (UiMetric.Animate) pspUiFadeout();
      }
    }
  } while (!ExitPSP);
}

void TrashMenu()
{
  TrashEmulator();

  if (rom_buffer)
    free(rom_buffer);

  /* Save options */
  SaveOptions();

  /* Trash menus */
  pl_menu_destroy(&SystemUiMenu.Menu);
  pl_menu_destroy(&OptionUiMenu.Menu);
  pl_menu_destroy(&ControlUiMenu.Menu);
  pl_menu_destroy(&SaveStateGallery.Menu);

  /* Trash images */
  if (Background) pspImageDestroy(Background);
  if (NoSaveIcon) pspImageDestroy(NoSaveIcon);
}

static int psp_load_rom(const char *path)
{
  uint file_size = 0;

  if (pl_file_is_of_type(path, "ZIP"))
  {
    char archived_file[512];
    unzFile zipfile = NULL;
    unz_global_info gi;
    unz_file_info fi;

    /* Open archive for reading */
    if (!(zipfile = unzOpen(path)))
      return 0;

    /* Get global ZIP file information */
    if (unzGetGlobalInfo(zipfile, &gi) != UNZ_OK)
    {
      unzClose(zipfile);
      return 0;
    }

    const char *extension;
    int i, j;

    for (i = 0; i < (int)gi.number_entry; i++)
    {
      /* Get name of the archived file */
      if (unzGetCurrentFileInfo(zipfile, &fi, archived_file, 
          sizeof(archived_file), NULL, 0, NULL, 0) != UNZ_OK)
      {
        unzClose(zipfile);
        return 0;
      }

      extension = pl_file_get_extension(archived_file);
      for (j = 1; QuickloadFilter[j]; j++)
      {
        if (strcasecmp(QuickloadFilter[j], extension) == 0)
        {
          file_size = fi.uncompressed_size;

          /* Open archived file for reading */
          if(unzOpenCurrentFile(zipfile) != UNZ_OK)
          {
            unzClose(zipfile);
            return 0;
          }

          if (rom_buffer)
          {
            free(rom_buffer);
            rom_buffer = NULL;
          }

          if (!(rom_buffer = (uint8*)malloc(file_size)))
          {
            unzCloseCurrentFile(zipfile);
            unzClose(zipfile); 
            return 0;
          }

          unzReadCurrentFile(zipfile, rom_buffer, file_size);
          unzCloseCurrentFile(zipfile);
          unzClose(zipfile);
          goto done;
        }
      }

      /* Go to the next file in the archive */
      if (i + 1 < (int)gi.number_entry)
      {
        if (unzGoToNextFile(zipfile) != UNZ_OK)
        {
          unzClose(zipfile);
          return 0;
        }
      }
    }

    unzClose(zipfile);
    return 0; /* no valid files in archive */
  }
  else
  {
	  FILE*	file;
	  if (!(file = fopen(path,"rb")))
      return 0;

    if (rom_buffer)
    {
      free(rom_buffer);
      rom_buffer = NULL;
    }

	  fseek(file,0,SEEK_END);
	  file_size = (uint)ftell(file);
	  fseek(file, 0, SEEK_SET);

	  if (!(rom_buffer = (uint8 *)malloc(file_size)))
    {
      fclose(file);
      return 0;
    }

	  fread(rom_buffer,1,file_size,file);
	  fclose(file);
  }

done:
	supervision_load(rom_buffer, file_size);
  SET_AS_CURRENT_GAME(path);

	return 1;
}

/* Save options */
static int SaveOptions()
{
  pl_file_path path;
  snprintf(path, sizeof(path) - 1, "%soptions.ini", pl_psp_get_app_directory());

  /* Initialize INI structure */
  pl_ini_file init;
  pl_ini_create(&init);

  /* Set values */
  pl_ini_set_int(&init, "Video", "Display Mode", Options.DisplayMode);
  pl_ini_set_int(&init, "Video", "Update Frequency", Options.UpdateFreq);
  pl_ini_set_int(&init, "Video", "Frameskip", Options.Frameskip);
  pl_ini_set_int(&init, "Video", "VSync", Options.VSync);
  pl_ini_set_int(&init, "Video", "PSP Clock Frequency",Options.ClockFreq);
  pl_ini_set_int(&init, "Video", "Show FPS", Options.ShowFps);
  pl_ini_set_int(&init, "Menu", "Control Mode", Options.ControlMode);
  pl_ini_set_int(&init, "Menu", "Animate", UiMetric.Animate);
  pl_ini_set_int(&init, "Input", "Autofire", Options.AutoFire);
  pl_ini_set_string(&init, "File", "Game Path", GamePath);

  pl_ini_set_int(&init, "System", "Color Scheme", Options.ColorScheme);

  /* Save INI file */
  int status = pl_ini_save(&init, path);

  /* Clean up */
  pl_ini_destroy(&init);

  return status;
}

/* Load options */
static void LoadOptions()
{
  pl_file_path path;
  snprintf(path, sizeof(path) - 1, "%soptions.ini", pl_psp_get_app_directory());

  /* Initialize INI structure */
  pl_ini_file init;
  pl_ini_load(&init, path);

  /* Load values */
  Options.DisplayMode = pl_ini_get_int(&init, "Video", "Display Mode", DISPLAY_MODE_UNSCALED);
  Options.UpdateFreq = pl_ini_get_int(&init, "Video", "Update Frequency", 60);
  Options.Frameskip = pl_ini_get_int(&init, "Video", "Frameskip", 0);
  Options.VSync = pl_ini_get_int(&init, "Video", "VSync", 0);
  Options.ClockFreq = pl_ini_get_int(&init, "Video", "PSP Clock Frequency", 222);
  Options.ShowFps = pl_ini_get_int(&init, "Video", "Show FPS", 0);
  Options.ControlMode = pl_ini_get_int(&init, "Menu", "Control Mode", 0);
  UiMetric.Animate = pl_ini_get_int(&init, "Menu", "Animate", 1);
  Options.AutoFire = pl_ini_get_int(&init, "Input", "Autofire", 2);
  pl_ini_get_string(&init, "File", "Game Path", NULL, GamePath, sizeof(GamePath));

  Options.ColorScheme = pl_ini_get_int(&init, "System", "Color Scheme", SV_COLOR_SCHEME_DEFAULT);

  /* Clean up */
  pl_ini_destroy(&init);
}

static int OnGenericCancel(const void *uiobject, const void* param)
{
  if (!GAME_LOADED) 
    return 0;

  ResumeEmulation = 1;
  return 1;
}

static void OnSplashRender(const void *splash, const void *null)
{
  int fh, i, x, y, height;
  const char *lines[] = 
  { 
    PSP_APP_NAME" version "PSP_APP_VER" ("__DATE__")",
    "\026http://psp.akop.org/potator",
    " ",
    "2009 Akop Karapetyan",
    "     David Raingeard",
    NULL
  };

  fh = pspFontGetLineHeight(UiMetric.Font);

  for (i = 0; lines[i]; i++);
  height = fh * (i - 1);

  /* Render lines */
  for (i = 0, y = SCR_HEIGHT / 2 - height / 2; lines[i]; i++, y += fh)
  {
    x = SCR_WIDTH / 2 - pspFontGetTextWidth(UiMetric.Font, lines[i]) / 2;
    pspVideoPrint(UiMetric.Font, x, y, lines[i], PSP_COLOR_GRAY);
  }

  /* Render PSP status */
  OnGenericRender(splash, null);
}

static int OnSplashButtonPress(const struct PspUiSplash *splash, 
  u32 button_mask)
{
  return OnGenericButtonPress(NULL, NULL, button_mask);
}

/* Handles drawing of generic items */
static void OnGenericRender(const void *uiobject, const void *item_obj)
{
  /* Draw tabs */
  int height = pspFontGetLineHeight(UiMetric.Font);
  int width;
  int i, x;
  for (i = 0, x = 5; i <= TAB_MAX; i++, x += width + 10)
  {
    width = -10;

    if (!GAME_LOADED && (i == TAB_STATE || i == TAB_SYSTEM))
      continue;

    /* Determine width of text */
    width = pspFontGetTextWidth(UiMetric.Font, TabLabel[i]);

    /* Draw background of active tab */
    if (i == TabIndex)
      pspVideoFillRect(x - 5, 0, x + width + 5, height + 1, UiMetric.TabBgColor);

    /* Draw name of tab */
    pspVideoPrint(UiMetric.Font, x, 0, TabLabel[i], PSP_COLOR_WHITE);
  }
}

static int OnGenericButtonPress(const PspUiFileBrowser *browser, 
  const char *path, u32 button_mask)
{
  int tab_index;

  /* If L or R are pressed, switch tabs */
  if (button_mask & PSP_CTRL_LTRIGGER)
  {
    TabIndex--;
    do
    {
      tab_index = TabIndex;
      if (!GAME_LOADED && (TabIndex == TAB_STATE || TabIndex == TAB_SYSTEM)) TabIndex--;
      if (TabIndex < 0) TabIndex = TAB_MAX;
    } while (tab_index != TabIndex);
  }
  else if (button_mask & PSP_CTRL_RTRIGGER)
  {
    TabIndex++;
    do
    {
      tab_index = TabIndex;
      if (!GAME_LOADED && (TabIndex == TAB_STATE || TabIndex == TAB_SYSTEM)) TabIndex++;
      if (TabIndex > TAB_MAX) TabIndex = 0;
    } while (tab_index != TabIndex);
  }
  else if ((button_mask & (PSP_CTRL_START | PSP_CTRL_SELECT)) 
    == (PSP_CTRL_START | PSP_CTRL_SELECT))
  {
    if (pl_util_save_vram_seq(ScreenshotPath, "ui"))
      pspUiAlert("Saved successfully");
    else
      pspUiAlert("ERROR: Not saved");
    return 0;
  }
  else return 0;

  return 1;
}

static int OnMenuItemChanged(const struct PspUiMenu *uimenu, 
  pl_menu_item* item, const pl_menu_option* option)
{
  if (uimenu == &ControlUiMenu)
  {
    ActiveConfig.ButtonMap[item->id] = (unsigned int)option->value;
  }
  else
  {
    switch(item->id)
    {
    case OPTION_DISPLAY_MODE:
      Options.DisplayMode = (int)option->value;
      break;
    case OPTION_SYNC_FREQ:
      Options.UpdateFreq = (int)option->value;
      break;
//    case OPTION_FRAMESKIP:
//      Options.Frameskip = (int)option->value;
//      break;
    case OPTION_VSYNC:
      Options.VSync = (int)option->value;
      break;
    case OPTION_CLOCK_FREQ:
      Options.ClockFreq = (int)option->value;
      break;
    case OPTION_SHOW_FPS:
      Options.ShowFps = (int)option->value;
      break;
    case OPTION_CONTROL_MODE:
      Options.ControlMode = (int)option->value;
      UiMetric.OkButton = (!(int)option->value) ? PSP_CTRL_CROSS
        : PSP_CTRL_CIRCLE;
      UiMetric.CancelButton = (!(int)option->value) ? PSP_CTRL_CIRCLE
        : PSP_CTRL_CROSS;
      break;
    case OPTION_ANIMATE:
      UiMetric.Animate = (int)option->value;
      break;
    case OPTION_AUTOFIRE:
      Options.AutoFire = (int)option->value;
      break;
    case SYSTEM_COLORS:
      Options.ColorScheme = (int)option->value;
      break;
    }
  }

  return 1;
}

static int OnMenuOk(const void *uimenu, const void* sel_item)
{
  if (uimenu == &ControlUiMenu)
  {
    /* Save to MS */
    if (SaveButtonConfig())
      pspUiAlert("Changes saved");
    else
      pspUiAlert("ERROR: Changes not saved");
  }
  else
  {
    switch (((const pl_menu_item*)sel_item)->id)
    {
    case SYSTEM_RESET:

      /* Reset system */
      if (pspUiConfirm("Reset the system?"))
      {
        ResumeEmulation = 1;
        supervision_reset();
        return 1;
      }
      break;

    case SYSTEM_SCRNSHOT:

      /* Save screenshot */
      if (!pl_util_save_image_seq(ScreenshotPath,
                                  pl_file_get_filename(CURRENT_GAME),
                                  Screen))
        pspUiAlert("ERROR: Screenshot not saved");
      else
        pspUiAlert("Screenshot saved successfully");
      break;
    }
  }

  return 0;
}

static int OnMenuButtonPress(const struct PspUiMenu *uimenu, 
  pl_menu_item* sel_item, 
  u32 button_mask)
{
  if (uimenu == &ControlUiMenu)
  {
    if (button_mask & PSP_CTRL_TRIANGLE)
    {
      pl_menu_item *item;
      int i;

      /* Load default mapping */
      InitButtonConfig();

      /* Modify the menu */
      for (item = ControlUiMenu.Menu.items, i = 0; item; item = item->next, i++)
        pl_menu_select_option_by_value(item, (void*)DefaultConfig.ButtonMap[i]);

      return 0;
    }
  }

  return OnGenericButtonPress(NULL, NULL, button_mask);
}

static int OnQuickloadOk(const void *browser, const void *path)
{
  if (!psp_load_rom((char*)path))
  {
    pspUiAlert("Error loading cartridge");
    return 0;
  }

  SET_AS_CURRENT_GAME((char*)path);
  pl_file_get_parent_directory((const char*)path,
                               GamePath,
                               sizeof(GamePath));

  /* Reset selected state */
  SaveStateGallery.Menu.selected = NULL;

  ResumeEmulation = 1;
  return 1;
}

static int OnSaveStateOk(const void *gallery, const void *item)
{
  if (!GAME_LOADED) 
    return 0;

  char *path;
  const char *config_name = pl_file_get_filename(CURRENT_GAME);

  path = (char*)malloc(strlen(SaveStatePath) + strlen(config_name) + 8);
  sprintf(path, "%s%s.s%02i", SaveStatePath, config_name,
    ((const pl_menu_item*)item)->id);

  if (pl_file_exists(path) && pspUiConfirm("Load state?"))
  {
    if (LoadState(path))
    {
      ResumeEmulation = 1;
      pl_menu_find_item_by_id(&(((const PspUiGallery*)gallery)->Menu),
        ((const pl_menu_item*)item)->id);
      free(path);

      return 1;
    }
    pspUiAlert("ERROR: State failed to load");
  }

  free(path);
  return 0;
}

static int OnSaveStateButtonPress(const PspUiGallery *gallery, 
      pl_menu_item *sel, u32 button_mask)
{
  if (!GAME_LOADED) 
    return 0;

  if (button_mask & PSP_CTRL_SQUARE 
    || button_mask & PSP_CTRL_TRIANGLE)
  {
    char caption[32];
    char *path;
    const char *config_name = pl_file_get_filename(CURRENT_GAME);

    path = (char*)malloc(strlen(SaveStatePath) + strlen(config_name) + 8);
    sprintf(path, "%s%s.s%02i", SaveStatePath, config_name, sel->id);

    do /* not a real loop; flow control construct */
    {
      if (button_mask & PSP_CTRL_SQUARE)
      {
        if (pl_file_exists(path) && !pspUiConfirm("Overwrite existing state?"))
          break;

        pspUiFlashMessage("Saving, please wait ...");

        PspImage *icon;
        if (!(icon = SaveState(path, Screen)))
        {
          pspUiAlert("ERROR: State not saved");
          break;
        }

        SceIoStat stat;

        /* Trash the old icon (if any) */
        if (sel->param && sel->param != NoSaveIcon)
          pspImageDestroy((PspImage*)sel->param);

        /* Update icon, help text */
        sel->param = icon;
        pl_menu_set_item_help_text(sel, PresentSlotText);

        /* Get file modification time/date */
        if (sceIoGetstat(path, &stat) < 0)
          sprintf(caption, "ERROR");
        else
          sprintf(caption, "%02i/%02i/%02i %02i:%02i", 
            stat.st_mtime.month,
            stat.st_mtime.day,
            stat.st_mtime.year - (stat.st_mtime.year / 100) * 100,
            stat.st_mtime.hour,
            stat.st_mtime.minute);

        pl_menu_set_item_caption(sel, caption);
      }
      else if (button_mask & PSP_CTRL_TRIANGLE)
      {
        if (!pl_file_exists(path) || !pspUiConfirm("Delete state?"))
          break;

        if (!pl_file_rm(path))
        {
          pspUiAlert("ERROR: State not deleted");
          break;
        }

        /* Trash the old icon (if any) */
        if (sel->param && sel->param != NoSaveIcon)
          pspImageDestroy((PspImage*)sel->param);

        /* Update icon, caption */
        sel->param = NoSaveIcon;
        pl_menu_set_item_help_text(sel, EmptySlotText);
        pl_menu_set_item_caption(sel, "Empty");
      }
    } while (0);

    if (path) free(path);
    return 0;
  }

  return OnGenericButtonPress(NULL, NULL, button_mask);
}

/* Handles any special drawing for the system menu */
static void OnSystemRender(const void *uiobject, const void *item_obj)
{
  int w, h, x, y;
  w = Screen->Viewport.Width;
  h = Screen->Viewport.Height;
  x = SCR_WIDTH - w - 8;
  y = SCR_HEIGHT - h - 80;

  /* Draw a small representation of the screen */
  pspVideoShadowRect(x, y, x + w - 1, y + h - 1, PSP_COLOR_BLACK, 3);
  sceGuDisable(GU_BLEND);
  pspVideoPutImage(Screen, x, y, w, h);
  sceGuEnable(GU_BLEND);
  pspVideoDrawRect(x, y, x + w - 1, y + h - 1, PSP_COLOR_GRAY);

  OnGenericRender(uiobject, item_obj);
}

/* Load state icon */
static PspImage* LoadStateIcon(const char *path)
{
  /* Open file for reading */
  FILE *f = fopen(path, "r");
  if (!f) return NULL;

  /* Load image */
  PspImage *image = pspImageLoadPngFd(f);
  fclose(f);

  return image;
}

/* Load state */
static int LoadState(const char *path)
{
  /* Open file for reading */
  FILE *f = fopen(path, "r");
  if (!f) return 0;

  /* Load image into temporary object */
  PspImage *image = pspImageLoadPngFd(f);
  pspImageDestroy(image);

  int status = 0;
  status = supervision_load_state(path, 0);
  fclose(f);

  return status;
}

/* Save state */
static PspImage* SaveState(const char *path, PspImage *icon)
{
  /* Open file for writing */
  FILE *f;
  if (!(f = fopen(path, "w")))
    return NULL;

  /* Create thumbnail */
  PspImage *thumb;
  thumb = (icon->Viewport.Width < 200)
    ? pspImageCreateCopy(icon) : pspImageCreateThumbnail(icon);
  if (!thumb) { fclose(f); return NULL; }

  /* Write the thumbnail */
  if (!pspImageSavePngFd(f, thumb))
  {
    pspImageDestroy(thumb);
    fclose(f);
    return NULL;
  }

  /* Save state */
  if (!supervision_save_state(path, 0))
  {
    pspImageDestroy(thumb);
    thumb = NULL;
  }

  fclose(f);
  return thumb;
}

/* Initialize game configuration */
static void InitButtonConfig()
{
  memcpy(&ActiveConfig, &DefaultConfig, sizeof(struct ButtonConfig));
}

/* Load game configuration */
static int LoadButtonConfig()
{
  pl_file_path path;
  snprintf(path, sizeof(path) - 1, "%sbuttons.cnf",
           pl_psp_get_app_directory());

  /* Open file for reading */
  FILE *file = fopen(path, "r");

  /* If no configuration, load defaults */
  if (!file)
  {
    InitButtonConfig();
    return 1;
  }

  /* Read contents of struct */
  int nread = fread(&ActiveConfig, sizeof(struct ButtonConfig), 1, file);
  fclose(file);

  if (nread != 1)
  {
    InitButtonConfig();
    return 0;
  }

  return 1;
}

/* Save game configuration */
static int SaveButtonConfig()
{
  pl_file_path path;
  snprintf(path, sizeof(path) - 1, "%sbuttons.cnf", pl_psp_get_app_directory());

  /* Open file for writing */
  FILE *file = fopen(path, "w");
  if (!file) return 0;

  /* Write contents of struct */
  int nwritten = fwrite(&ActiveConfig, sizeof(struct ButtonConfig), 1, file);
  fclose(file);

  return (nwritten == 1);
}

static void DisplayStateTab()
{
  if (!GAME_LOADED) 
    return;

  pl_menu_item *item, *sel;
  SceIoStat stat;
  char caption[32];
  ScePspDateTime latest;

  const char *config_name = pl_file_get_filename(CURRENT_GAME);
  char *path = (char*)malloc(strlen(SaveStatePath) + strlen(config_name) + 8);
  char *game_name = strdup(config_name);
  char *dot = strrchr(game_name, '.');
  if (dot) *dot='\0';

  memset(&latest, 0, sizeof(latest));

  /* Initialize icons */
  for (item = SaveStateGallery.Menu.items; item; item = item->next)
  {
    sprintf(path, "%s%s.s%02i", SaveStatePath, config_name, item->id);

    if (pl_file_exists(path))
    {
      if (sceIoGetstat(path, &stat) < 0)
        sprintf(caption, "ERROR");
      else
      {
        /* Determine the latest save state */
        if (pl_util_date_compare(&latest, &stat.st_mtime) < 0)
        {
          sel = item;
          latest = stat.st_mtime;
        }

        sprintf(caption, "%02i/%02i/%02i %02i:%02i",
          stat.st_mtime.month,
          stat.st_mtime.day,
          stat.st_mtime.year - (stat.st_mtime.year / 100) * 100,
          stat.st_mtime.hour,
          stat.st_mtime.minute);
      }

      pl_menu_set_item_caption(item, caption);
      item->param = LoadStateIcon(path);
      pl_menu_set_item_help_text(item, PresentSlotText);
    }
    else
    {
      pl_menu_set_item_caption(item, "Empty");
      item->param = NoSaveIcon;
      pl_menu_set_item_help_text(item, EmptySlotText);
    }
  }

  free(path);

  /* Highlight the latest save state if none are selected */
  if (SaveStateGallery.Menu.selected == NULL)
    SaveStateGallery.Menu.selected = sel;

  pspUiOpenGallery(&SaveStateGallery, game_name);
  free(game_name);

  /* Destroy any icons */
  for (item = SaveStateGallery.Menu.items; item; item = item->next)
    if (item->param != NULL && item->param != NoSaveIcon)
      pspImageDestroy((PspImage*)item->param);
}

