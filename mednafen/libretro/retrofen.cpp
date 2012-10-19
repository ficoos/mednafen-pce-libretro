#include        "mednafen.h"

#include        <string.h>
#include	<map>
#include	<stdarg.h>
#include	<errno.h>
#include	<sys/types.h>
#include	<sys/stat.h>

#ifdef _WIN32
#include "libretro/msvc_compat.h"
#else
#include	<unistd.h>
#endif

#include	"include/trio/trio.h"

#include	"general.h"

#include	"state.h"
#include        "video.h"
#include	"file.h"
#include	"cdrom/cdromif.h"
#include	"mempatcher.h"
#include	"tests.h"
#include	"md5.h"
#include	"clamp.h"
#include	"include/Fir_Resampler.h"

#include	"cdrom/CDUtility.h"

MDFNGI *MDFNGameInfo = NULL;

static Fir_Resampler<16> ff_resampler;
static double LastSoundMultiplier;

static MDFN_PixelFormat last_pixel_format;
static double last_sound_rate;

static std::vector<CDIF *> CDInterfaces;	// FIXME: Cleanup on error out.

void MDFNI_CloseGame(void)
{
 if(MDFNGameInfo)
 {
  MDFNGameInfo->CloseGame();
  if(MDFNGameInfo->name)
  {
   free(MDFNGameInfo->name);
   MDFNGameInfo->name=0;
  }
  MDFNMP_Kill();

  MDFNGameInfo = NULL;

  for(unsigned i = 0; i < CDInterfaces.size(); i++)
   delete CDInterfaces[i];
  CDInterfaces.clear();
 }
}

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

#ifdef WANT_SMS_EMU
extern MDFNGI EmulatedSMS, EmulatedGG;
#endif

static void ReadM3U(std::vector<std::string> &file_list, std::string path, unsigned depth = 0)
{
 std::vector<std::string> ret;
 FILE *m3u_file = NULL;
 std::string dir_path;
 char linebuf[2048];

 m3u_file = fopen(path.c_str(), "r");

 MDFN_GetFilePathComponents(path, &dir_path);

 while(fgets(linebuf, sizeof(linebuf), m3u_file))
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

 fclose(m3u_file);
}

MDFNGI *MDFNI_LoadCD(const char *force_module, const char *devicename)
{
 uint8 LayoutMD5[16];

 MDFNI_CloseGame();

 LastSoundMultiplier = 1;

 MDFN_printf(_("Loading %s...\n\n"), devicename ? devicename : _("PHYSICAL CD"));

 try
 {
  if(devicename && strlen(devicename) > 4 && !strcasecmp(devicename + strlen(devicename) - 4, ".m3u"))
  {
   std::vector<std::string> file_list;

   ReadM3U(file_list, devicename);

   for(unsigned i = 0; i < file_list.size(); i++)
    CDInterfaces.push_back(new CDIF(file_list[i].c_str()));

   GetFileBase(devicename);
  }
  else
  {
   CDInterfaces.push_back(new CDIF(devicename));
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
 for(unsigned i = 0; i < CDInterfaces.size(); i++)
 {
  CDUtility::TOC toc;

  CDInterfaces[i]->ReadTOC(&toc);

  MDFN_printf(_("CD %d Layout:\n"), i + 1);

  for(int32 track = toc.first_track; track <= toc.last_track; track++)
  {
   MDFN_printf(_("Track %2d, LBA: %6d  %s\n"), track, toc.tracks[track].lba, (toc.tracks[track].control & 0x4) ? "DATA" : "AUDIO");
  }

  MDFN_printf("Leadout: %6d\n", toc.tracks[100].lba);
  MDFN_printf("\n");
 }

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

 MDFNGameInfo = &EmulatedPCE_Fast;

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

  last_sound_rate = -1;
  memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));

 return(MDFNGameInfo);
}

