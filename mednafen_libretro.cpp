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
 * mednafen/mednafen.cpp
 * mednafen/mempatcher.cpp
 * mednafen/movie.cpp
 * mednafen/netplay.cpp
 * mednafen/video/font-data.cpp
 * mednafen/video/font-data-12x13.c
 * mednafen/video/font-data-18x18.c
 * mednafen/video/text.cpp
 * mednafen/player.cpp
 * mednafen/state.cpp
 * mednafen/video/video.cpp
 * mednafen/resampler/resample.c
 * mednafen/sound/Fir_Resampler.cpp
 */

// Mednafen includes
#include "mednafen/mednafen-types.h"
#include "mednafen/FileWrapper.h"
#include "mednafen/mednafen.h"
#include "mednafen/general.h"
#include "mednafen/md5.h"
#include "mednafen/video/surface.h"
#include "mednafen/cdrom/CDUtility.h"
#include "mednafen/cdrom/cdromif.h"
#include "mednafen/compress/minilzo.h"
#include "mednafen/clamp.h"

// C includes
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>

// C++ includes
#include <string>
#include <vector>
#include <list>
#include <map>

/* misc threading/timer includes */
#ifndef _WIN32
#include <pthread.h>
#endif

#if defined(__CELLOS_LV2__)
#include <sys/timer.h>
#include <unistd.h>
#elif defined(_WIN32) && !defined(_XBOX)
#elif defined(_XBOX) 
#include <xtl.h>
#else
#include <unistd.h>
#endif

// libretro includes
#include "libretro-mednafen/includes/trio/trio.h"
#include "thread.h"

bool MDFNnetplay = false;

/*============================================================
	STRING I/O
============================================================ */

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

void MDFN_printf(const char *format, ...) throw()      { base_printf(format);  }
void MDFN_PrintError(const char *format, ...)  throw() { base_printf(format);  }
void MDFN_DispMessage(const char *format, ...) throw() { base_printf(format);  }
void MDFND_Message(const char *str)            { fprintf(stderr, str); }
void MDFND_PrintError(const char* err)         { fprintf(stderr, err); }
void MDFN_DebugPrintReal(const char *file, const int line, const char *format, ...) { base_printf(format); }

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

static MDFNSetting_EnumList CompressorList[] =
{
 // TODO: Actual associated numerical values are not currently used.
 { "minilzo", -1, "MiniLZO" },
 { "quicklz", -1, "QuickLZ" },
 { "blz", -1, "BLZ" },

 { NULL, 0 },
};

static const char *fname_extra = gettext_noop("See fname_format.txt for more information.  Edit at your own risk.");

static MDFNSetting MednafenSettings[] =
{
  { "srwcompressor", MDFNSF_NOFLAGS, gettext_noop("Compressor to use with state rewinding"), NULL, MDFNST_ENUM, "quicklz", NULL, NULL, NULL, NULL, CompressorList },

  { "srwframes", MDFNSF_NOFLAGS, gettext_noop("Number of frames to keep states for when state rewinding is enabled."), 
	gettext_noop("WARNING: Setting this to a large value may cause excessive RAM usage in some circumstances, such as with games that stream large volumes of data off of CDs."), MDFNST_UINT, "600", "10", "99999" },

  { "filesys.untrusted_fip_check", MDFNSF_NOFLAGS, gettext_noop("Enable untrusted file-inclusion path security check."),
	gettext_noop("When this setting is set to \"1\", the default, paths to files referenced from files like CUE sheets and PSF rips are checked for certain characters that can be used in directory traversal, and if found, loading is aborted.  Set it to \"0\" if you want to allow constructs like absolute paths in CUE sheets, but only if you understand the security implications of doing so(see \"Security Issues\" section in the documentation)."), MDFNST_BOOL, "1" },
  { "filesys.path_snap", MDFNSF_NOFLAGS, gettext_noop("Path to directory for screen snapshots."), NULL, MDFNST_STRING, "snaps" },
  { "filesys.path_sav", MDFNSF_NOFLAGS, gettext_noop("Path to directory for save games and nonvolatile memory."), gettext_noop("WARNING: Do not set this path to a directory that contains Famicom Disk System disk images, or you will corrupt them when you load an FDS game and exit Mednafen."), MDFNST_STRING, "sav" },
  { "filesys.path_state", MDFNSF_NOFLAGS, gettext_noop("Path to directory for save states."), NULL, MDFNST_STRING, "mcs" },
  { "filesys.path_cheat", MDFNSF_NOFLAGS, gettext_noop("Path to directory for cheats."), NULL, MDFNST_STRING, "cheats" },
  { "filesys.path_palette", MDFNSF_NOFLAGS, gettext_noop("Path to directory for custom palettes."), NULL, MDFNST_STRING, "palettes" },
  { "filesys.path_firmware", MDFNSF_NOFLAGS, gettext_noop("Path to directory for firmware."), NULL, MDFNST_STRING, "firmware" },
  { "filesys.fname_state", MDFNSF_NOFLAGS, gettext_noop("Format string for state filename."), fname_extra, MDFNST_STRING, "%f.%M%X" /*"%F.%M%p.%x"*/ },
  { "filesys.fname_sav", MDFNSF_NOFLAGS, gettext_noop("Format string for save games filename."), gettext_noop("WARNING: %x should always be included, otherwise you run the risk of overwriting save data for games that create multiple save data files.\n\nSee fname_format.txt for more information.  Edit at your own risk."), MDFNST_STRING, "%F.%M%x" },
  { "filesys.fname_snap", MDFNSF_NOFLAGS, gettext_noop("Format string for screen snapshot filenames."), gettext_noop("WARNING: %x or %p should always be included, otherwise there will be a conflict between the numeric counter text file and the image data file.\n\nSee fname_format.txt for more information.  Edit at your own risk."), MDFNST_STRING, "%f-%p.%x" },
  { "filesys.disablesavegz", MDFNSF_NOFLAGS, gettext_noop("Disable gzip compression when saving save states and backup memory."), NULL, MDFNST_BOOL, "0" },

  { NULL }
};

