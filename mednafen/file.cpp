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

MDFNFILE::MDFNFILE()
{
 f_data = NULL;
 f_size = 0;
 f_ext = NULL;
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
 const char *ld;
 FILE *fp = fopen(path, "rb");

 if(!fp)
  goto error;

 fseek(fp, 0, SEEK_SET);
 fseek(fp, 0, SEEK_END);

 f_size = ftell(fp);
 fseek(fp, 0, SEEK_SET);

 f_data = (uint8*)malloc(f_size);

 if((int64)fread(f_data, 1, f_size, fp) != f_size)
  goto error_memwrap;

 ld = strrchr(path, '.');
 f_ext = strdup(ld ? ld + 1 : "");

 return true;

error_memwrap:
 free(f_data);
 fclose(fp);
error:
 MDFN_PrintError("Error opening file: %s\n", path);
 return false;
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

bool MDFN_DumpToFile(const char *filename, int compress, const void *data, uint64 length)
{
	FILE *fp = fopen(filename, "wb");

        if(!fp)
           goto error;

	fwrite(data, 1, length, fp);
	fclose(fp);
	return 1;

error:
        return 0;
}