MDFNGI *MDFNI_LoadGame(const char *force_module, const char *name)
{
        MDFNFILE GameFile;
	struct stat stat_buf;

	if(strlen(name) > 4 && (!strcasecmp(name + strlen(name) - 4, ".cue") || !strcasecmp(name + strlen(name) - 4, ".toc") || !strcasecmp(name + strlen(name) - 4, ".m3u")))
	 return(MDFNI_LoadCD(force_module, name));
	
	MDFNI_CloseGame();

	LastSoundMultiplier = 1;

	MDFNGameInfo = &EmulatedPCE_Fast;

	MDFN_printf(_("Loading %s...\n"),name);

        GetFileBase(name);

	if(!GameFile.Open(name))
        {
	 MDFNGameInfo = NULL;
	 return 0;
	}

	MDFN_printf(_("Using module: %s(%s)\n\n"), MDFNGameInfo->shortname, MDFNGameInfo->fullname);

        MDFNGameInfo->soundrate = 0;
        MDFNGameInfo->name = NULL;
        MDFNGameInfo->rotated = 0;

        if(MDFNGameInfo->Load(name, &GameFile) <= 0)
	{
         GameFile.Close();
         MDFNGameInfo = NULL;
         return(0);
        }

	MDFNI_SetLayerEnableMask(~0ULL);

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

        last_sound_rate = -1;
        memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));

        return(MDFNGameInfo);
}

int MDFNI_Initialize(const char *basedir)
{
	MDFNI_SetBaseDirectory(basedir);

        return(1);
}

void MDFNI_Kill(void)
{
}

static double multiplier_save, volume_save;
static std::vector<int16> SoundBufPristine;

static void ProcessAudio(EmulateSpecStruct *espec)
{
 if(espec->SoundVolume != 1)
  volume_save = espec->SoundVolume;

 if(espec->soundmultiplier != 1)
  multiplier_save = espec->soundmultiplier;

 if(espec->SoundBuf && espec->SoundBufSize)
 {
  int16 *const SoundBuf = espec->SoundBuf + espec->SoundBufSizeALMS * MDFNGameInfo->soundchan;
  int32 SoundBufSize = espec->SoundBufSize - espec->SoundBufSizeALMS;
  const int32 SoundBufMaxSize = espec->SoundBufMaxSize - espec->SoundBufSizeALMS;

  if(multiplier_save != LastSoundMultiplier)
  {
   ff_resampler.time_ratio(multiplier_save, 0.9965);
   LastSoundMultiplier = multiplier_save;
  }

  if(multiplier_save != 1)
  {
    if(MDFNGameInfo->soundchan == 2)
    {
     for(int i = 0; i < SoundBufSize * 2; i++)
      ff_resampler.buffer()[i] = SoundBuf[i];
    }
    else
    {
     for(int i = 0; i < SoundBufSize; i++)
     {
      ff_resampler.buffer()[i * 2] = SoundBuf[i];
      ff_resampler.buffer()[i * 2 + 1] = 0;
     }
    }   
    ff_resampler.write(SoundBufSize * 2);

    int avail = ff_resampler.avail();
    int real_read = std::min((int)(SoundBufMaxSize * MDFNGameInfo->soundchan), avail);

    if(MDFNGameInfo->soundchan == 2)
     SoundBufSize = ff_resampler.read(SoundBuf, real_read ) >> 1;
    else
     SoundBufSize = ff_resampler.read_mono_hack(SoundBuf, real_read );

    avail -= real_read;

    if(avail > 0)
    {
     printf("ff_resampler.avail() > espec->SoundBufMaxSize * MDFNGameInfo->soundchan - %d\n", avail);
     ff_resampler.clear();
    }
  }

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
 } // end to:  if(espec->SoundBuf && espec->SoundBufSize)
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
 multiplier_save = 1;
 volume_save = 1;

 // Initialize some espec member data to zero, to catch some types of bugs.
 espec->DisplayRect.x = 0;
 espec->DisplayRect.w = 0;
 espec->DisplayRect.y = 0;
 espec->DisplayRect.h = 0;

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

  ff_resampler.buffer_size((espec->SoundRate / 2) * 2);
 }

 MDFNGameInfo->Emulate(espec);

 ProcessAudio(espec);
}

void MDFN_indent(int indent)
{
   (void)indent;
}

void MDFN_printf(const char *format, ...) throw()
{
   fprintf(stderr, "%s\n", format);
}

void MDFN_PrintError(const char *format, ...) throw()
{
 char *temp;

 va_list ap;

 va_start(ap, format);

 temp = trio_vaprintf(format, ap);
 MDFND_PrintError(temp);
 free(temp);

 va_end(ap);
}