static MDFNSetting RenamedSettings[] =
{
 { "path_snap", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS  , 	"filesys.path_snap"	},
 { "path_sav", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS  , 	"filesys.path_sav"	},
 { "path_state", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS  ,	"filesys.path_state"	},
 { "path_movie", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS  , 	"filesys.path_movie"	},
 { "path_cheat", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS  , 	"filesys.path_cheat"	},
 { "path_palette", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS  , 	"filesys.path_palette"	},
 { "path_firmware", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS  , "filesys.path_firmware"	},

 { "sounddriver", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS  , "sound.driver"      },
 { "sounddevice", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS  , "sound.device"      },
 { "soundrate", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS    , "sound.rate"        },
 { "soundvol", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS     , "sound.volume"      },
 { "soundbufsize", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS , "sound.buffer_time" },

 { "fs", MDFNSF_NOFLAGS, NULL, NULL, MDFNST_ALIAS              , "video.fs" },

 { NULL }
};

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

std::vector<MDFNGI *> MDFNSystems;
static std::list<MDFNGI *> MDFNSystemsPrio;
static std::vector<CDIF *> CDInterfaces;	// FIXME: Cleanup on error out.

static MDFN_PixelFormat last_pixel_format;
static double last_sound_rate;

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
 };
 std::string i_modules_string, e_modules_string;

 for(unsigned int i = 0; i < sizeof(InternalSystems) / sizeof(MDFNGI *); i++)
 {
  MDFNSystems.push_back(InternalSystems[i]);
  if(i)
   i_modules_string += " ";
  i_modules_string += std::string(InternalSystems[i]->shortname);
 }

 for(unsigned int i = 0; i < ExternalSystems.size(); i++)
 {
  MDFNSystems.push_back(ExternalSystems[i]);
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

static void ReadM3U(std::vector<std::string> &file_list, std::string path, unsigned depth = 0)
{
 std::vector<std::string> ret;
 FileWrapper m3u_file(path.c_str(), FileWrapper::MODE_READ, _("M3U CD Set"));
 std::string dir_path;
 char linebuf[2048];

 MDFN_GetFilePathComponents(path, &dir_path);

 while(m3u_file.get_line(linebuf, sizeof(linebuf)))
 {
  std::string efp;

  if(linebuf[0] == '#') continue;
  MDFN_rtrim(linebuf);
  if(linebuf[0] == 0) continue;

  efp = MDFN_EvalFIP(dir_path, std::string(linebuf));

  if(efp.size() >= 4 && efp.substr(efp.size() - 4) == ".m3u")
  {
   if(efp == path)
    throw(MDFN_Error(0, _("M3U at \"%s\" references self."), efp.c_str()));

   if(depth == 99)
    throw(MDFN_Error(0, _("M3U load recursion too deep!")));

   ReadM3U(file_list, efp, depth++);
  }
  else
   file_list.push_back(efp);
 }
}

MDFNGI *MDFNI_LoadCD(const char *force_module, const char *devicename)
{
 uint8 LayoutMD5[16];

 MDFNI_CloseGame();

 MDFN_printf(_("Loading %s...\n\n"), devicename ? devicename : _("PHYSICAL CD"));

 try
 {
  if(devicename && strlen(devicename) > 4 && !strcasecmp(devicename + strlen(devicename) - 4, ".m3u"))
  {
   std::vector<std::string> file_list;

   ReadM3U(file_list, devicename);

   for(unsigned i = 0; i < file_list.size(); i++)
   {
    CDInterfaces.push_back(new CDIF(file_list[i].c_str()));
   }

   GetFileBase(devicename);
  }
  else
  {
   CDInterfaces.push_back(new CDIF(devicename));
   if(CDInterfaces[0]->IsPhysical())
   {
    GetFileBase("cdrom");
   }
   else
    GetFileBase(devicename);
  }
 }
 catch(std::exception &e)
 {
  MDFND_PrintError(e.what());
  MDFN_PrintError(_("Error opening CD."));
  return(0);
 }

 //
 // Print out a track list for all discs.
 //
 MDFN_indent(1);
 for(unsigned i = 0; i < CDInterfaces.size(); i++)
 {
  CDUtility::TOC toc;

  CDInterfaces[i]->ReadTOC(&toc);

  MDFN_printf(_("CD %d Layout:\n"), i + 1);
  MDFN_indent(1);

  for(int32 track = toc.first_track; track <= toc.last_track; track++)
  {
   MDFN_printf(_("Track %2d, LBA: %6d  %s\n"), track, toc.tracks[track].lba, (toc.tracks[track].control & 0x4) ? "DATA" : "AUDIO");
  }

  MDFN_printf("Leadout: %6d\n", toc.tracks[100].lba);
  MDFN_indent(-1);
  MDFN_printf("\n");
 }
 MDFN_indent(-1);
 //
 //

 // Calculate layout MD5.  The system emulation LoadCD() code is free to ignore this value and calculate
 // its own, or to use it to look up a game in its database.
 {
  md5_context layout_md5;

  layout_md5.starts();

  for(unsigned i = 0; i < CDInterfaces.size(); i++)
  {
   CD_TOC toc;

   CDInterfaces[i]->ReadTOC(&toc);

   layout_md5.update_u32_as_lsb(toc.first_track);
   layout_md5.update_u32_as_lsb(toc.last_track);
   layout_md5.update_u32_as_lsb(toc.tracks[100].lba);

   for(uint32 track = toc.first_track; track <= toc.last_track; track++)
   {
    layout_md5.update_u32_as_lsb(toc.tracks[track].lba);
    layout_md5.update_u32_as_lsb(toc.tracks[track].control & 0x4);
   }
  }

  layout_md5.finish(LayoutMD5);
 }

 MDFNGameInfo = NULL;

 for(std::list<MDFNGI *>::iterator it = MDFNSystemsPrio.begin(); it != MDFNSystemsPrio.end(); it++)  //_unsigned int x = 0; x < MDFNSystems.size(); x++)
 {
	 char tmpstr[256];
	 trio_snprintf(tmpstr, 256, "%s.enable", (*it)->shortname);

	 if(force_module)
	 {
		 if(!strcmp(force_module, (*it)->shortname))
		 {
			 MDFNGameInfo = *it;
			 break;
		 }
	 }
	 else
	 {
		 // Is module enabled?
		 if(!MDFN_GetSettingB(tmpstr))
			 continue; 

		 if(!(*it)->LoadCD || !(*it)->TestMagicCD)
			 continue;

		 if((*it)->TestMagicCD(&CDInterfaces))
		 {
			 MDFNGameInfo = *it;
			 break;
		 }
	 }
 }

 if(!MDFNGameInfo)
 {
	 if(force_module)
	 {
		 MDFN_PrintError(_("Unrecognized system \"%s\"!"), force_module);
		 return(0);
	 }

	 // This code path should never be taken, thanks to "cdplay"
	 MDFN_PrintError(_("Could not find a system that supports this CD."));
	 return(0);
 }

 // This if statement will be true if force_module references a system without CDROM support.
 if(!MDFNGameInfo->LoadCD)
 {
	 MDFN_PrintError(_("Specified system \"%s\" doesn't support CDs!"), force_module);
	 return(0);
 }

 MDFN_printf(_("Using module: %s(%s)\n\n"), MDFNGameInfo->shortname, MDFNGameInfo->fullname);


 // TODO: include module name in hash
 memcpy(MDFNGameInfo->MD5, LayoutMD5, 16);

 if(!(MDFNGameInfo->LoadCD(&CDInterfaces)))
 {
	 for(unsigned i = 0; i < CDInterfaces.size(); i++)
		 delete CDInterfaces[i];
	 CDInterfaces.clear();

	 MDFNGameInfo = NULL;
	 return(0);
 }

 MDFNI_SetLayerEnableMask(~0ULL);

 MDFNSS_CheckStates();

 MDFN_ResetMessages();   // Save state, status messages, etc.

 MDFN_StateEvilBegin();


 if(MDFNGameInfo->GameType != GMT_PLAYER)
 {
	 MDFN_LoadGameCheats(NULL);
	 MDFNMP_InstallReadPatches();
 }

 last_sound_rate = -1;
 memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));

 return(MDFNGameInfo);
}

