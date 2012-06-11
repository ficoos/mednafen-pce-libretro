/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2012 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2012 - Daniel De Matteis
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Mednafen libretro wrapper - gluecode between Mednafen and libretro
 *
 * FILES TO EXCLUDE / WHAT THIS WRAPPER REPLACES:
 * mednafen/error.cpp
 * mednafen/memory.cpp
 * mednafen/mempatcher.cpp
 * mednafen/video/font-data.cpp
 * mednafen/video/font-data-12x13.c
 * mednafen/video/font-data-18x18.c
 * mednafen/video/text.cpp
 * mednafen/player.cpp
 * mednafen/video/video.cpp
 */

// Mednafen includes
#include "mednafen/mednafen-types.h"
#include "mednafen/mednafen.h"
#include "mednafen/video/surface.h"

// C includes
#include <stdio.h>
#include <stdarg.h>

// C++ includes
#include <string>
#include <vector>

/* misc threading/timer includes */
#ifndef _WIN32
#include <pthread.h>
#endif

#ifdef __CELLOS_LV2__
#include <sys/timer.h>
#endif

#include <unistd.h>

// libretro includes
#include "thread.h"

/*============================================================
	STRING I/O
============================================================ */

static void base_printf(const char * format, ...)
{
 char msg[256];
 va_list ap;
 va_start(ap,format);

 vsnprintf(msg, sizeof(msg), format, ap);
 fprintf(stderr, msg);

 va_end(ap);
}

void MDFN_printf(const char *format, ...)      { base_printf(format);  }
void MDFN_PrintError(const char *format, ...)  { base_printf(format);  }
void MDFN_DispMessage(const char *format, ...) { base_printf(format);  }
void MDFND_Message(const char *str)            { fprintf(stderr, str); }
void MDFND_PrintError(const char* err)         { fprintf(stderr, err); }

/*============================================================
	THREADING
============================================================ */

