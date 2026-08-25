BBoot on SAM460ex
=================

BBoot is a small boot loader that starts AmigaOS without needing a
firmware ROM. This target, bboot-sam460, adds the SAM460ex board as
emulated by QEMU's sam460ex machine, so AmigaOS can be started without
U-Boot and without the second level boot loader it normally uses.

QEMU loads bboot with -kernel, bboot sets the board up the way the
firmware would have left it, unpacks the Kickstart modules from a zip
file passed with -initrd and starts AmigaOS from them.

What you need
=============

- qemu-system-ppc with the sam460ex machine
- the bboot-sam460 binary
- a Kickstart.zip made from the Kickstart drawer of the AmigaOS
  installation you want to boot
- that AmigaOS installation, as a disk image or as the installation CD

Making Kickstart.zip
====================

Zip a Kickstart drawer so that the archive contains Kickstart/Kicklayout
with the modules beside it, for example::

  zip -r Kickstart.zip Kickstart/

BBoot reads Kickstart/Kicklayout from the archive and loads the files its
EXEC and MODULE lines name, so the paths written in Kicklayout have to
match the paths in the zip. File names are case sensitive.

Use the Kickstart belonging to the system being booted. The one from an
installation CD boots that CD, the one from an installed hard disk boots
that installation.

Booting
=======

From a hard disk image::

  qemu-system-ppc -M sam460ex -m 2048M \
      -kernel bboot-sam460 -initrd Kickstart.zip \
      -append "serial debuglevel=1" \
      -drive if=none,id=hd0,file=hd0.qcow2,format=qcow2 \
      -device ide-hd,unit=0,drive=hd0,bus=ide.0 \
      -serial stdio

From an installation CD, on the second port instead::

      -drive if=none,id=cd,file=install.iso,format=raw,readonly=on \
      -device ide-cd,unit=0,drive=cd,bus=ide.1

The machine already provides an sii3112 controller of its own, which is
where the ide.0 and ide.1 buses come from, so there is no need to add
another one.

Kernel command line
===================

Whatever is given with -append is passed to AmigaOS as its command line.
"serial debuglevel=1" sends kernel debug output to the first serial port,
which is where -serial points. Higher debug levels print more, and a
Kickstart whose Kicklayout loads kernel.debug prints a great deal more
than one loading kernel.

Setting the debug level when booting with U-Boot
================================================

Without -kernel the machine boots the U-Boot image instead, which QEMU
loads as u-boot-sam460.bin from its data directory. That image has the
AmigaOS command line built into it as::

  os4_commandline=debuglevel=0

and with no saved environment in flash to override it, AmigaOS starts
with debug output off. Changing the 0 into a 1 is a single byte edit and
leaves the image the same size. The string is not at a fixed offset, so
search for it. Keep a copy of the original image first::

  python3 patch-uboot.py

with patch-uboot.py being::

  path = "u-boot-sam460.bin"
  key = b"os4_commandline=debuglevel="
  d = bytearray(open(path, "rb").read())
  i = d.find(key)
  if i < 0:
      raise SystemExit("string not found")
  print("found at offset", hex(i), "value", chr(d[i + len(key)]))
  d[i + len(key)] = ord("1")
  open(path, "wb").write(d)
  print("set to 1")

Note that the built in command line contains no serial keyword, so this
raises the debug level but does not by itself route the output to the
serial port. With bboot use -append, where both can be given.

Building
========

A PPC cross compiler is needed. The Makefile defaults to
powerpc64-linux-gnu- and this target is built 32 bit big endian, so the
toolchain needs 32 bit multilib support; the link step picks up libgcc
from -L$(CC -print-file-name=32) -lgcc::

  make bboot-sam460

Objects go to build-sam460/ and the result is bboot-sam460, an ELF that
QEMU's -kernel loads directly. The target is compiled with -mcpu=440
since the 460EX is a BookE core, unlike the other targets which are built
for classic PPC, and it shares all of the common sources with them.

