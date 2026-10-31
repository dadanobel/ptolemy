#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The launcher art, regenerated with python3 launcher-art.py (in assets/; numpy + Pillow). Original procedural
textures, no source images: launcher-fur.jpg (tiger-like fur), launcher-metal.jpg (brushed metal); mark-glass.png is
mark-transparent-1024.png turned into translucent dark glass with a light rim."""
import numpy as np
from PIL import Image, ImageFilter
W,H=1200,800
y,x=np.mgrid[0:H,0:W].astype(np.float32)
def noise(scale,seed):
    r=np.random.default_rng(seed);g=r.random((H//scale+3,W//scale+3)).astype(np.float32)
    im=Image.fromarray((g*255).astype(np.uint8)).resize((W+3*scale,H+3*scale),Image.BICUBIC).crop((scale,scale,W+scale,H+scale))
    return np.asarray(im,np.float32)/255
wa=noise(220,1)*2-1;wb=noise(80,2)*2-1
u=x+wa*110+wb*22+y*0.18                          # stripes run mostly up and down, gently curved
period=92
phase=(u/period)%1.0
thick=0.08+0.14*noise(120,3)                      # stripe width varies: tapers and forks
taper=np.clip(noise(55,4)*1.6-0.25,0,1)
r0=np.random.default_rng(11).random((H,W)).astype(np.float32)
kk=np.ones(15)/15;edge=np.apply_along_axis(lambda c:np.convolve(c,kk,mode='same'),0,r0)
d=np.abs(phase-0.5)+(edge-0.5)*0.05
breaks=np.clip((noise(60,12)-0.04)*3.5,0,1)
stripe=np.clip((thick*taper-d)/0.05,0,1)*breaks
# fibres: vertical streaks
r=np.random.default_rng(9).random((H,W)).astype(np.float32)
k=np.ones(9)/9;fib=np.apply_along_axis(lambda c:np.convolve(c,k,mode='same'),0,r)
fine=np.random.default_rng(10).random((H,W)).astype(np.float32)
light=0.78+0.30*noise(200,5)
orange=np.array([0.95,0.50,0.12])[:,None,None]
col=orange*light*(0.80+0.28*fib+0.06*fine)
belly=np.clip((noise(150,6)-0.70)*2.5,0,1)[None]*0.45
col=col*(1-belly)+np.array([0.98,0.86,0.66])[:,None,None]*belly
dark=np.array([0.06,0.04,0.03])[:,None,None]*(0.8+0.4*fib)
col=col*(1-stripe)+dark*stripe
img=Image.fromarray((np.clip(col.transpose(1,2,0),0,1)*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.5))
img.save('launcher-fur.jpg',quality=86)
# brushed metal: horizontal streaks
rng=np.random.default_rng(7);m=rng.random((400,1200)).astype(np.float32)*255
k=np.ones(61)/61;st=np.apply_along_axis(lambda r:np.convolve(r,k,mode='same'),1,m)/255
metal=np.clip(np.linspace(0.62,0.42,400)[:,None]+(st-0.5)*0.55,0,1)
Image.fromarray((np.stack([metal*0.97,metal*0.98,metal],2)*255).astype(np.uint8)).save('launcher-metal.jpg',quality=88)
# the mark as dark glass
mk=Image.open('mark-transparent-1024.png').convert('RGBA');mk=mk.crop(mk.getbbox());mk.thumbnail((440,440),Image.LANCZOS)
a=np.asarray(mk,np.float32)/255;al=a[...,3];L=a[...,:3].mean(2);prof=np.clip((L-0.45)*4,0,1)*al
inner=np.asarray(Image.fromarray((al*255).astype(np.uint8)).filter(ImageFilter.MinFilter(5)),np.float32)/255;rim=np.clip(al-inner,0,1)
out=np.zeros(a.shape,np.float32);stone=al*(1-prof);sheen=np.linspace(1.25,0.75,a.shape[0])[:,None]
out[...,:3]=stone[...,None]*np.array([0.16,0.17,0.19])*sheen[...,None];out[...,3]=stone*0.62
out[...,:3]=out[...,:3]*(1-rim[...,None])+rim[...,None]*0.85;out[...,3]=np.maximum(out[...,3],rim*0.75)
out[...,:3]=out[...,:3]*(1-prof[...,None])+prof[...,None]*0.95;out[...,3]=np.maximum(out[...,3],prof)
alpha=np.clip(out[...,3],1e-4,1);rgb=np.clip(out[...,:3]/alpha[...,None],0,1)
Image.fromarray((np.dstack([rgb,out[...,3]])*255).astype(np.uint8)).save('mark-glass.png')
