/* SPDX-License-Identifier: GPL-2.0-or-later
 * Reduced ports and their authors are listed in THIRD-PARTY.md. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLRenderers.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <mach/mach_time.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench.h"
#include "conformance.h"
#include "fork-view.h"
#include <math.h>

enum { W=32, H=32, BYTES=W*H*4 };
enum Kind { SAMPLE, COPY, READ, DOWNLOAD, LOGIC, BLEND, SUB, ELEMENTS, S3TC, CULL, FLAT, CI, SHADER, DEPTH, STENCIL, FOG, WRAP, COMBINE, OFFSET, CLIP };
typedef struct {
    const char *id;
    enum Kind kind;
    GLenum a,b;
    const char *source,*suite;
    unsigned tolerance;
} Case;
static const Case cases[]={
#include "conformance-cases.inc"
};
static const size_t count=sizeof cases/sizeof *cases;
static const char *version(const Case *c) {
    if(!strcmp(c->suite,"Antiochus")) return "cf-1";
    return !strcmp(c->suite,"mesa-demos") ? "8.4.0" : "62ef6b0db";
}
int ant_cf_list(void) {
    size_t i;
    for(i=0;i<count;++i)
        printf("%u\t%s\t%s\t%s\t%s\n",(unsigned)i+1,cases[i].id,cases[i].suite,version(cases+i),cases[i].source);
    return ferror(stdout)?2:0;
}
typedef struct {
    CGLContextObj ctx;
    CGLPixelFormatObj pf;
    CGLPBufferObj pb;
    GLuint fb, target, copy;
    GLint read_x, read_y;
    void (*bind_fb)(GLenum,GLuint);
    unsigned char surface[BYTES];
} Drawable;
enum { OK=1, FAIL=0, UNSUPPORTED=-1 };
static int broken;
static int reason(const char *s,int result) { ant_text("reason",s); ant_view_note(s); return result; }
static void after(void) {
    GLenum e;
    do {
        e=glGetError();
        ant_number("gl_error",e);
        if(e) broken=1;
    } while(e);
}
/* Flush the name before entering a possibly blocking driver call. */
#define STEP(call) do { if(!broken) { ant_text("step",#call); call; after(); } } while(0)
#define CGL(call) do { CGLError e; ant_text("step",#call); e=(call); ant_number("cgl_error",e); if(e) return reason(#call,FAIL); } while(0)
static int ext(const char *list,const char *name) {
    size_t n=strlen(name); const char *p=list;
    while(p && (p=strstr(p,name))) {
        if((p==list || p[-1]==' ') && (!p[n] || p[n]==' ')) return 1;
        p+=n;
    }
    return 0;
}
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
typedef GLint CglInt;
#else
typedef long CglInt;
#endif
static int drawable(Drawable *d,int software,enum Kind kind) {
    CGLPixelFormatAttribute hardware[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAPBuffer,
        kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,0,0,0};
    CGLPixelFormatAttribute generic[]={kCGLPFARendererID,kCGLRendererGenericFloatID,kCGLPFAOffScreen,
        kCGLPFAColorSize,32,kCGLPFAAlphaSize,8,0,0,0};
    CGLPixelFormatAttribute attrs[]={kCGLPFARendererID,kCGLPFAColorSize,kCGLPFAAlphaSize,
        kCGLPFADepthSize,kCGLPFAStencilSize,kCGLPFAAccelerated,kCGLPFADoubleBuffer};
    const char *keys[]={"renderer_id","color_bits","alpha_bits","depth_bits","stencil_bits","accelerated","double_buffer"};
    CglInt n=0,screen=0,value=0;
    unsigned i;
    CGLPixelFormatAttribute *pf=software?generic:hardware;
    if(kind==DEPTH || kind==OFFSET || kind==STENCIL) {
        unsigned end=0;
        while(pf[end]) ++end;
        pf[end]=kind==STENCIL?kCGLPFAStencilSize:kCGLPFADepthSize;
        pf[end+1]=kind==STENCIL?8:24;
    }
    CGLError err;
    ant_text("step","CGLChoosePixelFormat");
    err=CGLChoosePixelFormat(pf,&d->pf,&n);
    ant_number("cgl_error",err);
    if(err==kCGLBadPixelFormat) return reason("Requested CGL pixel format unavailable",UNSUPPORTED);
    if(err) return reason("CGLChoosePixelFormat failed",FAIL);
    if(!d->pf || !n) return reason("Requested CGL pixel format unavailable",UNSUPPORTED);
    CGL(CGLCreateContext(d->pf,NULL,&d->ctx));
    CGL(CGLGetVirtualScreen(d->ctx,&screen));
    for(i=0;i<sizeof attrs/sizeof *attrs;++i) {
        CGL(CGLDescribePixelFormat(d->pf,screen,attrs[i],&value));
        ant_number(keys[i],value);
    }
    if(software) { CGL(CGLSetOffScreen(d->ctx,W,H,4*W,d->surface)); }
    else {
        CGL(CGLCreatePBuffer(W,H,GL_TEXTURE_2D,GL_RGBA,0,&d->pb));
        CGL(CGLSetPBuffer(d->ctx,d->pb,0,0,screen));
    }
    CGL(CGLSetCurrentContext(d->ctx));
    return OK;
}
static int release_drawable(Drawable *d) {
    int ok=1;
#define RELEASE(call) do { CGLError err; ant_text("step",#call); err=(call); ant_number("cgl_error",err); if(err) ok=0; } while(0)
    if(d->ctx) { RELEASE(CGLSetCurrentContext(NULL)); RELEASE(CGLDestroyContext(d->ctx)); }
    if(d->pb) RELEASE(CGLDestroyPBuffer(d->pb));
    if(d->pf) RELEASE(CGLDestroyPixelFormat(d->pf));
#undef RELEASE
    return ok;
}
static void rect(int x,int y,int w,int h) {
    STEP(glRecti(x,y,x+w,y+h));
}
static void texture(void) {
    GLuint tex=0;
    STEP(glGenTextures(1,&tex)); STEP(glBindTexture(GL_TEXTURE_2D,tex));
    STEP(glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST));
    STEP(glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST));
}
static void sample_texture(GLenum mode) {
    STEP(glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,mode));
    STEP(glEnable(GL_TEXTURE_2D));
    /* glGetError is illegal between glBegin and glEnd. */
    STEP({ glBegin(GL_QUADS);
        glTexCoord2i(0,0); glVertex2i(0,0); glTexCoord2i(1,0); glVertex2i(W,0);
        glTexCoord2i(1,1); glVertex2i(W,H); glTexCoord2i(0,1); glVertex2i(0,H); glEnd(); });
}
static const GLubyte rgbw[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255}};
static GLubyte pattern_alpha(GLenum format,unsigned quadrant) {
    return format==GL_RGB8 || format==GL_RGB5 || (quadrant&1) ? 255 : 0;
}
static void pattern(int clears,GLenum format) {
    unsigned i;
    if(clears) STEP(glEnable(GL_SCISSOR_TEST));
    for(i=0;i<4;++i) {
        int x=(i%2)*W/2,y=(i/2)*H/2;
        if(clears) {
            STEP(glScissor(x,y,W/2,H/2));
            STEP(glClearColor(rgbw[i][0]/255.f,rgbw[i][1]/255.f,rgbw[i][2]/255.f,pattern_alpha(format,i)/255.f));
            STEP(glClear(GL_COLOR_BUFFER_BIT));
        } else {
            STEP(glColor4ub(rgbw[i][0],rgbw[i][1],rgbw[i][2],pattern_alpha(format,i))); rect(x,y,W/2,H/2);
        }
    }
    if(clears) STEP(glDisable(GL_SCISSOR_TEST));
}
enum { FB=0x8D40, COLOR0=0x8CE0, COMPLETE=0x8CD5, FORMAT_UNSUPPORTED=0x8CDD };
/* The source FBO is current on entry. Always leave the original drawable current. */
static void consume_fbo(Drawable *d,const Case *c,GLubyte *out) {
    if(c->kind==COPY) {
        STEP(glBindTexture(GL_TEXTURE_2D,d->copy));
        STEP(glCopyTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,0,0,W,H,0));
    } else if(c->kind==READ) {
        STEP(glReadPixels(d->read_x,d->read_y,W,H,GL_RGBA,GL_UNSIGNED_BYTE,out));
    }
    STEP(d->bind_fb(FB,0));
    STEP(glViewport(0,0,W,H));
    if(c->kind==READ) return;
    STEP(glBindTexture(GL_TEXTURE_2D,c->kind==COPY?d->copy:d->target));
    if(c->kind==DOWNLOAD) STEP(glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,out));
    else {
        sample_texture(GL_REPLACE);
        STEP(glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,out));
    }
}
static int framebuffer(Drawable *d,const Case *c,const char *extensions,GLubyte *out) {
    void (*gen)(GLsizei,GLuint *)=dlsym(RTLD_DEFAULT,"glGenFramebuffersEXT");
    d->bind_fb=dlsym(RTLD_DEFAULT,"glBindFramebufferEXT");
    void (*attach)(GLenum,GLenum,GLenum,GLuint,GLint)=dlsym(RTLD_DEFAULT,"glFramebufferTexture2DEXT");
    GLenum (*status)(GLenum)=dlsym(RTLD_DEFAULT,"glCheckFramebufferStatusEXT");
    GLint tex=0;
    int large=c->kind==READ && c->a==GL_RGBA8;
    int width=large?256:W,height=large?128:H;
    GLenum s=0;
    if(!ext(extensions,"GL_EXT_framebuffer_object")) return reason("EXT_framebuffer_object unavailable",UNSUPPORTED);
    if(c->a==0x881a && !ext(extensions,"GL_ARB_texture_float") && !ext(extensions,"GL_ATI_texture_float"))
        return reason("Floating-point textures unavailable",UNSUPPORTED);
    if(!gen || !d->bind_fb || !attach || !status) return reason("Advertised FBO entry point missing",FAIL);
    if(c->kind==COPY) {
        texture(); STEP(glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex)); d->copy=(GLuint)tex;
    }
    texture(); STEP(glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex)); d->target=(GLuint)tex;
    d->read_x=large?123:0; d->read_y=large?59:0;
    ant_number("target_width",width); ant_number("target_height",height);
    ant_number("roi_x",d->read_x); ant_number("roi_y",d->read_y);
    STEP(glTexImage2D(GL_TEXTURE_2D,0,c->a,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL));
    STEP(gen(1,&d->fb)); STEP(d->bind_fb(FB,d->fb));
    STEP(attach(FB,COLOR0,GL_TEXTURE_2D,d->target,0));
    STEP(glDrawBuffer(COLOR0)); STEP(glReadBuffer(COLOR0));
    STEP(s=status(FB)); ant_number("fbo_status",s);
    if(broken) return FAIL;
    if(s!=COMPLETE) return reason("Framebuffer incomplete",s==FORMAT_UNSUPPORTED?UNSUPPORTED:FAIL);
    STEP(glViewport(d->read_x,d->read_y,W,H));
    pattern(c->kind==SAMPLE,c->a);
    consume_fbo(d,c,out);
    return broken?FAIL:OK;
}
static void indexed_tiles(void) {
    enum { FIRST=513, TILES=128, VERTICES=4*TILES, COUNT=6*TILES };
    GLfloat vertices[FIRST+VERTICES][2]={{0,0}};
    GLushort indices[1+COUNT]={0};
    const int order[]={0,1,2,2,1,3};
    GLuint buffers[2]={0,0};
    int tile,k;
    for(tile=0;tile<TILES;++tile) {
        int row=tile/16,col=(tile%16)*2+(row&1);
        /* Scatter storage independently of the order in which tiles are drawn. */
        int base=FIRST+4*((tile*37)%TILES);
        for(k=0;k<4;++k) {
            vertices[base+k][0]=col+(k&1);
            vertices[base+k][1]=4*row+4*(k/2);
        }
        for(k=0;k<6;++k) indices[1+6*tile+k]=(GLushort)(base+order[k]);
    }
    ant_number("index_min",FIRST); ant_number("index_max",FIRST+VERTICES-1);
    ant_number("index_count",COUNT); ant_number("ebo_offset_bytes",sizeof indices[0]);
    STEP(glGenBuffers(2,buffers)); STEP(glBindBuffer(GL_ARRAY_BUFFER,buffers[0]));
    STEP(glBufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW));
    STEP(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]));
    STEP(glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof indices,indices,GL_STATIC_DRAW));
    STEP(glVertexPointer(2,GL_FLOAT,0,NULL)); STEP(glEnableClientState(GL_VERTEX_ARRAY));
    STEP(glDrawElements(GL_TRIANGLES,COUNT,GL_UNSIGNED_SHORT,(const void *)sizeof indices[0]));
}
static void elements(const Case *c) {
    const GLfloat vertices[][2]={{0,0},{2,4},{30,4},{2,25}};
    const GLubyte bytes[]={0,1,2,3};
    const GLushort shorts[]={0,1,2,3};
    const GLuint ints[]={0,1,2,3};
    const void *base=c->a==GL_UNSIGNED_BYTE?(const void *)bytes:c->a==GL_UNSIGNED_SHORT?(const void *)shorts:(const void *)ints;
    size_t stride=c->a==GL_UNSIGNED_BYTE?1:c->a==GL_UNSIGNED_SHORT?2:4;
    const void *indices=(const GLubyte *)base+stride,*v=vertices;
    if(c->b) {
        GLuint buffers[2]={0,0};
        STEP(glGenBuffers(2,buffers));
        STEP(glBindBuffer(GL_ARRAY_BUFFER,buffers[0]));
        STEP(glBufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW));
        STEP(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]));
        STEP(glBufferData(GL_ELEMENT_ARRAY_BUFFER,4*stride,base,GL_STATIC_DRAW));
        indices=(const void *)stride; v=NULL;
    }
    STEP(glVertexPointer(2,GL_FLOAT,0,v));
    STEP(glEnableClientState(GL_VERTEX_ARRAY));
    STEP(glDrawElements(GL_TRIANGLES,3,c->a,indices));
}
static void subimage(unsigned skip) {
    GLubyte patch[12][16][4],empty[BYTES]={0};
    unsigned x,y;
    for(y=0;y<12;++y) for(x=0;x<16;++x) {
        GLubyte *p=patch[y][x]; p[0]=13*x; p[1]=17*y; p[2]=x^y; p[3]=255;
    }
    texture();
    STEP(glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,empty));
    STEP(glPixelStorei(GL_UNPACK_ROW_LENGTH,16)); STEP(glPixelStorei(GL_UNPACK_SKIP_PIXELS,skip));
    STEP(glPixelStorei(GL_UNPACK_SKIP_ROWS,skip));
    STEP(glTexSubImage2D(GL_TEXTURE_2D,0,5,9,8,8,GL_RGBA,GL_UNSIGNED_BYTE,patch));
    sample_texture(GL_REPLACE);
}
static void dxt(void) {
    GLubyte data[32],block[]={0xe0,0x07,0,0,0,0,0,0};
    unsigned i;
    memset(data,0,sizeof data);
    for(i=0;i<4;++i) data[8*i+1]=0xf8;
    texture();
    STEP(glCompressedTexImage2DARB(GL_TEXTURE_2D,0,0x83f0,8,8,0,sizeof data,data));
    STEP(glCompressedTexSubImage2DARB(GL_TEXTURE_2D,0,4,0,4,4,0x83f0,sizeof block,block));
    sample_texture(GL_REPLACE);
}
static int shader_stage(GLhandleARB program,GLenum stage,const char *source) {
    GLhandleARB shader=0; GLint ok=0; GLsizei n=0;
    GLint length=0; char *log;
    ant_text("shader_stage",stage==GL_VERTEX_SHADER_ARB?"vertex":"fragment");
    ant_text("shader_source",source);
    STEP(shader=glCreateShaderObjectARB(stage));
    STEP(glShaderSourceARB(shader,1,&source,NULL)); STEP(glCompileShaderARB(shader));
    STEP(glGetObjectParameterivARB(shader,GL_OBJECT_COMPILE_STATUS_ARB,&ok));
    STEP(glGetObjectParameterivARB(shader,GL_OBJECT_INFO_LOG_LENGTH_ARB,&length));
    if(broken || length<0) return FAIL;
    log=calloc((size_t)length+1,1);
    if(!log) return reason("Shader log allocation failed",FAIL);
    STEP(glGetInfoLogARB(shader,length,&n,log)); ant_text("compile_log",log); free(log);
    if(!ok || broken) return reason("Shader compile failed",FAIL);
    STEP(glAttachObjectARB(program,shader));
    return !broken;
}
static int bands(const Case *c,const char *extensions) {
    char vs[160],fs[320]; GLhandleARB prog=0; GLint ok=0,length=0; GLsizei n=0; char *log;
    const GLubyte *v=NULL;
    if(!ext(extensions,"GL_ARB_shader_objects") || !ext(extensions,"GL_ARB_vertex_shader") ||
       !ext(extensions,"GL_ARB_fragment_shader")) return reason("Shader extensions unavailable",UNSUPPORTED);
    STEP(v=glGetString(0x8b8c)); ant_text("glsl_version",(const char *)v);
    if(broken) return FAIL;
    if(!v || atof((const char *)v)+0.0001<c->a/100.) return reason("GLSL version unavailable",UNSUPPORTED);
    snprintf(vs,sizeof vs,"#version %u\nvoid main(){gl_Position=ftransform();}",(unsigned)c->a);
    snprintf(fs,sizeof fs,"#version %u\nvoid main(){gl_FragColor=gl_FragCoord.x<16.0?"
        "vec4(1,0,0,1):vec4(0,1,0,1);}",(unsigned)c->a);
    STEP(prog=glCreateProgramObjectARB());
    if(!shader_stage(prog,GL_VERTEX_SHADER_ARB,vs) || !shader_stage(prog,GL_FRAGMENT_SHADER_ARB,fs)) return FAIL;
    STEP(glLinkProgramARB(prog));
    STEP(glGetObjectParameterivARB(prog,GL_OBJECT_LINK_STATUS_ARB,&ok));
    STEP(glGetObjectParameterivARB(prog,GL_OBJECT_INFO_LOG_LENGTH_ARB,&length));
    if(broken || length<0) return FAIL;
    log=calloc((size_t)length+1,1);
    if(!log) return reason("Program log allocation failed",FAIL);
    STEP(glGetInfoLogARB(prog,length,&n,log)); ant_text("link_log",log); free(log);
    if(!ok || broken) return reason("Shader link failed",FAIL);
    STEP(glUseProgramObjectARB(prog)); rect(0,0,W,H);
    return !broken;
}
static int core_image(Drawable *d,int software,GLubyte *out) {
    GLubyte input[BYTES];
    int x,y,rc=FAIL;
    CGContextRef bitmap=NULL;
    CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
    @try {
        NSDictionary *opts;
        CIContext *ctx=nil;
        if(!space) return reason("Color space allocation failed",FAIL);
        for(y=0;y<H;++y) for(x=0;x<W;++x) {
            GLubyte *p=input+4*(y*W+x);
            p[0]=255; p[1]=x*8; p[2]=255-x*8; p[3]=64;
        }
        opts=[NSDictionary dictionaryWithObjectsAndKeys:(id)space,kCIContextOutputColorSpace,
              (id)space,kCIContextWorkingColorSpace,[NSNumber numberWithBool:software],kCIContextUseSoftwareRenderer,nil];
        ant_text("step",software?"CI bitmap software context":"CI accelerated CGL context");
        if(software) {
            bitmap=CGBitmapContextCreate(out,W,H,8,W*4,space,(CGBitmapInfo)kCGImageAlphaPremultipliedLast);
            if(bitmap) ctx=[CIContext contextWithCGContext:bitmap options:opts];
        } else ctx=[CIContext contextWithCGLContext:d->ctx pixelFormat:d->pf options:opts];
        if(!ctx) return reason("CI context creation failed",FAIL);
        CIImage *im=[CIImage imageWithBitmapData:[NSData dataWithBytes:input length:BYTES]
            bytesPerRow:4*W size:CGSizeMake(W,H) format:kCIFormatARGB8 colorSpace:space];
        CIFilter *invert=[CIFilter filterWithName:@"CIColorInvert"];
        CIFilter *controls=[CIFilter filterWithName:@"CIColorControls"];
        if(!invert || !controls) return reason("CI filter unavailable",UNSUPPORTED);
        if(!im) return reason("CI input creation failed",FAIL);
        [invert setDefaults]; [invert setValue:im forKey:@"inputImage"];
        im=[invert valueForKey:@"outputImage"];
        if(!im) return reason("CIColorInvert returned nil",FAIL);
        [controls setDefaults]; [controls setValue:im forKey:@"inputImage"];
        [controls setValue:[NSNumber numberWithFloat:0.625f] forKey:@"inputSaturation"];
        im=[controls valueForKey:@"outputImage"];
        if(!im) return reason("CIColorControls returned nil",FAIL);
        ant_text("step","CI drawImage");
        [ctx drawImage:im inRect:CGRectMake(0,0,W,H) fromRect:CGRectMake(0,0,W,H)];
        after();
        if(bitmap) CGContextFlush(bitmap);
        rc=!broken;
    } @catch(NSException *ex) {
        ant_text("ci_exception",[[ex reason] UTF8String]); rc=reason("CI exception",FAIL);
    } @finally {
        [pool drain]; if(bitmap) CGContextRelease(bitmap); if(space) CGColorSpaceRelease(space);
    }
    return rc;
}
static void triangle(int cull) {
    if(cull) { STEP(glCullFace(GL_FRONT_AND_BACK)); STEP(glEnable(GL_CULL_FACE)); }
    else STEP(glShadeModel(GL_FLAT));
    STEP({ glBegin(GL_TRIANGLES);
        glColor3ub(0,0,255); glVertex2i(28,4);
        glColor3ub(255,0,0); glVertex2i(28,28);
        glColor3ub(0,255,0); glVertex2i(4,16); glEnd(); });
}
static void depth_case(GLenum func) {
    STEP(glEnable(GL_DEPTH_TEST)); STEP(glDepthFunc(GL_ALWAYS));
    STEP(glClearDepth(1)); STEP(glClear(GL_DEPTH_BUFFER_BIT));
    int i;
    for(i=0;i<3;++i) {
        STEP(glDepthRange(0.25*i,0.25*i)); rect(i*10,0,10,H);
    }
    STEP(glDepthRange(0.25,0.25)); STEP(glDepthFunc(func));
    STEP(glColor4ub(25,215,40,255)); rect(0,0,W,H);
}
static void stencil_case(GLenum op) {
    int increment=op==GL_INCR || op==GL_INCR_WRAP;
    int start=increment?255:0;
    int expected=op==GL_INCR_WRAP?0:op==GL_DECR_WRAP?255:start;
    STEP(glClearStencil(start)); STEP(glClear(GL_STENCIL_BUFFER_BIT));
    STEP(glEnable(GL_STENCIL_TEST)); STEP(glStencilFunc(GL_ALWAYS,0,255));
    STEP(glStencilOp(GL_KEEP,GL_KEEP,op));
    int half=W/2;
    rect(0,0,half,H);
    STEP(glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP));
    STEP(glStencilFunc(GL_EQUAL,expected,255)); STEP(glColor4ub(0,255,0,255));
    rect(0,0,W,H);
}
static void fog_case(GLenum mode) {
    const GLfloat color[]={0.125f,0.25f,0.5f,1};
    int i;
    /* Keep eye-space x and y small for both radial distance and |z| fog. */
    STEP(glMatrixMode(GL_PROJECTION)); STEP(glScalef(65536,65536,1));
    STEP(glMatrixMode(GL_MODELVIEW));
    STEP(glEnable(GL_FOG)); STEP(glFogi(GL_FOG_MODE,mode)); STEP(glFogfv(GL_FOG_COLOR,color));
    STEP(glFogf(GL_FOG_START,0)); STEP(glFogf(GL_FOG_END,1)); STEP(glFogf(GL_FOG_DENSITY,2));
    for(i=0;i<4;++i) {
        STEP(glLoadIdentity()); STEP(glTranslatef(0,0,-0.25f*i)); STEP(glScalef(1.f/65536,1.f/65536,1));
        rect(i*8,0,8,H);
    }
}
static void wrap_case(GLenum mode,int linear) {
    const GLfloat border[]={0,1,1,0};
    texture(); STEP(glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,rgbw));
    if(linear) {
        STEP(glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR));
        STEP(glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR));
        STEP(glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border));
    }
    STEP(glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,mode));
    STEP(glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,mode));
    STEP(glMatrixMode(GL_TEXTURE)); STEP(glTranslatef(-0.5f,-0.5f,0)); STEP(glScalef(2,2,1));
    STEP(glMatrixMode(GL_MODELVIEW)); sample_texture(GL_REPLACE);
}

