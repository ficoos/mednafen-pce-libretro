/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2012 - Hans-Kristian Arntzen
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
 * FILES TO EXCLUDE:
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

/* misc */
#ifndef _WIN32
#include <pthread.h>
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
#ifndef __CELLOS_LV2__
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