Another toolchain can be selected with CROSS, for instance a native 32
bit PPC cross compiler that has a 32 bit libgcc of its own::

  make bboot-sam460 CROSS=powerpc-linux-gnu- LDLIBS=-lgcc

Dependencies are regenerated into Depend by make. That file is generated
and untracked, so if it was created by a different user or environment
and cannot be rewritten, delete it and build again.

What this target sets up
========================

QEMU enters bboot with the ePAPR magic in r6 and a device tree in r3,
which is how the board is detected, and the memory size, initrd location
and boot arguments are read from that tree. Everything below mirrors the
state U-Boot leaves behind on this board, because AmigaOS inherits it
rather than establishing it itself.

TLB
  The mapping from the board TLB table in U-Boot is programmed into the
  low entries in the same order, with SDRAM in the entries after it in
  256MB pages, every access bit set and caching inhibited. The order
  matters: the kernel recycles TLB entries from the bottom once it takes
  over, so RAM must not be in entry 0. QEMU's -kernel entry supplies one
  fabricated entry for VA 0 with a 2GB size that the 44x size field
  cannot encode, and reading it back with tlbre yields 4KB, so it is
  replaced rather than inherited.

Stack
  Moved to just below the top of RAM, where U-Boot relocates itself and
  leaves its stack. The AmigaOS loader keeps using the stack pointer it
  is handed while allocating around its own image, so a stack sitting
  just below the loader gets written over.

Interrupt controllers
  UIC0 to UIC3 are programmed with the polarity, trigger, critical and
  vector values the firmware uses. Out of reset the polarity register is
  zero, meaning every input is treated as active low, while the board's
  interrupts, all of the PCI ones included, are active high.

Decrementer
  Started with a 1ms auto-reload tick, TCR[DIE|ARE] enabled and IVPR set
  to zero. AmigaOS does not program the tick itself. Without it the boot
  initialises its drivers and then stops, because everything waiting on a
  timeout waits forever.

PCI
  The outbound windows POM0 and POM1 map PCI memory at 0x80000000 and
  0x90000000, and the inbound window PIM0 covers all of memory, sized
  from the memory size as the firmware sizes it. PIM0 is what the bridge
  translates device DMA through, and a window smaller than memory
  silently drops transfers whose buffers lie above it. Devices are
  enumerated, BARs and command registers assigned, and interrupt lines
  written the way the firmware assigns them: 116 for the onboard display
  controller and 32 for everything else.

Handoff
  The AmigaOS loader is entered with the module list, the command line
  and a bd_t laid out for CONFIG_460EX, which is larger than the common
  one and carries the processor, PLB, OPB and PCI clock fields that the
  kernel reads.

Files
=====

brd_sam460.c
  Board support: TLB, interrupt controllers, decrementer, PCI setup and
  bd_t.

fdt.c, include/fdt.h
  Minimal flattened device tree reader, used for the memory size, initrd
  location and boot arguments from the tree QEMU passes.

Licence
=======

BBoot is free software distributed under the GNU General Public License
version 2 or later; see COPYING for the full text. The files added for
this board carry SPDX-License-Identifier: GPL-2.0-or-later, the same as
the rest of the source, and this document is under the same terms.

Copyright (c) 2023 BALATON Zoltan for BBoot itself.

drivers/pci.c comes from the libpayload project, Copyright (C) 2008
Advanced Micro Devices, Inc. and Copyright (C) 2008 coresystems GmbH,
under the licence stated at the top of that file.

The board setup values reproduced here, the TLB table, the interrupt
controller registers, the decrementer setup and the PCI window and
interrupt assignments, were derived from the SAM460ex board support in
U-Boot as published by ACube Systems Srl, which is itself distributed
under the GNU General Public License version 2 or later.

AmigaOS is a product of Hyperion Entertainment CVBA and is no part of
this software; the Kickstart modules that BBoot loads are not
distributed with it.
