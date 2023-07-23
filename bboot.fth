\ Example Forth script to use bboot with PegasosII firmware
\ Copyright (c) 2023 BALATON Zoltan
\ SPDX-License-Identifier: GPL-2.0-or-later
600000 constant initrd-start
\ Initrd to load (note: space needed after first quote!)
" hd:0,Kickstart.zip"

open-dev dup 0= if abort" Could not open initrd" then
dup initrd-start swap " load" rot $call-method
dup 0= if
  drop close-dev
  abort" Could not load initrd"
then
swap close-dev
initrd-start + \ initrd-end
initrd-start
" /chosen" find-device
  encode-int " linux,initrd-start" property
  encode-int " linux,initrd-end" property
  .properties
device-end
boot hd:0 bboot