// Return FALSE on fatal error(IPS file found but couldn't be applied),
// or TRUE on success(IPS patching succeeded, or IPS file not found).
static bool LoadIPS(MDFNFILE &GameFile, const char *path)
{
 FILE *IPSFile;

 MDFN_printf(_("Applying IPS file \"%s\"...\n"), path);

 IPSFile = fopen(path, "rb");
 if(!IPSFile)
 {
  ErrnoHolder ene(errno);

  MDFN_printf(_("Failed: %s\n"), ene.StrError());

  if(ene.Errno() == ENOENT)
   return(1);
  else
  {
   MDFN_PrintError(_("Error opening IPS file: %s\n"), ene.StrError());
   return(0);
  }  
 }

 if(!GameFile.ApplyIPS(IPSFile))
 {
  fclose(IPSFile);
  return(0);
 }
 fclose(IPSFile);

 return(1);
}

#ifdef __CELLOS_LV2__
#define S_ISREG(f) (1)
#endif

MDFNGI *MDFNI_LoadGame(const char *force_module, const char *name)
{
    MDFNFILE GameFile;
	std::vector<FileExtensionSpecStruct> valid_iae;

	if(strlen(name) > 4 && (!strcasecmp(name + strlen(name) - 4, ".cue") || !strcasecmp(name + strlen(name) - 4, ".toc") || !strcasecmp(name + strlen(name) - 4, ".m3u")))
	{
	 return(MDFNI_LoadCD(force_module, name));
	}

	MDFNI_CloseGame();

	MDFNGameInfo = NULL;

	MDFN_printf(_("Loading %s...\n"),name);

	MDFN_indent(1);

        GetFileBase(name);

	// Construct a NULL-delimited list of known file extensions for MDFN_fopen()
	for(unsigned int i = 0; i < MDFNSystems.size(); i++)
	{
	 const FileExtensionSpecStruct *curexts = MDFNSystems[i]->FileExtensions;

	 // If we're forcing a module, only look for extensions corresponding to that module
	 if(force_module && strcmp(MDFNSystems[i]->shortname, force_module))
	  continue;

	 if(curexts)	
 	  while(curexts->extension && curexts->description)
	  {
	   valid_iae.push_back(*curexts);
           curexts++;
 	  }
	}
	{
	 FileExtensionSpecStruct tmpext = { NULL, NULL };
	 valid_iae.push_back(tmpext);
	}

	if(!GameFile.Open(name, &valid_iae[0], _("game")))
        {
	 MDFNGameInfo = NULL;
	 return 0;
	}
        fprintf(stderr, "GameFile.Open: Data #1: %p, f_data #2: %p, Size #3: %u\n", GameFile.data, GameFile.f_data, (unsigned)GameFile.size);

	if(!LoadIPS(GameFile, MDFN_MakeFName(MDFNMKF_IPS, 0, 0).c_str()))
	{
	 MDFNGameInfo = NULL;
         GameFile.Close();
         return(0);
	}

	MDFNGameInfo = NULL;

	for(std::list<MDFNGI *>::iterator it = MDFNSystemsPrio.begin(); it != MDFNSystemsPrio.end(); it++)  //_unsigned int x = 0; x < MDFNSystems.size(); x++)
	{
	 char tmpstr[256];
	 trio_snprintf(tmpstr, 256, "%s.enable", (*it)->shortname);

	 if(force_module)
	 {
          if(!strcmp(force_module, (*it)->shortname))
          {
	   if(!(*it)->Load)
	   {
            GameFile.Close();

	    if((*it)->LoadCD)
             MDFN_PrintError(_("Specified system only supports CD(physical, or image files, such as *.cue and *.toc) loading."));
	    else
             MDFN_PrintError(_("Specified system does not support normal file loading."));
            MDFN_indent(-1);
            MDFNGameInfo = NULL;
            return 0;
	   }
           MDFNGameInfo = *it;
           break;
          }
	 }
	 else
	 {
	  // Is module enabled?
	  if(!MDFN_GetSettingB(tmpstr))
	   continue; 

	  if(!(*it)->Load || !(*it)->TestMagic)
	   continue;

	  if((*it)->TestMagic(name, &GameFile))
	  {
	   MDFNGameInfo = *it;
	   break;
	  }
	 }
	}

        if(!MDFNGameInfo)
        {
	 GameFile.Close();

	 if(force_module)
          MDFN_PrintError(_("Unrecognized system \"%s\"!"), force_module);
	 else
          MDFN_PrintError(_("Unrecognized file format.  Sorry."));

         MDFN_indent(-1);
         MDFNGameInfo = NULL;
         return 0;
        }

	MDFN_printf(_("Using module: %s(%s)\n\n"), MDFNGameInfo->shortname, MDFNGameInfo->fullname);
	MDFN_indent(1);

	assert(MDFNGameInfo->soundchan != 0);

        MDFNGameInfo->soundrate = 0;
        MDFNGameInfo->name = NULL;
        MDFNGameInfo->rotated = 0;

        fprintf(stderr, "MDFNGameInfo->Load: Data #1: %p, f_data #2: %p, Size #3: %u\n", GameFile.data, GameFile.f_data, (unsigned)GameFile.size);
        if(MDFNGameInfo->Load(name, &GameFile) <= 0)
	{
         GameFile.Close();
         MDFN_indent(-2);
         MDFNGameInfo = NULL;
         return(0);
        }

        if(MDFNGameInfo->GameType != GMT_PLAYER)
	{
	 MDFN_LoadGameCheats(NULL);
	 MDFNMP_InstallReadPatches();
	}

	MDFNI_SetLayerEnableMask(~0ULL);

	MDFNSS_CheckStates();

	MDFN_ResetMessages();	// Save state, status messages, etc.

	MDFN_indent(-2);

	if(!MDFNGameInfo->name)
        {
         unsigned int x;
         char *tmp;

         MDFNGameInfo->name = (UTF8 *)strdup(GetFNComponent(name));

         for(x=0;x<strlen((char *)MDFNGameInfo->name);x++)
         {
          if(MDFNGameInfo->name[x] == '_')
           MDFNGameInfo->name[x] = ' ';
         }
         if((tmp = strrchr((char *)MDFNGameInfo->name, '.')))
          *tmp = 0;
        }

        MDFN_StateEvilBegin();

        last_sound_rate = -1;
        memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));

        return(MDFNGameInfo);
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

  espec->SoundBufSize = espec->SoundBufSizeALMS + SoundBufSize;
 }
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
 }

 espec->NeedSoundReverse = false;

 MDFNGameInfo->Emulate(espec);

 ProcessAudio(espec);
}