void MDFND_Sleep(unsigned int time)
{
#ifdef __CELLOS_LV2__
   sys_timer_usleep(time * 1000);
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

/*============================================================
	ERROR
        replaces: mednafen/error.cpp
============================================================ */

#ifdef __LIBRETRO__
extern void retro_shutdown(void);

MDFN_Error::MDFN_Error() throw() { retro_shutdown(); }
#else
MDFN_Error::MDFN_Error() throw() { abort(); }
#endif

int MDFN_Error::GetErrno(void) throw() { return(errno_code); }

MDFN_Error::MDFN_Error(int errno_code_new, const char *format, ...) throw()
{
 (void)errno_code_new;
 base_printf(format);
}

MDFN_Error::~MDFN_Error() throw()
{
 if(error_message)
 {
  free(error_message);
  error_message = NULL;
 }
}

MDFN_Error::MDFN_Error(const MDFN_Error &ze_error) throw()
{
 if(ze_error.error_message)
  error_message = strdup(ze_error.error_message);
 else
  error_message = NULL;

 errno_code = ze_error.errno_code;
}

MDFN_Error& MDFN_Error::operator=(const MDFN_Error &ze_error) throw()
{
 char *new_error_message = ze_error.error_message ? strdup(ze_error.error_message) : NULL;
 int new_errno_code = ze_error.errno_code;

 if(error_message)
  free(error_message);

 error_message = new_error_message;
 errno_code = new_errno_code;

 return(*this);
}

const char * MDFN_Error::what(void) const throw()
{
 if(!error_message)
  return("Error allocating memory for the error message!");

 return(error_message);
}

static const char *srr_wrap(int ret, const char *local_strerror)
{
 if(ret == -1)
  return("ERROR IN strerror_r()!!!");

 return(local_strerror);
}

static const char *srr_wrap(const char *ret, const char *local_strerror)
{
 if(ret == NULL)
  return("ERROR IN strerror_r()!!!");

 return(ret);
}

void ErrnoHolder::SetErrno(int the_errno)
{
 local_errno = the_errno;

 if(the_errno == 0)
  local_strerror[0] = 0;
 else
 {
   strncpy(local_strerror, strerror(the_errno), 255);
   local_strerror[255] = 0;
 }
}

/*============================================================
	MEMORY
        replaces: mednafen/memory.cpp
============================================================ */

void *MDFN_calloc_real(uint32 nmemb, uint32 size, const char *purpose, const char *_file, const int _line)
{
 return calloc(nmemb, size);
}

void *MDFN_malloc_real(uint32 size, const char *purpose, const char *_file, const int _line)
{
 return malloc(size);
}

void *MDFN_realloc_real(void *ptr, uint32 size, const char *purpose, const char *_file, const int _line)
{
 return realloc(ptr, size);
}

void MDFN_free(void *ptr)
{
 free(ptr);
}

/*============================================================
	MEMORY PATCHER - STUB
        replaces: mednafen/mempatcher.cpp
============================================================ */
#include "mednafen/mempatcher.h"

std::vector<SUBCHEAT> SubCheats[8];

static void SettingChanged(const char *name) {}

MDFNSetting MDFNMP_Settings[] =
{
 { "cheats", MDFNSF_NOFLAGS, "Enable cheats.", NULL, MDFNST_BOOL, "1", NULL, NULL, NULL, SettingChanged },
 { NULL}
};

bool MDFNMP_Init(uint32 ps, uint32 numpages) { return 1; }
void MDFNMP_AddRAM(uint32 size, uint32 address, uint8 *RAM) {}
void MDFNMP_Kill(void) {}
void MDFNMP_InstallReadPatches(void) {}
void MDFNMP_RemoveReadPatches(void) {}
void MDFNMP_ApplyPeriodicCheats(void) {}
void MDFN_FlushGameCheats(int nosave) {}
void MDFN_LoadGameCheats(FILE *override) {}

/*============================================================
	TESTS
        replaces: mednafen/tests.cpp
============================================================ */

bool MDFN_RunMathTests(void) { return 1; }

/*============================================================
	MEDNAFEN
        replaces: mednafen/mednafen.cpp
============================================================ */

#ifdef HAVE_MEDNAFEN_IMPL

#ifdef WANT_NES_EMU
extern MDFNGI EmulatedNES;
#endif

#ifdef WANT_SNES_EMU
extern MDFNGI EmulatedSNES;
#endif

#ifdef WANT_GBA_EMU
extern MDFNGI EmulatedGBA;
#endif

#ifdef WANT_GB_EMU
extern MDFNGI EmulatedGB;
#endif

#ifdef WANT_LYNX_EMU
extern MDFNGI EmulatedLynx;
#endif

#ifdef WANT_MD_EMU
extern MDFNGI EmulatedMD;
#endif

#ifdef WANT_NGP_EMU
extern MDFNGI EmulatedNGP;
#endif

#ifdef WANT_PCE_EMU
extern MDFNGI EmulatedPCE;
#endif

#ifdef WANT_PCE_FAST_EMU
extern MDFNGI EmulatedPCE_Fast;
#endif

#ifdef WANT_PCFX_EMU
extern MDFNGI EmulatedPCFX;
#endif

#ifdef WANT_PSX_EMU
extern MDFNGI EmulatedPSX;
#endif

#ifdef WANT_VB_EMU
extern MDFNGI EmulatedVB;
#endif

#ifdef WANT_WSWAN_EMU
extern MDFNGI EmulatedWSwan;
#endif

extern MDFNGI EmulatedCDPlay;

std::vector<MDFNGI *> MDFNSystems;
static std::list<MDFNGI *> MDFNSystemsPrio;
static std::vector<CDIF *> CDInterfaces;	// FIXME: Cleanup on error out.

static double volume_save;
MDFNGI *MDFNGameInfo = NULL;

bool MDFNI_StartWAVRecord(const char *path, double SoundRate) { return false; }
bool MDFNI_StartAVRecord(const char *path, double SoundRate)  { return false; }
void MDFNI_StopAVRecord(void) {}
void MDFNI_StopWAVRecord(void) {}

void MDFNI_CloseGame(void)
{
 if(MDFNGameInfo)
 {
  if(MDFNGameInfo->GameType != GMT_PLAYER)
   MDFN_FlushGameCheats(0);

  MDFNGameInfo->CloseGame();
  if(MDFNGameInfo->name)
  {
   free(MDFNGameInfo->name);
   MDFNGameInfo->name=0;
  }
  MDFNMP_Kill();

  MDFNGameInfo = NULL;
  MDFN_StateEvilEnd();

  for(unsigned i = 0; i < CDInterfaces.size(); i++)
   delete CDInterfaces[i];
  CDInterfaces.clear();
 }
}

int MDFNI_NetplayStart(uint32 local_players, uint32 netmerge, const std::string &nickname, const std::string &game_key, const std::string &connect_password)
{
 return 0;
}

void MDFNI_Kill(void)
{
 MDFN_SaveSettings();
}

bool MDFNI_InitializeModules(const std::vector<MDFNGI *> &ExternalSystems)
{
 static MDFNGI *InternalSystems[] =
 {
  #ifdef WANT_NES_EMU
  &EmulatedNES,
  #endif

  #ifdef WANT_SNES_EMU
  &EmulatedSNES,
  #endif

  #ifdef WANT_GB_EMU
  &EmulatedGB,
  #endif

  #ifdef WANT_GBA_EMU
  &EmulatedGBA,
  #endif

  #ifdef WANT_PCE_EMU
  &EmulatedPCE,
  #endif

  #ifdef WANT_PCE_FAST_EMU
  &EmulatedPCE_Fast,
  #endif

  #ifdef WANT_LYNX_EMU
  &EmulatedLynx,
  #endif

  #ifdef WANT_MD_EMU
  &EmulatedMD,
  #endif

  #ifdef WANT_PCFX_EMU
  &EmulatedPCFX,
  #endif

  #ifdef WANT_NGP_EMU
  &EmulatedNGP,
  #endif

  #ifdef WANT_PSX_EMU
  &EmulatedPSX,
  #endif

  #ifdef WANT_VB_EMU
  &EmulatedVB,
  #endif

  #ifdef WANT_WSWAN_EMU
  &EmulatedWSwan,
  #endif

  &EmulatedCDPlay
 };
 std::string i_modules_string, e_modules_string;

 for(unsigned int i = 0; i < sizeof(InternalSystems) / sizeof(MDFNGI *); i++)
 {
  AddSystem(InternalSystems[i]);
  if(i)
   i_modules_string += " ";
  i_modules_string += std::string(InternalSystems[i]->shortname);
 }

 for(unsigned int i = 0; i < ExternalSystems.size(); i++)
 {
  AddSystem(ExternalSystems[i]);
  if(i)
   i_modules_string += " ";
  e_modules_string += std::string(ExternalSystems[i]->shortname);
 }

 MDFNI_printf(_("Internal emulation modules: %s\n"), i_modules_string.c_str());
 MDFNI_printf(_("External emulation modules: %s\n"), e_modules_string.c_str());


 for(unsigned int i = 0; i < MDFNSystems.size(); i++)
  MDFNSystemsPrio.push_back(MDFNSystems[i]);

 CDUtility::CDUtility_Init();

 return(1);
}

int MDFNI_Initialize(const char *basedir, const std::vector<MDFNSetting> &DriverSettings)
{
 if(!MDFN_RunMathTests())
  return(0);

 lzo_init();

 MDFNI_SetBaseDirectory(basedir);

 MDFN_InitFontData();

 // First merge all settable settings, then load the settings from the SETTINGS FILE OF DOOOOM
 MDFN_MergeSettings(MednafenSettings);
 MDFN_MergeSettings(MDFNMP_Settings);

 if(DriverSettings.size())
  MDFN_MergeSettings(DriverSettings);

 for(unsigned int x = 0; x < MDFNSystems.size(); x++)
 {
  if(MDFNSystems[x]->Settings)
   MDFN_MergeSettings(MDFNSystems[x]->Settings);
 }

 MDFN_MergeSettings(RenamedSettings);

 if(!MFDN_LoadSettings(basedir))
  return(0);

 return(1);
}

static void ProcessAudio(EmulateSpecStruct *espec)
{
 if(espec->SoundVolume != 1)
  volume_save = espec->SoundVolume;

 if(espec->SoundBuf && espec->SoundBufSize)
 {
  int16 *const SoundBuf = espec->SoundBuf + espec->SoundBufSizeALMS * MDFNGameInfo->soundchan;
  int32 SoundBufSize = espec->SoundBufSize - espec->SoundBufSizeALMS;
  const int32 SoundBufMaxSize = espec->SoundBufMaxSize - espec->SoundBufSizeALMS;

  if(volume_save != 1)
  {
   if(volume_save < 1)
   {
    int volume = (int)(16384 * volume_save);

    for(int i = 0; i < SoundBufSize * MDFNGameInfo->soundchan; i++)
     SoundBuf[i] = (SoundBuf[i] * volume) >> 14;
   }
   else
   {
    int volume = (int)(256 * volume_save);

    for(int i = 0; i < SoundBufSize * MDFNGameInfo->soundchan; i++)
    {
     int temp = ((SoundBuf[i] * volume) >> 8) + 32768;

     temp = clamp_to_u16(temp);

     SoundBuf[i] = temp - 32768;
    }
   }
  }

  // TODO: Optimize this.
  if(MDFNGameInfo->soundchan == 2 && MDFN_GetSettingB(std::string(std::string(MDFNGameInfo->shortname) + ".forcemono").c_str()))
  {
   for(int i = 0; i < SoundBufSize * MDFNGameInfo->soundchan; i += 2)
   {
    // We should use division instead of arithmetic right shift for correctness(rounding towards 0 instead of negative infinitininintinity), but I like speed.
    int32 mixed = (SoundBuf[i + 0] + SoundBuf[i + 1]) >> 1;

    SoundBuf[i + 0] =
    SoundBuf[i + 1] = mixed;
   }
  }

  espec->SoundBufSize = espec->SoundBufSizeALMS + SoundBufSize;
 } // end to:  if(espec->SoundBuf && espec->SoundBufSize)
}

void MDFN_MidSync(EmulateSpecStruct *espec)
{
 ProcessAudio(espec);

 MDFND_MidSync(espec);

 espec->SoundBufSizeALMS = espec->SoundBufSize;
 espec->MasterCyclesALMS = espec->MasterCycles;
}

void MDFNI_Emulate(EmulateSpecStruct *espec)
{
 volume_save = 1;

 assert((bool)(espec->SoundBuf != NULL) == (bool)espec->SoundRate && (bool)espec->SoundRate == (bool)espec->SoundBufMaxSize);

 espec->SoundBufSize = 0;

 espec->VideoFormatChanged = false;
 espec->SoundFormatChanged = false;

 if(memcmp(&last_pixel_format, &espec->surface->format, sizeof(MDFN_PixelFormat)))
 {
  espec->VideoFormatChanged = TRUE;

  last_pixel_format = espec->surface->format;
 }

 if(espec->SoundRate != last_sound_rate)
 {
  espec->SoundFormatChanged = true;
  last_sound_rate = espec->SoundRate;

  ff_resampler.buffer_size((espec->SoundRate / 2) * 2);
 }

 espec->NeedSoundReverse = MDFN_StateEvil(espec->NeedRewind);

 MDFNGameInfo->Emulate(espec);

 PrevInterlaced = false;

 ProcessAudio(espec);
}

void MDFN_indent(int indent) { }

void MDFNI_Power(void)       { assert(MDFNGameInfo); MDFN_QSimpleCommand(MDFN_MSC_POWER); }
void MDFNI_Reset(void)       { assert(MDFNGameInfo); MDFN_QSimpleCommand(MDFN_MSC_RESET); }
void MDFNI_ToggleDIPView(void) {}

void MDFNI_SetInput(int port, const char *type, void *ptr, uint32 ptr_len_thingy)
{
 if(MDFNGameInfo)
 {
  assert(port < 16);

  MDFNGameInfo->SetInput(port, type, ptr);
 }
}

#endif

/*============================================================
	PLAYER
        replaces: mednafen/player.cpp
============================================================ */

int Player_Init(int tsongs, const std::string &album, const std::string &artist, const std::string &copyright, const std::vector<std::string> &snames) { return 1; }
int Player_Init(int tsongs, const std::string &album, const std::string &artist, const std::string &copyright, char **snames) { return 1; }
void Player_Draw(MDFN_Surface *surface, MDFN_Rect *dr, int CurrentSong, int16_t *samples, int32_t sampcount) {}

/*============================================================
	STUBS
============================================================ */

// stubs
void MDFN_ResetMessages(void) {}
void MDFN_InitFontData(void) {}
void MDFND_DispMessage(unsigned char *str) { /* TODO */ }

void MDFND_MidSync(const EmulateSpecStruct*) {}

int MDFND_SendData(const void*, uint32) { return 0; }
int MDFND_RecvData(void *, uint32) { return 0; }
void MDFND_NetplayText(const uint8*, bool) {}
void MDFND_NetworkClose() {}

