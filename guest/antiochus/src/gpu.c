/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bench.h"
#include "image.h"
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#ifndef GL_SHADING_LANGUAGE_VERSION_ARB
#define GL_SHADING_LANGUAGE_VERSION_ARB 0x8b8c
#endif
#ifndef GL_RGBA16F_ARB
#define GL_RGBA16F_ARB 0x881a
#endif
#ifndef GL_FRAMEBUFFER_EXT
#define GL_FRAMEBUFFER_EXT 0x8d40
#define GL_COLOR_ATTACHMENT0_EXT 0x8ce0
#define GL_FRAMEBUFFER_COMPLETE_EXT 0x8cd5
#endif
/* The 10.4.0 SDK predates FBO declarations. Resolve only after capability discovery. */
static void (*pGenFramebuffers)(GLsizei,GLuint *);
static void (*pDeleteFramebuffers)(GLsizei,const GLuint *);
static void (*pBindFramebuffer)(GLenum,GLuint);
static void (*pFramebufferTexture2D)(GLenum,GLenum,GLenum,GLuint,GLint);
static GLenum (*pCheckFramebufferStatus)(GLenum);
static int load_fbo(void) {
    pGenFramebuffers=(void (*)(GLsizei,GLuint *))dlsym(RTLD_DEFAULT,"glGenFramebuffersEXT");
    pDeleteFramebuffers=(void (*)(GLsizei,const GLuint *))dlsym(RTLD_DEFAULT,"glDeleteFramebuffersEXT");
    pBindFramebuffer=(void (*)(GLenum,GLuint))dlsym(RTLD_DEFAULT,"glBindFramebufferEXT");
    pFramebufferTexture2D=(void (*)(GLenum,GLenum,GLenum,GLuint,GLint))dlsym(RTLD_DEFAULT,"glFramebufferTexture2DEXT");
    pCheckFramebufferStatus=(GLenum (*)(GLenum))dlsym(RTLD_DEFAULT,"glCheckFramebufferStatusEXT");
    return pGenFramebuffers && pDeleteFramebuffers && pBindFramebuffer && pFramebufferTexture2D && pCheckFramebufferStatus;
}
#ifndef kCGLRendererGenericFloatID
#define kCGLRendererGenericFloatID 0x00020400
#endif
enum { W = 512, H = 256, TW = 64, TH = 64, STRIDE = 67, GRID = 24 };
typedef struct {
    unsigned kind; const char *directory; CGLContextObj context[2]; CGLPBufferObj buffer;
    GLuint texture[2], target[2], framebuffer[2]; GLhandleARB program[2];
    unsigned char pixels[2][W * H * 4], offscreen[W * H * 4], diff[W * H * 4];
    unsigned char texels[STRIDE * TH * 4];
    GLfloat vertices[(GRID + 1) * (GRID + 1) * 3], colors[(GRID + 1) * (GRID + 1) * 3];
    GLushort indices[GRID * GRID * 6];
    int bad, saved; unsigned generation;
} Graphics;
static int extension(const char *all, const char *name) {
    size_t n = strlen(name); const char *p = all;
    if (!all) return 0;
    while ((p = strstr(p, name))) { if ((p == all || p[-1] == ' ') && (p[n] == 0 || p[n] == ' ')) return 1; p += n; }
    return 0;
}
static void quad(float x0, float y0, float x1, float y1) {
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex3f(x0,y0,0); glTexCoord2f(1,0); glVertex3f(x1,y0,0);
    glTexCoord2f(1,1); glVertex3f(x1,y1,0); glTexCoord2f(0,1); glVertex3f(x0,y1,0); glEnd();
}
static void reset(void) {
    glViewport(0,0,W,H); glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,W,0,H,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glDisable(GL_DITHER); glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D); glDisable(GL_FOG); glDisable(GL_SCISSOR_TEST); glDisable(GL_LIGHTING);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_TRUE); glStencilMask(~0u);
    glClearColor(0,0,0,1); glClearDepth(1); glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    glPixelStorei(GL_PACK_ALIGNMENT,1); glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1); glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glColor4ub(255,255,255,255); glShadeModel(GL_SMOOTH);
}
static void tile_color(unsigned x, unsigned y, unsigned phase) {
    glColor4ub((x * 17 + phase) & 255, (y * 17 + phase * 3) & 255, ((x ^ y) * 17 + phase * 7) & 255,255);
}
static void tiles(unsigned phase) {
    unsigned x,y;
    for (y=0;y<16;++y) for(x=0;x<16;++x) { tile_color(x,y,phase); quad(x*32,y*16,(x+1)*32,(y+1)*16); }
}
static void mesh(Graphics *g) {
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1.2,1.2,-1.2,1.2,-2,2);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glRotatef(21,0,1,0); glRotatef(13,1,0,0);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3,GL_FLOAT,0,g->vertices); glColorPointer(3,GL_FLOAT,0,g->colors);
    glDrawElements(GL_TRIANGLES,GRID*GRID*6,GL_UNSIGNED_SHORT,g->indices);
    glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
}
static void blended(void) {
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS,3,255); glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
    glColor4ub(32,96,160,255); quad(16,16,W-16,H-16);
    glPushMatrix();glTranslatef(0,0,-0.5f);glColor4ub(255,0,0,255);quad(16,16,W-16,H-16);glPopMatrix();
    glPushMatrix();glTranslatef(0,0,0.5f);glColor4ub(32,224,64,255);quad(32,32,112,112);glPopMatrix();
    glDepthFunc(GL_ALWAYS); glDepthMask(GL_FALSE); glStencilFunc(GL_EQUAL,3,255);
    glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST); glScissor(0,0,W/2,H);
    glColor4ub(224,64,32,128); quad(0,0,W,H);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_STENCIL_TEST); glDisable(GL_DEPTH_TEST);
}
static void draw(Graphics *g, unsigned context, int reference) {
    unsigned n, repetitions = reference ? 1 : 8;
    for(n=0;n<repetitions;++n) {
        reset();
        if (g->kind == 0) tiles(++g->generation);
        else if (g->kind == 1 || g->kind == 4 || g->kind >= 10) {
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,g->texture[context]);
            if (g->kind == 4) {
                unsigned x,y,phase=++g->generation;
                for(y=0;y<TH;++y)for(x=0;x<TW;++x){unsigned char *p=g->texels+4*(y*STRIDE+x);
                    p[0]=(x*13+y*3+phase*17)&255;p[1]=(x*7+y*11+phase*5)&255;p[2]=((x^y)*3+phase*7)&255;}
                glPixelStorei(GL_UNPACK_ROW_LENGTH,STRIDE);
                glTexSubImage2D(GL_TEXTURE_2D,0,0,0,TW,TH,GL_RGBA,GL_UNSIGNED_BYTE,g->texels);
                glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
            }
            glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE); quad(0,0,W,H);
        } else if (g->kind == 2) blended();
        else if (g->kind == 3) mesh(g);
        else if (g->kind == 5) {
            mesh(g);
            glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,W,0,H,-1,1);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glDisable(GL_DEPTH_TEST);
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,g->texture[context]);
            glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
            glColor4ub(255,255,255,128); quad(20,20,156,124); glDisable(GL_TEXTURE_2D);
            glEnable(GL_SCISSOR_TEST); glScissor(80,64,192,128); glColor4ub(64,160,224,80); quad(0,0,W,H);
        } else if (g->kind == 6) {
            pBindFramebuffer(GL_FRAMEBUFFER_EXT,g->framebuffer[context]); glViewport(0,0,W,H); tiles(++g->generation);
            pBindFramebuffer(GL_FRAMEBUFFER_EXT,0); glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,g->target[context]);
            glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE); quad(0,0,W,H);
        } else if (g->kind == 7 || g->kind == 8) {
            glUseProgramObjectARB(g->program[context]); quad(0,0,W,H); glUseProgramObjectARB(0);
        } else {
            glEnable(GL_MULTISAMPLE); glColor4ub(255,255,255,255);
            glBegin(GL_TRIANGLES); glVertex2f(32.3f,24.2f); glVertex2f(350.6f,40.1f); glVertex2f(92.4f,230.7f); glEnd();
        }
    }
}
static void run(void *opaque) {
    Graphics *g = opaque;
    if(CGLSetCurrentContext(g->context[0])) { g->bad=1; return; }
    draw(g,0,0); glFinish();
    {GLenum error=glGetError();if(error!=GL_NO_ERROR){g->bad=1;ant_number("gl_error",error);}}
}
static void analytic(Graphics *g) {
    unsigned x,y;
    for(y=0;y<H;++y) for(x=0;x<W;++x) {
        unsigned char *p=g->pixels[1]+4*(y*W+x);
        if(g->kind==0 || g->kind==6) { unsigned tx=x/32,ty=y/16;
            p[0]=(tx*17+g->generation)&255;p[1]=(ty*17+g->generation*3)&255;p[2]=((tx^ty)*17+g->generation*7)&255;p[3]=255;
        } else if(g->kind==1 || g->kind==4) {
            unsigned tx=(2*x+1)*TW/(2*W),ty=(2*y+1)*TH/(2*H);
            memcpy(p,g->texels+4*(ty*STRIDE+tx),4);
        } else if(g->kind==10) {
            unsigned tx=(2*x+1)*TW/(2*W),ty=(2*y+1)*TH/(2*H);
            p[0]=((tx%32)*255+15)/31;p[1]=((ty%32)*255+15)/31;p[2]=(((tx^ty)%32)*255+15)/31;p[3]=(tx&1)?255:0;
        } else if(g->kind==11) {
            static const unsigned char palette[4][4]={{255,0,0,255},{0,255,0,255},{170,85,0,255},{85,170,0,255}};
            unsigned tx=(2*x+1)*TW/(2*W);memcpy(p,palette[tx%4],4);
        } else if(g->kind==12) {
            unsigned tx=(2*x+1)*TW/(2*W),ty=(2*y+1)*TH/(2*H);
            p[0]=((tx%5)*255+2)/4;p[1]=((ty%5)*255+2)/4;p[2]=128;p[3]=255;
        } else if(g->kind==8) {
            unsigned band=x/32, value=0,k;
            for(k=0;k<16 && k<=band;++k) value+=((band+k)%2)==0?3:5;
            p[0]=value; p[1]=band*16; p[2]=128; p[3]=255;
        } else { /* GLSL 1.10 arithmetic, exact on these integer-valued inputs. */
            p[0]=(x/32)*17; p[1]=(y/16)*17; p[2]=64; p[3]=255;
        }
    }
}
static int raw_image(Graphics *g,const char *name,const unsigned char *p) {
    char path[4096]; FILE *f; int ok;
    if(snprintf(path,sizeof path,"%s/%s.rgba",g->directory,name)>=(int)sizeof path) return 0;
    f=fopen(path,"wb"); if(!f)return 0; ok=fwrite(p,4,W*H,f)==W*H; if(fclose(f))ok=0; return ok;
}
static int check(void *opaque) {
    Graphics *g=opaque; unsigned i, mismatched=0,maxdiff=0; int ok=1;
    if(CGLSetCurrentContext(g->context[0]))return 0;
    glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,g->pixels[0]);
    {GLenum error=glGetError();if(error!=GL_NO_ERROR || g->bad) {
        ant_number("readback_error",error);ant_text("reason","OpenGL error during rendering/readback; snapshot may be invalid");
        ant_image(g->directory,"actual",g->pixels[0],W,H);raw_image(g,"actual",g->pixels[0]);return 0;
    }}
    if(g->kind==2 || g->kind==3 || g->kind==5) {
        if(CGLSetCurrentContext(g->context[1]))return 0;
        draw(g,1,1); glFinish();
        glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,g->pixels[1]);
        if(glGetError()!=GL_NO_ERROR) { ant_text("reason","Software reference failed"); return 0; }
    } else if(g->kind==9) {
        unsigned partial=0,white=0,black=0;
        for(i=0;i<W*H;++i) { unsigned v=g->pixels[0][4*i]; if(v>0 && v<255)++partial; else if(v==255)++white;else ++black; }
        ant_number("partial_pixels",partial); ant_number("white_pixels",white);
        /* This is a coverage invariant, not an oracle for the card's sample positions. */
        ok=partial>50 && white>20000 && white<40000 && black>50000;
        memcpy(g->pixels[1],g->pixels[0],sizeof g->pixels[1]);
    } else analytic(g);
    mismatched=(unsigned)ant_compare_rgba(g->pixels[0],g->pixels[1],W*H,
        (g->kind==2 || g->kind==3 || g->kind==5)?3:1,g->diff,&maxdiff);
    ant_number("mismatched_pixels",mismatched); ant_number("compared_pixels",W*H); ant_number("max_channel_error",maxdiff);
    if(mismatched)ok=0;
    if(!g->saved || !ok) {
        if(!ant_image(g->directory,"actual",g->pixels[0],W,H) || !raw_image(g,"actual",g->pixels[0]))return 0;
        if(g->kind!=9 && (!ant_image(g->directory,"reference",g->pixels[1],W,H) ||
            !raw_image(g,"reference",g->pixels[1]) || !ant_image(g->directory,"difference",g->diff,W,H)))return 0;
        g->saved=1;
    }
    if(CGLSetCurrentContext(g->context[0]))return 0;return ok;
}
static GLhandleARB shader(GLenum type,const char *source) {
    GLhandleARB s=glCreateShaderObjectARB(type); GLint compiled=0; char log[4096]; GLsizei n=0;
    glShaderSourceARB(s,1,&source,NULL); glCompileShaderARB(s); glGetObjectParameterivARB(s,GL_OBJECT_COMPILE_STATUS_ARB,&compiled);
    glGetInfoLogARB(s,sizeof log-1,&n,log); log[n]=0; if(n)ant_text("shader_log",log);
    if(!compiled) { glDeleteObjectARB(s); return 0; } return s;
}
static int program(Graphics *g,unsigned c) {
    const char *v110="#version 110\nvoid main(){ gl_Position=ftransform(); }\n";
    const char *v120="#version 120\nvoid main(){ gl_Position=ftransform(); }\n";
    const char *f110="#version 110\nvoid main(){ vec2 t=floor(gl_FragCoord.xy/vec2(32.0,16.0)); gl_FragColor=vec4(t.x*17.0,t.y*17.0,64.0,255.0)/255.0; }\n";
    const char *f120="#version 120\nvoid main(){ float b=floor(gl_FragCoord.x/32.0); float s=0.0; for(int i=0;i<16;i++){ if(float(i)>b)break; if(mod(b+float(i),2.0)<1.0)s+=3.0;else s+=5.0; } gl_FragColor=vec4(s,b*16.0,128.0,255.0)/255.0; }\n";
    GLhandleARB vs=shader(GL_VERTEX_SHADER_ARB,g->kind==8?v120:v110),fs=shader(GL_FRAGMENT_SHADER_ARB,g->kind==8?f120:f110);
    GLint linked=0;
    if(!vs || !fs) { if(vs)glDeleteObjectARB(vs); if(fs)glDeleteObjectARB(fs); return 0; }
    g->program[c]=glCreateProgramObjectARB(); glAttachObjectARB(g->program[c],vs); glAttachObjectARB(g->program[c],fs);
    glLinkProgramARB(g->program[c]); glGetObjectParameterivARB(g->program[c],GL_OBJECT_LINK_STATUS_ARB,&linked);
    {char log[4096];GLsizei length=0;glGetInfoLogARB(g->program[c],sizeof log-1,&length,log);log[length]=0;
     ant_number("program_linked",linked);if(length)ant_text("program_log",log);}
    glDeleteObjectARB(vs); glDeleteObjectARB(fs); return linked!=0;
}
static void destroy(void *opaque) {
    Graphics *g=opaque; unsigned c;
    for(c=0;c<2;++c)if(g->context[c]) {
        CGLSetCurrentContext(g->context[c]);
        if(g->program[c])glDeleteObjectARB(g->program[c]);
        if(g->framebuffer[c])pDeleteFramebuffers(1,&g->framebuffer[c]);
        if(g->texture[c])glDeleteTextures(1,&g->texture[c]); if(g->target[c])glDeleteTextures(1,&g->target[c]);
        CGLSetCurrentContext(NULL); CGLDestroyContext(g->context[c]);
    }
    if(g->buffer)CGLDestroyPBuffer(g->buffer); free(g);
}
int ant_gpu(const char *id,const char *directory,AntWork *w) {
    static const char *names[]={"gpu-fill","gpu-texture","gpu-depth-blend","gpu-geometry","gpu-upload","gpu-scene",
        "gpu-render-texture","gpu-shader","gpu-shader-branch","gpu-msaa","gpu-packed","gpu-dxt1","gpu-float"};
    int recovery=!strcmp(id,"gpu-shader-branch-fallback");
    Graphics *g; unsigned kind,c,i,x,y,k=0; const char *extensions; CGLPixelFormatObj pf=NULL; long n=0,screen=0;
    CGLPixelFormatAttribute accel[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAPBuffer,kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,kCGLPFADepthSize,24,kCGLPFAStencilSize,8,0};
    CGLPixelFormatAttribute msaa[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAPBuffer,kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,kCGLPFADepthSize,24,kCGLPFAStencilSize,8,kCGLPFAMultisample,kCGLPFAMinimumPolicy,kCGLPFASampleBuffers,1,kCGLPFASamples,4,0};
    CGLPixelFormatAttribute soft[]={kCGLPFARendererID,kCGLRendererGenericFloatID,kCGLPFAOffScreen,kCGLPFAColorSize,32,kCGLPFAAlphaSize,8,kCGLPFADepthSize,24,kCGLPFAStencilSize,8,0};
    for(kind=0;kind<sizeof names/sizeof *names;++kind)if(!strcmp(id,names[kind]))break;
    if(recovery)kind=8;
    if(kind==sizeof names/sizeof *names)return 0;
    g=calloc(1,sizeof *g); if(!g)return 0; g->kind=kind;g->directory=directory;
    if(recovery)memmove(accel+1,accel+2,sizeof accel-2*sizeof *accel);
    ant_text("recovery_policy",recovery?"allowed; diagnostic only, hardware execution unverified":"forbidden");
    if(CGLChoosePixelFormat(kind==9?msaa:accel,&pf,&n) || !pf) {
        ant_text("reason","Requested accelerated pbuffer format unavailable"); destroy(g);return kind==9?-1:0;
    }
    if(CGLCreateContext(pf,NULL,&g->context[0])) { CGLDestroyPixelFormat(pf);destroy(g);return 0; }
    CGLDestroyPixelFormat(pf);pf=NULL;
    if(CGLCreatePBuffer(W,H,GL_TEXTURE_2D,GL_RGBA,0,&g->buffer) || CGLGetVirtualScreen(g->context[0],&screen) ||
        CGLSetPBuffer(g->context[0],g->buffer,0,0,screen) || CGLSetCurrentContext(g->context[0])) {destroy(g);return 0;}
    ant_text("gl_vendor",(const char *)glGetString(GL_VENDOR));ant_text("gl_renderer",(const char *)glGetString(GL_RENDERER));
    ant_text("gl_version",(const char *)glGetString(GL_VERSION));extensions=(const char *)glGetString(GL_EXTENSIONS);ant_text("gl_extensions",extensions);
    ant_number("width",W);ant_number("height",H);ant_number("frames_per_job",8);
    { GLint buffers=0,samples=0;glGetIntegerv(GL_SAMPLE_BUFFERS,&buffers);glGetIntegerv(GL_SAMPLES,&samples);
      ant_number("sample_buffers",buffers);ant_number("samples",samples);
      if(glGetError()!=GL_NO_ERROR){ant_text("reason","Sample-buffer query failed");destroy(g);return 0;}
      if(kind==9 && (buffers<1 || samples<2)){
          ant_text("reason","Current pbuffer has no multisample storage");destroy(g);return -1;
      } }
    if((kind==6 && !extension(extensions,"GL_EXT_framebuffer_object")) ||
       (kind==11 && !extension(extensions,"GL_EXT_texture_compression_s3tc")) ||
       (kind==12 && !extension(extensions,"GL_ARB_texture_float") && !extension(extensions,"GL_ATI_texture_float")) ||
       ((kind==7 || kind==8) && (!extension(extensions,"GL_ARB_shader_objects") || !extension(extensions,"GL_ARB_vertex_shader") || !extension(extensions,"GL_ARB_fragment_shader")))) {
        ant_text("reason","Required extension not advertised");destroy(g);return -1;
    }
    if(kind==7 || kind==8) {
        const char *version=(const char *)glGetString(GL_SHADING_LANGUAGE_VERSION_ARB);ant_text("glsl_version",version);
        if(!version || (kind==8 && atof(version)<1.2)) {ant_text("reason","GLSL 1.20 unavailable");destroy(g);return -1;}
    }
    if(kind==6 && !load_fbo()){ant_text("reason","Advertised FBO entry points unavailable");destroy(g);return 0;}
    if(kind==2 || kind==3 || kind==5) {
        if(CGLChoosePixelFormat(soft,&pf,&n) || !pf) {ant_text("reason","Independent software reference unavailable");destroy(g);return 0;}
        if(CGLCreateContext(pf,NULL,&g->context[1])) {CGLDestroyPixelFormat(pf);destroy(g);return 0;}
        CGLDestroyPixelFormat(pf);pf=NULL;
        if(CGLSetOffScreen(g->context[1],W,H,W*4,g->offscreen)) {destroy(g);return 0;}
        ant_text("oracle","Apple generic float OpenGL, same pixel, RGBA tolerance 3");
    } else ant_text("oracle",kind==9?"MSAA coverage invariant only":"Independent integer image, same pixel, RGBA tolerance 1");
    for(y=0;y<TH;++y)for(x=0;x<STRIDE;++x) {
        unsigned char *p=g->texels+4*(y*STRIDE+x);p[0]=(x*13+y*3)&255;p[1]=(x*7+y*11)&255;p[2]=(x^y)*3;p[3]=255;
    }
    for(y=0;y<=GRID;++y)for(x=0;x<=GRID;++x) {
        i=y*(GRID+1)+x;g->vertices[3*i]=2.0f*x/GRID-1;g->vertices[3*i+1]=2.0f*y/GRID-1;
        g->vertices[3*i+2]=(GLfloat)(0.15*sin(x*0.4)*cos(y*0.3));
        g->colors[3*i]=(GLfloat)x/GRID;g->colors[3*i+1]=(GLfloat)y/GRID;g->colors[3*i+2]=0.5f;
        if(x<GRID && y<GRID) {g->indices[k++]=i;g->indices[k++]=i+1;g->indices[k++]=i+GRID+1;
            g->indices[k++]=i+1;g->indices[k++]=i+GRID+2;g->indices[k++]=i+GRID+1;}
    }
    for(c=0;c<2;++c)if(g->context[c]) {
        CGLSetCurrentContext(g->context[c]);reset();glGenTextures(1,&g->texture[c]);glBindTexture(GL_TEXTURE_2D,g->texture[c]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,STRIDE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,TW,TH,0,GL_RGBA,GL_UNSIGNED_BYTE,g->texels);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        if(kind==10) {
            GLushort packed[TW*TH];
            for(y=0;y<TH;++y)for(x=0;x<TW;++x)packed[y*TW+x]=(x%32)<<11 | (y%32)<<6 | ((x^y)%32)<<1 | (x&1);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB5_A1,TW,TH,0,GL_RGBA,GL_UNSIGNED_SHORT_5_5_5_1,packed);
        } else if(kind==11) {
            unsigned char blocks[TW*TH/2];
            for(i=0;i<sizeof blocks;i+=8){blocks[i]=0;blocks[i+1]=0xf8;blocks[i+2]=0xe0;blocks[i+3]=7;
                memset(blocks+i+4,0xe4,4);}
            glCompressedTexImage2DARB(GL_TEXTURE_2D,0,GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,TW,TH,0,sizeof blocks,blocks);
        } else if(kind==12) {
            GLfloat values[TW*TH*4];
            for(y=0;y<TH;++y)for(x=0;x<TW;++x){GLfloat *p=values+4*(y*TW+x);p[0]=(x%5)*0.25f;p[1]=(y%5)*0.25f;p[2]=0.5f;p[3]=1;}
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F_ARB,TW,TH,0,GL_RGBA,GL_FLOAT,values);
        }
        if(kind==6) {
            glGenTextures(1,&g->target[c]);glBindTexture(GL_TEXTURE_2D,g->target[c]);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            pGenFramebuffers(1,&g->framebuffer[c]);pBindFramebuffer(GL_FRAMEBUFFER_EXT,g->framebuffer[c]);
            pFramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,g->target[c],0);
            if(pCheckFramebufferStatus(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT){destroy(g);return 0;}
            pBindFramebuffer(GL_FRAMEBUFFER_EXT,0);
        }
        if((kind==7 || kind==8) && !program(g,c)){destroy(g);return 0;}
        if(glGetError()!=GL_NO_ERROR){ant_text("reason","OpenGL resource setup failed");destroy(g);return 0;}
    }
    if(kind==8) {
        unsigned char pixel[4]={0,0,0,0};char value[40];
        CGLSetCurrentContext(g->context[0]);glUseProgramObjectARB(0);glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);glFinish();glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        snprintf(value,sizeof value,"%u,%u,%u,%u",pixel[0],pixel[1],pixel[2],pixel[3]);
        ant_text("clear_only_rgba",value);ant_number("clear_only_error",glGetError());
    }
    w->data=g;w->run=run;w->check=check;w->destroy=destroy;return 1;
}
