#ifndef __MDFN_SURFACE_H
#define __MDFN_SURFACE_H

typedef struct
{
 int32 x, y, w, h;
} MDFN_Rect;

enum
{
 MDFN_COLORSPACE_RGB = 0,
 MDFN_COLORSPACE_YCbCr = 1,
 //MDFN_COLORSPACE_YUV = 2, // TODO, maybe.
};

class MDFN_PixelFormat
{
 public:

 MDFN_PixelFormat();
 MDFN_PixelFormat(const unsigned int p_colorspace, const uint8 p_rs, const uint8 p_gs, const uint8 p_bs, const uint8 p_as);

 unsigned int bpp;	// 32 only for now(16 wip)
 unsigned int colorspace;

 union
 {
  uint8 Rshift;  // Bit position of the lowest bit of the red component
  uint8 Yshift;
 };

 union
 {
  uint8 Gshift;  // [...] green component
  uint8 Ushift;
  uint8 Cbshift;
 };

 union
 {
  uint8 Bshift;  // [...] blue component
  uint8 Vshift;
  uint8 Crshift;
 };

 uint8 Ashift;  // [...] alpha component.

 // For 16bpp, WIP
 uint8 Rprec;
 uint8 Gprec;
 uint8 Bprec;
 uint8 Aprec;

 // Creates a color value for the surface corresponding to the 8-bit R/G/B/A color passed.
 INLINE uint32 MakeColor(uint8 r, uint8 g, uint8 b, uint8 a = 0) const
 {
    return (((r >> 3) << 10) | ((g >> 3) << 5) | ((b >> 3) << 0));
 }

 // Gets the R/G/B/A values for the passed 32-bit surface pixel value
 INLINE void DecodeColor(uint32 value, int &r, int &g, int &b, int &a) const
 {
    r = ((value & 0x1f) << 10) & 0x7fff;
    g = (((value & 0x3e0) << 5) <<5) & 0x7fff;
    b = (((value & 0x7c00) >> 10)) & 0x7fff;
 }

}; // MDFN_PixelFormat;

struct MDFN_PaletteEntry
{
 uint8 r, g, b;
};

#include <vector>
typedef std::vector<MDFN_PaletteEntry> MDFN_Palette;

// Supports 32-bit RGBA
//  16-bit is WIP
class MDFN_Surface //typedef struct
{
 public:

 MDFN_Surface(void *const p_pixels, const uint32 p_width, const uint32 p_height, const uint32 p_pitchinpix, const MDFN_PixelFormat &nf);

 ~MDFN_Surface();

 uint16 *pixels16;
 uint32 *pixels;

 bool pixels_is_external;

 // w, h, and pitch32 should always be > 0
 int32 w;
 int32 h;

 union
 {
  int32 pitch32; // In pixels, not in bytes.
  int32 pitchinpix;	// New name, new code should use this.
 };

 MDFN_PixelFormat format;

 void Fill(uint8 r, uint8 g, uint8 b, uint8 a);
 void SetFormat(const MDFN_PixelFormat &new_format, bool convert);

 // Creates a 32-bit value for the surface corresponding to the R/G/B/A color passed.
 INLINE uint32 MakeColor(uint8 r, uint8 g, uint8 b, uint8 a = 0) const
 {
    return (((r >> 3) << 10) | ((g >> 3) << 5) | ((b >> 3) << 0));
 }

 // Gets the R/G/B/A values for the passed 32-bit surface pixel value
 INLINE void DecodeColor(uint32 value, int &r, int &g, int &b, int &a) const
 {
    r = ((value & 0x1f) << 10) & 0x7fff;
    g = (((value & 0x3e0) << 5) <<5) & 0x7fff;
    b = (((value & 0x7c00) >> 10)) & 0x7fff;
 }

 INLINE void DecodeColor(uint32 value, int &r, int &g, int &b) const
 {
  int dummy_a;

  r = ((value & 0x1f) << 10) & 0x7fff;
  g = (((value & 0x3e0) << 5) <<5) & 0x7fff;
  b = (((value & 0x7c00) >> 10)) & 0x7fff;
 }
 private:
 void Init(void *const p_pixels, const uint32 p_width, const uint32 p_height, const uint32 p_pitchinpix, const MDFN_PixelFormat &nf);
};

#endif
