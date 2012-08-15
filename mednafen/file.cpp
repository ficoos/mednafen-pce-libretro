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

#include <errno.h>
#include "include/trio/trio.h"

#include "file.h"
#include "general.h"

// This function should ALWAYS close the system file "descriptor"(FILE *) it's given,
// even if it errors out.
bool MDFNFILE::MakeMemWrapAndClose(FILE *tz)
{
 bool ret = false;

 location = 0;

 fseek(tz, 0, SEEK_END);
 f_size = ftell(tz);
 fseek(tz, 0, SEEK_SET);

 if(!(f_data = (uint8*)MDFN_malloc(f_size, _("file read buffer"))))
  goto doret;

 if((int64)fread(f_data, 1, f_size, tz) != f_size)
 {
  ErrnoHolder ene(errno);
  MDFN_PrintError(_("Error reading file: %s"), ene.StrError());

  free(f_data);
  goto doret;
 }

 ret = true;

 doret:
  fclose(tz);

 return ret;
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
 if(!Open(path))
  throw(MDFN_Error(0, "TODO ERROR"));
}


MDFNFILE::~MDFNFILE()
{
 Close();
}


bool MDFNFILE::Open(const char *path)
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

  MDFN_PrintError(_("Error opening \"%s\": %s"), path, ene.StrError());

  return 0;
 }

 fseek(fp, 0, SEEK_SET);

 if(!MakeMemWrapAndClose(fp))
   return 0;

 const char *ld = strrchr(path, '.');
 f_ext = strdup(ld ? ld + 1 : "");

 error_code = 0;

 return true;
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

 return 1;
}

#include <vector>

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
   return 0;
  }

  for(unsigned int i = 0; i < pearpairs.size(); i++)
  {
   const void *data = pearpairs[i].data;
   const uint64 length = pearpairs[i].length;

   if(fwrite(data, 1, length, fp) != length)
   {
    ErrnoHolder ene(errno);

    MDFN_PrintError(_("Error writing to \"%s\": %s"), filename, ene.StrError());
    fclose(fp);
    return 0;
   }
  }

  if(fclose(fp) == EOF)
  {
   ErrnoHolder ene(errno);

   MDFN_PrintError(_("Error closing \"%s\": %s"), filename, ene.StrError());
   return 0;
  }
 }
 return 1;
}