void MDFN_DoSimpleCommand(int cmd)
{
 MDFNGameInfo->DoSimpleCommand(cmd);
}

void MDFN_QSimpleCommand(int cmd)
{
   MDFN_DoSimpleCommand(cmd);
}

void MDFNI_Reset(void)
{
 MDFN_QSimpleCommand(MDFN_MSC_RESET);
}

void MDFNI_SetLayerEnableMask(uint64 mask)
{
 if(MDFNGameInfo && MDFNGameInfo->SetLayerEnableMask)
  MDFNGameInfo->SetLayerEnableMask(mask);
}

void MDFNI_SetInput(int port, const char *type, void *ptr, uint32 ptr_len_thingy)
{
 if(MDFNGameInfo)
  MDFNGameInfo->SetInput(port, type, ptr);
}

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
                return 1;
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
	if(!strcmp(PCE_MODULE".cdbios", name))
        {
                fprintf(stderr, "%s.cdbios: %s\n", PCE_MODULE, std::string("syscard3.pce").c_str());
		return std::string("syscard3.pce");
        }
	if(!strcmp("filesys.path_firmware", name))
        {
                fprintf(stderr, "filesys.path_firmware: %s\n", std::string(g_rom_dir).c_str());
		return std::string(g_rom_dir);
        }
	if(!strcmp("filesys.path_palette", name))
        {
                fprintf(stderr, "filesys.path_palette: %s\n", std::string(g_rom_dir).c_str());
		return std::string(g_rom_dir);
        }
	if(!strcmp("filesys.path_sav", name))
        {
                fprintf(stderr, "filesys.path_sav: %s\n", std::string(g_rom_dir).c_str());
		return std::string(g_rom_dir);
        }
	if(!strcmp("filesys.path_state", name))
        {
                fprintf(stderr, "filesys.path_state: %s\n", std::string(g_rom_dir).c_str());
		return std::string(g_rom_dir);
        }
	if(!strcmp("filesys.path_cheat", name))
        {
                fprintf(stderr, "filesys.path_cheat: %s\n", std::string(g_rom_dir).c_str());
		return std::string(g_rom_dir);
        }
	if(!strcmp("filesys.fname_state", name))
        {
                fprintf(stderr, "filesys.fname_state: %s%s\n", std::string(g_basename).c_str(), std::string(".sav").c_str());
		return std::string(g_basename) + std::string(".sav");
        }
	if(!strcmp("filesys.fname_sav", name))
        {
                fprintf(stderr, "filesys.fname_sav: %s%s\n", std::string(g_basename).c_str(), std::string(".bsv").c_str());
		return std::string(g_basename) + std::string(".bsv");
        }
	fprintf(stderr, "unhandled setting S: %s\n", name);
	assert(0);
	return 0;
}

#define RLSB 		MDFNSTATE_RLSB	//0x80000000

static int32 smem_read(StateMem *st, void *buffer, uint32 len)
{
 if((len + st->loc) > st->len)
  return(0);

 memcpy(buffer, st->data + st->loc, len);
 st->loc += len;

 return(len);
}

static int32 smem_write(StateMem *st, void *buffer, uint32 len)
{
 if((len + st->loc) > st->malloced)
 {
  uint32 newsize = (st->malloced >= 32768) ? st->malloced : (st->initial_malloc ? st->initial_malloc : 32768);

  while(newsize < (len + st->loc))
   newsize *= 2;
  st->data = (uint8 *)realloc(st->data, newsize);
  st->malloced = newsize;
 }
 memcpy(st->data + st->loc, buffer, len);
 st->loc += len;

 if(st->loc > st->len) st->len = st->loc;

 return(len);
}

static int32 smem_putc(StateMem *st, int value)
{
 uint8 tmpval = value;
 if(smem_write(st, &tmpval, 1) != 1)
  return(-1);
 return(1);
}

static int32 smem_tell(StateMem *st)
{
 return(st->loc);
}

static int32 smem_seek(StateMem *st, uint32 offset, int whence)
{
 switch(whence)
 {
  case SEEK_SET: st->loc = offset; break;
  case SEEK_END: st->loc = st->len - offset; break;
  case SEEK_CUR: st->loc += offset; break;
 }

 if(st->loc > st->len)
 {
  st->loc = st->len;
  return(-1);
 }

 if(st->loc < 0)
 {
  st->loc = 0;
  return(-1);
 }

 return(0);
}

