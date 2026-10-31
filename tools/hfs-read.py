#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read one data fork from a stopped VM's raw HFS+/APM image, without mounting.
Supports bare HFS+, APM and the HFS wrapper used by Tiger's system disk.
This deliberately has no write support. Extent overflow/compression/encryption
are rejected; the private benchmark volume uses ordinary inline extents.
"""
import os,struct,sys,unicodedata
from pathlib import Path
U16=lambda b,o:struct.unpack_from('>H',b,o)[0]
U32=lambda b,o:struct.unpack_from('>I',b,o)[0]
U64=lambda b,o:struct.unpack_from('>Q',b,o)[0]
class HFS:
 def __init__(self,path):
  self.fd=os.open(path,os.O_RDONLY)
  try:self.load()
  except BaseException:
   self.close();raise
 def load(self):
  self.size=os.fstat(self.fd).st_size;self.base=0;self.end=self.size
  first=self.read(512,512)
  if first[:2]==b'PM':
   count=U32(first,4)
   if not 1<=count<=4096:raise ValueError('invalid APM count')
   for i in range(count):
    p=self.read(512*(i+1),512)
    if p[:2]!=b'PM':raise ValueError('invalid APM entry')
    if p[48:80].split(b'\0')[0]==b'Apple_HFS':
     self.base=U32(p,8)*512;self.end=self.base+U32(p,12)*512
     if not 0<=self.base<self.end<=self.size:raise ValueError('invalid HFS partition bounds')
     break
   else:raise ValueError('no HFS partition')
  v=self.volume_read(self.base+1024,512)
  if v[:2]==b'BD':
   # xnu-792 bsd/hfs/hfs_format.h: HFSMasterDirectoryBlock, packed 2.
   # drAlBlSt is in 512-byte sectors; drEmbedExtent in wrapper allocation blocks.
   if v[124:126] not in [b'H+',b'HX']:raise ValueError('HFS without embedded HFS+')
   alloc=U32(v,20);start=U16(v,126);count=U16(v,128)
   if alloc<512 or alloc%512 or not count:raise ValueError('invalid HFS wrapper extent')
   embedded=self.base+U16(v,28)*512+start*alloc;end=embedded+count*alloc
   if not self.base<=embedded<end<=self.end:raise ValueError('embedded HFS+ bounds')
   self.base=embedded;self.end=end;v=self.volume_read(self.base+1024,512)
  if v[:2] not in [b'H+',b'HX']:raise ValueError('not raw HFS+/HFSX')
  self.block=U32(v,40)
  if self.block<512 or self.block>1048576 or self.block&(self.block-1):raise ValueError('invalid block size')
  total=U32(v,44)*self.block
  if total<1536 or self.base+total>self.end:raise ValueError('HFS+ volume bounds')
  self.end=self.base+total
  catalog=self.fork(v[272:352],128*1024*1024)
  ns=U16(catalog,32);node=U32(catalog,24);seen=set();self.entries={}
  if not 512<=ns<=32768 or ns&(ns-1):raise ValueError('invalid catalog node size')
  while node:
   if node in seen or (node+1)*ns>len(catalog):raise ValueError('invalid catalog leaf chain')
   seen.add(node);b=catalog[node*ns:(node+1)*ns];node=U32(b,0)
   if b[8]!=255:raise ValueError('expected catalog leaf')
   nr=U16(b,10)
   if nr>(ns-14)//2:raise ValueError('invalid catalog record count')
   for i in range(nr):
    lo=U16(b,ns-2*(i+1));hi=U16(b,ns-2*(i+2))
    if not 14<=lo<hi<=ns-2*(nr+1):raise ValueError('invalid catalog record bounds')
    r=b[lo:hi];kl=U16(r,0);parent=U32(r,2);nc=U16(r,6)
    if kl<6 or 8+nc*2>2+kl or 2+kl+2>len(r):raise ValueError('invalid catalog key')
    name=r[8:8+nc*2].decode('utf-16-be');body=r[2+kl:];kind=U16(body,0)
    if kind in [1,2]:self.entries[parent,unicodedata.normalize('NFD',name)]=(kind,body)
 def read(self,offset,n):
  if offset<0 or n<0 or offset+n>self.size:raise ValueError('image bounds')
  b=os.pread(self.fd,n,offset)
  if len(b)!=n:raise ValueError('short image read')
  return b
 def fork(self,b,limit):
  logical=U64(b,0)
  if logical>limit:raise ValueError('fork too large')
  out=bytearray()
  for i in range(8):
   start,count=struct.unpack_from('>II',b,16+8*i);n=min(count*self.block,logical-len(out))
   if n:out+=self.volume_read(self.base+start*self.block,n)
   if len(out)==logical:return bytes(out)
  raise ValueError('extent overflow is unsupported')
 def file(self,path):
  parts=[p for p in path.split('/') if p];parent=2
  if not parts or any(p in ['.','..'] for p in parts):raise ValueError('invalid guest path')
  for i,p in enumerate(parts):
   kind,b=self.entries[parent,unicodedata.normalize('NFD',p)]
   if i==len(parts)-1:
    if kind!=2:raise ValueError('not a file')
    if len(b)<168:raise ValueError('short catalog file record')
    if b[41]&0x20:raise ValueError('compressed data fork is unsupported')
    return self.fork(b[88:168],1024*1024*1024)
   if kind!=1:raise ValueError('not a directory')
   parent=U32(b,8)
 def volume_read(self,offset,n):
  if offset<self.base or offset+n>self.end:raise ValueError('HFS volume bounds')
  return self.read(offset,n)
 def close(self):
  if self.fd is not None:os.close(self.fd);self.fd=None
if __name__=='__main__':
 if len(sys.argv)!=4:raise SystemExit('usage: hfs-read.py STOPPED-RAW-IMAGE /guest/file NEW-HOST-FILE')
 h=HFS(sys.argv[1])
 try:
  data=h.file(sys.argv[2])
  with open(sys.argv[3],'xb') as out:out.write(data)
  print(f'{sys.argv[2]}: {len(data)} bytes')
 finally:h.close()
