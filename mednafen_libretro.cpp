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
	STUBS
============================================================ */

// stubs
void MDFN_ResetMessages(void) {}
int Player_Init(int tsongs, const std::string &album, const std::string &artist, const std::string &copyright, const std::vector<std::string> &snames) { return 1; }
int Player_Init(int tsongs, const std::string &album, const std::string &artist, const std::string &copyright, char **snames) { return 1; }
void Player_Draw(MDFN_Surface *surface, MDFN_Rect *dr, int CurrentSong, int16_t *samples, int32_t sampcount) {}
void MDFN_InitFontData(void) {}
void MDFND_DispMessage(unsigned char *str) { /* TODO */ }

void MDFND_MidSync(const EmulateSpecStruct*) {}

int MDFND_SendData(const void*, uint32) { return 0; }
int MDFND_RecvData(void *, uint32) { return 0; }
void MDFND_NetplayText(const uint8*, bool) {}
void MDFND_NetworkClose() {}