int MDFN_RawInputStateAction(StateMem *sm, int load, int data_only) { return 1; }

void MDFN_indent(int indent) { }

void MDFN_DoSimpleCommand(int cmd) { MDFNGameInfo->DoSimpleCommand(cmd); }
void MDFN_QSimpleCommand(int cmd)  { MDFN_DoSimpleCommand(cmd); }
void MDFNI_Power(void)       { assert(MDFNGameInfo); MDFN_QSimpleCommand(MDFN_MSC_POWER); }
void MDFNI_Reset(void)       { assert(MDFNGameInfo); MDFN_QSimpleCommand(MDFN_MSC_RESET); }
void MDFNI_ToggleDIPView(void) {}
void MDFNI_ToggleDIP(int which) { MDFN_QSimpleCommand(MDFN_MSC_TOGGLE_DIP0 + which); }
void MDFNI_InsertCoin(void)  { MDFN_QSimpleCommand(MDFN_MSC_INSERT_COIN); }
void MDFNI_DiskInsert(int which) { MDFN_QSimpleCommand(MDFN_MSC_INSERT_DISK0 + which); }
void MDFNI_DiskSelect() { MDFN_QSimpleCommand(MDFN_MSC_SELECT_DISK); }
void MDFNI_DiskInsert() { MDFN_QSimpleCommand(MDFN_MSC_INSERT_DISK); }
void MDFNI_DiskEject()  { MDFN_QSimpleCommand(MDFN_MSC_EJECT_DISK); }

void MDFNI_SetLayerEnableMask(uint64 mask)
{
 if(MDFNGameInfo && MDFNGameInfo->SetLayerEnableMask)
 {
  MDFNGameInfo->SetLayerEnableMask(mask);
 }
}

void MDFNI_SetInput(int port, const char *type, void *ptr, uint32 ptr_len_thingy)
{
 if(MDFNGameInfo)
  MDFNGameInfo->SetInput(port, type, ptr);
}

/*============================================================
	MOVIE
        replaces: mednafen/movie.cpp
============================================================ */

bool MDFNMOV_IsPlaying(void) { return 0; }
bool MDFNMOV_IsRecording(void) { return 0; }
void MDFNI_SelectMovie(int w) { }
void MDFNMOV_ForceRecord(StateMem *sm) { }
void MDFNMOV_RecordState(void) {}

/*============================================================
	NETPLAY
        replaces: mednafen/netplay.cpp
============================================================ */

void MDFNNET_SendState(void) {}

/*============================================================
	STATE
        replaces: mednafen/state.cpp
============================================================ */

static int SaveStateStatus[10];

#define RLSB 		MDFNSTATE_RLSB	//0x80000000

void MDFND_SetStateStatus(StateStatusStruct *status) { }

int32 smem_read(StateMem *st, void *buffer, uint32 len)
{
 if((len + st->loc) > st->len)
  return(0);

 memcpy(buffer, st->data + st->loc, len);
 st->loc += len;

 return(len);
}

int32 smem_write(StateMem *st, void *buffer, uint32 len)
{
 if((len + st->loc) > st->malloced)
 {
  uint32 newsize = (st->malloced >= 32768) ? st->malloced : (st->initial_malloc ? st->initial_malloc : 32768);

  while(newsize < (len + st->loc))
   newsize *= 2;
  st->data = (uint8 *)realloc(st->data, newsize);
  st->malloced = newsize;
 }
 memcpy(st->data + st->loc, buffer, len);
 st->loc += len;

 if(st->loc > st->len) st->len = st->loc;

 return(len);
}

