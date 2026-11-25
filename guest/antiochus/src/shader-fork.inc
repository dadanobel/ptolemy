/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Antiochus fork of Piglit shader_runner (62ef6b0db). See THIRD-PARTY.md. */
enum SrOp {
 SR_VS,SR_FS,SR_FP,SR_VP,SR_PASSVS,SR_ALL3,SR_ALL4,SR_ARRAYS,SR_BOX4,SR_CHECKER,
 SR_CLEAR,SR_CLEARCOLOR,SR_CLEARZ,SR_COLOR,SR_DISABLE,SR_ENABLE,SR_HINT,SR_LINKERR,SR_LINKOK,
 SR_MIP,SR_ORTHO,SR_ORTHOWINDOW,SR_PIX3,SR_PIX4,SR_PLANE,SR_P_ENV_VP,SR_P_LOCAL_FP,
 SR_P_LOCAL_VP,SR_QUAD,SR_QUADT,SR_REL3,SR_REL4,SR_RGB1,SR_RGB2,SR_RGB3,SR_TCOORD,
 SR_TEXPARAM,SR_U_FLOAT,SR_U_INT,SR_U_IVEC2,SR_U_IVEC4,SR_U_MAT2X2,SR_U_MAT4,SR_U_UINT,
 SR_U_VEC2,SR_U_VEC3,SR_U_VEC4,SR_WARN4,SR_Z1,SR_Z2,SR_ZPIX
};
typedef struct { enum SrOp type; int line; const char *source; } SrStage;
typedef struct { int line; const char *text; enum SrOp op; GLenum arg; const char *name; GLfloat v[16]; } SrCommand;
typedef struct {
    const char *id,*path,*hash,*extensions;
    int glsl,gl,w,h,depth,arrays,stage,nstage,cmd,ncmd;
} SrCase;
#include "shader-corpus.inc"
typedef struct {
    const SrCase *c;
    const char *dir,*side;
    GLhandleARB program;
    GLbitfield clear;
    GLint linked; int drew,warnings,probes,failed_probes;
    GLubyte *rgba;
} SrRun;

