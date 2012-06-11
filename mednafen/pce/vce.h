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

#ifndef __PCE_VCE_H
#define __PCE_VCE_H

#include "../hw_cpu/huc6280/huc6280.h"
#include "../hw_video/huc6270/vdc.h"

namespace MDFN_IEN_PCE
{

//class VDC;
//#include "vdc.h"

class VCE : public HuC6280_Support
{
	public:

	VCE(bool want_sgfx, bool nospritelimit);
	~VCE();

	void SetShowHorizOS(bool show);

	int StateAction(StateMem *sm, int load, int data_only);

	void SetPixelFormat(const MDFN_PixelFormat &format);
	bool SetCustomColorMap(const uint8 *triplets, const uint32 count);	// count = 512 or 1024

	void StartFrame(MDFN_Surface *surface, MDFN_Rect *DisplayRect, MDFN_Rect *LineWidths, int skip);
	bool RunPartial(void);

	inline int GetScanlineNo(void)
	{
	 return(scanline);
	}

	virtual int32 Sync(const int32 timestamp);

	void Reset(const int32 timestamp);

	void Write(uint32 A, uint8 V);
	uint8 Read(uint32 A);

        uint8 ReadVDC(uint32 A);
        void WriteVDC(uint32 A, uint8 V);
        void WriteVDC_ST(uint32 A, uint8 V);

	void SetLayerEnableMask(uint64 mask);

        void IRQChangeCheck(void);

        bool WS_Hook(int32 vdc_cycles);


	// So wrong, but feels so...MUSHROOMY.
	// THIS IS BROKEN!
	// We need to put Sync() call before that, or bias
	// cd_event and the value to HuCPU->SetEvent by (HuCPU->timestamp - last_ts)
	INLINE void SetCDEvent(const int32 cycles)
	{
	 const int32 time_behind = HuCPU->Timestamp() - last_ts;

	 cd_event = cycles + time_behind;
	 HuCPU->SetEvent(CalcNextEvent() - time_behind);
	}

	private:

        void FixPCache(int entry);
        void SetVCECR(uint8 V);

	INLINE int32 CalcNextEvent(void)
	{
	 int32 next_event = hblank_counter;

         if(next_event > vblank_counter)
	  next_event = vblank_counter;

	 if(next_event > cd_event)
	  next_event = cd_event;

	 for(int chip = 0; chip < chip_count; chip++)
	 {
	  int fwoom = (child_event[chip] * dot_clock_ratio - clock_divider);

	  if(fwoom < 1)
	   fwoom = 1;

	  if(next_event > fwoom)
	   next_event = fwoom;
	 }

	 if(next_event < 1)
	  next_event = 1;

	 return(next_event);
	}

	int32 child_event[2];

        int32 cd_event;

	uint16 *fb;	// Pointer to the framebuffer.
	uint32 pitch32;	// Pitch(in 32-bit pixels)
	bool FrameDone;

	void (*RunHook)(int32 clocks, uint16 *pixels);
	
	// Called when the state changes, with the new state.
	void (*HBlankHook)(bool status);
	void (*VBlankHook)(bool status);

	int32 clock_divider;

	int32 scanline;
	uint16 *scanline_out_ptr;	// Pointer into fb
	int32 pixel_offset;

	int32 hblank_counter;
	int32 vblank_counter;

	bool hblank;	// TRUE if in HBLANK, FALSE if not.
	bool vblank;	// TRUE if in vblank, FALSE if not

	bool NeedSLReset;


        uint8 CR;		// Control Register
        bool lc263;     	// CR->263 line count if set, 262 if not
        bool bw;        	// CR->Black and White
        uint8 dot_clock;	// CR->Dot Clock(5, 7, or 10 MHz = 0, 1, 2/3)
	int32 dot_clock_ratio;	// CR->Dot Clock ratio cache

	int32 ws_counter;

        uint16 color_table[0x200];
        uint32 color_table_cache[0x200 * 2];	// * 2 for user layer disabling stuff.
        uint16 ctaddress;


	uint8 *CustomColorMap; // 1024 * 3
	uint32 CustomColorMapLen;        // 512 or 1024

	uint32 systemColorMap32[512], bw_systemColorMap32[512];

	int32 last_ts;

	VDC *vdc[2];

	// SuperGrafx stuff:
	bool sgfx;
	int chip_count;	// = 1 when sgfx is FALSE, = 2 when sgfx is TRUE
        uint8 priority[2];
        uint16 winwidths[2];
        uint8 st_mode;
	int32 window_counter[2];

	bool ShowHorizOS;

	bool SubTValid;
	uint16 SubTBuffer[0x100];
	uint32 SubTLUT[4096];
};


};

#endif
