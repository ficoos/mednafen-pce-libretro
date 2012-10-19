#include        "mednafen.h"
#include	"mednafen-endian.h"

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
#include	"okiadpcm.h"

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

void Endian_A16_Swap(void *src, uint32 nelements)
{
 uint32 i;
 uint8 *nsrc = (uint8 *)src;

 for(i = 0; i < nelements; i++)
 {
  uint8 tmp = nsrc[i * 2];

  nsrc[i * 2] = nsrc[i * 2 + 1];
  nsrc[i * 2 + 1] = tmp;
 }
}

void Endian_A32_Swap(void *src, uint32 nelements)
{
 uint32 i;
 uint8 *nsrc = (uint8 *)src;

 for(i = 0; i < nelements; i++)
 {
  uint8 tmp1 = nsrc[i * 4];
  uint8 tmp2 = nsrc[i * 4 + 1];

  nsrc[i * 4] = nsrc[i * 4 + 3];
  nsrc[i * 4 + 1] = nsrc[i * 4 + 2];

  nsrc[i * 4 + 2] = tmp2;
  nsrc[i * 4 + 3] = tmp1;
 }
}

void Endian_A64_Swap(void *src, uint32 nelements)
{
 uint32 i;
 uint8 *nsrc = (uint8 *)src;

 for(i = 0; i < nelements; i++)
 {
  uint8 *base = &nsrc[i * 8];

  for(int z = 0; z < 4; z++)
  {
   uint8 tmp = base[z];

   base[z] = base[7 - z];
   base[7 - z] = tmp;
  }
 }
}

void Endian_A16_NE_to_LE(void *src, uint32 nelements)
{
 #ifdef MSB_FIRST
 Endian_A16_Swap(src, nelements);
 #endif
}

void Endian_A32_NE_to_LE(void *src, uint32 nelements)
{
 #ifdef MSB_FIRST
 Endian_A32_Swap(src, nelements);
 #endif
}

void Endian_A64_NE_to_LE(void *src, uint32 nelements)
{
 #ifdef MSB_FIRST
 Endian_A64_Swap(src, nelements);
 #endif
}


void Endian_A16_LE_to_NE(void *src, uint32 nelements)
{
 #ifdef MSB_FIRST
 uint32 i;
 uint8 *nsrc = (uint8 *)src;

 for(i = 0; i < nelements; i++)
 {
  uint8 tmp = nsrc[i * 2];

  nsrc[i * 2] = nsrc[i * 2 + 1];
  nsrc[i * 2 + 1] = tmp;
 }
 #endif
}

void Endian_A16_BE_to_NE(void *src, uint32 nelements)
{
 #ifdef LSB_FIRST
 uint32 i;
 uint8 *nsrc = (uint8 *)src;

 for(i = 0; i < nelements; i++)
 {
  uint8 tmp = nsrc[i * 2];

  nsrc[i * 2] = nsrc[i * 2 + 1];
  nsrc[i * 2 + 1] = tmp;
 }
 #endif
}


void Endian_A32_LE_to_NE(void *src, uint32 nelements)
{
 #ifdef MSB_FIRST
 uint32 i;
 uint8 *nsrc = (uint8 *)src;

 for(i = 0; i < nelements; i++)
 {
  uint8 tmp1 = nsrc[i * 4];
  uint8 tmp2 = nsrc[i * 4 + 1];

  nsrc[i * 4] = nsrc[i * 4 + 3];
  nsrc[i * 4 + 1] = nsrc[i * 4 + 2];

  nsrc[i * 4 + 2] = tmp2;
  nsrc[i * 4 + 3] = tmp1;
 }
 #endif
}

void Endian_A64_LE_to_NE(void *src, uint32 nelements)
{
 #ifdef MSB_FIRST
 uint32 i;
 uint8 *nsrc = (uint8 *)src;

 for(i = 0; i < nelements; i++)
 {
  uint8 *base = &nsrc[i * 8];

  for(int z = 0; z < 4; z++)
  {
   uint8 tmp = base[z];

   base[z] = base[7 - z];
   base[7 - z] = tmp;
  }
 }
 #endif
}

void FlipByteOrder(uint8 *src, uint32 count)
{
 uint8 *start=src;
 uint8 *end=src+count-1;

 if((count&1) || !count)        return;         /* This shouldn't happen. */

 count >>= 1;

 while(count--)
 {
  uint8 tmp;

  tmp=*end;
  *end=*start;
  *start=tmp;
  end--;
  start++;
 }
}

void Endian_V_LE_to_NE(void *src, uint32 bytesize)
{
 #ifdef MSB_FIRST
 FlipByteOrder((uint8 *)src, bytesize);
 #endif
}

void Endian_V_NE_to_LE(void *src, uint32 bytesize)
{
 #ifdef MSB_FIRST
 FlipByteOrder((uint8 *)src, bytesize);
 #endif
}

int write16le(uint16 b, FILE *fp)
{
 uint8 s[2];
 s[0]=b;
 s[1]=b>>8;
 return((fwrite(s,1,2,fp)<2)?0:2);
}

int write32le(uint32 b, FILE *fp)
{
 uint8 s[4];
 s[0]=b;
 s[1]=b>>8;
 s[2]=b>>16;
 s[3]=b>>24;
 return((fwrite(s,1,4,fp)<4)?0:4);
}

int read32le(uint32 *Bufo, FILE *fp)
{
 uint32 buf;
 if(fread(&buf,1,4,fp)<4)
  return 0;
 #ifdef LSB_FIRST
 *(uint32*)Bufo=buf;
 #else
 *(uint32*)Bufo=((buf&0xFF)<<24)|((buf&0xFF00)<<8)|((buf&0xFF0000)>>8)|((buf&0xFF000000)>>24);
 #endif
 return 1;
}

int read16le(char *d, FILE *fp)
{
 #ifdef LSB_FIRST
 return((fread(d,1,2,fp)<2)?0:2);
 #else
 int ret;
 ret=fread(d+1,1,1,fp);
 ret+=fread(d,1,1,fp);
 return ret<2?0:2;
 #endif
}