int32 smem_putc(StateMem *st, int value)
{
 uint8 tmpval = value;
 if(smem_write(st, &tmpval, 1) != 1)
  return(-1);
 return(1);
}

int32 smem_tell(StateMem *st)
{
 return(st->loc);
}

int32 smem_seek(StateMem *st, uint32 offset, int whence)
{
 switch(whence)
 {
  case SEEK_SET: st->loc = offset; break;
  case SEEK_END: st->loc = st->len - offset; break;
  case SEEK_CUR: st->loc += offset; break;
 }

 if(st->loc > st->len)
 {
  st->loc = st->len;
  return(-1);
 }

 if(st->loc < 0)
 {
  st->loc = 0;
  return(-1);
 }

 return(0);
}

int smem_write32le(StateMem *st, uint32 b)
{
 uint8 s[4];
 s[0]=b;
 s[1]=b>>8;
 s[2]=b>>16;
 s[3]=b>>24;
 return((smem_write(st, s, 4)<4)?0:4);
}

int smem_read32le(StateMem *st, uint32 *b)
{
 uint8 s[4];

 if(smem_read(st, s, 4) < 4)
  return(0);

 *b = s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24);

 return(4);
}


static bool ValidateSFStructure(SFORMAT *sf)
{
 SFORMAT *saved_sf = sf;

 while(sf->size || sf->name)
 {
  SFORMAT *sub_sf = saved_sf;
  while(sub_sf->size || sub_sf->name)
  {
   if(sf != sub_sf)
   {
    if(!strncmp(sf->name, sub_sf->name, 32))
    {
     printf("Duplicate state variable name: %.32s\n", sf->name);
    }
   }
   sub_sf++;
  }

  sf++;
 }
 return(1);
}


static bool SubWrite(StateMem *st, SFORMAT *sf, int data_only, const char *name_prefix = NULL)
{
 // FIXME?  It's kind of slow, and we definitely don't want it on with state rewinding...
 if(!data_only) 
  ValidateSFStructure(sf);

 while(sf->size || sf->name)	// Size can sometimes be zero, so also check for the text name.  These two should both be zero only at the end of a struct.
 {
  if(!sf->size || !sf->v)
  {
   sf++;
   continue;
  }

  if(sf->size == (uint32)~0)		/* Link to another struct.	*/
  {
   if(!SubWrite(st, (SFORMAT *)sf->v, data_only, name_prefix))
    return(0);

   sf++;
   continue;
  }

  int32 bytesize = sf->size;

  // If we're only saving the raw data, and we come across a bool type, we save it as it is in memory, rather than converting it to
  // 1-byte.  In the SFORMAT structure, the size member for bool entries is the number of bool elements, not the total in-memory size,
  // so we adjust it here.
  if(data_only && (sf->flags & MDFNSTATE_BOOL))
  {
   bytesize *= sizeof(bool);
  }
  
  if(!data_only)
  {
   char nameo[1 + 256];
   int slen;

   slen = trio_snprintf(nameo + 1, 256, "%s%s", name_prefix ? name_prefix : "", sf->name);
   nameo[0] = slen;

   if(slen >= 255)
   {
    printf("Warning:  state variable name possibly too long: %s %s %s %d\n", sf->name, name_prefix, nameo, slen);
    slen = 255;
   }

   smem_write(st, nameo, 1 + nameo[0]);
   smem_write32le(st, bytesize);

   /* Flip the byte order... */
   if(sf->flags & MDFNSTATE_BOOL)
   {

   }
   else if(sf->flags & MDFNSTATE_RLSB64)
    Endian_A64_NE_to_LE(sf->v, bytesize / sizeof(uint64));
   else if(sf->flags & MDFNSTATE_RLSB32)
    Endian_A32_NE_to_LE(sf->v, bytesize / sizeof(uint32));
   else if(sf->flags & MDFNSTATE_RLSB16)
    Endian_A16_NE_to_LE(sf->v, bytesize / sizeof(uint16));
   else if(sf->flags & RLSB)
    Endian_V_NE_to_LE(sf->v, bytesize);
  }
    
  // Special case for the evil bool type, to convert bool to 1-byte elements.
  // Don't do it if we're only saving the raw data.
  if((sf->flags & MDFNSTATE_BOOL) && !data_only)
  {
   for(int32 bool_monster = 0; bool_monster < bytesize; bool_monster++)
   {
    uint8 tmp_bool = ((bool *)sf->v)[bool_monster];
    //printf("Bool write: %.31s\n", sf->name);
    smem_write(st, &tmp_bool, 1);
   }
  }
  else
   smem_write(st, (uint8 *)sf->v, bytesize);

  if(!data_only)
  {
   /* Now restore the original byte order. */
   if(sf->flags & MDFNSTATE_BOOL)
   {

   }
   else if(sf->flags & MDFNSTATE_RLSB64)
    Endian_A64_LE_to_NE(sf->v, bytesize / sizeof(uint64));
   else if(sf->flags & MDFNSTATE_RLSB32)
    Endian_A32_LE_to_NE(sf->v, bytesize / sizeof(uint32));
   else if(sf->flags & MDFNSTATE_RLSB16)
    Endian_A16_LE_to_NE(sf->v, bytesize / sizeof(uint16));
   else if(sf->flags & RLSB)
    Endian_V_LE_to_NE(sf->v, bytesize);
  }
  sf++; 
 }

 return(TRUE);
}

static int WriteStateChunk(StateMem *st, const char *sname, SFORMAT *sf, int data_only)
{
 int32 data_start_pos;
 int32 end_pos;

 if(!data_only)
 {
  uint8 sname_tmp[32];

  memset(sname_tmp, 0, sizeof(sname_tmp));
  strncpy((char *)sname_tmp, sname, 32);

  if(strlen(sname) > 32)
   printf("Warning: section name is too long: %s\n", sname);

  smem_write(st, sname_tmp, 32);

  smem_write32le(st, 0);                // We'll come back and write this later.
 }

 data_start_pos = smem_tell(st);

 if(!SubWrite(st, sf, data_only))
  return(0);

 end_pos = smem_tell(st);

 if(!data_only)
 {
  smem_seek(st, data_start_pos - 4, SEEK_SET);
  smem_write32le(st, end_pos - data_start_pos);
  smem_seek(st, end_pos, SEEK_SET);
 }

 return(end_pos - data_start_pos);
}