static void combine_case(GLenum op) {
    const GLfloat weight[]={0.25f,0.5f,0.75f,1};
    const GLubyte tile[][4]={{20,70,190,255},{100,140,210,255},{230,180,150,255},{70,240,45,255}};
    texture(); STEP(glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,tile));
    STEP(glColor4ub(45,90,135,255));
    STEP(glTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,weight));
    STEP(glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,op));
    STEP(glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_TEXTURE));
    STEP(glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_PRIMARY_COLOR));
    STEP(glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_RGB,GL_CONSTANT));
    STEP(glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND2_RGB,GL_SRC_COLOR));
    sample_texture(GL_COMBINE);
}
static void offset_case(int positive) {
    STEP(glClearDepth(1)); STEP(glClear(GL_DEPTH_BUFFER_BIT));
    STEP(glEnable(GL_DEPTH_TEST)); STEP(glDepthFunc(GL_LESS));
    STEP(glColor3ub(20,160,40)); rect(0,0,W,H);
    STEP(glEnable(GL_POLYGON_OFFSET_FILL)); STEP(glPolygonOffset(0,positive?4:-4));
    STEP(glColor3ub(200,35,85)); rect(4,6,20,16);
}
static void clip_case(void) {
    const GLdouble plane[][4]={{1,0,0,-4},{-1,0,0,25},{0,1,0,-7},{0,-1,0,22}};
    unsigned i;
    for(i=0;i<4;++i) { STEP(glClipPlane(GL_CLIP_PLANE0+i,plane[i])); STEP(glEnable(GL_CLIP_PLANE0+i)); }
    rect(0,0,W,H);
}
static int render(Drawable *d,const Case *c,int sw,GLubyte *out) {
    const GLubyte *renderer=NULL,*v=NULL,*extensions=NULL;
    int captured=0,rc=OK;
    broken=0;
    rc=drawable(d,sw,c->kind);
    if(rc!=OK) return rc;
    STEP(renderer=glGetString(GL_RENDERER)); ant_text("gl_renderer",(const char *)renderer);
    STEP(v=glGetString(GL_VERSION)); ant_text("gl_version",(const char *)v);
    STEP(extensions=glGetString(GL_EXTENSIONS)); ant_text("gl_extensions",(const char *)extensions);
    {
        GLint bits=0;
        GLenum properties[]={GL_RED_BITS,GL_GREEN_BITS,GL_BLUE_BITS,GL_ALPHA_BITS,GL_DEPTH_BITS,GL_STENCIL_BITS};
        const char *keys[]={"drawable_red_bits","drawable_green_bits","drawable_blue_bits","drawable_alpha_bits","drawable_depth_bits","drawable_stencil_bits"};
        unsigned k;
        for(k=0;k<6;++k) {
            STEP(glGetIntegerv(properties[k],&bits));
            if(broken) return reason("Drawable bit query failed",FAIL);
            ant_number(keys[k],bits);
            if((k<4 && bits<8) || (k==4 && (c->kind==DEPTH || c->kind==OFFSET) && bits==0) ||
               (k==5 && c->kind==STENCIL && bits!=8))
                return reason("Required drawable channel precision unavailable",UNSUPPORTED);
        }
    }
    STEP(glViewport(0,0,W,H)); STEP(glDisable(GL_DITHER));
    STEP(glMatrixMode(GL_PROJECTION)); STEP(glLoadIdentity()); STEP(glOrtho(0,W,0,H,-1,1));
    STEP(glMatrixMode(GL_MODELVIEW)); STEP(glLoadIdentity());
    STEP(glClearColor(0.25f,0.5f,0.75f,1)); STEP(glClear(GL_COLOR_BUFFER_BIT));
    if(broken) return FAIL;
    switch(c->kind) {
    case SAMPLE: case COPY: case READ: case DOWNLOAD:
        if(sw) pattern(0,c->a);
        else { rc=framebuffer(d,c,(const char *)extensions,out); captured=1; }
        break;
    case LOGIC:
        STEP(glColor4ub(0x53,0x96,0xe1,0x35)); rect(0,0,W,H);
        STEP(glEnable(GL_COLOR_LOGIC_OP)); STEP(glLogicOp(c->a));
        STEP(glColor4ub(0xb4,0x67,0x28,0xcb)); rect(2,3,W-4,H-6);
        break;
    case BLEND:
        STEP(glColor4ub(53,96,181,117)); rect(0,0,W,H);
        STEP(glEnable(GL_BLEND)); STEP(glBlendFunc(c->a,c->b));
        STEP(glColor4ub(174,97,58,83)); rect(2,3,W-4,H-6);
        break;
    case DEPTH: depth_case(c->a); break;
    case STENCIL: stencil_case(c->a); break;
    case FOG: fog_case(c->a); break;
    case COMBINE: combine_case(c->a); break;
    case OFFSET: offset_case(c->a); break;
    case CLIP: clip_case(); break;
    case WRAP: wrap_case(c->a,c->b); break;
    case SUB: subimage(c->a); break;
    case ELEMENTS:
        if(c->a==GL_UNSIGNED_SHORT && c->b) indexed_tiles(); else elements(c);
        break;
    case S3TC:
        if(sw) {
            STEP(glColor4ub(255,0,0,255)); rect(0,0,W,H);
            STEP(glColor4ub(0,255,0,255)); rect(W/2,0,W/2,H/2);
        } else if(ext((const char *)extensions,"GL_EXT_texture_compression_s3tc")) dxt();
        else rc=reason("S3TC not advertised",UNSUPPORTED);
        break;
    case CULL: case FLAT: triangle(c->kind==CULL); break;
    case CI: rc=core_image(d,sw,out); captured=sw; break;
    case SHADER:
        if(sw) {
            STEP(glColor4ub(255,0,0,255)); rect(0,0,W/2,H);
            STEP(glColor4ub(0,255,0,255)); rect(W/2,0,W/2,H);
        } else rc=bands(c,(const char *)extensions);
        break;
    }
    if(rc==OK && !captured) STEP(glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,out));
    return broken?reason("OpenGL step failed; see gl_error",FAIL):rc;
}
static int save(const char *dir,const char *name,const GLubyte *buf) {
    char path[4096]; FILE *f; int ok;
    if(!ant_image(dir,name,buf,W,H) ||
       snprintf(path,sizeof path,"%s/%s.rgba",dir,name)>=(int)sizeof path) return FAIL;
    f=fopen(path,"wb"); if(!f) return FAIL;
    ok=fwrite(buf,1,BYTES,f)==BYTES; if(fclose(f)) ok=0;
    return ok;
}
static void report_pixel(const char *key,const GLubyte *p) {
    char s[32]; snprintf(s,sizeof s,"%u,%u,%u,%u",p[0],p[1],p[2],p[3]); ant_text(key,s);
}
static int compare(const Case *c,const char *dir,const GLubyte *a,const GLubyte *b,const char *difference) {
    unsigned p,k,bad=0,maximum=0; GLubyte diff[BYTES];
    for(p=0;p<W*H;++p) {
        unsigned largest=0;
        for(k=0;k<4;++k) {
            unsigned v=abs((int)a[4*p+k]-b[4*p+k]);
            if(v>largest) largest=v;
        }
        memset(diff+4*p,largest,3); diff[4*p+3]=255;
        if(largest>maximum) maximum=largest;
        if(largest>c->tolerance) {
            if(!bad) {
                ant_number("first_x",p%W); ant_number("first_y",p/W);
                report_pixel("expected_rgba",b+4*p); report_pixel("actual_rgba",a+4*p);
            }
            ++bad;
        }
    }
    ant_number("bad_pixels",bad); ant_number("max_channel_error",maximum);
    if(!ant_image(dir,difference,diff,W,H)) return reason("Difference write failed",FAIL);
    return bad?reason("Pixel mismatch",FAIL):OK;
}
static int reread(Drawable *d,const Case *c,const char *dir,const GLubyte *primary) {
    const GLubyte marker[4]={255,0,255,255};
    GLubyte actual[BYTES],expected[BYTES];
    int x,y;
    ant_text("probe","same_fbo_rewrite");
    STEP(glDisable(GL_TEXTURE_2D));
    STEP(d->bind_fb(FB,d->fb)); STEP(glViewport(d->read_x,d->read_y,W,H));
    STEP(glColor4ubv(marker)); rect(7,3,12,5);
    consume_fbo(d,c,actual);
    if(broken) return reason("Same FBO rewrite or read failed",FAIL);
    memcpy(expected,primary,BYTES);
    for(y=3;y<8;++y) for(x=7;x<19;++x) memcpy(expected+4*(y*W+x),marker,4);
    ant_view_note("Same texture after partial rewrite"); ant_view_frame(actual,W,H);
    if(!save(dir,"same-fbo-actual",actual) || !save(dir,"same-fbo-reference",expected))
        return reason("Same FBO image write failed",FAIL);
    return compare(c,dir,actual,expected,"same-fbo-difference");
}
static int followup(const Case *c,const char *dir) {
    GLubyte actual[BYTES],expected[BYTES];
    const GLubyte base[4]={30,70,130,255},mark[4]={220,175,45,255};
    int x,y;
    ant_text("probe","post_read_progress");
    STEP(glDisable(GL_TEXTURE_2D));
    STEP(glColor4ubv(base)); rect(0,0,W,H);
    STEP(glColor4ubv(mark)); rect(7,3,12,5);
    STEP(glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,actual));
    if(broken) return reason("Post-read GL operation failed",FAIL);
    for(y=0;y<H;++y) for(x=0;x<W;++x)
        memcpy(expected+4*(y*W+x),x>=7 && x<19 && y>=3 && y<8?mark:base,4);
    ant_view_note("Progress after readback"); ant_view_frame(actual,W,H);
    if(!save(dir,"post-read-actual",actual) || !save(dir,"post-read-reference",expected))
        return reason("Post-read artifact write failed",FAIL);
    return compare(c,dir,actual,expected,"post-read-difference");
}
static int execute(const Case *c,const char *dir) {
    GLubyte ref[BYTES],actual[BYTES]; int side,rc;
    for(side=0;side<2;++side) {
        Drawable d;
        GLubyte *img=side?actual:ref;
        memset(&d,0,sizeof d); memset(img,side?0xb3:0x47,BYTES);
        ant_text("side",side?"accelerated":"reference");
        ant_view_note(side?"Accelerated GL / readback":"Apple software reference");
        rc=render(&d,c,!side,img);
        if(rc==OK) ant_view_frame(img,W,H);
        if(rc==OK && !save(dir,side?"actual":"reference",img)) rc=reason("Image write failed",FAIL);
        if(side && rc==OK) {
            ant_text("probe","primary");
            rc=compare(c,dir,actual,ref,"difference");
            if(rc==OK && c->kind<=DOWNLOAD) rc=reread(&d,c,dir,ref);
            if(rc==OK && c->kind<=DOWNLOAD) rc=followup(c,dir);
        }
        if(!release_drawable(&d)) return reason("CGL cleanup failed",FAIL);
        if(rc!=OK) return rc;
        if(!side && c->kind<=DOWNLOAD) {
            int x,y;
            for(y=0;y<H;++y) for(x=0;x<W;++x)
            {
                unsigned q=(x>=W/2)+2*(y>=H/2);
                const GLubyte *p=ref+4*(y*W+x);
                if(memcmp(p,rgbw[q],3) || p[3]!=pattern_alpha(c->a,q))
                    return reason("Software RGBW oracle failed its control",FAIL);
            }
        }
        if(!side && c->kind==FOG) {
            int first=ref[4*(16*W+4)],last=ref[4*(16*W+28)];
            ant_number("fog_red_span",first-last);
            if(first-last<100) return reason("Software fog reference lacks contrast",FAIL);
        }
        if(!side && c->kind==CI && ref[0]<=ref[(W-1)*4]+16)
            return reason("Software CI oracle failed its inverted ramp control",FAIL);
    }
    return OK;
}
static void marker(const char *event,unsigned ordinal,const char *id,uint64_t t) {
    printf("cf_marker\t%s %u %s %llu\n",event,ordinal,id,(unsigned long long)t); fflush(stdout);
}
int ant_cf_run(const char *id,const char *dir) {
    size_t i; int rc;
    uint64_t start,end;
    mach_timebase_info_data_t tb;
    for(i=0;i<count;++i) if(!strcmp(id,cases[i].id)) break;
    if(i==count) { reason("Unknown conformance id",FAIL); ant_text("status","fail"); return 64; }
    const Case *c=cases+i;
    start=mach_absolute_time(); marker("begin",(unsigned)i+1,id,start);
    ant_text("cf_protocol","2"); ant_text("suite",c->suite); ant_text("source_test",c->source);
    ant_text("source_version",version(c)); ant_number("tolerance_rgba8",c->tolerance);
    ant_number("width",W); ant_number("height",H);
    ant_number("argument_a",c->a); ant_number("argument_b",c->b);
    if(c->kind!=SHADER) ant_text("shader_logs",c->kind==CI?"Core Image owns generated programs":"no application shader");
    if(ant_view_open(id,W,H)) rc=execute(c,dir);
    else rc=reason("Visible guest window unavailable",FAIL);
    ant_view_close(rc==OK?"Pass":rc==UNSUPPORTED?"Unsupported":"Fail - see result.tsv");
    end=mach_absolute_time(); mach_timebase_info(&tb);
    ant_number("seconds",(end-start)*(double)tb.numer/tb.denom*1e-9);
    ant_text("status",rc==OK?"pass":rc==UNSUPPORTED?"unsupported":"fail");
    marker("end",(unsigned)i+1,id,end);
    return ferror(stdout)?2:rc==FAIL?1:0;
}

#include "shader-fork.inc"
