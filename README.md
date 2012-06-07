mednafen-pce-libretro
=====================

Port of Mednafen's PCE core to libretro.

TG-CD emulation requires the CD BIOS image, named as syscard3.pce. This BIOS image is to be placed into the Mednafen base directory ~/.mednafen/. Then, you will have to indicate to Mednafen where the syscard3.pce file is located. To do this, edit the ~/.mednafen/mednafen.cfg file:

;Path to the ROM BIOS

pce.cdbios /path/to/file/syscard3.pce