struct compare_cstr
{
 bool operator()(const char *s1, const char *s2) const
 {
  return(strcmp(s1, s2) < 0);
 }
};

typedef std::map<const char *, SFORMAT *, compare_cstr> SFMap_t;

static void MakeSFMap(SFORMAT *sf, SFMap_t &sfmap)
{
 while(sf->size || sf->name) // Size can sometimes be zero, so also check for the text name.  These two should both be zero only at the end of a struct.
 {
  if(!sf->size || !sf->v)
  {
   sf++;
   continue;
  }

  if(sf->size == (uint32)~0)            /* Link to another SFORMAT structure. */
   MakeSFMap((SFORMAT *)sf->v, sfmap);
  else
  {
   assert(sf->name);

   if(sfmap.find(sf->name) != sfmap.end())
    printf("Duplicate save state variable in internal emulator structures(CLUB THE PROGRAMMERS WITH BREADSTICKS): %s\n", sf->name);

   sfmap[sf->name] = sf;
  }

  sf++;
 }
}

// Fast raw chunk reader
static void DOReadChunk(StateMem *st, SFORMAT *sf)
{
 while(sf->size || sf->name)       // Size can sometimes be zero, so also check for the text name.  
				// These two should both be zero only at the end of a struct.
 {
  if(!sf->size || !sf->v)
  {
   sf++;
   continue;
  }

  if(sf->size == (uint32) ~0) // Link to another SFORMAT struct
  {
   DOReadChunk(st, (SFORMAT *)sf->v);
   sf++;
   continue;
  }

  int32 bytesize = sf->size;

  // Loading raw data, bool types are stored as they appear in memory, not as single bytes in the full state format.
  // In the SFORMAT structure, the size member for bool entries is the number of bool elements, not the total in-memory size,
  // so we adjust it here.
  if(sf->flags & MDFNSTATE_BOOL)
   bytesize *= sizeof(bool);

  smem_read(st, (uint8 *)sf->v, bytesize);
  sf++;
 }
}

static int ReadStateChunk(StateMem *st, SFORMAT *sf, int size, int data_only)
{
 int temp;

 if(data_only)
 {
  DOReadChunk(st, sf);
 }
 else
 {
  SFMap_t sfmap;
  SFMap_t sfmap_found;	// Used for identifying variables that are missing in the save state.

  MakeSFMap(sf, sfmap);

  temp = smem_tell(st);
  while(smem_tell(st) < (temp + size))
  {
   uint32 recorded_size;	// In bytes
   uint8 toa[1 + 256];	// Don't change to char unless cast toa[0] to unsigned to smem_read() and other places.

   if(smem_read(st, toa, 1) != 1)
   {
    puts("Unexpected EOF");
    return(0);
   }

   if(smem_read(st, toa + 1, toa[0]) != toa[0])
   {
    puts("Unexpected EOF?");
    return 0;
   }

   toa[1 + toa[0]] = 0;

   smem_read32le(st, &recorded_size);

   SFMap_t::iterator sfmit;

   sfmit = sfmap.find((char *)toa + 1);

   if(sfmit != sfmap.end())
   {
    SFORMAT *tmp = sfmit->second;
    uint32 expected_size = tmp->size;	// In bytes

    if(recorded_size != expected_size)
    {
     printf("Variable in save state wrong size: %s.  Need: %d, got: %d\n", toa + 1, expected_size, recorded_size);
     if(smem_seek(st, recorded_size, SEEK_CUR) < 0)
     {
      puts("Seek error");
      return(0);
     }
    }
    else
    {
     sfmap_found[tmp->name] = tmp;

     smem_read(st, (uint8 *)tmp->v, expected_size);

     if(tmp->flags & MDFNSTATE_BOOL)
     {
      // Converting downwards is necessary for the case of sizeof(bool) > 1
      for(int32 bool_monster = expected_size - 1; bool_monster >= 0; bool_monster--)
      {
       ((bool *)tmp->v)[bool_monster] = ((uint8 *)tmp->v)[bool_monster];
      }
     }
     if(tmp->flags & MDFNSTATE_RLSB64)
      Endian_A64_LE_to_NE(tmp->v, expected_size / sizeof(uint64));
     else if(tmp->flags & MDFNSTATE_RLSB32)
      Endian_A32_LE_to_NE(tmp->v, expected_size / sizeof(uint32));
     else if(tmp->flags & MDFNSTATE_RLSB16)
      Endian_A16_LE_to_NE(tmp->v, expected_size / sizeof(uint16));
     else if(tmp->flags & RLSB)
      Endian_V_LE_to_NE(tmp->v, expected_size);
    }
   }
   else
   {
    printf("Unknown variable in save state: %s\n", toa + 1);
    if(smem_seek(st, recorded_size, SEEK_CUR) < 0)
    {
     puts("Seek error");
     return(0);
    }
   }
  } // while(...)

  for(SFMap_t::const_iterator it = sfmap.begin(); it != sfmap.end(); it++)
  {
   if(sfmap_found.find(it->second->name) == sfmap_found.end())
   {
    printf("Variable missing from save state: %s\n", it->second->name);
   }
  }

  assert(smem_tell(st) == (temp + size));
 }
 return 1;
}

static int CurrentState = 0;
static int RecentlySavedState = -1;

