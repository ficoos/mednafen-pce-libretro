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

#include "../mednafen.h"
#include "surface.h"

MDFN_PixelFormat::MDFN_PixelFormat()
{
 bpp = 0;
 colorspace = 0;

 Rshift = 0;
 Gshift = 0;
 Bshift = 0;
 Ashift = 0;

 Rprec = 0;
 Gprec = 0;
 Bprec = 0;
 Aprec = 0;
}

MDFN_PixelFormat::MDFN_PixelFormat(const unsigned int p_colorspace, const uint8 p_rs, const uint8 p_gs, const uint8 p_bs, const uint8 p_as)
{
 bpp = 16;
 colorspace = p_colorspace;

 Rshift = p_rs;
 Gshift = p_gs;
 Bshift = p_bs;
 Ashift = p_as;

 Rprec = 8;
 Gprec = 8;
 Bprec = 8;
 Aprec = 8;
}

MDFN_Surface::MDFN_Surface(void *const p_pixels, const uint32 p_width, const uint32 p_height, const uint32 p_pitchinpix, const MDFN_PixelFormat &nf)
{
 Init(p_pixels, p_width, p_height, p_pitchinpix, nf);
}

void MDFN_Surface::Init(void *const p_pixels, const uint32 p_width, const uint32 p_height, const uint32 p_pitchinpix, const MDFN_PixelFormat &nf)
{
 void *rpix = NULL;
 assert(nf.bpp == 16 || nf.bpp == 32);

 format = nf;

 if(nf.bpp == 16)
 {
  assert(nf.Rprec && nf.Gprec && nf.Bprec && nf.Aprec);
 }
 else
 {
  assert((nf.Rshift + nf.Gshift + nf.Bshift + nf.Ashift) == 48);
  assert(!((nf.Rshift | nf.Gshift | nf.Bshift | nf.Ashift) & 0x7));

  format.Rprec = 8;
  format.Gprec = 8;
  format.Bprec = 8;
  format.Aprec = 8;
 }

 pixels16 = NULL;
 pixels = NULL;

 pixels_is_external = false;

 if(p_pixels)
 {
  rpix = p_pixels;
  pixels_is_external = true;
 }
 else
 {
  if(!(rpix = calloc(1, p_pitchinpix * p_height * (nf.bpp / 8))))
   throw(1);
 }

 if(nf.bpp == 16)
  pixels16 = (uint16 *)rpix;
 else
  pixels = (uint32 *)rpix;

 w = p_width;
 h = p_height;

 pitchinpix = p_pitchinpix;
}

void MDFN_Surface::SetFormat(const MDFN_PixelFormat &nf, bool convert)
{
 if(nf.bpp == 16)
 {

 }
 else
 {
  assert((nf.Rshift + nf.Gshift + nf.Bshift + nf.Ashift) == 48);
  assert(!((nf.Rshift | nf.Gshift | nf.Bshift | nf.Ashift) & 0x7));
 }
 
 if(nf.bpp != format.bpp)
 {
  void *rpix = calloc(1, pitchinpix * h * (nf.bpp / 8));
  void *oldpix;

  if(nf.bpp == 16)	// 32bpp to 16bpp
  {
   pixels16 = (uint16 *)rpix;

   oldpix = pixels;
   pixels = NULL;
  }
  else			// 16bpp to 32bpp
  {
   pixels = (uint32 *)rpix;

   oldpix = pixels16;
   pixels16 = NULL;
  }
  if(oldpix && !pixels_is_external)
   free(oldpix);

  pixels_is_external = false;
 }

 format = nf;
}

void MDFN_Surface::Fill(uint8 r, uint8 g, uint8 b, uint8 a)
{
 uint32 color = MakeColor(r, g, b, a);

  for(int32 i = 0; i < pitchinpix * h; i++)
   pixels16[i] = color;
}

MDFN_Surface::~MDFN_Surface()
{
 if(!pixels_is_external)
 {
  if(pixels)
   free(pixels);
  if(pixels16)
   free(pixels16);
 }
}

