BBoot boot loader
=================

BBoot is a simple, minimal boot loader to load AmigaOS on pegasos2. It
is meant as a replacement for amigaboot.of for use on emulated
pegasos2 in QEMU, because amigaboot.of only works with the non-free
board firmware whereas BBoot also works with QEMU's built in Virtual
Open Firmware (VOF).

VOF only provides minimal Open Firmware services therefore BBoot does
not load files from disk but expects an initrd with the kickstart
files in zip format loaded in memory and boots from that. This not
only allows BBoot to remain simple but may also be more convenient to
work with in an emulated environment and boot faster. The initrd
should be a zip archive of all boot files within a Kickstart directory
that can be created with "zip -r Kickstart.zip Kickstart/" for
example. BBoot will look for Kickstart/Kicklayout and boot the first
config (LABEL) found in there. Currently BBoot takes no input and does
not provide a boot menu so other than 1st config defined in Kicklayout
cannot be chosen but one could have different zip files for different
configs and chose on the QEMU command line.

Apart from replacing amigaboot.of, BBoot can also configure PCI
devices that is normally done by the pegasos2 firmware but VOF does
not do that as neither Linux nor MorphOS needs it as they scan and
configure PCI devices themselves during boot. AmigaOS relies on the
firmware to do this so BBoot implements this too. While configuring
PCI, BBoot prints debug messages that can be useful to check how these
devices are configured, that can help in debugging PCI pass through or
even when using newer graphics cards on a real PegasosII as BBoot can
also patch 64 bit BARs that the Pegasos2 version of AmigaOS kernel
cannot handle.

Usage
=====

At least QEMU 8.1 is needed for the -initrd option. The bboot binary
is in the distribution archive. After preparing the initrd zip as
described above, AmigaOS should boot using these QEMU options:

.. code-block:: shell
qemu-system-ppc -M -pegasos2 -kernel bboot -initrd Kickstart.zip

When using -kernel there's no need to use -bios, then this will use
VOF and the pegasos2 ROM is not needed. If additional kernel options
are to be passed to AmigaOS, these can be added with the -append
option such as:

.. code-block:: shell
-append "serial debuglevel=3"

Apart from the primary usage above, the same bboot binary also works
with the Pegasos2 ROM firmware which can be useful when debugging PCI
or maybe booting a real machine faster. In this case -kernel and
-initrd cannot be used but the machine would boot with -bios
pegasos2.rom option and the initrd and bboot binary must be loaded
from the firmware prompt. See the bboot.fth Forth script for an
example. One inconvenience is that the zip file size must be edited in
the script then it can be loaded with "boot hd:0 bboot.fth" and should
do the rest automatically.

Troubleshooting
===============

BBoot uses serial output to log all messages so look for errors there.
On QEMU it can be redirected with -serial stdio or similar. On real
hardware it should set 115200,8,N,1 but it is untested. These
parameters are now hard coded in drivers/uart8250mem.c. For
explanation of the debug messages printed during boot, see comment at
the top brd_pegasos2.c.

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
`<https://qmiga.osdn.io/>`_
`<https://osdn.net/projects/qmiga/wiki/SubprojectBBoot>`_
