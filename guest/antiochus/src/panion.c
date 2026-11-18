/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "panion.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
const char *pan_ids[5]={"gpu-rosetta","gpu-paneion","gpu-phalanx","gpu-cavalry","combined-panion"};
const char *pan_titles[5]={"I - Rosetta Stone","II - The springs of Paneion","III - The phalanxes","IV - Cavalry on the ridge","V - The battle of Panion"};
static const unsigned char bronze[4]={188,137,65,255},linen[4]={223,205,157,255},skin[4]={173,124,87,255};
static const unsigned char red[4]={157,48,36,255},blue[4]={40,80,112,255},wood[4]={103,72,42,255};
static const unsigned char rock[4]={169,153,119,255},leaf[4]={83,103,57,255};
int pan_stage(const char *id){int i;for(i=0;i<5;++i)if(!strcmp(id,pan_ids[i]))return i;return -1;}
static uint32_t hash(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
static float terrain(float x,float z){float ridge=fmaxf(0,(fabsf(x)-46)*.20f);float h=ridge+fmaxf(0,(-z-60)*.55f)*(.7f+.3f*sinf(x*.035f))+1.3f*sinf(x*.055f)*cosf(z*.06f),bank=fabsf(x-(-55+sinf(z*.025f)*5));if(bank<3.5f)return -.4f;if(bank<6)h*=((bank-3.5f)/2.5f);return h;}
static int reserve(PanMesh *m,size_t n){if(m->n+n>m->capacity){size_t c=(m->n+n)*3/2+1024;PanVertex *v=realloc(m->v,c*sizeof *v);if(!v)return 0;m->v=v;m->capacity=c;}return 1;}
static void triangle(PanScene *s,int layer,const float a[3],const float b[3],const float c[3],const unsigned char *color){
    PanMesh *m=&s->mesh[layer];float ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2],vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2];
    float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx,n=sqrtf(nx*nx+ny*ny+nz*nz);
    float light=(layer==2||layer==3)?1:.54f+.46f*fabsf((nx*.4f+ny*.82f+nz*.4f)/(n>0?n:1));
    int i,j;const float *p[3]={a,b,c};if(!reserve(m,3)){s->ok=0;return;}
    for(i=0;i<3;++i){PanVertex *v=&m->v[m->n++];v->x=p[i][0];v->y=p[i][1];v->z=p[i][2];v->u=p[i][0]*.12f;v->v=p[i][2]*.12f;
        for(j=0;j<3;++j)v->c[j]=(unsigned char)(color[j]*light);v->c[3]=color[3];}
}
static void box(PanScene *s,int layer,float x,float y,float z,float w,float h,float d,const unsigned char *color){
    float p[8][3];int i;static const unsigned char ix[36]={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,1,2,6,1,6,5,0,4,7,0,7,3};
    for(i=0;i<8;++i){p[i][0]=x+((i==1||i==2||i==5||i==6)?w:-w);p[i][1]=y+((i==2||i==3||i==6||i==7)?h:-h);p[i][2]=z+(i>=4?d:-d);}
    for(i=0;i<36;i+=3)triangle(s,layer,p[ix[i]],p[ix[i+1]],p[ix[i+2]],color);
}
static void ellipsoid(PanScene *s,int layer,float x,float y,float z,float rx,float ry,float rz,const unsigned char *color){
    static const float cs[7]={1,.5f,-.5f,-1,-.5f,.5f,1},sn[7]={0,.8660254f,.8660254f,0,-.8660254f,-.8660254f,0};
    static const float latitude[4]={-1,-.5f,.5f,1},radius[4]={0,.8660254f,.8660254f,0};
    int i,j;for(j=0;j<3;++j)for(i=0;i<6;++i){
        float p[4][3]={{x+rx*radius[j]*cs[i],y+ry*latitude[j],z+rz*radius[j]*sn[i]},
            {x+rx*radius[j]*cs[i+1],y+ry*latitude[j],z+rz*radius[j]*sn[i+1]},
            {x+rx*radius[j+1]*cs[i+1],y+ry*latitude[j+1],z+rz*radius[j+1]*sn[i+1]},
            {x+rx*radius[j+1]*cs[i],y+ry*latitude[j+1],z+rz*radius[j+1]*sn[i]}};
        triangle(s,layer,p[0],p[1],p[2],color);triangle(s,layer,p[0],p[2],p[3],color);}

}
static void pole(PanScene *s,float x,float y,float z,float dx,float dy,float dz,float radius,const unsigned char *color){
    float p[4][3]={{x-radius,y,z},{x+radius,y,z},{x+dx+radius,y+dy,z+dz},{x+dx-radius,y+dy,z+dz}};
    triangle(s,1,p[0],p[1],p[2],color);triangle(s,1,p[0],p[2],p[3],color);
    {float q[4][3]={{x,y,z-radius},{x,y,z+radius},{x+dx,y+dy,z+dz+radius},{x+dx,y+dy,z+dz-radius}};
    triangle(s,1,q[0],q[1],q[2],color);triangle(s,1,q[0],q[2],q[3],color);}
}
static void shield(PanScene *s,float x,float y,float z,float direction,const unsigned char *cloth){
    static const float cs[11]={1,.809017f,.309017f,-.309017f,-.809017f,-1,-.809017f,-.309017f,.309017f,.809017f,1};
    static const float sn[11]={0,.5877853f,.9510565f,.9510565f,.5877853f,0,-.5877853f,-.9510565f,-.9510565f,-.5877853f,0};
    int i;float center[3]={x,y,z+.11f*direction};for(i=0;i<10;++i){
        float p[3]={x+.34f*cs[i],y+.34f*sn[i],z},q[3]={x+.34f*cs[i+1],y+.34f*sn[i+1],z};
        triangle(s,1,center,p,q,i%3?bronze:cloth);}

}
static void soldier(PanScene *s,float x,float y,float z,unsigned side,float phase,int rider){
    const unsigned char *cloth=side?blue:red;float dir=side?-1:1,step=sinf(phase)*.16f;
    box(s,1,x,y+.88f,z,.23f,.31f,.14f,cloth);
    box(s,1,x-.12f,y+.35f,z+step,.08f,.28f,.09f,linen);box(s,1,x+.12f,y+.35f,z-step,.08f,.28f,.09f,linen);
    box(s,1,x,y+1.15f,z,.26f,.055f,.17f,linen);ellipsoid(s,1,x,y+1.46f,z,.16f,.20f,.16f,skin);
    box(s,1,x,y+1.59f,z,.17f,.07f,.17f,bronze);box(s,1,x-.28f,y+.98f,z,.065f,.20f,.065f,skin);
    shield(s,x-.28f,y+.91f,z+dir*.22f,dir,cloth);
    pole(s,x+.29f,y+.76f,z,0,rider?1.5f:.8f,dir*(rider?1.5f:3.4f),.018f,wood);
}
static void horse(PanScene *s,const PanAgent *a,unsigned i){
    unsigned char coat[4]={90+(i%5)*15,67+(i%3)*12,48+(i%4)*9,255};float x=a->x/65536.0f,z=a->z/65536.0f,y=terrain(x,z),d=a->side?-1:1;
    float phase=s->tick*.25f+i;int j;ellipsoid(s,1,x,y+1.0f,z,.40f,.48f,.82f,coat);
    for(j=0;j<4;++j)box(s,1,x+(j&1?.27f:-.27f),y+.43f,z+(j&2?.5f:-.5f)+sinf(phase+j)*.16f,.07f,.40f,.08f,coat);
    ellipsoid(s,1,x,y+1.55f,z+d*.57f,.23f,.55f,.25f,coat);ellipsoid(s,1,x,y+1.94f,z+d*.81f,.18f,.23f,.36f,coat);
    box(s,1,x,y+1.36f,z,.43f,.04f,.45f,a->side?blue:red);soldier(s,x,y+1.10f,z,a->side,phase,1);
}
static void elephant(PanScene *s,const PanAgent *a,unsigned i){
    const unsigned char gray[4]={118,119,108,255};float x=a->x/65536.0f,z=a->z/65536.0f,y=terrain(x,z),d=a->side?-1:1;int j;
    ellipsoid(s,1,x,y+2.05f,z,1.05f,1.22f,1.8f,gray);for(j=0;j<4;++j)box(s,1,x+(j&1?.72f:-.72f),y+.79f,z+(j&2?1.08f:-1.08f),.27f,.78f,.28f,gray);
    ellipsoid(s,1,x,y+2.48f,z+d*1.65f,.80f,.82f,.78f,gray);
    for(j=0;j<2;++j)ellipsoid(s,1,x+(j?1:-1)*.80f,y+2.35f,z+d*1.44f,.34f,.72f,.18f,gray);
    for(j=0;j<4;++j)ellipsoid(s,1,x+sinf(s->tick*.06f+i)*.12f*j,y+2.0f-j*.34f,z+d*(2.15f+j*.12f),.20f-j*.025f,.30f,.23f,gray);
    for(j=0;j<2;++j)pole(s,x+(j?.48f:-.48f),y+1.98f,z+d*2,0,-.1f,d*.9f,.06f,linen);
    box(s,1,x,y+3.18f,z,.8f,.05f,.9f,red);soldier(s,x,y+2.87f,z+d*.5f,0,0,1);
}
static void landscape(PanScene *s){
    int x,z,i;for(z=-25;z<25;++z)for(x=-25;x<25;++x){float xx=x*4,zz=z*4;
        float p[4][3]={{xx,terrain(xx,zz),zz},{xx+4,terrain(xx+4,zz),zz},{xx+4,terrain(xx+4,zz+4),zz+4},{xx,terrain(xx,zz+4),zz+4}};
        unsigned h=hash((x+25)*51+z+25);unsigned char c[4]={162+h%16,150+h%19,105+h%15,255};
        if(zz< -64){c[0]=rock[0];c[1]=rock[1];c[2]=rock[2];}
        if(xx<-47 && xx>-65){c[0]=84;c[1]=112;c[2]=79;}
        triangle(s,0,p[0],p[1],p[2],c);triangle(s,0,p[0],p[2],p[3],c);}
    for(i=0;i<(int)s->trees;++i){unsigned h=hash(i+319);float xx=-96+(h%19200)*.01f,zz=-96+((h>>16)%19200)*.01f;
        if(fabsf(xx)<49 && fabsf(zz)<62)xx=(xx<0?-1:1)*(58+(h%3000)*.01f);
        float yy=terrain(xx,zz),scale=.75f+(h%90)*.01f;box(s,0,xx,yy+scale,zz,.13f,scale,.13f,wood);
        ellipsoid(s,0,xx,yy+2.1f*scale,zz,scale,1.4f*scale,scale,leaf);}
    /* Limestone outcrops and a grotto; no later Roman temple facade. */

    {const unsigned char dark[4]={42,49,43,255};ellipsoid(s,0,-10,3,-81,4,3.5f,.5f,dark);}
}
static int cell(int32_t x,int32_t z){int a=(x/65536+128)/4,b=(z/65536+128)/4;if(a<0)a=0;if(a>63)a=63;if(b<0)b=0;if(b>63)b=63;return b*64+a;}
static void force(const PanScene *s,unsigned i,int brute,int32_t *ox,int32_t *oz){
    const PanAgent *a=&s->agent[i];int64_t fx=(a->tx-a->x)/32,fz=(a->tz-a->z)/32;int c=cell(a->x,a->z),cx=c%64,cz=c/64,xx,zz,j;
    for(zz=brute?0:cz-1;zz<=(brute?0:cz+1);++zz)for(xx=brute?0:cx-1;xx<=(brute?0:cx+1);++xx){
        if(!brute && (xx<0||xx>63||zz<0||zz>63))continue;
        for(j=brute?0:s->head[zz*64+xx];j>=0 && j<(int)s->count;j=brute?j+1:s->next[j])if((unsigned)j!=i){
            int64_t dx=(int64_t)a->x-s->agent[j].x,dz=(int64_t)a->z-s->agent[j].z;
            if(llabs(dx)<60000 && llabs(dz)<60000 && dx*dx+dz*dz<3600000000LL){fx+=dx/8;fz+=dz/8;}}
    }
    {int limit=a->kind==1?2500:900;if(fx>limit)fx=limit;if(fx< -limit)fx= -limit;if(fz>limit)fz=limit;if(fz< -limit)fz= -limit;}
    *ox=a->x+(int32_t)fx;*oz=a->z+(int32_t)fz;
}
static void step(PanScene *s){unsigned i;for(i=0;i<4096;++i)s->head[i]=-1;
    for(i=0;i<s->count;++i){int c=cell(s->agent[i].x,s->agent[i].z);s->next[i]=s->head[c];s->head[c]=i;}
    for(i=0;i<s->count;++i)force(s,i,0,&s->agent[i].nx,&s->agent[i].nz);
    for(i=0;i<s->count;++i){s->agent[i].x=s->agent[i].nx;s->agent[i].z=s->agent[i].nz;}++s->tick;
}
static void camera(PanScene *s,unsigned frame,int w,int h){
    double eye[3],target[3]={0,0,0},f[3],r[3],u[3],n;int i;double angle=frame*.003;
    if(s->stage==0){eye[0]=0;eye[1]=1.7;eye[2]=5;target[1]=1.25;}else {eye[0]=90+sin(angle)*22;eye[1]=65+sin(angle)*8;eye[2]=150+cos(angle)*8;target[1]=0;}
    for(i=0;i<3;++i){f[i]=target[i]-eye[i];s->eye[i]=eye[i];}n=sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);for(i=0;i<3;++i)f[i]/=n;
    r[0]=-f[2];r[1]=0;r[2]=f[0];n=sqrt(r[0]*r[0]+r[2]*r[2]);for(i=0;i<3;++i)r[i]/=n;
    u[0]=r[1]*f[2]-r[2]*f[1];u[1]=r[2]*f[0]-r[0]*f[2];u[2]=r[0]*f[1]-r[1]*f[0];memset(s->view,0,sizeof s->view);
    for(i=0;i<3;++i){s->view[i*4]=r[i];s->view[i*4+1]=u[i];s->view[i*4+2]=-f[i];s->view[12]-=r[i]*eye[i];s->view[13]-=u[i]*eye[i];s->view[14]+=f[i]*eye[i];}s->view[15]=1;
    memset(s->projection,0,sizeof s->projection);n=1/tan((s->stage==0?37:s->stage==1?43:s->stage==2?13+2*sin(angle):s->stage==3?21+3*sin(angle):25+5*sin(angle))*3.141592653589793/360);
    s->projection[0]=n*h/w;s->projection[5]=n;double near=s->stage?1.0:.1;s->projection[10]=-(600.0+near)/(600.0-near);s->projection[11]=-1;s->projection[14]=-2*600*near/(600-near);
}
static void stone(PanScene *s,unsigned frame){
    static const float outline[8][2]={{-.72f,.1f},{.72f,.1f},{.76f,1.9f},{.61f,2.34f},{.21f,2.38f},{-.20f,2.13f},{-.58f,2.00f},{-.73f,1.70f}};
    unsigned char c[4]={173,176,166,255};float co=cosf(frame*.025f),si=sinf(frame*.025f);int i;size_t first=s->mesh[3].n;
    for(i=0;i<8;++i){int j=(i+1)%8;float a[3]={0,1.2f,.22f},b[3]={outline[i][0],outline[i][1],.22f},d[3]={outline[j][0],outline[j][1],.22f};
        float e[3]={b[0],b[1],-.22f},f[3]={d[0],d[1],-.22f},back[3]={0,1.2f,-.22f};
        triangle(s,3,a,b,d,c);triangle(s,3,back,f,e,c);triangle(s,3,b,e,f,c);triangle(s,3,b,f,d,c);}
    for(i=(int)first;i<(int)s->mesh[3].n;++i){PanVertex *v=&s->mesh[3].v[i];float x=v->x,z=v->z;
        v->u=(v->x+.76f)/1.52f;v->v=v->y/2.4f;if(z<.2f){v->u=.02f;v->v=.02f;}
        v->x=x*co+z*si;v->z=z*co-x*si;}
    {const unsigned char base[4]={89,75,60,255};box(s,1,0,-.05f,0,1,.1f,.65f,base);}
}
static void transparent(PanScene *s,unsigned frame){
    int i;if(s->stage<1)return;
    for(i=-25;i<25;++i){float z=i*4,x=-55+sinf(z*.025f)*5;unsigned char c[4]={71,139,147,160};
        float p[4][3]={{x-3,.35f,z},{x+3,.35f,z},{x+3,.35f,z+4},{x-3,.35f,z+4}};
        triangle(s,2,p[0],p[1],p[2],c);triangle(s,2,p[0],p[2],p[3],c);}
    for(i=0;i<(int)s->particles;++i){uint32_t h=hash(i+88);float x=-40+(h%8000)*.01f,z=-22+((h>>16)%4400)*.01f,y=terrain(x,z)+.5f+((frame+i)%80)*.025f;
        float r=.45f+(h%100)*.009f;unsigned char c[4]={192,171,127,32};float p[4][3]={{x-r,y-r,z},{x+r,y-r,z},{x+r,y+r,z},{x-r,y+r,z}};
        triangle(s,2,p[0],p[1],p[2],c);triangle(s,2,p[0],p[2],p[3],c);}
}
static float sort_eye[3];
static int far_first(const void *aa,const void *bb){const PanVertex *a=aa,*b=bb;double da=0,db=0;int i;for(i=0;i<3;++i){da+=(a[i].x-sort_eye[0])*(a[i].x-sort_eye[0])+(a[i].y-sort_eye[1])*(a[i].y-sort_eye[1])+(a[i].z-sort_eye[2])*(a[i].z-sort_eye[2]);db+=(b[i].x-sort_eye[0])*(b[i].x-sort_eye[0])+(b[i].y-sort_eye[1])*(b[i].y-sort_eye[1])+(b[i].z-sort_eye[2])*(b[i].z-sort_eye[2]);}if(da!=db)return da>db?-1:1;
    /* Equal distances must have the same order on big- and little-endian hosts. */
    for(i=0;i<3;++i){float av[5]={a[i].x,a[i].y,a[i].z,a[i].u,a[i].v},bv[5]={b[i].x,b[i].y,b[i].z,b[i].u,b[i].v};int j,c;
        for(j=0;j<5;++j)if(av[j]!=bv[j])return av[j]<bv[j]?-1:1;
        c=memcmp(a[i].c,b[i].c,4);if(c)return c;}
    return 0;}
