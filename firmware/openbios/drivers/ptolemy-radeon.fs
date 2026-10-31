\ SPDX-License-Identifier: GPL-2.0-or-later
\ Ptolemy Optics: Open Firmware linear boot display, ABI 1.
\ The host initializes a linear XRGB32 big-endian framebuffer in BAR0.
\ No Bochs ports, imported NDRV, EFI protocol or acceleration claim.

fcode-version3

: pmd-xt  ( string length -- xt )
  $find 0= if
    ." Missing OpenBIOS display word" cr -1 throw
  then
;
" openbios-video-width" pmd-xt cell+ value pmd-width-cell
" openbios-video-height" pmd-xt cell+ value pmd-height-cell
" depth-bits" pmd-xt cell+ value pmd-depth-cell
" line-bytes" pmd-xt cell+ value pmd-stride-cell
" fb8-fillrect" pmd-xt value pmd-fill-xt
" pci-bar>pci-addr" pmd-xt value pmd-bar-xt

: pmd-width pmd-width-cell @ ;
: pmd-height pmd-height-cell @ ;
: pmd-stride pmd-stride-cell @ ;
-1 value pmd-fb

external
: dimensions  ( -- width height ) pmd-width pmd-height ;
: color!  ( r g b index -- ) 2drop 2drop ;
: fill-rectangle  ( color x y width height -- ) pmd-fill-xt execute ;

headerless
: pmd-display-install  ( -- )
  pmd-fb -1 = if
    pmd-depth-cell @ d# 32 <> if
      ." PMD boot display requires 32-bit pixels" cr -1 throw
    then
    pmd-width 0> 0= pmd-height 0> 0= or
    pmd-width d# 16384 > or pmd-height d# 16384 > or if
      ." Invalid PMD boot display dimensions" cr -1 throw
    then
    pmd-stride pmd-width d# 4 * <> if
      ." PMD boot display requires a tight linear stride" cr -1 throw
    then
    h# 10 pmd-bar-xt execute if
      dup pmd-stride pmd-height * < if
        2drop 2drop ." PMD framebuffer exceeds BAR0" cr -1 throw
      then
      " pci-map-in" $call-parent to pmd-fb
    else
      ." PMD framebuffer BAR0 is absent" cr -1 throw
    then
    pmd-fb to frame-buffer-adr
    default-font set-font
    frame-buffer-adr encode-int " address" property
    pmd-width pmd-height over char-width / over char-height / fb8-install
  then
;

: pmd-display-init  ( -- )
  pmd-width encode-int " width" property
  pmd-height encode-int " height" property
  pmd-depth-cell @ encode-int " depth" property
  pmd-stride encode-int " linebytes" property
  d# 1 encode-int " ptolemy,required-graphics-abi" property
  " powerpc-metal-dance" encode-string " ptolemy,boot-display" property
  " XRGB32BE-linear" encode-string " ptolemy,boot-pixel-layout" property
  \ ATI identity the Radeon 9700 kext reads (formerly injected by the stand's boot-command)
  " ATY,R300" encode-string " model" property
  " 113-A06500-124" encode-string " ATY,Rom#" property
  " 109-A06501-00" encode-string " ATY,Card#" property
  " 1.88" encode-string " ATY,Fcode" property
  h# 10 pmd-bar-xt execute if
    encode-int " VRAM,totalsize" property drop 2drop
  then
  ['] pmd-display-install is-install
;
pmd-display-init
end0