static uint8 **RAMPtrs = NULL;
static uint32 PageSize;
static uint32 NumPages;

typedef struct
{
 bool excluded;
 uint8 value; 
} CompareStruct;

typedef struct __CHEATF
{
           char *name;
           char *conditions;

           uint32 addr;
           uint64 val;
           uint64 compare;

           unsigned int length;
           bool bigendian;
           unsigned int icount; // Instance count
           char type;   /* 'R' for replace, 'S' for substitute(GG), 'C' for substitute with compare */
           int status;
} CHEATF;

static std::vector<CHEATF> cheats;
static int savecheats;
static CompareStruct **CheatComp = NULL;
static uint32 resultsbytelen = 1;
static bool resultsbigendian = 0;
static bool CheatsActive = TRUE;

bool SubCheatsOn = 0;
std::vector<SUBCHEAT> SubCheats[8];

static void RebuildSubCheats(void)
{
 std::vector<CHEATF>::iterator chit;

 SubCheatsOn = 0;
 for(int x = 0; x < 8; x++)
  SubCheats[x].clear();

 if(!CheatsActive) return;

 for(chit = cheats.begin(); chit != cheats.end(); chit++)
 {
  if(chit->status && chit->type != 'R')
  {
   for(unsigned int x = 0; x < chit->length; x++)
   {
    SUBCHEAT tmpsub;
    unsigned int shiftie;

    if(chit->bigendian)
     shiftie = (chit->length - 1 - x) * 8;
    else
     shiftie = x * 8;
    
    tmpsub.addr = chit->addr + x;
    tmpsub.value = (chit->val >> shiftie) & 0xFF;
    if(chit->type == 'C')
     tmpsub.compare = (chit->compare >> shiftie) & 0xFF;
    else
     tmpsub.compare = -1;
    SubCheats[(chit->addr + x) & 0x7].push_back(tmpsub);
    SubCheatsOn = 1;
   }
  }
 }
}

bool MDFNMP_Init(uint32 ps, uint32 numpages)
{
 PageSize = ps;
 NumPages = numpages;

 RAMPtrs = (uint8 **)calloc(numpages, sizeof(uint8 *));
 CheatComp = (CompareStruct **)calloc(numpages, sizeof(CompareStruct *));

 CheatsActive = MDFN_GetSettingB("cheats");
 return(1);
}

void MDFNMP_Kill(void)
{
 if(CheatComp)
 {
  free(CheatComp);
  CheatComp = NULL;
 }
 if(RAMPtrs)
 {
  free(RAMPtrs);
  RAMPtrs = NULL;
 }
}


void MDFNMP_AddRAM(uint32 size, uint32 A, uint8 *RAM)
{
 uint32 AB = A / PageSize;
 
 size /= PageSize;

 for(unsigned int x = 0; x < size; x++)
 {
  RAMPtrs[AB + x] = RAM;
  if(RAM) // Don't increment the RAM pointer if we're passed a NULL pointer
   RAM += PageSize;
 }
}

void MDFNMP_InstallReadPatches(void)
{
 if(!CheatsActive) return;

 std::vector<SUBCHEAT>::iterator chit;

 for(unsigned int x = 0; x < 8; x++)
  for(chit = SubCheats[x].begin(); chit != SubCheats[x].end(); chit++)
  {
   if(MDFNGameInfo->InstallReadPatch)
    MDFNGameInfo->InstallReadPatch(chit->addr);
  }
}

void MDFNMP_RemoveReadPatches(void)
{
 if(MDFNGameInfo->RemoveReadPatches)
  MDFNGameInfo->RemoveReadPatches();
}

static void CheatMemErr(void)
{
 MDFN_PrintError(_("Error allocating memory for cheat data."));
}

/* This function doesn't allocate any memory for "name" */
static int AddCheatEntry(char *name, char *conditions, uint32 addr, uint64 val, uint64 compare, int status, char type, unsigned int length, bool bigendian)
{
 CHEATF temp;

 memset(&temp, 0, sizeof(CHEATF));

 temp.name=name;
 temp.conditions = conditions;
 temp.addr=addr;
 temp.val=val;
 temp.status=status;
 temp.compare=compare;
 temp.length = length;
 temp.bigendian = bigendian;
 temp.type=type;

 cheats.push_back(temp);
 return(1);
}

static bool SeekToOurSection(FILE *fp) // Tentacle monster section aisle five, stale eggs and donkeys in aisle 2E.
{
 char buf[2048];

 while(fgets(buf,2048,fp) > 0)
 {
  if(buf[0] == '[')
  {
   if(!strncmp((char *)buf + 1, md5_context::asciistr(MDFNGameInfo->MD5, 0).c_str(), 16))
    return(1);
  }
 }
 return(0);
}