static int smem_write32le(StateMem *st, uint32 b)
{
 uint8 s[4];
 s[0]=b;
 s[1]=b>>8;
 s[2]=b>>16;
 s[3]=b>>24;
 return((smem_write(st, s, 4)<4)?0:4);
}

static int smem_read32le(StateMem *st, uint32 *b)
{
 uint8 s[4];

 if(smem_read(st, s, 4) < 4)
  return(0);

 *b = s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24);

 return(4);
}

static bool ValidateSFStructure(SFORMAT *sf)
{
 SFORMAT *saved_sf = sf;

 while(sf->size || sf->name)
 {
  SFORMAT *sub_sf = saved_sf;
  while(sub_sf->size || sub_sf->name)
  {
   if(sf != sub_sf)
   {
    if(!strncmp(sf->name, sub_sf->name, 32))
    {
     printf("Duplicate state variable name: %.32s\n", sf->name);
    }
   }
   sub_sf++;
  }

  sf++;
 }
 return(1);
}


static bool SubWrite(StateMem *st, SFORMAT *sf, int data_only, const char *name_prefix = NULL)
{
  ValidateSFStructure(sf);

 while(sf->size || sf->name)	// Size can sometimes be zero, so also check for the text name.  These two should both be zero only at the end of a struct.
 {
  if(!sf->size || !sf->v)
  {
   sf++;
   continue;
  }

  if(sf->size == (uint32)~0)		/* Link to another struct.	*/
  {
   if(!SubWrite(st, (SFORMAT *)sf->v, 0, name_prefix))
    return(0);

   sf++;
   continue;
  }

  int32 bytesize = sf->size;

  {
   char nameo[1 + 256];
   int slen;

   slen = trio_snprintf(nameo + 1, 256, "%s%s", name_prefix ? name_prefix : "", sf->name);
   nameo[0] = slen;

   if(slen >= 255)
   {
    printf("Warning:  state variable name possibly too long: %s %s %s %d\n", sf->name, name_prefix, nameo, slen);
    slen = 255;
   }

   smem_write(st, nameo, 1 + nameo[0]);
   smem_write32le(st, bytesize);

   /* Flip the byte order... */
   if(sf->flags & MDFNSTATE_BOOL)
   {

   }
   else if(sf->flags & MDFNSTATE_RLSB64)
    Endian_A64_NE_to_LE(sf->v, bytesize / sizeof(uint64));
   else if(sf->flags & MDFNSTATE_RLSB32)
    Endian_A32_NE_to_LE(sf->v, bytesize / sizeof(uint32));
   else if(sf->flags & MDFNSTATE_RLSB16)
    Endian_A16_NE_to_LE(sf->v, bytesize / sizeof(uint16));
   else if(sf->flags & RLSB)
    Endian_V_NE_to_LE(sf->v, bytesize);
  }
    
  // Special case for the evil bool type, to convert bool to 1-byte elements.
  // Don't do it if we're only saving the raw data.
  if((sf->flags & MDFNSTATE_BOOL))
  {
   for(int32 bool_monster = 0; bool_monster < bytesize; bool_monster++)
   {
    uint8 tmp_bool = ((bool *)sf->v)[bool_monster];
    //printf("Bool write: %.31s\n", sf->name);
    smem_write(st, &tmp_bool, 1);
   }
  }
  else
   smem_write(st, (uint8 *)sf->v, bytesize);

  {
   /* Now restore the original byte order. */
   if(sf->flags & MDFNSTATE_BOOL)
   {

   }
   else if(sf->flags & MDFNSTATE_RLSB64)
    Endian_A64_LE_to_NE(sf->v, bytesize / sizeof(uint64));
   else if(sf->flags & MDFNSTATE_RLSB32)
    Endian_A32_LE_to_NE(sf->v, bytesize / sizeof(uint32));
   else if(sf->flags & MDFNSTATE_RLSB16)
    Endian_A16_LE_to_NE(sf->v, bytesize / sizeof(uint16));
   else if(sf->flags & RLSB)
    Endian_V_LE_to_NE(sf->v, bytesize);
  }
  sf++; 
 }

 return(TRUE);
}

