#include "../mednafen.h"
#include "../git.h"
#include "../general.h"
#include "libretro.h"
#include <stdarg.h>

#if defined(__CELLOS_LV2__)
#include <sys/timer.h>
#include <unistd.h>
#elif defined(_WIN32) && !defined(_XBOX)
#include <windows.h>
#elif defined(_XBOX) 
#include <xtl.h>
#else
#include <unistd.h>
#endif

#include "thread.h"

#if defined(WANT_PCE_EMU)
#define WIDTH 680
#define HEIGHT 480
#elif defined(WANT_PCE_FAST_EMU)
#define WIDTH 512
#define HEIGHT 242
#endif

static MDFNGI *game;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static MDFN_Surface *surf;
static char g_rom_dir[1024];

#ifdef _MSC_VER
static unsigned short mednafen_buf[WIDTH * HEIGHT];
#else
static unsigned short mednafen_buf[WIDTH * HEIGHT] __attribute__((aligned(16)));
#endif

#define base_printf(format) \
{ \
 char msg[256]; \
 va_list ap; \
 va_start(ap,format); \
 \
 vsnprintf(msg, sizeof(msg), format, ap); \
 fprintf(stderr, msg); \
 \
 va_end(ap); \
}

void MDFND_PrintError(const char* err)         { fprintf(stderr, err); }
void MDFND_Message(const char *str)            { fprintf(stderr, str); }
void MDFND_DispMessage(unsigned char *str) { fprintf(stderr, "%s\n", str); }

void MDFND_MidSync(const EmulateSpecStruct*) {}

void MDFND_SetStateStatus(StateStatusStruct *status) {}

/*============================================================
	THREADING
============================================================ */

void MDFND_Sleep(unsigned int time)
{
#if defined(__CELLOS_LV2__)
   sys_timer_usleep(time * 1000);
#elif defined(_WIN32)
   Sleep(time);
#else
   usleep(time * 1000);
#endif
}

MDFN_Thread *MDFND_CreateThread(int (*fn)(void *), void *data)
{
   return (MDFN_Thread*)sthread_create((void (*)(void*))fn, data);
}

void MDFND_WaitThread(MDFN_Thread *thr, int *val)
{
   sthread_join((sthread_t*)thr);

   if (val)
   {
      *val = 0;
      //std::cerr << "WaitThread relies on return value." << std::endl;
   }
}

void MDFND_KillThread(MDFN_Thread *thr)
{
   //std::cerr << "Killing a thread is a BAD IDEA!" << std::endl;
}

MDFN_Mutex *MDFND_CreateMutex()
{
   return (MDFN_Mutex*)slock_new();
}

void MDFND_DestroyMutex(MDFN_Mutex *lock)
{
   slock_free((slock_t*)lock);
}

int MDFND_LockMutex(MDFN_Mutex *lock)
{
   slock_lock((slock_t*)lock);
   return 0;
}

int MDFND_UnlockMutex(MDFN_Mutex *lock)
{
   slock_unlock((slock_t*)lock);
   return 0;
}

static void extract_basename(char *buf, const char *path, size_t size)
{
   const char *base = strrchr(path, '/');
   if (!base)
      base = strrchr(path, '\\');
   if (!base)
      base = path;

   if (*base == '\\' || *base == '/')
      base++;

   strncpy(buf, base, size - 1);
   buf[size - 1] = '\0';

   char *ext = strrchr(buf, '.');
   if (ext)
      *ext = '\0';
}

static void extract_directory(char *buf, const char *path, size_t size)
{
   strncpy(buf, path, size - 1);
   buf[size - 1] = '\0';

   char *base = strrchr(buf, '/');
   if (!base)
      base = strrchr(buf, '\\');

   if (base)
      *base = '\0';
   else
      buf[0] = '\0';
}

void retro_init()
{
   MDFN_PixelFormat pix_fmt(MDFN_COLORSPACE_RGB, 16, 8, 0, 24);
   surf = new MDFN_Surface(mednafen_buf, WIDTH, HEIGHT, WIDTH, pix_fmt);

   std::vector<MDFNGI*> ext;
   MDFNI_InitializeModules(ext);
}

void retro_deinit()
{
   delete surf;
   surf = NULL;
}

void retro_reset()
{
   MDFNI_Reset();
}

bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t)
{
   return false;
}

bool retro_load_game(const struct retro_game_info *info)
{
   extract_directory(g_rom_dir, info->path, sizeof(g_rom_dir));

   std::vector<MDFNSetting> settings;
   MDFNI_Initialize(g_rom_dir, settings);

#ifdef WANT_PCE_FAST_EMU
   game = MDFNI_LoadGame("pce_fast", info->path);
#else
   game = MDFNI_LoadGame("pce", info->path);
#endif
   return game;
}

void retro_unload_game()
{
   MDFNI_CloseGame();
}

// See mednafen/[core]/input/gamepad.cpp

static void update_input (void)
{
   static uint8_t input_buf_p1;
   static uint8_t input_buf_p2;
   input_buf_p1 = input_buf_p2 = 0;
   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_Y,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_X,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_L2,
   };

   for (unsigned i = 0; i < 13; i++)
   {
      input_buf_p1 |= map[i] != -1u &&
         input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;
      input_buf_p2 |= map[i] != -1u &&
         input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;
   }

   // Possible endian bug ...
   MDFNI_SetInput(0, "gamepad", &input_buf_p1, 0);
   MDFNI_SetInput(1, "gamepad", &input_buf_p2, 0);
}

void retro_run()
{
   input_poll_cb();

   update_input();

   static int16_t sound_buf[0x10000];
   static MDFN_Rect rects[HEIGHT];

   EmulateSpecStruct spec = {0}; 
   spec.surface = surf;
   spec.SoundRate = 44100;
   spec.SoundBuf = sound_buf;
   spec.LineWidths = rects;
   spec.SoundBufMaxSize = sizeof(sound_buf) / 2;
   spec.SoundVolume = 1.0;
   spec.soundmultiplier = 1.0;

   MDFNI_Emulate(&spec);

#ifdef WANT_PCE_FAST_EMU
   unsigned width = rects->w;
   unsigned height = rects->h;
#else
   unsigned width = 320;
   unsigned height = 240;
#endif

   const uint16_t *pix = surf->pixels16;
   video_cb(pix, width, height, WIDTH << 1);

   audio_batch_cb(spec.SoundBuf, spec.SoundBufSize);
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
#ifdef WANT_PCE_FAST_EMU
   info->library_name     = "Mednafen PCE FAST";
#else
   info->library_name     = "Mednafen PCE";
#endif
   info->library_version  = "v0.9.24";
   info->need_fullpath    = true;
   info->valid_extensions = "pce|PCE|cue|CUE";
   info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   // Just assume NTSC for now. TODO: Verify FPS.
   info->timing.fps            = 59.97;
   info->timing.sample_rate    = 44100;
   info->geometry.base_width   = game->nominal_width;
   info->geometry.base_height  = game->nominal_height;
   info->geometry.max_width    = WIDTH;
   info->geometry.max_height   = HEIGHT;
   info->geometry.aspect_ratio = 4.0 / 3.0;
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

// TODO: Allow for different kinds of joypads?
void retro_set_controller_port_device(unsigned, unsigned)
{}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *, size_t)
{
   return false;
}

bool retro_unserialize(const void *, size_t)
{
   return false;
}

void *retro_get_memory_data(unsigned)
{
   return NULL;
}

size_t retro_get_memory_size(unsigned)
{
   return 0;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned, bool, const char *)
{}

void retro_shutdown(void)
{
   environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}