void MDFN_LoadGameCheats(FILE *override)
{
 char linebuf[2048];
 FILE *fp;

 unsigned int addr;
 unsigned long long val;
 unsigned int status;
 char type;
 unsigned long long compare;
 unsigned int x;
 unsigned int length;
 unsigned int icount;
 bool bigendian;

 int tc=0;

 savecheats=0;

 if(override)
  fp = override;
 else
 {
  std::string fn = MDFN_MakeFName(MDFNMKF_CHEAT,0,0).c_str();

  MDFN_printf("\n");
  MDFN_printf(_("Loading cheats from %s...\n"), fn.c_str());
  MDFN_indent(1);

  if(!(fp = fopen(fn.c_str(),"rb")))
  {
   ErrnoHolder ene(errno);

   MDFN_printf(_("Error opening file: %s\n"), ene.StrError());
   MDFN_indent(-1);
   return;
  }
 }

 if(SeekToOurSection(fp))
 {
  while(fgets(linebuf,2048,fp) > 0)
  { 
   char namebuf[2048];
   char *tbuf=linebuf;

   addr=val=compare=status=type=0;
   bigendian = 0;
   icount = 0;

   if(tbuf[0] == '[') // No more cheats for this game, so sad :(
   {
    break;
   }

   if(tbuf[0] == '\n' || tbuf[0] == '\r' || tbuf[0] == '\t' || tbuf[0] == ' ') // Don't parse if the line starts(or is just) white space
    continue;

   if(tbuf[0] != 'R' && tbuf[0] != 'C' && tbuf[0] != 'S')
   {
    MDFN_printf(_("Invalid cheat type: %c\n"), tbuf[0]);
    break;
   }
   type = tbuf[0];
   namebuf[0] = 0;

   char status_tmp, endian_tmp;
   if(type == 'C')
    trio_sscanf(tbuf, "%c %c %d %c %d %08x %16llx %16llx %.2047[^\r\n]", &type, &status_tmp, &length, &endian_tmp, &icount, &addr, &val, &compare, namebuf);
   else
    trio_sscanf(tbuf, "%c %c %d %c %d %08x %16llx %.2047[^\r\n]", &type, &status_tmp, &length, &endian_tmp, &icount, &addr, &val, namebuf);

   status = (status_tmp == 'A') ? 1 : 0;
   bigendian = (endian_tmp == 'B') ? 1 : 0;

   for(x=0;x<strlen(namebuf);x++)
   {
    if(namebuf[x]==10 || namebuf[x]==13)
    {
     namebuf[x]=0;
    break;
    }
    else if(namebuf[x]<0x20) namebuf[x]=' ';
   }

   // November 9, 2009 return value fix.
   if(fgets(linebuf, 2048, fp) == NULL)
    linebuf[0] = 0;

   for(x=0;x<strlen(linebuf);x++)
   {
    if(linebuf[x]==10 || linebuf[x]==13)
    {
     linebuf[x]=0;
     break;
    }
    else if(linebuf[x]<0x20) linebuf[x]=' ';
   }

   AddCheatEntry(strdup(namebuf), strdup(linebuf), addr, val, compare, status, type, length, bigendian);
   tc++;
  }
 }

 RebuildSubCheats();

 if(!override)
 {
  MDFN_printf(_("%lu cheats loaded.\n"), (unsigned long)cheats.size());
  MDFN_indent(-1);
  fclose(fp);
 }
}

static void WriteOurCheats(FILE *tmp_fp, bool needheader)
{
     if(needheader)
      trio_fprintf(tmp_fp, "[%s] %s\n", md5_context::asciistr(MDFNGameInfo->MD5, 0).c_str(), MDFNGameInfo->name ? (char *)MDFNGameInfo->name : "");

     std::vector<CHEATF>::iterator next;

     for(next = cheats.begin(); next != cheats.end(); next++)
     {
      if(next->type == 'C')
      {
       if(next->length == 1)
        trio_fprintf(tmp_fp, "%c %c %d %c %d %08x %02llx %02llx %s\n", next->type, next->status ? 'A' : 'I', next->length, next->bigendian ? 'B' : 'L', next->icount, next->addr, next->val, next->compare, next->name);
       else if(next->length == 2)
        trio_fprintf(tmp_fp, "%c %c %d %c %d %08x %04llx %04llx %s\n", next->type, next->status ? 'A' : 'I', next->length, next->bigendian ? 'B' : 'L', next->icount, next->addr, next->val, next->compare, next->name);
       else
        trio_fprintf(tmp_fp, "%c %c %d %c %d %08x %016llx %016llx %s\n", next->type, next->status ? 'A' : 'I', next->length, next->bigendian ? 'B' : 'L', next->icount, next->addr, next->val, next->compare, next->name);
      }
      else
      {
       if(next->length == 1)
        trio_fprintf(tmp_fp, "%c %c %d %c %d %08x %02llx %s\n", next->type, next->status ? 'A' : 'I', next->length, next->bigendian ? 'B' : 'L', next->icount, next->addr, next->val, next->name);
       else if(next->length == 2)
        trio_fprintf(tmp_fp, "%c %c %d %c %d %08x %04llx %s\n", next->type, next->status ? 'A' : 'I', next->length, next->bigendian ? 'B' : 'L', next->icount, next->addr, next->val, next->name);
       else
        trio_fprintf(tmp_fp, "%c %c %d %c %d %08x %016llx %s\n", next->type, next->status ? 'A' : 'I', next->length, next->bigendian ? 'B' : 'L', next->icount, next->addr, next->val, next->name);
      }
      trio_fprintf(tmp_fp, "%s\n", next->conditions ? next->conditions : "");
      free(next->name);
      if(next->conditions)
       free(next->conditions);
     }
}

void MDFN_FlushGameCheats(int nosave)
{
 if(CheatComp)
 {
  free(CheatComp);
  CheatComp = 0;
 }

 if(!savecheats || nosave)
 {
  std::vector<CHEATF>::iterator chit;

  for(chit = cheats.begin(); chit != cheats.end(); chit++)
  {
   free(chit->name);
   if(chit->conditions)
    free(chit->conditions);
  }
  cheats.clear();
 }
 else
 {
  uint8 linebuf[2048];
  std::string fn, tmp_fn;

  fn = MDFN_MakeFName(MDFNMKF_CHEAT, 0, 0);
  tmp_fn = MDFN_MakeFName(MDFNMKF_CHEAT_TMP, 0, 0);

  FILE *fp;
  int insection = 0;

  if((fp = fopen(fn.c_str(), "rb")))
  {
   FILE *tmp_fp = fopen(tmp_fn.c_str(), "wb");

   while(fgets((char*)linebuf, 2048, fp) > 0)
   {
    if(linebuf[0] == '[' && !insection)
    {
     if(!strncmp((char *)linebuf + 1, md5_context::asciistr(MDFNGameInfo->MD5, 0).c_str(), 16))
     {
      insection = 1;
      if(cheats.size())
       fputs((char*)linebuf, tmp_fp);
     }
     else
      fputs((char*)linebuf, tmp_fp);
    }
    else if(insection == 1)
    {
     if(linebuf[0] == '[') 
     {
      // Write any of our game cheats here.
      WriteOurCheats(tmp_fp, 0);
      insection = 2;     
      fputs((char*)linebuf, tmp_fp);
     }
    }
    else
    {
     fputs((char*)linebuf, tmp_fp);
    }
   }

   if(cheats.size())
   {
    if(!insection)
     WriteOurCheats(tmp_fp, 1);
    else if(insection == 1)
     WriteOurCheats(tmp_fp, 0);
   }

   fclose(fp);
   fclose(tmp_fp);

   #ifdef WIN32
   unlink(fn.c_str()); // Windows is evil. EVIIILL.  rename() won't overwrite an existing file.  TODO:  Change this to an autoconf define or something
		      // if we ever come across other platforms with lame rename().
   #endif
   rename(tmp_fn.c_str(), fn.c_str());
  }
  else if(errno == ENOENT) // Only overwrite the cheats file if it doesn't exist...heh.  Race conditions abound!
  {
   fp = fopen(fn.c_str(), "wb");
   WriteOurCheats(fp, 1);
   fclose(fp);
  }
 }
 RebuildSubCheats();
}