int pan_create(PanScene *s,int stage){
    unsigned i;memset(s,0,sizeof *s);s->ok=1;s->stage=stage;if(stage<0||stage>4)return 0;
    s->infantry=stage==2?512:stage==3?1536:stage==4?4096:0;s->cavalry=stage==3?96:stage==4?192:0;s->elephants=stage==4?24:0;
    s->particles=stage==3?192:stage==4?768:0;s->trees=stage==1?320:stage==2?480:stage==3?640:stage==4?800:0;s->count=s->infantry+s->cavalry+s->elephants;
    s->agent=calloc(s->count?s->count:1,sizeof *s->agent);s->next=malloc((s->count?s->count:1)*sizeof *s->next);if(!s->agent||!s->next){pan_destroy(s);return 0;}
    for(i=0;i<s->count;++i){PanAgent *a=&s->agent[i];unsigned j=i%(s->infantry?s->infantry/2:1);a->side=i<s->infantry/2?0:1;
        unsigned cols=stage==2?32:stage==3?48:64;float x=((int)(j%cols)-(cols-1)*.5f)*1.15f,z=(j/cols)*1.05f+7;
        if(a->side)z=-z;
        if(i>=s->infantry && i<s->infantry+s->cavalry){j=i-s->infantry;a->kind=1;a->side=j%2;x=(j%2?-1:1)*(45+(j/2%8)*1.5f);z=(int)(j/16)*2-14;}
        if(i>=s->infantry+s->cavalry){j=i-s->infantry-s->cavalry;a->kind=2;a->side=0;x=((int)j-11.5f)*3;z=3;}
        a->x=(int32_t)(x*65536);a->z=(int32_t)(z*65536);a->tx=a->x;a->tz=(int32_t)((a->side?5:-5)*65536);
        if(a->kind==1){a->tx=(int32_t)((a->side?-31:31)*65536);a->tz=(int32_t)((a->side?20:-20)*65536);}}
    for(i=0;i<64*64;++i){unsigned n=222+hash(i)%34;s->texture[i*4]=s->texture[i*4+1]=s->texture[i*4+2]=n;s->texture[i*4+3]=255;}
    for(i=0;i<256*512;++i){unsigned x=i%256,y=i/256,h=hash(i);int ink=0,band=y>390?0:y>215?1:2,row=band==0?13:band==1?6:4;
        if(x>18&&x<238&&y>22&&y<478){unsigned c=hash(x/(band==0?9:5)+(y/row)*131);ink=(y%row==1 && (c&3)) || (x%5==1 && y%row<3 && (c&4));}
        unsigned n=ink?173+(h%38):66+h%22;s->stone[i*4]=n;s->stone[i*4+1]=n;s->stone[i*4+2]=n-3;s->stone[i*4+3]=255;}
    if(stage)landscape(s);if(!s->ok){pan_destroy(s);return 0;}return 1;
}
int pan_frame(PanScene *s,unsigned frame,int width,int height){
    unsigned i;s->mesh[1].n=s->mesh[2].n=s->mesh[3].n=0;camera(s,frame,width,height);
    if(s->stage==0)stone(s,frame);
    else {step(s);step(s);for(i=0;i<s->count;++i){PanAgent *a=&s->agent[i];if(a->kind==1)horse(s,a,i);else if(a->kind==2)elephant(s,a,i);else {float x=a->x/65536.0f,z=a->z/65536.0f;soldier(s,x,terrain(x,z),z,a->side,s->tick*.24f+i,0);}}
        transparent(s,frame);memcpy(sort_eye,s->eye,sizeof sort_eye);qsort(s->mesh[2].v,s->mesh[2].n/3,3*sizeof(PanVertex),far_first);}
    return s->ok;
}
int pan_state_check(PanScene *s){
    unsigned i;uint32_t h=2166136261u;for(i=0;i<s->count;++i){PanAgent *a=&s->agent[i];if(a->x < -128*65536 || a->x > 128*65536 || a->z < -128*65536 || a->z > 128*65536)return 0;h=(h^(uint32_t)a->x)*16777619u;h=(h^(uint32_t)a->z)*16777619u;}
    /* Compare the spatial index with an independent all-pairs search on a distributed subset. */
    for(i=0;i<4096;++i)s->head[i]=-1;for(i=0;i<s->count;++i){int c=cell(s->agent[i].x,s->agent[i].z);s->next[i]=s->head[c];s->head[c]=i;}
    for(i=0;i<s->count;i+=s->count/32+1){int32_t ax,az,bx,bz;force(s,i,0,&ax,&az);force(s,i,1,&bx,&bz);if(ax!=bx||az!=bz)return 0;++s->comparisons;}
    s->state_hash=h;return s->ok;
}
void pan_destroy(PanScene *s){int i;for(i=0;i<4;++i)free(s->mesh[i].v);free(s->agent);free(s->next);memset(s,0,sizeof *s);}
