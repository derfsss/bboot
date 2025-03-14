BBoot boot loader
=================

BBoot is a simple, minimal boot loader to load AmigaOS on QEMU
emulated amigaone and pegasos2 machines. It is replacing the second
level boot loader (amigaboot.of on pegasos2 and slb_v2 on amigaone)
and makes booting AmigaOS on QEMU simpler and without needing a
firmware ROM. The original amigaboot.of only works with the non-free
pegasos2 SmartFirmware and slb_v2 is tied to U-Boot, whereas BBoot
also works with QEMU's built in minimal Virtual Open Firmware (VOF) on
pegasos2 or without U-Boot on amigaone.

The amigaboot.of and slb_v2 boot loaders rely on firmware to read
files from disk. BBoot runs without firmware and does not have disk
drivers so it does not load files from disk but instead expects an
initrd with the kickstart files in zip format loaded in memory and
boots from that. This not only allows BBoot to remain simple but may
also be more convenient to work with in an emulated environment and
boot faster. The initrd should be a zip archive of all boot files
within a Kickstart directory that can be created with "zip -r
Kickstart.zip Kickstart/" for example. BBoot will look for
Kickstart/Kicklayout file within the zip file and boot the first
config (LABEL) found in it. Currently BBoot takes no input and does
not provide a boot menu so other than 1st config defined in Kicklayout
cannot be chosen, but one could have different zip files for different
configs and choose on the QEMU command line by using a different
initrd zip for each config.

BBoot can also configure PCI devices that is normally done by the
firmware. Linux and MorphOS don't need this as they scan and configure
PCI devices themselves during boot but AmigaOS relies on the firmware
to do this so BBoot implements this too. While configuring PCI, BBoot
prints debug messages that can be useful to check how these devices
are configured. This can help in debugging PCI pass through or even
using newer graphics cards on a real PegasosII as BBoot can also patch
64 bit BARs that the Pegasos2 version of AmigaOS kernel cannot handle.

Usage
=====

At least QEMU 8.1 is needed which added -initrd option for pegasos2 or
QEMU 8.2 for the amigaone machine and QEMU 10.0 for -initrd on
amigaone. A pre-compiled bboot binary is in the distribution archive.
After preparing the Kickstart.zip as described above, AmigaOS should
boot using these QEMU options:

.. code-block:: shell

  qemu-system-ppc -M pegasos2 -kernel bboot -initrd Kickstart.zip

When using -kernel there's no need to use -bios and the pegasos2 ROM
is not needed, then it will use VOF (QEMU's built in Virtual Open
Firmware). If additional kernel options are to be passed to AmigaOS,
these can be added with the -append option. This is not normally
needed, only to get debug info, for example:

.. code-block:: shell

  -append "serial debuglevel=3"

For booting amigaone with QEMU before version 10.0 where -kernel and
-initrd options were not yet supported or for BBoot versions less than
0.8 the following QEMU options are needed:

.. code-block:: shell

  qemu-system-ppc -M amigaone -device loader,cpu-num=0,file=bboot \
                  -device loader,addr=0x600000,file=Kickstart.zip

On amigaone, firmware ROM can be added with -bios but it's bypassed
with bboot and not needed. Running BBoot from U-Boot is not supported.

Apart from the primary usage above, the same bboot binary also works
with the Pegasos2 ROM firmware which can be useful when debugging PCI
or using vfio-pci with QEMU on Linux host to pass through graphics
cards (for some cards the BIOS emulator in the firmware may be needed
to correctly initialise the card but BBoot is also needed to fix up 64
bit BARs and interrupts for the AmigaOS kernel) or even to boot a real
machine faster. When using with pegasos2 ROM the -kernel and -initrd
options cannot be used, instead the firmware binary must be given with
the -bios pegasos2.rom option and the Kickstart.zip, bboot binary and
bboot.fth must be copied to the pegasos2 hard disk from where it's
loaded by the firmware ROM. The bboot.fth Forth script can load these
which can be run with "boot hd:0 bboot.fth" from the SmartFirmware ok
prompt and should do the rest automatically. It assumes that bboot
binary and Kickstart.zip are on hd:0 (first partition of first hard
disk) or this can be edited in the beginning and end of bboot.fth.

Configuration
=============

There are a few options that can be configured, see details in the
comment in cfg.c. Options under pegasos2 firmware can be set in the
device tree adding a /options/bboot string the same way as for example
os4_commandline is defined, that is first define a variable such as
'16 nodefault-bytes bboot' then set options with setenv bboot <opts
string>. This is mostly only useful on real machine, with QEMU there
is no way to set it currently but the defaults #define'ed in cfg.c as
DEFAULT_OPTS are what is needed for QEMU so there should be no reason
to change it. On real PegasosII hardware the settings "Of V0 Ab" (or
V1 if you still want to get some feedback) might be better which skips
PCI config that is already done by firmware and omits most output so
may be faster and will print errors to the SmartFirmware console so no
serial is needed. It is also possible to get messages on both console
and serial with "Ofs".

Troubleshooting
===============

BBoot uses serial output by default to log all messages so look for
errors there. On QEMU it can be redirected with -serial stdio or
appear in a virtual console window. On real hardware it should set
115200,8,N,1. These parameters are now hard coded in
drivers/uart8250mem.c. For explanation of the PCI configuration debug
messages printed during boot, see comment at the top brd_pegasos2.c.

Amiga file systems are case insensitive but BBoot is case sensitive so
if a file is not found in Kickstart.zip check that its name matches
what is in Kicklayout literally with no upper/lower case differences.
In particular SFSFileSystem is sometimes written as SFSFilesystem so
either Kicklayout needs to be edited or the file renamed.

Source
======

BBoot is open source, distributed under the GNU GPL version 2, see
COPYING for details. If you modify it please contribute back so
everybody can benefit from your work. To build it on Linux or Unix
system only a PPC cross compiler is needed then just typing make
should build it or check and adjust paths at the top of the Makefile.

More info
=========

Copyright (c) 2023 BALATON Zoltan
`<https://qmiga.codeberg.page/>`_
`<https://codeberg.org/qmiga/pages/wiki/SubprojectBBoot>`_
