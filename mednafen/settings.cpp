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

#include "mednafen.h"
#include <errno.h>
#include <string.h>
#include <vector>
#include <string>
#include "include/trio/trio.h"
#include <map>
#include <list>
#include "settings.h"
#include "md5.h"
#include "string/escape.h"
#include "FileWrapper.h"

#ifdef _WIN32
#include "libretro/msvc_compat.h"
#endif

#ifdef WANT_PCE_FAST_EMU
#define PCE_MODULE "pce_fast"
#else
#define PCE_MODULE "pce"
#endif

extern char g_rom_dir[1024];
extern char g_basename[1024];

uint64 MDFN_GetSettingUI(const char *name)
{
	if(!strcmp(PCE_MODULE".ocmultiplier", name))
		return 1;
	if(!strcmp(PCE_MODULE".cdspeed", name))
		return 2;
	if(!strcmp(PCE_MODULE".cdpsgvolume", name))
		return 100;
	if(!strcmp(PCE_MODULE".cddavolume", name))
		return 100;
	if(!strcmp(PCE_MODULE".adpcmvolume", name))
		return 100;
	if(!strcmp(PCE_MODULE".slstart", name))
		return 4;
	if(!strcmp(PCE_MODULE".slend", name))
		return 235;
        fprintf(stderr, "Unhandled setting UI: %s\n", name);
	assert(0);
	return 0;
}

int64 MDFN_GetSettingI(const char *name)
{
   fprintf(stderr, "Unhandled setting I: %s\n", name);
   assert(0);
   return 0;
}

double MDFN_GetSettingF(const char *name)
{
   if(!strcmp(PCE_MODULE".mouse_sensitivity", name))
	   return 0.50;
   fprintf(stderr, "unhandled setting F: %s\n", name);
   assert(0);
   return 0;
}

bool MDFN_GetSettingB(const char *name)
{
if(!strcmp("cheats", name))
		return 0;
        if(!strcmp(PCE_MODULE".input.multitap", name))
                return 0;
	if(!strcmp("filesys.disablesavegz", name))
		return 1;
	if(!strcmp(PCE_MODULE".arcadecard", name))
		return 1;
	if(!strcmp(PCE_MODULE".forcesgx", name))
		return 0;
	if(!strcmp(PCE_MODULE".nospritelimit", name))
		return 0;
	if(!strcmp(PCE_MODULE".forcemono", name))
		return 0;
	if(!strcmp(PCE_MODULE".disable_softreset", name))
		return 0;
	if(!strcmp(PCE_MODULE".adpcmlp", name))
		return 0;
	if(!strcmp(PCE_MODULE".correct_aspect", name))
		return 1;
	if(!strcmp("cdrom.lec_eval", name))
		return 1;
	if(!strcmp("filesys.untrusted_fip_check", name))
		return 0;
	fprintf(stderr, "unhandled setting B: %s\n", name);
	assert(0);
	return 0;
}

std::string MDFN_GetSettingS(const char *name)
{
#ifdef _WIN32
const char *slash = "\\";
#else
const char *slash = "/";
#endif
	if(!strcmp(PCE_MODULE".cdbios", name))
        {
                //fprintf(stderr, "%s.cdbios: %s\n", PCE_MODULE, std::string("syscard3.pce").c_str());
		return std::string("syscard3.pce");
        }
	if(!strcmp("filesys.path_firmware", name))
        {
                //fprintf(stderr, "filesys.path_firmware: %s\n", std::string(g_rom_dir).c_str());
		return std::string(g_rom_dir);
        }
	if(!strcmp("filesys.path_palette", name))
        {
                //fprintf(stderr, "filesys.path_palette: %s\n", std::string(g_rom_dir).c_str());
		return std::string(g_rom_dir);
        }
	if(!strcmp("filesys.path_sav", name))
        {
                //fprintf(stderr, "filesys.path_sav: %s\n", std::string(g_rom_dir).c_str());
		return std::string(g_rom_dir);
        }
	if(!strcmp("filesys.path_state", name))
        {
                //fprintf(stderr, "filesys.path_state: %s\n", std::string(g_rom_dir).c_str());
		return std::string(g_rom_dir);
        }
	if(!strcmp("filesys.path_cheat", name))
        {
                //fprintf(stderr, "filesys.path_cheat: %s\n", std::string(g_rom_dir).c_str());
		return std::string(g_rom_dir);
        }
	if(!strcmp("filesys.fname_state", name))
        {
                //fprintf(stderr, "filesys.fname_state: %s%s%s%s\n", std::string(g_rom_dir).c_str(), slash, std::string(g_basename).c_str(), std::string(".sav").c_str());
		return std::string(g_rom_dir) + std::string(slash) + std::string(g_basename) + std::string(".sav");
        }
	if(!strcmp("filesys.fname_sav", name))
        {
                //fprintf(stderr, "filesys.fname_sav: %s%s%s%s\n", std::string(g_rom_dir).c_str(), slash, std::string(g_basename).c_str(), std::string(".bsv").c_str());
		return std::string(g_rom_dir) + std::string(slash) + std::string(g_basename) + std::string(".bsv");
        }
	fprintf(stderr, "unhandled setting S: %s\n", name);
	assert(0);
	return 0;
}
