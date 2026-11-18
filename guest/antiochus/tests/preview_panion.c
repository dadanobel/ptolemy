/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/panion_gl.h"
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void ppm(const char *path,const unsigned char *p){FILE *f=fopen(path,"wb");int x,y;if(!f)exit(3);fprintf(f,"P6\n%d %d\n255\n",PAN_WIDTH,PAN_HEIGHT);for(y=PAN_HEIGHT-1;y>=0;--y)for(x=0;x<PAN_WIDTH;++x)fwrite(p+(y*PAN_WIDTH+x)*4,1,3,f);fclose(f);}
int main(int argc,char **argv){CGLPixelFormatAttribute a[]={kCGLPFAAccelerated,kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,kCGLPFADepthSize,24,0};CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0;
    PanScene *s=calloc(1,sizeof *s);PanGL g;PanOracle *o=malloc(sizeof *o);unsigned char *pixels=malloc(PAN_WIDTH*PAN_HEIGHT*4),*ref=malloc(PAN_WIDTH*PAN_HEIGHT*4),*diff=malloc(PAN_WIDTH*PAN_HEIGHT*4);unsigned f,frame=argc>2?atoi(argv[2]):40;int stage=argc>1?atoi(argv[1]):4,ok;char path[128];
    if(CGLChoosePixelFormat(a,&pf,&n)||!pf||CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 2;
    {GLuint fb,rb[2];glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);glGenRenderbuffersEXT(2,rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb[0]);glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,PAN_WIDTH,PAN_HEIGHT);glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb[0]);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb[1]);glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_DEPTH_COMPONENT24,PAN_WIDTH,PAN_HEIGHT);glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_RENDERBUFFER_EXT,rb[1]);if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 6;}
    if(!pan_create(s,stage)||!pan_gl_create(&g,s))return 3;for(f=1;f<=frame;++f)if(!pan_frame(s,f,PAN_WIDTH,PAN_HEIGHT))return 4;
    size_t actors=s->mesh[1].n;if(argc>3 && !strcmp(argv[3],"omit-actors"))s->mesh[1].n=0;
    if(!pan_gl_draw(&g,s,frame,PAN_WIDTH,PAN_HEIGHT))return 5;s->mesh[1].n=actors;glFinish();glReadPixels(0,0,PAN_WIDTH,PAN_HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,pixels);pan_reference(s,o,PAN_WIDTH,PAN_HEIGHT);ok=pan_compare(o,pixels,PAN_WIDTH,PAN_HEIGHT,ref,diff);
    printf("%s: probes %u, bad %u, max %u, marker %d, renderer %s\n",pan_ids[stage],o->tested,o->bad,o->maximum,pan_marker_check(pixels,PAN_WIDTH,frame),glGetString(GL_RENDERER));
    printf("Actor probes: %u\n",o->actor_tested);
    for(f=0;f<PAN_PROBES;++f)if(o->safe[f]){int x=(int)((f%PAN_PROBES_X+.5)*PAN_WIDTH/PAN_PROBES_X),y=16+(int)((f/PAN_PROBES_X+.5)*(PAN_HEIGHT-16)/PAN_PROBES_Y),k;for(k=0;k<3;++k)if(abs(pixels[(y*PAN_WIDTH+x)*4+k]-o->color[f][k])>4){printf("Mismatch: %d,%d expected %u,%u,%u actual %u,%u,%u depth %.9g actor %d\n",x,y,o->color[f][0],o->color[f][1],o->color[f][2],pixels[(y*PAN_WIDTH+x)*4],pixels[(y*PAN_WIDTH+x)*4+1],pixels[(y*PAN_WIDTH+x)*4+2],o->depth[f],o->actor[f]);break;}}
    snprintf(path,sizeof path,"build/work/preview-%d.ppm",stage);ppm(path,pixels);snprintf(path,sizeof path,"build/work/reference-%d.ppm",stage);ppm(path,ref);snprintf(path,sizeof path,"build/work/difference-%d.ppm",stage);ppm(path,diff);
    pan_gl_destroy(&g);pan_destroy(s);CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);CGLDestroyPixelFormat(pf);free(s);free(o);free(pixels);free(ref);free(diff);return ok?0:1;
}
