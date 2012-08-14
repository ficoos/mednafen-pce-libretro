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

#include <string.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <string>
#include <map>
#include "include/trio/trio.h"

#include "general.h"
#include "state.h"

#include "md5.h"

using namespace std;

static string BaseDirectory;
static string FileBase;
static string FileExt;	/* Includes the . character, as in ".nes" */
static string FileBaseDirectory;

void MDFNI_SetBaseDirectory(const char *dir)
{
 BaseDirectory = string(dir);
}

// Really dumb, maybe we should use boost?
static bool IsAbsolutePath(const char *path)
{
 #if PSS_STYLE==4
  if(path[0] == ':')
 #elif PSS_STYLE==1
  if(path[0] == '/')
 #else
  if(path[0] == '\\'
  #if PSS_STYLE!=3
   || path[0] == '/'
  #endif
 )
 #endif
 {
  return(TRUE);
 }

 // FIXME if we add DOS support(HAHAHAHA).
 #if defined(WIN32)
 if((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z'))
  if(path[1] == ':')
  {
   return(TRUE);
  }
 #endif

 return(FALSE);
}

static bool IsAbsolutePath(const std::string &path)
{
 return(IsAbsolutePath(path.c_str()));
}

bool MDFN_IsFIROPSafe(const std::string &path)
{
 // We could make this more OS-specific, but it shouldn't hurt to try to weed out usage of characters that are path
 // separators in one OS but not in another, and we'd also run more of a risk of missing a special path separator case
 // in some OS.

 if(!MDFN_GetSettingB("filesys.untrusted_fip_check"))
  return(true);

 if(path.find('\0') != string::npos)
  return(false);

 if(path.find(':') != string::npos)
  return(false);

 if(path.find('\\') != string::npos)
  return(false);

 if(path.find('/') != string::npos)
  return(false);

 return(true);
}

void MDFN_GetFilePathComponents(const std::string &file_path, std::string *dir_path_out, std::string *file_base_out, std::string *file_ext_out)
{
 size_t final_ds;		// in file_path
 string file_name;
 size_t fn_final_dot;		// in local var file_name
 // Temporary output:
 string dir_path, file_base, file_ext;

#if PSS_STYLE==4
 final_ds = file_path.find_last_of(':');
#elif PSS_STYLE==1
 final_ds = file_path.find_last_of('/');
#else
 final_ds = file_path.find_last_of('\\');

 #if PSS_STYLE!=3
  {
   size_t alt_final_ds = file_path.find_last_of('/');

   if(final_ds == string::npos || (alt_final_ds != string::npos && alt_final_ds > final_ds))
    final_ds = alt_final_ds;
  }
 #endif
#endif

 if(final_ds == string::npos)
 {
  dir_path = string(".");
  file_name = file_path;
 }
 else
 {
  dir_path = file_path.substr(0, final_ds);
  file_name = file_path.substr(final_ds + 1);
 }

 fn_final_dot = file_name.find_last_of('.');

 if(fn_final_dot != string::npos)
 {
  file_base = file_name.substr(0, fn_final_dot);
  file_ext = file_name.substr(fn_final_dot);
 }
 else
 {
  file_base = file_name;
  file_ext = string("");
 }

 if(dir_path_out)
  *dir_path_out = dir_path;

 if(file_base_out)
  *file_base_out = file_base;

 if(file_ext_out)
  *file_ext_out = file_ext;
}

std::string MDFN_EvalFIP(const std::string &dir_path, const std::string &rel_path, bool skip_safety_check)
{
 if(!skip_safety_check && !MDFN_IsFIROPSafe(rel_path))
  throw MDFN_Error(0, _("Referenced path \"%s\" is potentially unsafe.  See \"filesys.untrusted_fip_check\" setting.\n"), rel_path.c_str());

 if(IsAbsolutePath(rel_path.c_str()))
  return(rel_path);
 else
 {
  return(dir_path + std::string(PSS) + rel_path);
 }
}


typedef std::map<char, std::string> FSMap;

static std::string EvalPathFS(const std::string &fstring, /*const (won't work because entry created if char doesn't exist) */ FSMap &fmap)
{
 std::string ret = "";
 const char *str = fstring.c_str();
 bool in_spec = false;

 while(*str)
 {
  int c = *str;

  if(!in_spec && c == '%')
   in_spec = true;
  else if(in_spec == true)
  {
   if(c == '%')
    ret = ret + std::string("%");
   else
    ret = ret + fmap[(char)c];
   in_spec = false;
  }
  else
  {
   char ct[2];
   ct[0] = c;
   ct[1] = 0;
   ret += std::string(ct);
  }

  str++;
 }

 return(ret);
}

#if 0
static void CreateMissingDirs(const char *path)
{
 const char *s = path;
 bool first_psep = true;
 char last_char = 0;
 const char char_test1 = '/', char_test2 = '/';


 while(*s)
 {
  if(*s == char_test1 || *s == char_test2)
  {
   if(last_char != *s)	//char_test1 && last_char != char_test2)
   {
    if(!first_psep)
    {
     char tmpbuf[(s - path) + 1];
     tmpbuf[s - path] = 0;
     strncpy(tmpbuf, path, s - path);

     puts(tmpbuf);
     //MDFN_mkdir(tmpbuf, S_IRWXU);
    }
   }

   first_psep = false;
  }
  last_char = *s;
  s++;
 }
}
#endif

extern char g_rom_dir[1024];
extern char g_basename[1024];

std::string MDFN_MakeFName(MakeFName_Type type, int id1, const char *cd1)
{
 char tmp_path[4096];
 char numtmp[64];
 struct stat tmpstat;

#ifdef _WIN32
 const char *slash = "\\";
#else
 const char *slash= "/";
#endif

 //printf("%s\n", EvalPathFS(std::string("%f.%m.sav"), fmap).c_str());

 switch(type)
 {
  default: tmp_path[0] = 0;
	   break;

  case MDFNMKF_STATE:
  case MDFNMKF_SAV:
		     {
                      trio_snprintf(tmp_path, sizeof(tmp_path), "%s%s%s.sav", g_rom_dir, slash, g_basename);
                      fprintf(stderr, "Savestate name: %s\n", tmp_path);
                      return tmp_path;
	             }
  case MDFNMKF_AUX: if(IsAbsolutePath(cd1))
		     trio_snprintf(tmp_path, 4096, "%s", (char *)cd1);
		    else
		     trio_snprintf(tmp_path, 4096, "%s"PSS"%s", FileBaseDirectory.c_str(), (char *)cd1);
		    break;

  case MDFNMKF_IPS:  trio_snprintf(tmp_path, 4096, "%s"PSS"%s%s.ips", FileBaseDirectory.c_str(), FileBase.c_str(), FileExt.c_str());
                     break;

  case MDFNMKF_FIRMWARE:
		    {
		     std::string overpath = MDFN_GetSettingS("filesys.path_firmware");

		     if(IsAbsolutePath(cd1))
		     {
		      trio_snprintf(tmp_path, 4096, "%s", cd1);
		     }
		     else
		     {
		      if(IsAbsolutePath(overpath))
                       trio_snprintf(tmp_path, 4096, "%s"PSS"%s",overpath.c_str(), cd1);
                      else
		      {
                       trio_snprintf(tmp_path, 4096, "%s"PSS"%s"PSS"%s", BaseDirectory.c_str(), overpath.c_str(), cd1);

		       // For backwards-compatibility with < 0.9.0
		       if(stat(tmp_path,&tmpstat) == -1)
                        trio_snprintf(tmp_path, 4096, "%s"PSS"%s", BaseDirectory.c_str(), cd1);
		      }
		     }
		    }
		    break;

  case MDFNMKF_PALETTE:
		      {
                      char tmp_path[4096];
                      trio_snprintf(tmp_path, sizeof(tmp_path), "%s%s%s.pal", g_rom_dir, slash, g_basename);
                      return tmp_path;
		      }
                      break;
 }
 return(tmp_path);
}

const char * GetFNComponent(const char *str)
{
 const char *tp1;

 #if PSS_STYLE==4
     tp1=((char *)strrchr(str,':'));
 #elif PSS_STYLE==1
     tp1=((char *)strrchr(str,'/'));
 #else
     tp1=((char *)strrchr(str,'\\'));
  #if PSS_STYLE!=3
  {
     const char *tp3;
     tp3=((char *)strrchr(str,'/'));
     if(tp1<tp3) tp1=tp3;
  }
  #endif
 #endif

 if(tp1)
  return(tp1+1);
 else
  return(str);
}

void GetFileBase(const char *f)
{
        const char *tp1,*tp3;

 #if PSS_STYLE==4
     tp1=((char *)strrchr(f,':'));
 #elif PSS_STYLE==1
     tp1=((char *)strrchr(f,'/'));
 #else
     tp1=((char *)strrchr(f,'\\'));
  #if PSS_STYLE!=3
     tp3=((char *)strrchr(f,'/'));
     if(tp1<tp3) tp1=tp3;
  #endif
 #endif
     if(!tp1)
     {
      tp1=f;
      FileBaseDirectory = ".";
     }
     else
     {
      char tmpfn[256];

      memcpy(tmpfn,f,tp1-f);
      tmpfn[tp1-f]=0;
      FileBaseDirectory = string(tmpfn);

      tp1++;
     }

     if(((tp3=strrchr(f,'.'))!=NULL) && (tp3>tp1))
     {
      char tmpbase[256];

      memcpy(tmpbase,tp1,tp3-tp1);
      tmpbase[tp3-tp1]=0;
      FileBase = string(tmpbase);
      FileExt = string(tp3);
     }
     else
     {
      FileBase = string(tp1);
      FileExt = "";
     }
}

char *MDFN_RemoveControlChars(char *str)
{
 char *orig = str;
 if(str)
  while(*str)
  {
   if(*str < 0x20) *str = 0x20;
   str++;
  }
 return(orig);
}

// Remove whitespace from beginning of string
void MDFN_ltrim(char *string)
{
 int32 di, si;
 bool InWhitespace = TRUE;

 di = si = 0;

 while(string[si])
 {
  if(InWhitespace && (string[si] == ' ' || string[si] == '\r' || string[si] == '\n' || string[si] == '\t' || string[si] == 0x0b))
  {

  }
  else
  {
   InWhitespace = FALSE;
   string[di] = string[si];
   di++;
  }
  si++;
 }
 string[di] = 0;
}

// Remove whitespace from end of string
void MDFN_rtrim(char *string)
{
 int32 len = strlen(string);

 if(len)
 {
  for(int32 x = len - 1; x >= 0; x--)
  {
   if(string[x] == ' ' || string[x] == '\r' || string[x] == '\n' || string[x] == '\t' || string[x] == 0x0b)
    string[x] = 0;
   else
    break;
  }
 }

}

void MDFN_trim(char *string)
{
 MDFN_rtrim(string);
 MDFN_ltrim(string);
}
