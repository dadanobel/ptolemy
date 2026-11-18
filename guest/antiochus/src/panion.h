/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ANT_PANION_H
#define ANT_PANION_H
#include <stdint.h>
#include <stddef.h>
enum { PAN_WIDTH=768, PAN_HEIGHT=432, PAN_FRAMES=48, PAN_MAX_AGENTS=4312, PAN_PROBES_X=192, PAN_PROBES_Y=108, PAN_PROBES=PAN_PROBES_X*PAN_PROBES_Y };
typedef struct { float x,y,z,u,v; unsigned char c[4]; } PanVertex;
typedef struct { PanVertex *v; size_t n,capacity; } PanMesh;
typedef struct { int32_t x,z,tx,tz,nx,nz; unsigned kind,side; } PanAgent;
typedef struct {
    int stage,ok; unsigned tick,count,infantry,cavalry,elephants,particles,trees;
    PanAgent *agent; int *next,head[4096]; PanMesh mesh[4];
    double projection[16],view[16]; float eye[3];
    unsigned char texture[64*64*4],stone[256*512*4];
    unsigned comparisons; uint32_t state_hash;
} PanScene;
typedef struct { unsigned char color[PAN_PROBES][4],safe[PAN_PROBES],actor[PAN_PROBES]; float depth[PAN_PROBES]; unsigned tested,bad,maximum,actor_tested,need_actors; } PanOracle;
extern const char *pan_ids[5],*pan_titles[5];
int pan_create(PanScene *s,int stage);
void pan_destroy(PanScene *s);
int pan_frame(PanScene *s,unsigned frame,int width,int height);
int pan_state_check(PanScene *s);
void pan_reference(const PanScene *s,PanOracle *o,int width,int height);
int pan_compare(PanOracle *o,const unsigned char *actual,int width,int height,unsigned char *reference,unsigned char *difference);
void pan_marker(unsigned char *rgba,int width,unsigned frame);
int pan_marker_check(const unsigned char *rgba,int width,unsigned frame);
int pan_stage(const char *id);
#endif
