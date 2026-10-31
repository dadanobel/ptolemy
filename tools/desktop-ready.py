#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# 8x8 reference comparison of the Finder desktop frame.
import json,sys
from pathlib import Path
sig=json.loads(Path(sys.argv[1]).read_text())
def ready(p):
 data=p.read_bytes();header=data.split(b'\n',3);w,h=map(int,header[1].split());pixels=header[3]
 if (w,h)!=(1024,768):return False
 for key,(x0,y0,x1,y1) in sig['regions'].items():
  ref=sig['blocks'][key];good=n=0
  for by in range(y0,y1,8):
   for bx in range(x0,x1,8):
    sums=[0,0,0]
    for y in range(by,by+8):
     row=(y*w+bx)*3
     for x in range(0,24,3):
      for c in range(3):sums[c]+=pixels[row+x+c]
    good+=max(abs(sums[c]//64-ref[n][c]) for c in range(3))<16;n+=1
  if good<0.9*n:return False
 return True
for p in sorted(Path(sys.argv[2]).glob('shot-*.ppm')):
 if ready(p):print(p.name);break
else:raise SystemExit('Finder signature not found')
