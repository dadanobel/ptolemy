/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CPU reference executor for OpticsDraw (tests only, never linked into the product): runs the decoded PVS/US programs
 * with the foundation interpreter, rasterizes triangles with top-left pixel centres and perspective-correct
 * varyings, samples the decoded textures and applies the OpticsDraw blend. It checks the draw DESCRIPTION against the
 * recorded trace (optics-frontend-replay.c); the product executes the same description on Metal. */
#include "frontend.h"
#include "pixel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { const OpticsDraw *d; uint8_t *tex[OPTICS_DRAW_TEXTURES]; } Ctx;
static float clamp01(float x) { return x<0?0:x>1?1:x; }
/* OpticsDraw blend (Metal factor/operation enum values), 8-bit target: clamped inputs and result. */
static float factor(unsigned f,int k,const float *s,const float *d) {
    float sa=clamp01(s[3]);
    switch(f){case 0:return 0;case 1:return 1;case 2:return clamp01(s[k]);case 3:return 1-clamp01(s[k]);case 4:return sa;case 5:return 1-sa;
        case 6:return d[k];case 7:return 1-d[k];case 8:return d[3];case 9:return 1-d[3];default:return k==3?1:fminf(sa,1-d[3]);}
}
static float blend(uint32_t b,int k,const float *s,const float *d) {
    float sv=clamp01(s[k]); if(!b) return sv;
    unsigned sh=k==3?12:0,op=b>>(sh+8)&7; float x=sv*factor(b>>sh&15,k,s,d),y=d[k]*factor(b>>(sh+4)&15,k,s,d);
    return clamp01(op==0?x+y:op==1?x-y:op==2?y-x:op==3?fminf(sv,d[k]):fmaxf(sv,d[k]));
}
static void texel(const Ctx *c,unsigned u,int x,int y,float out[4]) {
    const OpticsTexture *t=&c->d->texture[u];
    if(x<0)x=0; if(y<0)y=0; if((uint32_t)x>=t->width)x=(int)t->width-1; if((uint32_t)y>=t->height)y=(int)t->height-1;
    uint8_t rgba[4]; OpticsPixelLayout l={t->layout,{t->select[0],t->select[1],t->select[2],t->select[3]}};
    optics_pixel_reference(l,c->tex[u]+((size_t)y*t->pitch+(size_t)x)*t->bpp,rgba);
    for(unsigned k=0;k<4;k++) out[k]=rgba[k]/255.0f;
}
static bool sample(void *o,unsigned unit,unsigned op,const float coord[4],float result[4]) {
    Ctx *c=o; if(unit>=OPTICS_DRAW_TEXTURES || !c->tex[unit]) return false;
    const OpticsTexture *t=&c->d->texture[unit]; float s=coord[0],q=coord[1];
    if(op==3 && coord[3]!=0){s/=coord[3];q/=coord[3];}
    float fx=s*t->width-0.5f,fy=q*t->height-0.5f;
    if(!t->linear){texel(c,unit,(int)floorf(fx+0.5f),(int)floorf(fy+0.5f),result);return true;}
    int x0=(int)floorf(fx),y0=(int)floorf(fy); float ax=fx-x0,ay=fy-y0,a[4],b[4],e[4],f[4];
    texel(c,unit,x0,y0,a);texel(c,unit,x0+1,y0,b);texel(c,unit,x0,y0+1,e);texel(c,unit,x0+1,y0+1,f);
    for(int k=0;k<4;k++) result[k]=(a[k]*(1-ax)+b[k]*ax)*(1-ay)+(e[k]*(1-ax)+f[k]*ax)*ay;
    return true;
}
int optics_draw_reference(void *opaque,PtRadeon *dev,const OpticsDraw *d,const char **reason) {
    (void)opaque; Ctx c={d,{0}}; int rc=1;
    for(unsigned u=0;u<OPTICS_DRAW_TEXTURES;u++) if(d->texture_mask>>u&1) {
        const OpticsTexture *t=&d->texture[u]; size_t n=(size_t)t->pitch*t->height*t->bpp;
        c.tex[u]=malloc(n); if(!c.tex[u] || !optics_frontend_gpu_read(dev,t->offset,c.tex[u],n)){*reason="texture outside GPU memory";goto out;}
        optics_texture_unswap(t,c.tex[u],n);
    }
    OpticsVertexOut *vo=calloc(d->vertices?d->vertices:1,sizeof *vo); PtR2ShaderIO *io=calloc(1,sizeof *io); PtR2ShaderError e;
    if(!vo || !io || !optics_draw_vertices(d,vo,reason)){free(vo);free(io);goto out;}
    for(uint32_t t=0;t+2<d->indices;t+=3) {
        const uint32_t *ix=d->index+t; float *A=vo[ix[0]].window,*B=vo[ix[1]].window,*C=vo[ix[2]].window;
        float area=(B[0]-A[0])*(C[1]-A[1])-(B[1]-A[1])*(C[0]-A[0]); if(area==0) continue;
        int x0=(int)floorf(fminf(A[0],fminf(B[0],C[0]))),x1=(int)ceilf(fmaxf(A[0],fmaxf(B[0],C[0])));
        int y0=(int)floorf(fminf(A[1],fminf(B[1],C[1]))),y1=(int)ceilf(fmaxf(A[1],fmaxf(B[1],C[1])));
        if(x0<d->scissor[0])x0=d->scissor[0]; if(y0<d->scissor[1])y0=d->scissor[1]; if(x1>d->scissor[2])x1=d->scissor[2]; if(y1>d->scissor[3])y1=d->scissor[3];
        for(int y=y0;y<y1;y++) {
            uint8_t row[4*8192]; size_t n=(size_t)(x1-x0)*4; if(x1<=x0) continue;
            uint64_t at=(uint64_t)d->cb_offset+((uint64_t)y*d->cb_pitch+(uint64_t)x0)*4;
            if(!optics_frontend_gpu_read(dev,at,row,n)){*reason="colour target outside GPU memory";goto out2;}
            for(int x=x0;x<x1;x++) {
                float px=x+0.5f,py=y+0.5f;
                float wa=((B[0]-px)*(C[1]-py)-(B[1]-py)*(C[0]-px))/area,wb=((C[0]-px)*(A[1]-py)-(C[1]-py)*(A[0]-px))/area,wc=1-wa-wb;
                /* top-left fill convention approximated by a half-open test on exact edges */
                if(wa<0||wb<0||wc<0) continue;
                float ia=wa/A[3],ib=wb/B[3],ic=wc/C[3],sum=ia+ib+ic;
                memset(io,0,sizeof *io); memcpy(io->constant,d->us_constant,sizeof d->us_constant);
                for(unsigned k=0;k<d->varyings;k++) for(unsigned q=0;q<4;q++)
                    io->input[d->us_input[k]][q]=(vo[ix[0]].varying[k][q]*ia+vo[ix[1]].varying[k][q]*ib+vo[ix[2]].varying[k][q]*ic)/sum;
                if(pt_r2_shader_execute(d->us,&d->fi,io,sample,&c,&e)!=PT_R2_SHADER_OK){*reason="US reference run failed";goto out2;}
                if(io->killed) continue;
                float *s=io->output[d->color_output]; uint8_t *p=row+(size_t)(x-x0)*4;   /* ARGB8888 LE: B,G,R,A */
                float dst[4]={p[2]/255.0f,p[1]/255.0f,p[0]/255.0f,p[3]/255.0f},out[4];
                for(int k=0;k<4;k++) out[k]=blend(d->blend,k,s,dst);
                uint8_t q[4]={(uint8_t)lrintf(out[2]*255),(uint8_t)lrintf(out[1]*255),(uint8_t)lrintf(out[0]*255),(uint8_t)lrintf(out[3]*255)};
                static const int ch[4]={2,1,0,3}; for(int k=0;k<4;k++) if(d->color_mask>>k&1) p[ch[k]]=q[ch[k]];
            }
            if(!optics_frontend_gpu_write(dev,at,row,n)){*reason="colour target outside GPU memory";goto out2;}
        }
    }
    rc=0;
out2: free(io); free(vo);
out: for(unsigned u=0;u<OPTICS_DRAW_TEXTURES;u++) free(c.tex[u]);
    return rc;
}