int MDFNI_AddCheat(const char *name, uint32 addr, uint64 val, uint64 compare, char type, unsigned int length, bool bigendian)
{
 char *t;

 if(!(t = strdup(name)))
 {
  CheatMemErr();
  return(0);
 }

 if(!AddCheatEntry(t, NULL, addr,val,compare,1,type, length, bigendian))
 {
  free(t);
  return(0);
 }

 savecheats = 1;

 MDFNMP_RemoveReadPatches();
 RebuildSubCheats();
 MDFNMP_InstallReadPatches();

 return(1);
}

int MDFNI_DelCheat(uint32 which)
{
 free(cheats[which].name);
 cheats.erase(cheats.begin() + which);

 savecheats=1;

 MDFNMP_RemoveReadPatches();
 RebuildSubCheats();
 MDFNMP_InstallReadPatches();

 return(1);
}

/*
 Condition format(ws = white space):
 
  <variable size><ws><endian><ws><address><ws><operation><ws><value>
	  [,second condition...etc.]

  Value should be unsigned integer, hex(with a 0x prefix) or
  base-10.  

  Operations:
   >=
   <=
   >
   <
   ==
   !=
   &	// Result of AND between two values is nonzero
   !&   // Result of AND between two values is zero
   ^    // same, XOR
   !^
   |	// same, OR
   !|

  Full example:

  2 L 0xADDE == 0xDEAD, 1 L 0xC000 == 0xA0

*/