/* This function is called by the game driver(NES, GB, GBA) to save a state. */
int MDFNSS_StateAction(StateMem *st, int load, int data_only, std::vector <SSDescriptor> &sections)
{
 std::vector<SSDescriptor>::iterator section;

 if(load)
 {
  if(data_only)
  {
   for(section = sections.begin(); section != sections.end(); section++)
   {
     ReadStateChunk(st, section->sf, ~0, 1);
   }
  }
  else
  {
   char sname[32];

   for(section = sections.begin(); section != sections.end(); section++)
   {
    int found = 0;
    uint32 tmp_size;
    uint32 total = 0;

    while(smem_read(st, (uint8 *)sname, 32) == 32)
    {
     if(smem_read32le(st, &tmp_size) != 4)
      return(0);

     total += tmp_size + 32 + 4;

     // Yay, we found the section
     if(!strncmp(sname, section->name, 32))
     {
      if(!ReadStateChunk(st, section->sf, tmp_size, 0))
      {
       printf("Error reading chunk: %s\n", section->name);
       return(0);
      }
      found = 1;
      break;
     } 
     else
     {
      // puts("SEEK");
      if(smem_seek(st, tmp_size, SEEK_CUR) < 0)
      {
       puts("Chunk seek failure");
       return(0);
      }
     }
    }
    if(smem_seek(st, -total, SEEK_CUR) < 0)
    {
     puts("Reverse seek error");
     return(0);
    }
    if(!found && !section->optional) // Not found.  We are sad!
    {
     printf("Section missing:  %.32s\n", section->name);
     return(0);
    }
   }
  }
 }
 else
 {
  for(section = sections.begin(); section != sections.end(); section++)
  {
   if(!WriteStateChunk(st, section->name, section->sf, data_only))
    return(0);
  }
 }

 return(1);
}

int MDFNSS_StateAction(StateMem *st, int load, int data_only, SFORMAT *sf, const char *name, bool optional)
{
 std::vector <SSDescriptor> love;

 love.push_back(SSDescriptor(sf, name, optional));
 return(MDFNSS_StateAction(st, load, data_only, love));
}

int MDFNSS_SaveSM(StateMem *st, int wantpreview, int data_only, const MDFN_Surface *surface, const MDFN_Rect *DisplayRect, const MDFN_Rect *LineWidths)
{
        static uint8 header[32]="MEDNAFENSVESTATE";
	int neowidth = 0, neoheight = 0;

	if(!data_only)
	{
         memset(header+16,0,16);
	 MDFN_en32lsb(header + 16, MEDNAFEN_VERSION_NUMERIC);
	 MDFN_en32lsb(header + 24, neowidth);
	 MDFN_en32lsb(header + 28, neoheight);
	 smem_write(st, header, 32);
	}

        // State rewinding code path hack, FIXME
        if(data_only)
        {
         if(!MDFN_RawInputStateAction(st, 0, data_only))
          return(0);
        }

	if(!MDFNGameInfo->StateAction(st, 0, data_only))
	 return(0);

	if(!data_only)
	{
	 uint32 sizy = smem_tell(st);
	 smem_seek(st, 16 + 4, SEEK_SET);
	 smem_write32le(st, sizy);
	}
	return(1);
}

int MDFNSS_Save(const char *fname, const char *suffix, const MDFN_Surface *surface, const MDFN_Rect *DisplayRect, const MDFN_Rect *LineWidths)
{
	StateMem st;

	memset(&st, 0, sizeof(StateMem));


	if(!MDFNGameInfo->StateAction)
	{
	 MDFN_DispMessage(_("Module \"%s\" doesn't support save states."), MDFNGameInfo->shortname);
	 return(0);
	}

	if(!MDFNSS_SaveSM(&st, (DisplayRect && LineWidths), 0, surface, DisplayRect, LineWidths))
	{
	 if(st.data)
	  free(st.data);
	 if(!fname && !suffix)
 	  MDFN_DispMessage(_("State %d save error."), CurrentState);
	 return(0);
	}

	if(!MDFN_DumpToFile(fname ? fname : MDFN_MakeFName(MDFNMKF_STATE,CurrentState,suffix).c_str(), 6, st.data, st.len))
	{
         SaveStateStatus[CurrentState] = 0;
	 free(st.data);

         if(!fname && !suffix)
          MDFN_DispMessage(_("State %d save error."),CurrentState);

	 return(0);
	}

	free(st.data);

	SaveStateStatus[CurrentState] = 1;
	RecentlySavedState = CurrentState;

	if(!fname && !suffix)
	 MDFN_DispMessage(_("State %d saved."),CurrentState);

	return(1);
}

// Convenience function for movie.cpp
int MDFNSS_SaveFP(gzFile fp, const MDFN_Surface *surface, const MDFN_Rect *DisplayRect, const MDFN_Rect *LineWidths)
{
 StateMem st;

 memset(&st, 0, sizeof(StateMem));

 if(!MDFNSS_SaveSM(&st, (DisplayRect && LineWidths), 0, surface, DisplayRect, LineWidths))
 {
  if(st.data)
   free(st.data);
  return(0);
 }

#if 0
 if(gzwrite(fp, st.data, st.len) != (int32)st.len)
 {
  if(st.data)
   free(st.data);
  return(0);
 }
#endif

 if(st.data)
  free(st.data);

 return(1);
}


int MDFNSS_LoadSM(StateMem *st, int haspreview, int data_only)
{
        uint8 header[32];
	uint32 stateversion;

	if(data_only)
	{
	 stateversion = MEDNAFEN_VERSION_NUMERIC;
	}
	else
	{
         smem_read(st, header, 32);
         if(memcmp(header,"MEDNAFENSVESTATE",16))
          return(0);

	 stateversion = MDFN_de32lsb(header + 16);

	 if(stateversion < 0x0600)
 	 {
	  printf("State too old: %08x\n", stateversion);
	  return(0);
	 }
	}

	if(haspreview)
        {
         uint32 width = MDFN_de32lsb(header + 24);
         uint32 height = MDFN_de32lsb(header + 28);
	 uint32 psize;

	 psize = width * height * 3;
	 smem_seek(st, psize, SEEK_CUR);	// Skip preview
 	}

	// State rewinding code path hack, FIXME
	if(data_only)
	{
	 if(!MDFN_RawInputStateAction(st, stateversion, data_only))
	  return(0);
	}

	return(MDFNGameInfo->StateAction(st, stateversion, data_only));
}

