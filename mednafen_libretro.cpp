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
 * mednafen/video/font-data.cpp
 * mednafen/video/font-data-12x13.c
 * mednafen/video/font-data-18x18.c
 * mednafen/video/text.cpp
 * mednafen/player.cpp
 * mednafen/video/video.cpp
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

/* misc threading/timer includes */
#ifndef _WIN32
#include <pthread.h>
#endif

#ifdef __CELLOS_LV2__
#include <sys/timer.h>
#endif

#include <unistd.h>

// libretro includes
#include "libretro-mednafen/includes/trio/trio.h"
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
void MDFN_DebugPrintReal(const char *file, const int line, const char *format, ...) { base_printf(format); }

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

MDFNGI *MDFNI_LoadGame(const char *force_module, const char *name)
{
        MDFNFILE GameFile;
	struct stat stat_buf;
	std::vector<FileExtensionSpecStruct> valid_iae;

	if(strlen(name) > 4 && (!strcasecmp(name + strlen(name) - 4, ".cue") || !strcasecmp(name + strlen(name) - 4, ".toc") || !strcasecmp(name + strlen(name) - 4, ".m3u")))
	{
	 return(MDFNI_LoadCD(force_module, name));
	}
	
	if(!stat(name, &stat_buf) && !S_ISREG(stat_buf.st_mode))
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