static int sr_file(const char *dir,const char *name,const void *bytes,size_t len) {
    char path[4096]; FILE *f; int ok;
    if(snprintf(path,sizeof path,"%s/%s",dir,name)>=(int)sizeof path || !(f=fopen(path,"wb"))) return 0;
    ok=fwrite(bytes,1,len,f)==len; if(fclose(f)) ok=0; return ok;
}
static int sr_save(SrRun *r,const char *name,const GLubyte *bytes) {
    char raw[128]; snprintf(raw,sizeof raw,"%s.rgba",name);
    return sr_file(r->dir,raw,bytes,(size_t)r->c->w*r->c->h*4) &&
           ant_image(r->dir,name,bytes,r->c->w,r->c->h);
}
static int sr_object_log(SrRun *r,GLhandleARB obj,const char *kind,int index) {
    GLint length=0; GLsizei got=0; char file[96],*log;
    STEP(glGetObjectParameterivARB(obj,GL_OBJECT_INFO_LOG_LENGTH_ARB,&length));
    if(broken || length<0) return 0;
    log=calloc((size_t)length+1,1); if(!log) return reason("Log allocation failed",FAIL);
    STEP(glGetInfoLogARB(obj,length,&got,log));
    snprintf(file,sizeof file,"%s-%s-%d.log",r->side,kind,index);
    int ok=!broken && sr_file(r->dir,file,log,got);
    ant_text("driver_log",file); ant_text(kind,log); free(log);
    return ok?OK:reason("Driver log capture failed",FAIL);
}
static int sr_program(SrRun *r) {
    const SrCase *c=r->c;
    if(c->glsl) STEP(r->program=glCreateProgramObjectARB());
    for(int i=0;i<c->nstage;++i) {
        const SrStage *s=sr_stages+c->stage+i; const char *body=s->source;
        char prefix[48]="",name[96]; char *source; size_t len;
        if(s->type==SR_PASSVS) body="attribute vec4 piglit_vertex;\nvoid main(){gl_Position=piglit_vertex;}\n";
        if(c->glsl && !strstr(body,"#version ")) snprintf(prefix,sizeof prefix,"#version %d\n",c->glsl);
        len=strlen(prefix)+strlen(body);
        source=malloc(len+1); if(!source) return reason("Stage allocation failed",FAIL);
        strcpy(source,prefix); strcat(source,body); const char *input=source;
        snprintf(name,sizeof name,"stage-%02d.%s",i,(s->type==SR_FS?"frag":s->type==SR_VP?"vp":s->type==SR_FP?"fp":"vert"));
        ant_text("stage_file",name); ant_number("upstream_first_source_line",s->line);
        ant_number("injected_version_lines",*prefix?1:0);
        if(!sr_file(r->dir,name,source,len)) { free(source); return reason("Stage file write failed",FAIL); }
        if(s->type==SR_VP || s->type==SR_FP) {
            GLuint obj=0; GLint pos=-1,limit=0;
            GLenum t=s->type==SR_VP?GL_VERTEX_PROGRAM_ARB:GL_FRAGMENT_PROGRAM_ARB;
            STEP(glGenProgramsARB(1,&obj)); STEP(glBindProgramARB(t,obj));
            STEP(glProgramStringARB(t,GL_PROGRAM_FORMAT_ASCII_ARB,(GLsizei)len,source)); free(source);
            /* ARB error details are available even after INVALID_OPERATION. */
            ant_text("step","ARB upload diagnostics"); glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&pos);
            ant_number("arb_error_position",pos);
            const GLubyte *msg=glGetString(GL_PROGRAM_ERROR_STRING_ARB);
            ant_text("arb_driver_log",msg?(const char *)msg:""); after();
            if(broken || pos!=-1) return reason("ARB program upload failed",FAIL);
            STEP(glGetProgramivARB(t,GL_PROGRAM_INSTRUCTIONS_ARB,&limit)); ant_number("arb_instructions",limit);
            STEP(glGetProgramivARB(t,GL_MAX_PROGRAM_NATIVE_INSTRUCTIONS_ARB,&limit)); ant_number("arb_native_instruction_limit",limit);
            STEP(glGetProgramivARB(t,GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB,&limit)); ant_number("arb_under_native_limits",limit);
            STEP(glEnable(t));
        } else {
            GLhandleARB obj=0; GLint compiled=0;
            STEP(obj=glCreateShaderObjectARB(s->type==SR_FS?GL_FRAGMENT_SHADER_ARB:GL_VERTEX_SHADER_ARB));
            STEP(glShaderSourceARB(obj,1,&input,NULL)); free(source); STEP(glCompileShaderARB(obj));
            STEP(glGetObjectParameterivARB(obj,GL_OBJECT_COMPILE_STATUS_ARB,&compiled));
            ant_number("compile_ok",compiled);
            if(!sr_object_log(r,obj,"compile_log",i) || !compiled) return reason("GLSL compilation failed",FAIL);
            STEP(glAttachObjectARB(r->program,obj));
        }
        if(broken) return reason("Stage setup GL error",FAIL);
    }
    if(r->program) {
        STEP(glBindAttribLocationARB(r->program,0,"piglit_vertex"));
        STEP(glBindAttribLocationARB(r->program,1,"piglit_texcoord"));
        STEP(glLinkProgramARB(r->program));
        STEP(glGetObjectParameterivARB(r->program,GL_OBJECT_LINK_STATUS_ARB,&r->linked));
        ant_number("link_ok",r->linked);
        if(!sr_object_log(r,r->program,"link_log",0)) return FAIL;
        if(r->linked) STEP(glUseProgramObjectARB(r->program));
    } else r->linked=1;
    return !broken;
}
static void sr_texture(const SrCommand *cmd) {
    static const GLfloat palette[8][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,1,1},
                                        {1,1,0,1},{1,0,1,1},{0,1,1,1},{0,0,0,1}};
    const GLfloat *v=cmd->v;
    GLenum t=cmd->op==SR_RGB1 || cmd->op==SR_Z1?GL_TEXTURE_1D:cmd->op==SR_RGB3?GL_TEXTURE_3D:GL_TEXTURE_2D;
    int z=cmd->op==SR_Z1 || cmd->op==SR_Z2,w=8,h=8,levels=cmd->op==SR_MIP?4:1;
    GLfloat color[8*8*4],depth[32*32]; GLuint tex=0;
    STEP(glActiveTexture(GL_TEXTURE0+(int)v[0])); STEP(glGenTextures(1,&tex)); STEP(glBindTexture(t,tex));
    GLint wrap=cmd->op==SR_CHECKER?GL_CLAMP_TO_BORDER:GL_CLAMP_TO_EDGE;
    STEP(glTexParameteri(t,GL_TEXTURE_WRAP_S,wrap)); STEP(glTexParameteri(t,GL_TEXTURE_WRAP_T,wrap));
    if(cmd->op==SR_CHECKER) STEP(glTexParameterfv(t,GL_TEXTURE_BORDER_COLOR,palette[0]));
    STEP(glTexParameteri(t,GL_TEXTURE_MAG_FILTER,GL_NEAREST));
    STEP(glTexParameteri(t,GL_TEXTURE_MIN_FILTER,levels==4?GL_NEAREST_MIPMAP_NEAREST:GL_NEAREST));
    if(z) {
        w=(int)v[1]; h=t==GL_TEXTURE_1D?1:(int)v[2];
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) depth[y*w+x]=(GLfloat)x/(w-1);
        if(t==GL_TEXTURE_1D) STEP(glTexImage1D(t,0,GL_DEPTH_COMPONENT,w,0,GL_DEPTH_COMPONENT,GL_FLOAT,depth));
        else STEP(glTexImage2D(t,0,GL_DEPTH_COMPONENT,w,h,0,GL_DEPTH_COMPONENT,GL_FLOAT,depth));
        STEP(glTexParameteri(t,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_R_TO_TEXTURE));
        STEP(glTexParameteri(t,GL_TEXTURE_COMPARE_FUNC,GL_GREATER));
        if(t==GL_TEXTURE_2D) STEP(glEnable(t));
    } else if(t==GL_TEXTURE_3D) {
        STEP(glTexParameteri(t,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE));
        STEP(glTexImage3D(t,0,GL_RGBA,2,2,2,0,GL_RGBA,GL_FLOAT,palette));
    } else if(t==GL_TEXTURE_1D) STEP(glTexImage1D(t,0,GL_RGBA,4,0,GL_RGBA,GL_FLOAT,palette));
    else {
        if(cmd->op==SR_RGB2) { w=v[1]; h=v[2]; }
        if(cmd->op==SR_CHECKER) { w=v[2]; h=v[3]; }
        for(int l=0;l<levels;++l,w/=2,h/=2) {
            for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
                int q=(x>=w/2)+2*(y>=h/2);
                const float *p=cmd->op==SR_CHECKER?v+(q==1||q==2?8:4):palette[levels==4?l:q];
                memcpy(color+4*(y*w+x),p,4*sizeof(float));
            }
            STEP(glTexImage2D(t,l,GL_RGBA,w,h,0,GL_RGBA,GL_FLOAT,color));
        }
        STEP(glEnable(t));
    }
}
static void sr_quad(SrRun *r,const SrCommand *cmd) {
    const float *v=cmd->v;
    GLfloat pos[4][2],uv[4][2]; GLint attr=-1,tex=-1;
    for(int i=0;i<4;++i) {
        pos[i][0]=v[0]+(i&1)*v[2]; pos[i][1]=v[1]+(i/2)*v[3];
        uv[i][0]=v[4]+(i&1)*v[6]; uv[i][1]=v[5]+(i/2)*v[7];
    }
    if(r->program) STEP(attr=glGetAttribLocationARB(r->program,"piglit_vertex"));
    if(attr>=0) {
        STEP(glEnableVertexAttribArrayARB(attr)); STEP(glVertexAttribPointerARB(attr,2,GL_FLOAT,GL_FALSE,0,pos));
        if(cmd->op==SR_QUADT) STEP(tex=glGetAttribLocationARB(r->program,"piglit_texcoord"));
        if(tex>=0) { STEP(glEnableVertexAttribArrayARB(tex)); STEP(glVertexAttribPointerARB(tex,2,GL_FLOAT,GL_FALSE,0,uv)); }
    } else {
        STEP(glEnableClientState(GL_VERTEX_ARRAY)); STEP(glVertexPointer(2,GL_FLOAT,0,pos));
        if(cmd->op==SR_QUADT) { STEP(glEnableClientState(GL_TEXTURE_COORD_ARRAY)); STEP(glTexCoordPointer(2,GL_FLOAT,0,uv)); }
    }
    STEP(glDrawArrays(GL_TRIANGLE_STRIP,0,4));
    if(attr>=0) { STEP(glDisableVertexAttribArrayARB(attr)); if(tex>=0) STEP(glDisableVertexAttribArrayARB(tex)); }
    else { STEP(glDisableClientState(GL_VERTEX_ARRAY)); if(cmd->op==SR_QUADT) STEP(glDisableClientState(GL_TEXTURE_COORD_ARRAY)); }
    r->drew=1;
}
static void sr_state(SrRun *r);
static int sr_probe(SrRun *r,const SrCommand *cmd) {
    const SrCase *c=r->c; const float *e=cmd->v;
    int x=0,y=0,w=c->w,h=c->h,n=4;
    if(cmd->op==SR_REL3 || cmd->op==SR_REL4) {
        x=fmin(c->w-1,e[0]*c->w); y=fmin(c->h-1,e[1]*c->h); w=h=1; e+=2;
    } else if(cmd->op==SR_PIX3 || cmd->op==SR_PIX4 || cmd->op==SR_ZPIX) { x=e[0];y=e[1];w=h=1;e+=2; }
    else if(cmd->op==SR_BOX4) { x=e[0];y=e[1];w=e[2];h=e[3];e+=4; }
    if(cmd->op==SR_REL3 || cmd->op==SR_PIX3 || cmd->op==SR_ALL3) n=3;
    int depth=cmd->op==SR_ZPIX,components=depth?1:4;
    if(depth) n=1;
    if(x<0 || y<0 || w<=0 || h<=0 || x+w>c->w || y+h>c->h) return reason("Probe outside drawable",FAIL);
    float *read=malloc((size_t)w*h*components*sizeof(float));
    if(!read) return reason("Probe allocation failed",FAIL);
    STEP(glReadPixels(x,y,w,h,depth?GL_DEPTH_COMPONENT:GL_RGBA,GL_FLOAT,read));
    if(broken){free(read);return reason("Probe readback failed",FAIL);}
    int first=-1,bad=0; float maximum=0;
    for(int i=0;i<w*h;++i) {
        int mismatch=0;
        for(int k=0;k<n;++k) {
            float delta=fabsf(read[i*components+k]-e[k]);
            if(!(delta<=0.01f)) mismatch=1;
            maximum=fmaxf(maximum,delta);
        }
        if(mismatch) { if(first<0) first=i; ++bad; }
    }
    ++r->probes;
    ant_number("probe_x",x); ant_number("probe_y",y); ant_number("probe_width",w); ant_number("probe_height",h);
    ant_number("probe_components",n); ant_text("probe_plane",depth?"depth":"color");
    ant_number("bad_pixels",bad);ant_number("max_channel_error_normalized",maximum);
    if(first>=0) {
        ant_number("first_x",x+first%w);ant_number("first_y",y+first/w);
        for(int k=0;k<n;++k){ ant_number("expected_channel",e[k]); ant_number("actual_channel",read[first*components+k]); }
        /* Only the upstream probe rectangle/channels have a defined expected value. */
        GLubyte *diff=calloc((size_t)c->w*c->h,4),*want=malloc((size_t)c->w*c->h*4);
        char tag[96];
        if(!diff || !want) {free(diff);free(want);free(read);return reason("Difference allocation failed",FAIL);}
        STEP(glReadPixels(0,0,c->w,c->h,GL_RGBA,GL_UNSIGNED_BYTE,r->rgba));
        if(depth) { memset(r->rgba,0,(size_t)c->w*c->h*4);memset(want,0,(size_t)c->w*c->h*4); } else memcpy(want,r->rgba,(size_t)c->w*c->h*4);
        for(int i=0;i<w*h;++i) {
            int p=(y+i/w)*c->w+x+i%w; float peak=0;
            for(int k=0;k<n;++k) {
                peak=fmaxf(peak,fabsf(read[i*components+k]-e[k]));
                want[4*p+k]=(GLubyte)(fminf(1,fmaxf(0,e[k]))*255+.5f);
            }
            if(depth) {
                memset(r->rgba+4*p,(int)(fminf(1,fmaxf(0,read[i]))*255+.5f),3);
                memset(want+4*p,(int)(fminf(1,fmaxf(0,e[0]))*255+.5f),3);
            }
            memset(diff+4*p,(int)fminf(255,peak*255+.5f),3);
        }
        snprintf(tag,sizeof tag,"%s-L%d-observed",r->side,cmd->line); int saved=sr_save(r,tag,r->rgba);
        snprintf(tag,sizeof tag,"%s-L%d-expected",r->side,cmd->line); saved&=sr_save(r,tag,want);
        snprintf(tag,sizeof tag,"%s-L%d-difference",r->side,cmd->line); saved&=ant_image(r->dir,tag,diff,c->w,c->h);
        if(depth) {snprintf(tag,sizeof tag,"%s-L%d-depth.f32",r->side,cmd->line);saved&=sr_file(r->dir,tag,read,w*h*sizeof(float));}
        free(diff);free(want);
        if(!saved || broken){free(read);return reason("Failure capture failed",FAIL);}
    }
    free(read);
    if(bad && cmd->op==SR_WARN4) {++r->warnings;ant_text("upstream_warning","probe warn mismatch");return OK;}
    if(bad) { ++r->failed_probes; sr_state(r); ant_text("probe_status","fail"); }
    return OK;
}
static int sr_command(SrRun *r,const SrCommand *cmd,const char *extensions,int glver) {
    const float *v=cmd->v; GLint loc=-1,iv[4]={0};
    if(cmd->op==SR_LINKERR) return r->linked?reason("Expected link rejection",FAIL):OK;
    if(!r->linked) return reason("Link failed before command",FAIL);
    if(cmd->op>=SR_U_FLOAT && cmd->op<=SR_U_VEC4) {
        STEP(loc=glGetUniformLocationARB(r->program,cmd->name));
        ant_number("uniform_location",loc);
        if(loc<0) return reason("Active uniform missing",FAIL);
    }
    switch(cmd->op) {
    case SR_LINKOK: break;
    case SR_CLEAR: STEP(glClear(r->clear));r->drew=1;break;
    case SR_CLEARCOLOR: STEP(glClearColor(v[0],v[1],v[2],v[3]));r->clear|=GL_COLOR_BUFFER_BIT;break;
    case SR_CLEARZ: STEP(glClearDepth(v[0]));r->clear|=GL_DEPTH_BUFFER_BIT;break;
    case SR_COLOR: STEP(glColor4fv(v));break;
    case SR_QUAD: case SR_QUADT: sr_quad(r,cmd);break;
    case SR_ARRAYS:{
        const GLfloat pos[][2]={{0,0},{10,0},{0,10},{10,10}};
        if(!r->c->arrays || v[0]<0 || v[0]+v[1]>4) return reason("Invalid vertex fixture",FAIL);
        STEP(loc=glGetAttribLocationARB(r->program,"in_pos"));
        if(loc<0)return reason("Vertex data attribute missing",FAIL);
        STEP(glEnableVertexAttribArrayARB(loc));STEP(glVertexAttribPointerARB(loc,2,GL_FLOAT,GL_FALSE,0,pos));
        STEP(glDrawArrays(cmd->arg,(GLint)v[0],(GLsizei)v[1]));STEP(glDisableVertexAttribArrayARB(loc));r->drew=1;break;}
    case SR_REL3:case SR_REL4:case SR_PIX3:case SR_PIX4:case SR_BOX4:case SR_ALL3:case SR_ALL4:
    case SR_WARN4:case SR_ZPIX:return sr_probe(r,cmd);
    case SR_ORTHO:case SR_ORTHOWINDOW:
        STEP(glMatrixMode(GL_PROJECTION));STEP(glLoadIdentity());
        if(cmd->op==SR_ORTHO) STEP(glOrtho(v[0],v[1],v[2],v[3],-1,1));
        else STEP(glOrtho(0,r->c->w,0,r->c->h,-1,1));
        STEP(glMatrixMode(GL_MODELVIEW));STEP(glLoadIdentity());break;
    case SR_TCOORD: STEP(glMultiTexCoord4fv(GL_TEXTURE0+(int)v[0],v+1));break;
    case SR_PLANE:{GLdouble p[]={v[1],v[2],v[3],v[4]};STEP(glClipPlane(GL_CLIP_PLANE0+(int)v[0],p));break;}
    case SR_ENABLE: STEP(glEnable(cmd->arg));break;
    case SR_DISABLE:STEP(glDisable(cmd->arg));break;
    case SR_HINT:STEP(glHint(cmd->arg,GL_NICEST));break;
    case SR_TEXPARAM:STEP(glTexParameteri(cmd->arg,(GLenum)v[0],(GLint)v[1]));break;
    case SR_Z1:case SR_Z2:case SR_RGB1:case SR_RGB2:case SR_RGB3:case SR_MIP:case SR_CHECKER:sr_texture(cmd);break;
    case SR_P_ENV_VP:STEP(glProgramEnvParameter4fvARB(GL_VERTEX_PROGRAM_ARB,(GLuint)v[0],v+1));break;
    case SR_P_LOCAL_FP:case SR_P_LOCAL_VP:
        STEP(glProgramLocalParameter4fvARB(cmd->op==SR_P_LOCAL_VP?GL_VERTEX_PROGRAM_ARB:GL_FRAGMENT_PROGRAM_ARB,(GLuint)v[0],v+1));break;
    case SR_U_INT: STEP(glUniform1iARB(loc,(GLint)v[0]));break;
    case SR_U_UINT:{
        void (*set)(GLint,GLuint)=NULL;
        if(glver>=30) set=dlsym(RTLD_DEFAULT,"glUniform1ui");
        if(!set && ext(extensions,"GL_EXT_gpu_shader4")) set=dlsym(RTLD_DEFAULT,"glUniform1uiEXT");
        if(!set) return reason("Unsigned uniform upload unavailable",UNSUPPORTED);
        STEP(set(loc,(GLuint)v[0]));break;}
    case SR_U_IVEC2:case SR_U_IVEC4:
        for(int i=0;i<4;++i) iv[i]=(GLint)v[i];
        if(cmd->op==SR_U_IVEC2) STEP(glUniform2ivARB(loc,1,iv));else STEP(glUniform4ivARB(loc,1,iv));break;
    case SR_U_FLOAT:STEP(glUniform1fvARB(loc,1,v));break;
    case SR_U_VEC2:STEP(glUniform2fvARB(loc,1,v));break;
    case SR_U_VEC3:STEP(glUniform3fvARB(loc,1,v));break;
    case SR_U_VEC4:STEP(glUniform4fvARB(loc,1,v));break;
    case SR_U_MAT2X2:STEP(glUniformMatrix2fvARB(loc,1,GL_FALSE,v));break;
    case SR_U_MAT4:STEP(glUniformMatrix4fvARB(loc,1,GL_FALSE,v));break;
    default:return reason("Unimplemented command",FAIL);
    }
    return broken?reason("Command produced GL error",FAIL):OK;
}
/* Capture state after obtaining the original error or mismatch. */
static void sr_state(SrRun *r) {
    GLint v[4]={0};
    const GLenum properties[]={GL_VIEWPORT,GL_SCISSOR_BOX,GL_DRAW_BUFFER,GL_READ_BUFFER,GL_ACTIVE_TEXTURE,
        GL_TEXTURE_BINDING_2D,GL_DEPTH_BITS,GL_RED_BITS,GL_GREEN_BITS,GL_BLUE_BITS,GL_ALPHA_BITS};
    const char *keys[]={"viewport","scissor","draw_buffer","read_buffer","active_texture","texture_2d","depth_bits","red_bits","green_bits","blue_bits","alpha_bits"};
    ant_text("step","failure GL state");
    for(unsigned i=0;i<sizeof properties/sizeof *properties;++i) {
        glGetIntegerv(properties[i],v); GLenum err=glGetError();
        if(err){ant_number("diagnostic_gl_error",err);continue;}
        if(i<2){char s[80];snprintf(s,sizeof s,"%d %d %d %d",(int)v[0],(int)v[1],(int)v[2],(int)v[3]);ant_text(keys[i],s);}
        else ant_number(keys[i],v[0]);
    }
    ant_number("probes_completed",r->probes);
}
static int sr_side(SrRun *r,CGLContextObj ctx,CGLPixelFormatObj pf) {
    const SrCase *c=r->c;const GLubyte *version=NULL,*ex=NULL,*renderer=NULL,*vendor=NULL;
    CglInt screen=0,value=0; int major=0,minor=0;
    CGL(CGLSetCurrentContext(ctx)); CGL(CGLGetVirtualScreen(ctx,&screen));
    const CGLPixelFormatAttribute a[]={kCGLPFARendererID,kCGLPFAAccelerated,kCGLPFAColorSize,kCGLPFAAlphaSize,kCGLPFADepthSize,kCGLPFADoubleBuffer};
    const char *keys[]={"renderer_id","accelerated","color_bits","alpha_bits","depth_bits","double_buffer"};
    for(unsigned i=0;i<sizeof a/sizeof *a;++i){CGL(CGLDescribePixelFormat(pf,screen,a[i],&value));ant_number(keys[i],value);}
    STEP(renderer=glGetString(GL_RENDERER));ant_text("gl_renderer",(const char*)renderer);
    STEP(vendor=glGetString(GL_VENDOR));ant_text("gl_vendor",(const char*)vendor);
    STEP(version=glGetString(GL_VERSION));ant_text("gl_version",(const char*)version);
    STEP(ex=glGetString(GL_EXTENSIONS));ant_text("gl_extensions",(const char*)ex);
    if(broken || !version || sscanf((const char*)version,"%d.%d",&major,&minor)!=2) return reason("GL version query failed",FAIL);
    int glver=major*10+minor;
    if(glver<c->gl)return reason("Required GL version unavailable",UNSUPPORTED);
    char required[180];snprintf(required,sizeof required,"%s",c->extensions);
    for(char *p=strtok(required," ");p;p=strtok(NULL," ")) if(!ext((const char*)ex,p)) { ant_text("missing_extension",p);return reason("Required extension unavailable",UNSUPPORTED); }
    if(c->glsl) {
        if(glver<20 && (!ext((const char*)ex,"GL_ARB_shader_objects") || !ext((const char*)ex,"GL_ARB_shading_language_100")))
            return reason("GLSL unavailable",UNSUPPORTED);
        STEP(version=glGetString(0x8b8c));ant_text("glsl_version",(const char*)version);
        if(broken || !version) return reason("GLSL query failed",FAIL);
        if(atof((const char*)version)*100+.01<c->glsl) return reason("Required GLSL version unavailable",UNSUPPORTED);
    }
    GLint bits=0;
    if(c->depth){STEP(glGetIntegerv(GL_DEPTH_BITS,&bits));if(!bits)return reason("Missing depth buffer",UNSUPPORTED);}
    STEP(glGetIntegerv(GL_ALPHA_BITS,&bits));if(bits<8)return reason("Missing RGBA8 alpha",UNSUPPORTED);
    STEP(glViewport(0,0,c->w,c->h));STEP(glClearColor(0,0,0,0));STEP(glClearDepth(1));
    STEP(glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT));
    if(!sr_program(r))return FAIL;
    for(int i=0;i<c->ncmd;++i) {
        const SrCommand *cmd=sr_commands+c->cmd+i;
        printf("command_ticks\t%llu\n",(unsigned long long)mach_absolute_time());
        ant_number("source_line",cmd->line);ant_text("command",cmd->text);
        int rc=sr_command(r,cmd,(const char *)ex,glver);
        if(rc!=OK) return rc;
    }
    ant_number("probes_completed",r->probes);ant_number("warning_count",r->warnings);ant_number("failed_probes",r->failed_probes);
    return r->failed_probes?reason("Upstream pixel assertion failed",FAIL):OK;
}
static int sr_reference_context(Drawable *d,void *backing,const SrCase *c) {
    CGLPixelFormatAttribute a[]={kCGLPFARendererID,kCGLRendererGenericFloatID,kCGLPFAOffScreen,
        kCGLPFAColorSize,32,kCGLPFAAlphaSize,8,0,0,0};
    CglInt n=0;
    if(c->depth){a[7]=kCGLPFADepthSize;a[8]=24;}
    CGLError e=CGLChoosePixelFormat(a,&d->pf,&n);ant_number("cgl_error",e);
    if(e==kCGLBadPixelFormat || (!e && (!d->pf || !n))) return reason("Software pixel format unavailable",UNSUPPORTED);
    if(e) return reason("Software format selection failed",FAIL);
    CGL(CGLCreateContext(d->pf,NULL,&d->ctx));CGL(CGLSetOffScreen(d->ctx,c->w,c->h,4*c->w,backing));return OK;
}
int ant_sr_list(void) {
    for(unsigned i=0;i<sizeof sr_cases/sizeof *sr_cases;++i)
        printf("%u\t%s\tPiglit fork\t62ef6b0db\t%s\n",(unsigned)count+i+1,sr_cases[i].id,sr_cases[i].path);
    return ferror(stdout)?2:0;
}
int ant_sr_run(const char *id,const char *dir) {
    unsigned i=0,n=sizeof sr_cases/sizeof *sr_cases;
    while(i<n && strcmp(sr_cases[i].id,id))++i;
    if(i==n){ant_text("reason","Unknown shader id");ant_text("status","fail");return 64;}
    const SrCase *c=sr_cases+i;
    unsigned ordinal=(unsigned)count+i+1;
    uint64_t begin=mach_absolute_time(); marker("begin",ordinal,id,begin);
    ant_text("sr_protocol","1");ant_text("suite","Antiochus Piglit fork");ant_text("source_version","62ef6b0db");
    ant_text("source_test",c->path);ant_text("source_sha256",c->hash);ant_number("width",c->w);ant_number("height",c->h);
    ant_text("pixel_tolerance","0.01 normalized per tested channel");ant_text("required_extensions",c->extensions);
    ant_number("required_glsl",c->glsl);ant_number("required_gl_times_10",c->gl);
    size_t size=(size_t)c->w*c->h*4;void *backing=calloc(1,size);GLubyte *frame=malloc(size);
    int results[2]={FAIL,FAIL};
    if(!backing || !frame) reason("Frame allocation failed",FAIL);
    else if(!ant_view_open(c->path,c->w,c->h)) reason("Visible guest window unavailable",FAIL);
    else for(int hw=0;hw<=1;++hw) {
        Drawable d;memset(&d,0,sizeof d);broken=0;
        SrRun r;memset(&r,0,sizeof r);r.c=c;r.dir=dir;r.rgba=frame;r.side=hw?"accelerated":"reference";
        ant_text("side",r.side);ant_view_note(hw?"Accelerated window / upstream assertions":"Apple software reference / upstream assertions");
        int rc=hw?ant_view_gl(c->depth,&d.ctx,&d.pf):sr_reference_context(&d,backing,c);
        if(hw && rc!=OK) reason("Accelerated window format or context unavailable",rc);
        if(rc==OK) {
            rc=sr_side(&r,d.ctx,d.pf);
            if(rc==FAIL) sr_state(&r);
            if(r.drew) {
                glReadPixels(0,0,c->w,c->h,GL_RGBA,GL_UNSIGNED_BYTE,frame);
                GLenum error=glGetError();ant_number("capture_gl_error",error);
                if(error || !sr_save(&r,hw?"actual":"reference",frame)) rc=reason("Final image capture failed",FAIL);
                else ant_view_frame(frame,c->w,c->h);
                if(hw) { ant_view_present(); ant_text("presentation","accelerated window swapped"); }
            }
        }
        if(!hw && !release_drawable(&d)) rc=reason("Software context release failed",FAIL);
        ant_text("side_status",rc==OK?"pass":rc==UNSUPPORTED?"unsupported":"fail");results[hw]=rc;
    }
    int rc=(results[0]==FAIL||results[1]==FAIL)?FAIL:(results[0]==UNSUPPORTED||results[1]==UNSUPPORTED)?UNSUPPORTED:OK;
    const char *status=rc==OK?"pass":rc==UNSUPPORTED?"unsupported":"fail";
    char final_note[128];
    snprintf(final_note,sizeof final_note,"Software: %s   Accelerated: %s",
        results[0]==OK?"pass":results[0]==FAIL?"fail":"unsupported",results[1]==OK?"pass":results[1]==FAIL?"fail":"unsupported");
    ant_view_note(final_note);ant_view_close(status);free(frame);free(backing);
    uint64_t end=mach_absolute_time();mach_timebase_info_data_t t;mach_timebase_info(&t);
    ant_number("seconds",(end-begin)*(double)t.numer/t.denom*1e-9);
    ant_text("status",status);marker("end",ordinal,id,end);return ferror(stdout)?2:rc==FAIL?1:0;
}