static int WriteStateChunk(StateMem *st, const char *sname, SFORMAT *sf, int data_only)
{
 int32 data_start_pos;
 int32 end_pos;

 {
  uint8 sname_tmp[32];

  memset(sname_tmp, 0, sizeof(sname_tmp));
  strncpy((char *)sname_tmp, sname, 32);

  if(strlen(sname) > 32)
   printf("Warning: section name is too long: %s\n", sname);

  smem_write(st, sname_tmp, 32);

  smem_write32le(st, 0);                // We'll come back and write this later.
 }

 data_start_pos = smem_tell(st);

 if(!SubWrite(st, sf, 0))
  return(0);

 end_pos = smem_tell(st);

 {
  smem_seek(st, data_start_pos - 4, SEEK_SET);
  smem_write32le(st, end_pos - data_start_pos);
  smem_seek(st, end_pos, SEEK_SET);
 }

 return(end_pos - data_start_pos);
}

struct compare_cstr
{
 bool operator()(const char *s1, const char *s2) const
 {
  return(strcmp(s1, s2) < 0);
 }
};

typedef std::map<const char *, SFORMAT *, compare_cstr> SFMap_t;

static void MakeSFMap(SFORMAT *sf, SFMap_t &sfmap)
{
 while(sf->size || sf->name) // Size can sometimes be zero, so also check for the text name.  These two should both be zero only at the end of a struct.
 {
  if(!sf->size || !sf->v)
  {
   sf++;
   continue;
  }

  if(sf->size == (uint32)~0)            /* Link to another SFORMAT structure. */
   MakeSFMap((SFORMAT *)sf->v, sfmap);
  else
  {
   assert(sf->name);

   if(sfmap.find(sf->name) != sfmap.end())
    printf("Duplicate save state variable in internal emulator structures(CLUB THE PROGRAMMERS WITH BREADSTICKS): %s\n", sf->name);

   sfmap[sf->name] = sf;
  }

  sf++;
 }
}

static int ReadStateChunk(StateMem *st, SFORMAT *sf, int size, int data_only)
{
 int temp;

 {
  SFMap_t sfmap;
  SFMap_t sfmap_found;	// Used for identifying variables that are missing in the save state.

  MakeSFMap(sf, sfmap);

  temp = smem_tell(st);
  while(smem_tell(st) < (temp + size))
  {
   uint32 recorded_size;	// In bytes
   uint8 toa[1 + 256];	// Don't change to char unless cast toa[0] to unsigned to smem_read() and other places.

   if(smem_read(st, toa, 1) != 1)
   {
    puts("Unexpected EOF");
    return(0);
   }

   if(smem_read(st, toa + 1, toa[0]) != toa[0])
   {
    puts("Unexpected EOF?");
    return 0;
   }

   toa[1 + toa[0]] = 0;

   smem_read32le(st, &recorded_size);

   SFMap_t::iterator sfmit;

   sfmit = sfmap.find((char *)toa + 1);

   if(sfmit != sfmap.end())
   {
    SFORMAT *tmp = sfmit->second;
    uint32 expected_size = tmp->size;	// In bytes

    if(recorded_size != expected_size)
    {
     printf("Variable in save state wrong size: %s.  Need: %d, got: %d\n", toa + 1, expected_size, recorded_size);
     if(smem_seek(st, recorded_size, SEEK_CUR) < 0)
     {
      puts("Seek error");
      return(0);
     }
    }
    else
    {
     sfmap_found[tmp->name] = tmp;

     smem_read(st, (uint8 *)tmp->v, expected_size);

     if(tmp->flags & MDFNSTATE_BOOL)
     {
      // Converting downwards is necessary for the case of sizeof(bool) > 1
      for(int32 bool_monster = expected_size - 1; bool_monster >= 0; bool_monster--)
      {
       ((bool *)tmp->v)[bool_monster] = ((uint8 *)tmp->v)[bool_monster];
      }
     }
     if(tmp->flags & MDFNSTATE_RLSB64)
      Endian_A64_LE_to_NE(tmp->v, expected_size / sizeof(uint64));
     else if(tmp->flags & MDFNSTATE_RLSB32)
      Endian_A32_LE_to_NE(tmp->v, expected_size / sizeof(uint32));
     else if(tmp->flags & MDFNSTATE_RLSB16)
      Endian_A16_LE_to_NE(tmp->v, expected_size / sizeof(uint16));
     else if(tmp->flags & RLSB)
      Endian_V_LE_to_NE(tmp->v, expected_size);
    }
   }
   else
   {
    printf("Unknown variable in save state: %s\n", toa + 1);
    if(smem_seek(st, recorded_size, SEEK_CUR) < 0)
    {
     puts("Seek error");
     return(0);
    }
   }
  } // while(...)

  for(SFMap_t::const_iterator it = sfmap.begin(); it != sfmap.end(); it++)
  {
   if(sfmap_found.find(it->second->name) == sfmap_found.end())
   {
    printf("Variable missing from save state: %s\n", it->second->name);
   }
  }

  assert(smem_tell(st) == (temp + size));
 }
 return 1;
}

