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
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <errno.h>
#include "include/trio/trio.h"

#include "file.h"
#include "general.h"

static const int64 MaxROMImageSize = (int64)1 << 26; // 2 ^ 26 = 64MiB

enum
{
 MDFN_FILETYPE_PLAIN = 0,
 MDFN_FILETYPE_GZIP = 1,
 MDFN_FILETYPE_ZIP = 2,
};

// This function should ALWAYS close the system file "descriptor"(gzip library, zip library, or FILE *) it's given,
// even if it errors out.
bool MDFNFILE::MakeMemWrapAndClose(FILE *tz)
{
 bool ret = FALSE;

 location = 0;

 {
  ::fseek(tz, 0, SEEK_END);
  f_size = ::ftell(tz);
  ::fseek(tz, 0, SEEK_SET);

  if(f_size > MaxROMImageSize)
  {
   MDFN_PrintError(_("ROM image is too large; maximum size allowed is %llu bytes."), (unsigned long long)MaxROMImageSize);
   goto doret;
  }

   if(!(f_data = (uint8*)MDFN_malloc(f_size, _("file read buffer"))))
   {
    goto doret;
   }
   if((int64)::fread(f_data, 1, f_size, tz) != f_size)
   {
    ErrnoHolder ene(errno);
    MDFN_PrintError(_("Error reading file: %s"), ene.StrError());

    free(f_data);
    goto doret;
   }
 }

 ret = TRUE;

 doret:
  fclose(tz);

 return(ret);
}

MDFNFILE::MDFNFILE()
{
 f_data = NULL;
 f_size = 0;
 f_ext = NULL;

 location = 0;

}

MDFNFILE::MDFNFILE(const char *path)
{
 if(!Open(path, false))
 {
  throw(MDFN_Error(0, "TODO ERROR"));
 }
}


MDFNFILE::~MDFNFILE()
{
 Close();
}


bool MDFNFILE::Open(const char *path, const bool suppress_notfound_pe)
{
 local_errno = 0;
 error_code = MDFNFILE_EC_OTHER;	// Set to 0 at the end if the function succeeds.

 FILE *fp;

 if(!(fp = fopen(path, "rb")))
 {
  ErrnoHolder ene(errno);
  local_errno = ene.Errno();

  if(ene.Errno() == ENOENT)
  {
   local_errno = ene.Errno();
   error_code = MDFNFILE_EC_NOTFOUND;
  }

  if(ene.Errno() != ENOENT || !suppress_notfound_pe)
   MDFN_PrintError(_("Error opening \"%s\": %s"), path, ene.StrError());

  return(0);
 }

 ::fseek(fp, 0, SEEK_SET);

 if(!MakeMemWrapAndClose(fp))
   return(0);

 const char *ld = strrchr(path, '.');
 f_ext = strdup(ld ? ld + 1 : "");

 error_code = 0;

 return(TRUE);
}

bool MDFNFILE::Close(void)
{
 if(f_ext)
 {
  free(f_ext);
  f_ext = NULL;
 }

 if(f_data)
 {
   free(f_data);
   f_data = NULL;
 }

 return(1);
}

uint64 MDFNFILE::fread(void *ptr, size_t element_size, size_t nmemb)
{
 uint32 total = element_size * nmemb;

 if(location >= f_size)
  return 0;

 if((location + total) > f_size)
 {
  int64 ak = f_size - location;

  memcpy((uint8*)ptr, f_data + location, ak);

  location = f_size;

  return(ak / element_size);
 }
 else
 {
  memcpy((uint8*)ptr, f_data + location, total);

  location += total;

  return nmemb;
 }
}

int MDFNFILE::fseek(int64 offset, int whence)
{
  switch(whence)
  {
   case SEEK_SET:if(offset >= f_size)
                  return(-1);
                 location = offset;
		 break;

   case SEEK_CUR:if((offset + location) > f_size)
                  return(-1);

                 location += offset;
                 break;
  }    
  return 0;
}

bool MDFN_DumpToFile(const char *filename, int compress, const void *data, uint64 length)
{
 std::vector<PtrLengthPair> pearpairs;
 pearpairs.push_back(PtrLengthPair(data, length));

 compress = 0;

 {
  FILE *fp = fopen(filename, "wb");
  if(!fp)
  {
   ErrnoHolder ene(errno);

   MDFN_PrintError(_("Error opening \"%s\": %s"), filename, ene.StrError());
   return(0);
  }

  for(unsigned int i = 0; i < pearpairs.size(); i++)
  {
   const void *data = pearpairs[i].GetData();
   const uint64 length = pearpairs[i].GetLength();

   if(fwrite(data, 1, length, fp) != length)
   {
    ErrnoHolder ene(errno);

    MDFN_PrintError(_("Error writing to \"%s\": %s"), filename, ene.StrError());
    fclose(fp);
    return(0);
   }
  }

  if(fclose(fp) == EOF)
  {
   ErrnoHolder ene(errno);

   MDFN_PrintError(_("Error closing \"%s\": %s"), filename, ene.StrError());
   return(0);
  }
 }
 return(1);
}
