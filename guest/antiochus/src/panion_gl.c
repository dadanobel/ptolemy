/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "panion_gl.h"
#include <OpenGL/gl.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
int pan_gl_create(PanGL *g,const PanScene *s){int i;memset(g,0,sizeof *g);glGenBuffers(4,g->buffer);glGenTextures(2,g->texture);
    for(i=0;i<2;++i){glBindTexture(GL_TEXTURE_2D,g->texture[i]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,i?256:64,i?512:64,0,GL_RGBA,GL_UNSIGNED_BYTE,i?s->stone:s->texture);}
    glBindBuffer(GL_ARRAY_BUFFER,g->buffer[0]);glBufferData(GL_ARRAY_BUFFER,s->mesh[0].n*sizeof(PanVertex),s->mesh[0].v,GL_STATIC_DRAW);glBindBuffer(GL_ARRAY_BUFFER,0);return glGetError()==GL_NO_ERROR;
}
int pan_gl_draw(PanGL *g,const PanScene *s,unsigned frame,int width,int height){
    int order[4]={0,1,3,2},i;glViewport(0,0,width,height);glDisable(GL_DITHER);glDisable(GL_LIGHTING);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_FOG);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glClearDepth(1);glClearColor(s->stage?186/255.0f:28/255.0f,s->stage?199/255.0f:33/255.0f,s->stage?195/255.0f:38/255.0f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);glLoadMatrixd(s->projection);glMatrixMode(GL_MODELVIEW);glLoadMatrixd(s->view);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glShadeModel(GL_FLAT);
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_TEXTURE_COORD_ARRAY);glEnableClientState(GL_COLOR_ARRAY);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    for(i=0;i<4;++i){int layer=order[i];const PanMesh *m=&s->mesh[layer];if(!m->n)continue;
        if(layer==2){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);glDisable(GL_TEXTURE_2D);}else {glDisable(GL_BLEND);glDepthMask(GL_TRUE);glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,g->texture[layer==3]);}
        if(layer==3){
            size_t at;GLfloat ambient[4]={.54f,.54f,.54f,1},black[4]={0,0,0,1},diffuse[4]={.46f,.46f,.46f,1},light[4]={.4f,.82f,.4f,0};
            glBindBuffer(GL_ARRAY_BUFFER,0);glEnable(GL_LIGHTING);glEnable(GL_LIGHT0);glEnable(GL_LIGHT1);glEnable(GL_COLOR_MATERIAL);glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);glLightfv(GL_LIGHT0,GL_AMBIENT,black);glLightfv(GL_LIGHT1,GL_AMBIENT,black);glLightfv(GL_LIGHT0,GL_DIFFUSE,diffuse);glLightfv(GL_LIGHT1,GL_DIFFUSE,diffuse);glLightfv(GL_LIGHT0,GL_POSITION,light);light[0]=-.4f;light[1]=-.82f;light[2]=-.4f;glLightfv(GL_LIGHT1,GL_POSITION,light);
            glBegin(GL_TRIANGLES);for(at=0;at<m->n;at+=3){const PanVertex *v=&m->v[at];float ax=v[1].x-v[0].x,ay=v[1].y-v[0].y,az=v[1].z-v[0].z,bx=v[2].x-v[0].x,by=v[2].y-v[0].y,bz=v[2].z-v[0].z,nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx,n=sqrtf(nx*nx+ny*ny+nz*nz);int j;glNormal3f(nx/n,ny/n,nz/n);for(j=0;j<3;++j){glColor4ubv(v[j].c);glTexCoord2f(v[j].u,v[j].v);glVertex3f(v[j].x,v[j].y,v[j].z);}}glEnd();glDisable(GL_LIGHTING);glDisable(GL_COLOR_MATERIAL);continue;
        }
        glBindBuffer(GL_ARRAY_BUFFER,g->buffer[layer]);if(layer)glBufferData(GL_ARRAY_BUFFER,m->n*sizeof(PanVertex),m->v,GL_STREAM_DRAW);
        glVertexPointer(3,GL_FLOAT,sizeof(PanVertex),(const GLvoid *)offsetof(PanVertex,x));glTexCoordPointer(2,GL_FLOAT,sizeof(PanVertex),(const GLvoid *)offsetof(PanVertex,u));glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(PanVertex),(const GLvoid *)offsetof(PanVertex,c));glDrawArrays(GL_TRIANGLES,0,(GLsizei)m->n);
    }
    glBindBuffer(GL_ARRAY_BUFFER,0);glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_TEXTURE_COORD_ARRAY);glDisableClientState(GL_COLOR_ARRAY);glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,width,0,height,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBegin(GL_QUADS);for(i=0;i<16 && frame!=0xffffffffu;++i){unsigned char c=((frame>>i)&1)?255:0;glColor4ub(c,c,c,255);glVertex2f(i*width/16.0f,0);glVertex2f((i+1)*width/16.0f,0);glVertex2f((i+1)*width/16.0f,12);glVertex2f(i*width/16.0f,12);}glEnd();return glGetError()==GL_NO_ERROR;
}
void pan_gl_destroy(PanGL *g){glDeleteBuffers(4,g->buffer);glDeleteTextures(2,g->texture);memset(g,0,sizeof *g);}