static bool TestConditions(const char *string)
{
 char address[64];
 char operation[64];
 char value[64];
 char endian;
 unsigned int bytelen;
 bool passed = 1;

 //printf("TR: %s\n", string);
 while(trio_sscanf(string, "%u %c %.63s %.63s %.63s", &bytelen, &endian, address, operation, value) == 5 && passed)
 {
  uint32 v_address;
  uint64 v_value;
  uint64 value_at_address;

  if(address[0] == '0' && address[1] == 'x')
   v_address = strtoul(address + 2, NULL, 16);
  else
   v_address = strtoul(address, NULL, 10);

  if(value[0] == '0' && value[1] == 'x')
   v_value = strtoull(value + 2, NULL, 16);
  else
   v_value = strtoull(value, NULL, 0);

  value_at_address = 0;
  for(unsigned int x = 0; x < bytelen; x++)
  {
   unsigned int shiftie;

   if(endian == 'B')
    shiftie = (bytelen - 1 - x) * 8;
   else
    shiftie = x * 8;
   value_at_address |= MDFNGameInfo->MemRead(v_address + x) << shiftie;
  }

  //printf("A: %08x, V: %08llx, VA: %08llx, OP: %s\n", v_address, v_value, value_at_address, operation);
  if(!strcmp(operation, ">="))
  {
   if(!(value_at_address >= v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "<="))
  {
   if(!(value_at_address <= v_value))
    passed = 0;
  }
  else if(!strcmp(operation, ">"))
  {
   if(!(value_at_address > v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "<"))
  {
   if(!(value_at_address < v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "==")) 
  {
   if(!(value_at_address == v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "!="))
  {
   if(!(value_at_address != v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "&"))
  {
   if(!(value_at_address & v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "!&"))
  {
   if(value_at_address & v_value)
    passed = 0;
  }
  else if(!strcmp(operation, "^"))
  {
   if(!(value_at_address ^ v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "!^"))
  {
   if(value_at_address ^ v_value)
    passed = 0;
  }
  else if(!strcmp(operation, "|"))
  {
   if(!(value_at_address | v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "!|"))
  {
   if(value_at_address | v_value)
    passed = 0;
  }
  else
   puts("Invalid operation");
  string = strchr(string, ',');
  if(string == NULL)
   break;
  else
   string++;
  //printf("Foo: %s\n", string);
 }

 return(passed);
}

void MDFNMP_ApplyPeriodicCheats(void)
{
 std::vector<CHEATF>::iterator chit;


 if(!CheatsActive)
  return;

 //TestConditions("2 L 0x1F00F5 == 0xDEAD");
 //if(TestConditions("1 L 0x1F0058 > 0")) //, 1 L 0xC000 == 0x01"));
 for(chit = cheats.begin(); chit != cheats.end(); chit++)
 {
  if(chit->status && chit->type == 'R')
  {
   if(!chit->conditions || TestConditions(chit->conditions))
    for(unsigned int x = 0; x < chit->length; x++)
    {
     uint32 page = ((chit->addr + x) / PageSize) % NumPages;
     if(RAMPtrs[page])
     {
      uint64 tmpval = chit->val;

      if(chit->bigendian)
       tmpval >>= (chit->length - 1 - x) * 8;
      else
       tmpval >>= x * 8;

      RAMPtrs[page][(chit->addr + x) % PageSize] = tmpval;
     }
   }
  }
 }
}


void MDFNI_ListCheats(int (*callb)(char *name, uint32 a, uint64 v, uint64 compare, int s, char type, unsigned int length, bool bigendian, void *data), void *data)
{
 std::vector<CHEATF>::iterator chit;

 for(chit = cheats.begin(); chit != cheats.end(); chit++)
 {
  if(!callb(chit->name, chit->addr, chit->val, chit->compare, chit->status, chit->type, chit->length, chit->bigendian, data)) break;
 }
}

int MDFNI_GetCheat(uint32 which, char **name, uint32 *a, uint64 *v, uint64 *compare, int *s, char *type, unsigned int *length, bool *bigendian)
{
 CHEATF *next = &cheats[which];

 if(name)
  *name=next->name;
 if(a)
  *a=next->addr; 
 if(v)
  *v=next->val;
 if(s)
  *s=next->status;
 if(compare)
  *compare=next->compare;
 if(type)
  *type=next->type;
 if(length)
  *length = next->length;
 if(bigendian)
  *bigendian = next->bigendian;
 return(1);
}

static uint8 CharToNibble(char thechar)
{
 const char lut[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

 thechar = toupper(thechar);

 for(int x = 0; x < 16; x++)
  if(lut[x] == thechar)
   return(x);

 return(0xFF);
}

bool MDFNI_DecodeGBGG(const char *instr, uint32 *a, uint8 *v, uint8 *c, char *type)
{
 char str[10];
 int len;

 for(int x = 0; x < 9; x++)
 {
  while(*instr && CharToNibble(*instr) == 255)
   instr++;
  if(!(str[x] = *instr)) break;
  instr++;
 }
 str[9] = 0;

 len = strlen(str);

 if(len != 9 && len != 6)
  return(0);

 uint32 tmp_address;
 uint8 tmp_value;
 uint8 tmp_compare = 0;

 tmp_address =  (CharToNibble(str[5]) << 12) | (CharToNibble(str[2]) << 8) | (CharToNibble(str[3]) << 4) | (CharToNibble(str[4]) << 0);
 tmp_address ^= 0xF000;
 tmp_value = (CharToNibble(str[0]) << 4) | (CharToNibble(str[1]) << 0);

 if(len == 9)
 {
  tmp_compare = (CharToNibble(str[6]) << 4) | (CharToNibble(str[8]) << 0);
  tmp_compare = (tmp_compare >> 2) | ((tmp_compare << 6) & 0xC0);
  tmp_compare ^= 0xBA;
 }

 *a = tmp_address;
 *v = tmp_value;

 if(len == 9)
 {
  *c = tmp_compare;
  *type = 'C';
 }
 else
 {
  *c = 0;
  *type = 'S';
 }

 return(1);
}

static int GGtobin(char c)
{
 static char lets[16]={'A','P','Z','L','G','I','T','Y','E','O','X','U','K','S','V','N'};
 int x;

 for(x=0;x<16;x++)
  if(lets[x] == toupper(c)) return(x);
 return(0);
}

/* Returns 1 on success, 0 on failure. Sets *a,*v,*c. */
int MDFNI_DecodeGG(const char *str, uint32 *a, uint8 *v, uint8 *c, char *type)
{
 uint16 A;
 uint8 V,C;
 uint8 t;
 int s;

 A=0x8000;
 V=0;
 C=0;

 s=strlen(str);
 if(s!=6 && s!=8) return(0);

 t=GGtobin(*str++);
 V|=(t&0x07);
 V|=(t&0x08)<<4;

 t=GGtobin(*str++);
 V|=(t&0x07)<<4;
 A|=(t&0x08)<<4;

 t=GGtobin(*str++);
 A|=(t&0x07)<<4;
 //if(t&0x08) return(0);	/* 8-character code?! */

 t=GGtobin(*str++);
 A|=(t&0x07)<<12;
 A|=(t&0x08);

 t=GGtobin(*str++);
 A|=(t&0x07);
 A|=(t&0x08)<<8;

 if(s==6)
 {
  t=GGtobin(*str++);
  A|=(t&0x07)<<8;
  V|=(t&0x08);

  *a=A;
  *v=V;
  *type = 'S';
  *c = 0;
  return(1);
 }
 else
 {
  t=GGtobin(*str++);
  A|=(t&0x07)<<8;
  C|=(t&0x08);

  t=GGtobin(*str++);
  C|=(t&0x07);
  C|=(t&0x08)<<4;
  
  t=GGtobin(*str++);
  C|=(t&0x07)<<4;
  V|=(t&0x08);
  *a=A;
  *v=V;
  *c=C;
  *type = 'C';
  return(1);
 }
 return(0);
}

int MDFNI_DecodePAR(const char *str, uint32 *a, uint8 *v, uint8 *c, char *type)
{
 int boo[4];
 if(strlen(str)!=8) return(0);

 trio_sscanf(str,"%02x%02x%02x%02x",boo,boo+1,boo+2,boo+3);

 *c = 0;

 if(1)
 {
  *a=(boo[3]<<8)|(boo[2]+0x7F);
  *v=0;
 }
 else
 {
  *v=boo[3];
  *a=boo[2]|(boo[1]<<8);
 }

 *type = 'S';
 return(1);
}

/* name can be NULL if the name isn't going to be changed. */
int MDFNI_SetCheat(uint32 which, const char *name, uint32 a, uint64 v, uint64 compare, int s, char type, unsigned int length, bool bigendian)
{
 CHEATF *next = &cheats[which];

 if(name)
 {
  char *t;

  if((t=(char *)realloc(next->name,strlen(name+1))))
  {
   next->name=t;
   strcpy(next->name,name);
  }
  else
   return(0);
 }
 next->addr=a;
 next->val=v;
 next->status=s;
 next->compare=compare;
 next->type=type;
 next->length = length;
 next->bigendian = bigendian;

 RebuildSubCheats();
 savecheats=1;

 return(1);
}

/* Convenience function. */
int MDFNI_ToggleCheat(uint32 which)
{
 cheats[which].status = !cheats[which].status;
 savecheats = 1;
 RebuildSubCheats();

 return(cheats[which].status);
}

void MDFNI_CheatSearchSetCurrentAsOriginal(void)
{
 for(uint32 page = 0; page < NumPages; page++)
 {
  if(CheatComp[page])
  {
   for(uint32 addr = 0; addr < PageSize; addr++)
   {
    if(!CheatComp[page][addr].excluded)
    {
     CheatComp[page][addr].value = RAMPtrs[page][addr];
    }
   }
  }
 }
}

void MDFNI_CheatSearchShowExcluded(void)
{
 for(uint32 page = 0; page < NumPages; page++)
 {
  if(CheatComp[page])
  {
   for(uint32 addr = 0; addr < PageSize; addr++)
   {
    CheatComp[page][addr].excluded = 0;
   }
  }
 }
}


int32 MDFNI_CheatSearchGetCount(void)
{
 uint32 count = 0;

 for(uint32 page = 0; page < NumPages; page++)
 {
  if(CheatComp[page])
  {
   for(uint32 addr = 0; addr < PageSize; addr++)
   {
    if(!CheatComp[page][addr].excluded)
     count++;
   }
  }
 }
 return count;
}

/* This function will give the initial value of the search and the current value at a location. */

void MDFNI_CheatSearchGet(int (*callb)(uint32 a, uint64 last, uint64 current, void *data), void *data)
{
 for(uint32 page = 0; page < NumPages; page++)
 {
  if(CheatComp[page])
  {
   for(uint32 addr = 0; addr < PageSize; addr++)
   {
    if(!CheatComp[page][addr].excluded)
    {
     uint64 ccval;
     uint64 ramval;

     ccval = ramval = 0;
     for(unsigned int x = 0; x < resultsbytelen; x++)
     {
      uint32 curpage = (page + (addr + x) / PageSize) % NumPages;
      if(CheatComp[curpage])
      {
       unsigned int shiftie;

       if(resultsbigendian)
        shiftie = (resultsbytelen - 1 - x) * 8;
       else
        shiftie = x * 8;
       ccval |= CheatComp[curpage][(addr + x) % PageSize].value << shiftie;
       ramval |= RAMPtrs[curpage][(addr + x) % PageSize] << shiftie;
      }
     }

     if(!callb(page * PageSize + addr, ccval, ramval, data))
      return;
    }
   }
  }
 }
}

void MDFNI_CheatSearchBegin(void)
{
 resultsbytelen = 1;
 resultsbigendian = 0;

 for(uint32 page = 0; page < NumPages; page++)
 {
  if(RAMPtrs[page])
  {
   if(!CheatComp[page])
    CheatComp[page] = (CompareStruct *)calloc(PageSize, sizeof(CompareStruct));

   for(uint32 addr = 0; addr < PageSize; addr++)
   {
    CheatComp[page][addr].excluded = 0;
    CheatComp[page][addr].value = RAMPtrs[page][addr];
   }
  }
 }
}


static uint64 INLINE CAbs(uint64 x)
{
 if(x < 0)
  return(0 - x);
 return x;
}

void MDFNI_CheatSearchEnd(int type, uint64 v1, uint64 v2, unsigned int bytelen, bool bigendian)
{
 v1 &= (~0ULL) >> (8 - bytelen);
 v2 &= (~0ULL) >> (8 - bytelen);

 resultsbytelen = bytelen;
 resultsbigendian = bigendian;

 for(uint32 page = 0; page < NumPages; page++)
 {
  if(CheatComp[page])
  {
   for(uint32 addr = 0; addr < PageSize; addr++)
   {
    if(!CheatComp[page][addr].excluded)
    {
     bool doexclude = 0;
     uint64 ccval;
     uint64 ramval;

     ccval = ramval = 0;
     for(unsigned int x = 0; x < bytelen; x++)
     {
      uint32 curpage = (page + (addr + x) / PageSize) % NumPages;
      if(CheatComp[curpage])
      {
       unsigned int shiftie;

       if(bigendian)
        shiftie = (bytelen - 1 - x) * 8;
       else
        shiftie = x * 8;
       ccval |= CheatComp[curpage][(addr + x) % PageSize].value << shiftie;
       ramval |= RAMPtrs[curpage][(addr + x) % PageSize] << shiftie;
      }
     }

     switch(type)
     {
      case 0: // Change to a specific value.
	if(!(ccval == v1 && ramval == v2))
	 doexclude = 1;
	break;
	 
      case 1: // Search for relative change(between values).
	if(!(ccval == v1 && CAbs(ccval - ramval) == v2))
	 doexclude = 1;
	break;

      case 2: // Purely relative change.
	if(!(CAbs(ccval - ramval) == v2))
	 doexclude = 1;
	break;
      case 3: // Any change
        if(!(ccval != ramval))
         doexclude = 1;
        break;
      case 4: // Value decreased
        if(ramval >= ccval)
         doexclude = 1;
        break;
      case 5: // Value increased
        if(ramval <= ccval)
         doexclude = 1;
        break;
     }
     if(doexclude)
      CheatComp[page][addr].excluded = TRUE;
    }
   }
  }
 }

 if(type >= 4)
  MDFNI_CheatSearchSetCurrentAsOriginal();
}

static void SettingChanged(const char *name)
{
 MDFNMP_RemoveReadPatches();

 CheatsActive = MDFN_GetSettingB("cheats");

 RebuildSubCheats();

 MDFNMP_InstallReadPatches();
}


MDFNSetting MDFNMP_Settings[] =
{
 { "cheats", MDFNSF_NOFLAGS, "Enable cheats.", NULL, MDFNST_BOOL, "1", NULL, NULL, NULL, SettingChanged },
 { NULL}
};

MDFN_Error::MDFN_Error() throw()
{
 abort();
}

MDFN_Error::MDFN_Error(int errno_code_new, const char *format, ...) throw()
{
 errno_code = errno_code_new;

 va_list ap;
 va_start(ap, format);
 error_message = trio_vaprintf(format, ap);
 va_end(ap);
}


MDFN_Error::MDFN_Error(const ErrnoHolder &enh)
{
 errno_code = enh.Errno();

 error_message = trio_aprintf("%s", enh.StrError());
}


MDFN_Error::~MDFN_Error() throw()
{
 if(error_message)
 {
  free(error_message);
  error_message = NULL;
 }
}

MDFN_Error::MDFN_Error(const MDFN_Error &ze_error) throw()
{
 if(ze_error.error_message)
  error_message = strdup(ze_error.error_message);
 else
  error_message = NULL;

 errno_code = ze_error.errno_code;
}

MDFN_Error& MDFN_Error::operator=(const MDFN_Error &ze_error) throw()
{
 char *new_error_message = ze_error.error_message ? strdup(ze_error.error_message) : NULL;
 int new_errno_code = ze_error.errno_code;

 if(error_message)
  free(error_message);

 error_message = new_error_message;
 errno_code = new_errno_code;

 return(*this);
}


const char * MDFN_Error::what(void) const throw()
{
 if(!error_message)
  return("Error allocating memory for the error message!");

 return(error_message);
}

int MDFN_Error::GetErrno(void) const throw()
{
 return(errno_code);
}

static const char *srr_wrap(int ret, const char *local_strerror)
{
 if(ret == -1)
  return("ERROR IN strerror_r()!!!");

 return(local_strerror);
}

static const char *srr_wrap(const char *ret, const char *local_strerror)
{
 if(ret == NULL)
  return("ERROR IN strerror_r()!!!");

 return(ret);
}

void ErrnoHolder::SetErrno(int the_errno)
{
 local_errno = the_errno;

 if(the_errno == 0)
  local_strerror[0] = 0;
 else
 {
   strncpy(local_strerror, strerror(the_errno), 255);

  local_strerror[255] = 0;
 }
}

/*
 * RFC 1321 compliant MD5 implementation,
 * by Christophe Devine <devine@cr0.net>;
 * this program is licensed under the GPL.
 */
/* Converted to C++ for use in Mednafen */

#define GET_UINT32(n,b,i)                       \
{                                               \
    (n) = ( (uint32) (b)[(i) + 3] << 24 )       \
        | ( (uint32) (b)[(i) + 2] << 16 )       \
        | ( (uint32) (b)[(i) + 1] <<  8 )       \
        | ( (uint32) (b)[(i)    ]       );      \
}

#define PUT_UINT32(n,b,i)                       \
{                                               \
    (b)[(i)    ] = (uint8) ( (n)       );       \
    (b)[(i) + 1] = (uint8) ( (n) >>  8 );       \
    (b)[(i) + 2] = (uint8) ( (n) >> 16 );       \
    (b)[(i) + 3] = (uint8) ( (n) >> 24 );       \
}

md5_context::md5_context(void)
{


}

md5_context::~md5_context(void)
{

}

void md5_context::starts(void)
{
    total[0] = 0;
    total[1] = 0;
    state[0] = 0x67452301;
    state[1] = 0xEFCDAB89;
    state[2] = 0x98BADCFE;
    state[3] = 0x10325476;
}

void md5_context::process(const uint8 data[64])
{
    uint32 A, B, C, D, X[16];

    GET_UINT32( X[0],  data,  0 );
    GET_UINT32( X[1],  data,  4 );
    GET_UINT32( X[2],  data,  8 );
    GET_UINT32( X[3],  data, 12 );
    GET_UINT32( X[4],  data, 16 );
    GET_UINT32( X[5],  data, 20 );
    GET_UINT32( X[6],  data, 24 );
    GET_UINT32( X[7],  data, 28 );
    GET_UINT32( X[8],  data, 32 );
    GET_UINT32( X[9],  data, 36 );
    GET_UINT32( X[10], data, 40 );
    GET_UINT32( X[11], data, 44 );
    GET_UINT32( X[12], data, 48 );
    GET_UINT32( X[13], data, 52 );
    GET_UINT32( X[14], data, 56 );
    GET_UINT32( X[15], data, 60 );

#define S(x,n) ((x << n) | ((x & 0xFFFFFFFF) >> (32 - n)))

#define P(a,b,c,d,k,s,t)                                \
{                                                       \
    a += F(b,c,d) + X[k] + t; a = S(a,s) + b;           \
}

    A = state[0];
    B = state[1];
    C = state[2];
    D = state[3];

#define F(x,y,z) (z ^ (x & (y ^ z)))

    P( A, B, C, D,  0,  7, 0xD76AA478 );
    P( D, A, B, C,  1, 12, 0xE8C7B756 );
    P( C, D, A, B,  2, 17, 0x242070DB );
    P( B, C, D, A,  3, 22, 0xC1BDCEEE );
    P( A, B, C, D,  4,  7, 0xF57C0FAF );
    P( D, A, B, C,  5, 12, 0x4787C62A );
    P( C, D, A, B,  6, 17, 0xA8304613 );
    P( B, C, D, A,  7, 22, 0xFD469501 );
    P( A, B, C, D,  8,  7, 0x698098D8 );
    P( D, A, B, C,  9, 12, 0x8B44F7AF );
    P( C, D, A, B, 10, 17, 0xFFFF5BB1 );
    P( B, C, D, A, 11, 22, 0x895CD7BE );
    P( A, B, C, D, 12,  7, 0x6B901122 );
    P( D, A, B, C, 13, 12, 0xFD987193 );
    P( C, D, A, B, 14, 17, 0xA679438E );
    P( B, C, D, A, 15, 22, 0x49B40821 );

#undef F

#define F(x,y,z) (y ^ (z & (x ^ y)))

    P( A, B, C, D,  1,  5, 0xF61E2562 );
    P( D, A, B, C,  6,  9, 0xC040B340 );
    P( C, D, A, B, 11, 14, 0x265E5A51 );
    P( B, C, D, A,  0, 20, 0xE9B6C7AA );
    P( A, B, C, D,  5,  5, 0xD62F105D );
    P( D, A, B, C, 10,  9, 0x02441453 );
    P( C, D, A, B, 15, 14, 0xD8A1E681 );
    P( B, C, D, A,  4, 20, 0xE7D3FBC8 );
    P( A, B, C, D,  9,  5, 0x21E1CDE6 );
    P( D, A, B, C, 14,  9, 0xC33707D6 );
    P( C, D, A, B,  3, 14, 0xF4D50D87 );
    P( B, C, D, A,  8, 20, 0x455A14ED );
    P( A, B, C, D, 13,  5, 0xA9E3E905 );
    P( D, A, B, C,  2,  9, 0xFCEFA3F8 );
    P( C, D, A, B,  7, 14, 0x676F02D9 );
    P( B, C, D, A, 12, 20, 0x8D2A4C8A );

#undef F
    
#define F(x,y,z) (x ^ y ^ z)

    P( A, B, C, D,  5,  4, 0xFFFA3942 );
    P( D, A, B, C,  8, 11, 0x8771F681 );
    P( C, D, A, B, 11, 16, 0x6D9D6122 );
    P( B, C, D, A, 14, 23, 0xFDE5380C );
    P( A, B, C, D,  1,  4, 0xA4BEEA44 );
    P( D, A, B, C,  4, 11, 0x4BDECFA9 );
    P( C, D, A, B,  7, 16, 0xF6BB4B60 );
    P( B, C, D, A, 10, 23, 0xBEBFBC70 );
    P( A, B, C, D, 13,  4, 0x289B7EC6 );
    P( D, A, B, C,  0, 11, 0xEAA127FA );
    P( C, D, A, B,  3, 16, 0xD4EF3085 );
    P( B, C, D, A,  6, 23, 0x04881D05 );
    P( A, B, C, D,  9,  4, 0xD9D4D039 );
    P( D, A, B, C, 12, 11, 0xE6DB99E5 );
    P( C, D, A, B, 15, 16, 0x1FA27CF8 );
    P( B, C, D, A,  2, 23, 0xC4AC5665 );

#undef F

#define F(x,y,z) (y ^ (x | ~z))

    P( A, B, C, D,  0,  6, 0xF4292244 );
    P( D, A, B, C,  7, 10, 0x432AFF97 );
    P( C, D, A, B, 14, 15, 0xAB9423A7 );
    P( B, C, D, A,  5, 21, 0xFC93A039 );
    P( A, B, C, D, 12,  6, 0x655B59C3 );
    P( D, A, B, C,  3, 10, 0x8F0CCC92 );
    P( C, D, A, B, 10, 15, 0xFFEFF47D );
    P( B, C, D, A,  1, 21, 0x85845DD1 );
    P( A, B, C, D,  8,  6, 0x6FA87E4F );
    P( D, A, B, C, 15, 10, 0xFE2CE6E0 );
    P( C, D, A, B,  6, 15, 0xA3014314 );
    P( B, C, D, A, 13, 21, 0x4E0811A1 );
    P( A, B, C, D,  4,  6, 0xF7537E82 );
    P( D, A, B, C, 11, 10, 0xBD3AF235 );
    P( C, D, A, B,  2, 15, 0x2AD7D2BB );
    P( B, C, D, A,  9, 21, 0xEB86D391 );

#undef F

    state[0] += A;
    state[1] += B;
    state[2] += C;
    state[3] += D;
}

void md5_context::update(const uint8 *input, uint32 length )
{
    uint32 left, fill;

    if( ! length ) return;

    left = ( total[0] >> 3 ) & 0x3F;
    fill = 64 - left;

    total[0] += length <<  3;
    total[1] += length >> 29;

    total[0] &= 0xFFFFFFFF;
    total[1] += total[0] < ( length << 3 );

    if( left && length >= fill )
    {
        memcpy( (void *) (buffer + left), (void *) input, fill );
        process(buffer );
        length -= fill;
        input  += fill;
        left = 0;
    }

    while( length >= 64 )
    {
        process(input );
        length -= 64;
        input  += 64;
    }

    if( length )
    {
        memcpy( (void *) (buffer + left), (void *) input, length );
    }
}

static const uint8 md5_padding[64] =
{
 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void md5_context::finish(uint8 digest[16] )
{
    uint32 last, padn;
    uint8 msglen[8];

    PUT_UINT32( total[0], msglen, 0 );
    PUT_UINT32( total[1], msglen, 4 );

    last = ( total[0] >> 3 ) & 0x3F;
    padn = ( last < 56 ) ? ( 56 - last ) : ( 120 - last );

    update( md5_padding, padn );
    update( msglen, 8 );

    PUT_UINT32( state[0], digest,  0 );
    PUT_UINT32( state[1], digest,  4 );
    PUT_UINT32( state[2], digest,  8 );
    PUT_UINT32( state[3], digest, 12 );
}


/* Uses a static buffer, so beware of how it's used. */
//static 
std::string md5_context::asciistr(const uint8 digest[16], bool borked_order)
{
 static char str[33];
 static char trans[16]={'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
 int x;

 for(x=0;x<16;x++)
 {
  if(borked_order)
  {
   str[x*2]=trans[digest[x]&0x0F];
   str[x*2+1]=trans[digest[x]>>4];
  }
  else
  {
   str[x*2+1]=trans[digest[x]&0x0F];
   str[x*2]=trans[digest[x]>>4];
  }
 }
 return(std::string(str));
}

const int OKIADPCM_StepSizes[49] =
{
 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50,
 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157,
 173, 190,  209, 230, 253, 279, 307, 337, 371, 408, 449,
 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
};

const int OKIADPCM_StepIndexDeltas[16] =
{
 -1, -1, -1, -1, 2, 4, 6, 8,
 -1, -1, -1, -1, 2, 4, 6, 8
};

const int32 OKIADPCM_DeltaTable[49][16] =
{
 #ifndef OKIADPCM_GENERATE_DELTATABLE
 #include "okiadpcm-deltatable.h"
 #endif
};
