// TODO/WIP

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
#include "Stream.h"

#include "include/trio/trio.h"

Stream::Stream()
{
// line_read_skip = 256;
}

Stream::~Stream()
{

}

StreamFilter::StreamFilter()
{
 target_stream = NULL;
}

StreamFilter::StreamFilter(Stream *target_arg)
{
 target_stream = target_arg;
}

StreamFilter::~StreamFilter()
{
 if(target_stream)
  delete target_stream;
}

Stream* StreamFilter::steal(void)
{
 Stream *ret = target_stream;
 target_stream = NULL;
 return ret;
}