/* This function is called by the game driver(NES, GB, GBA) to save a state. */
int MDFNSS_StateAction(StateMem *st, int load, int data_only, std::vector <SSDescriptor> &sections)
{
 std::vector<SSDescriptor>::iterator section;

 if(load)
 {
  {
   char sname[32];

   for(section = sections.begin(); section != sections.end(); section++)
   {
    int found = 0;
    uint32 tmp_size;
    uint32 total = 0;

    while(smem_read(st, (uint8 *)sname, 32) == 32)
    {
     if(smem_read32le(st, &tmp_size) != 4)
      return(0);

     total += tmp_size + 32 + 4;

     // Yay, we found the section
     if(!strncmp(sname, section->name, 32))
     {
      if(!ReadStateChunk(st, section->sf, tmp_size, 0))
      {
       printf("Error reading chunk: %s\n", section->name);
       return(0);
      }
      found = 1;
      break;
     } 
     else
     {
      if(smem_seek(st, tmp_size, SEEK_CUR) < 0)
      {
       puts("Chunk seek failure");
       return(0);
      }
     }
    }
    if(smem_seek(st, -total, SEEK_CUR) < 0)
    {
     puts("Reverse seek error");
     return(0);
    }
    if(!found && !section->optional) // Not found.  We are sad!
    {
     printf("Section missing:  %.32s\n", section->name);
     return(0);
    }
   }
  }
 }
 else
 {
  for(section = sections.begin(); section != sections.end(); section++)
  {
   if(!WriteStateChunk(st, section->name, section->sf, 0))
    return(0);
  }
 }

 return(1);
}

int MDFNSS_StateAction(StateMem *st, int load, int data_only, SFORMAT *sf, const char *name, bool optional)
{
 std::vector <SSDescriptor> love;

 love.push_back(SSDescriptor(sf, name, optional));
 return(MDFNSS_StateAction(st, load, 0, love));
}

int MDFNSS_SaveSM(StateMem *st)
{
	static const char *header_magic = "MDFNSVST";
        uint8 header[32];
	int neowidth = 0, neoheight = 0;

	memset(header, 0, sizeof(header));
	memcpy(header, header_magic, 8);

	MDFN_en32lsb(header + 16, MEDNAFEN_VERSION_NUMERIC);
	MDFN_en32lsb(header + 24, neowidth);
	MDFN_en32lsb(header + 28, neoheight);
	smem_write(st, header, 32);

	if(!MDFNGameInfo->StateAction(st, 0, 0))
	 return(0);

	uint32 sizy = smem_tell(st);
	smem_seek(st, 16 + 4, SEEK_SET);
	smem_write32le(st, sizy);

	return(1);
}

int MDFNSS_LoadSM(StateMem *st)
{
 uint8 header[32];
 uint32 stateversion;

 smem_read(st, header, 32);

 if(memcmp(header, "MEDNAFENSVESTATE", 16) && memcmp(header, "MDFNSVST", 8))
  return(0);

 stateversion = MDFN_de32lsb(header + 16);

 return(MDFNGameInfo->StateAction(st, stateversion, 0));
}

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

void MDFN_DispMessage(const char *format, ...) throw()
{
 va_list ap;
 va_start(ap,format);
 char *msg = NULL;

 trio_vasprintf(&msg, format,ap);
 va_end(ap);

 fprintf(stderr, "%s\n", msg);
}
