\ Example Forth script to use bboot with PegasosII firmware

\ Change 0 to initrd (Kixkstart.zip) size here:
d# 0 constant initrd-len

initrd-len 0= if abort" Edit script to set initrd-size" then

600000 initrd-len + \ initrd-end
600000              \ initrd-start
dev /chosen
 encode-int " linux,initrd-start" property
 encode-int " linux,initrd-end" property

setenv load-base 0x600000
load hd:0 Kickstart.zip
setenv load-base 0x400000
boot hd:0 bboot