int MDFNSS_LoadFP(gzFile fp)
{
 uint8 header[32];
 StateMem st;
 
 memset(&st, 0, sizeof(StateMem));

 if(gzread(fp, header, 32) != 32)
 {
  return(0);
 }
 st.len = MDFN_de32lsb(header + 16 + 4);

 if(st.len < 32)
  return(0);

 if(!(st.data = (uint8 *)malloc(st.len)))
  return(0);

 memcpy(st.data, header, 32);
 if(gzread(fp, st.data + 32, st.len - 32) != ((int32)st.len - 32))
 {
  free(st.data);
  return(0);
 }
 if(!MDFNSS_LoadSM(&st, 1, 0))
 {
  free(st.data);
  return(0);
 }
 free(st.data);
 return(1);
}

int MDFNSS_Load(const char *fname, const char *suffix)
{
	gzFile st;

        if(!MDFNGameInfo->StateAction)
        {
         MDFN_DispMessage(_("Module \"%s\" doesn't support save states."), MDFNGameInfo->shortname);
         return(0);
        }

        if(fname)
         st=gzopen(fname, "rb");
        else
        {
         st=gzopen(MDFN_MakeFName(MDFNMKF_STATE,CurrentState,suffix).c_str(),"rb");
	}

	if(st == NULL)
	{
	 if(!fname && !suffix)
	 {
          MDFN_DispMessage(_("State %d load error."),CurrentState);
          SaveStateStatus[CurrentState]=0;
	 }
	 return(0);
	}

	if(MDFNSS_LoadFP(st))
	{
	 if(!fname && !suffix)
	 {
          SaveStateStatus[CurrentState]=1;
          MDFN_DispMessage(_("State %d loaded."),CurrentState);
          SaveStateStatus[CurrentState]=1;
	 }
	 gzclose(st);
         return(1);
        }   
        else
        {
         SaveStateStatus[CurrentState]=1;
         MDFN_DispMessage(_("State %d read error!"),CurrentState);
	 gzclose(st);
         return(0);
        }
}

void MDFNSS_CheckStates(void)
{
	time_t last_time = 0;

        if(!MDFNGameInfo->StateAction) 
         return;


	for(int ssel = 0; ssel < 10; ssel++)
        {
	 struct stat stat_buf;

	 SaveStateStatus[ssel] = 0;
	 //printf("%s\n", MDFN_MakeFName(MDFNMKF_STATE, ssel, 0).c_str());
	 if(stat(MDFN_MakeFName(MDFNMKF_STATE, ssel, 0).c_str(), &stat_buf) == 0)
	 {
	  SaveStateStatus[ssel] = 1;
	  if(stat_buf.st_mtime > last_time)
	  {
	   RecentlySavedState = ssel;
	   last_time = stat_buf.st_mtime;
 	  }
	 }
        }

	CurrentState = 0;
	MDFND_SetStateStatus(NULL);
}

void MDFNSS_GetStateInfo(const char *filename, StateStatusStruct *status) { }

void MDFNI_SelectState(int w)
{
 if(!MDFNGameInfo->StateAction) 
  return;


 if(w == -1) 
 {  
  MDFND_SetStateStatus(NULL);
  return; 
 }
 MDFNI_SelectMovie(-1);

 if(w == 666 + 1)
  CurrentState = (CurrentState + 1) % 10;
 else if(w == 666 - 1)
 {
  CurrentState--;

  if(CurrentState < 0 || CurrentState > 9)
   CurrentState = 9;
 }
 else
  CurrentState = w;

 MDFN_ResetMessages();

 StateStatusStruct *status = (StateStatusStruct*)MDFN_calloc(1, sizeof(StateStatusStruct), _("Save state status"));
 
 memcpy(status->status, SaveStateStatus, 10 * sizeof(int));

 status->current = CurrentState;
 status->recently_saved = RecentlySavedState;

 MDFND_SetStateStatus(status);
}  

void MDFNI_SaveState(const char *fname, const char *suffix, const MDFN_Surface *surface, const MDFN_Rect *DisplayRect, const MDFN_Rect *LineWidths)
{
 if(!MDFNGameInfo->StateAction) 
  return;

 MDFND_SetStateStatus(NULL);
 MDFNSS_Save(fname, suffix, surface, DisplayRect, LineWidths);
}

void MDFNI_LoadState(const char *fname, const char *suffix)
{
 if(!MDFNGameInfo->StateAction) 
  return;

 MDFND_SetStateStatus(NULL);

 /* For network play and movies, be load the state locally, and then save the state to a temporary buffer,
    and send or record that.  This ensures that if an older state is loaded that is missing some
    information expected in newer save states, desynchronization won't occur(at least not
    from this ;)).
 */
 if(MDFNSS_Load(fname, suffix))
 {
  if(MDFNnetplay)
   MDFNNET_SendState();

  if(MDFNMOV_IsRecording())
   MDFNMOV_RecordState();
 }
}

#include "compress/minilzo.h"
#include "compress/quicklz.h"
#include "compress/blz.h"

static union
{
 char qlz_scratch_compress[/*QLZ_*/SCRATCH_COMPRESS];
 char qlz_scratch_decompress[/*QLZ_*/SCRATCH_DECOMPRESS];
};

enum
{
 SRW_COMPRESSOR_MINILZO = 0,
 SRW_COMPRESSOR_QUICKLZ,
 SRW_COMPRESSOR_BLZ
};

struct StateMemPacket
{
	uint8 *data;
	uint32 compressed_len;
	uint32 uncompressed_len;

	StateMem MovieLove;
};

static int SRW_NUM = 600;
static int SRWCompressor;
static StateMemPacket *bcs;
static int32 bcspos;

void MDFN_StateEvilBegin(void) {}

bool MDFN_StateEvilIsRunning(void) { return false; }

void MDFN_StateEvilEnd(void) { }

void MDFN_StateEvilFlushMovieLove(void) { }

int MDFN_StateEvil(int rewind) { return(0); }
void MDFNI_EnableStateRewind(int enable) { }

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

