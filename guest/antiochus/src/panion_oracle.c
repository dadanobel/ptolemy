/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "panion.h"
#include <math.h>
#include <string.h>
typedef struct {double x,y,z,w,u,v;} Projected;
static Projected project(const PanScene *s,const PanVertex *v,int width,int height){
    Projected p;double a[4]={v->x,v->y,v->z,1},b[4]={0,0,0,0},c[4]={0,0,0,0};int i,j;
    for(i=0;i<4;++i)for(j=0;j<4;++j)b[i]+=s->view[j*4+i]*a[j];
    for(i=0;i<4;++i)for(j=0;j<4;++j)c[i]+=s->projection[j*4+i]*b[j];
    p.w=c[3];p.x=(c[0]/p.w+1)*width*.5;p.y=(c[1]/p.w+1)*height*.5;p.z=(c[2]/p.w+1)*.5;p.u=v->u;p.v=v->v;return p;
}
static double edge(Projected a,Projected b,double x,double y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
static int px(int i,int width){return (int)((i+.5)*width/PAN_PROBES_X);}
static int py(int i,int height){return 16+(int)((i+.5)*(height-16)/PAN_PROBES_Y);}
void pan_reference(const PanScene *s,PanOracle *o,int width,int height){
    double uncertain[PAN_PROBES];const double depth_guard=2.0/16777216.0,edge_guard=.25;
    unsigned i;int order[4]={0,1,3,2},pass;memset(o,0,sizeof *o);o->need_actors=s->count?8:0;
    for(i=0;i<PAN_PROBES;++i){uncertain[i]=2;o->depth[i]=1;o->safe[i]=1;o->color[i][0]=s->stage?186:28;o->color[i][1]=s->stage?199:33;o->color[i][2]=s->stage?195:38;o->color[i][3]=255;}
    for(pass=0;pass<4;++pass){int layer=order[pass];const PanMesh *m=&s->mesh[layer];size_t at;
        for(at=0;at<m->n;at+=3){const PanVertex *v=&m->v[at];Projected p[3];double area,minx,maxx,miny,maxy,light=1;int x0,x1,y0,y1,x,y,k;
            if(layer==3){double ax=v[1].x-v[0].x,ay=v[1].y-v[0].y,az=v[1].z-v[0].z,bx=v[2].x-v[0].x,by=v[2].y-v[0].y,bz=v[2].z-v[0].z,nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx,n=sqrt(nx*nx+ny*ny+nz*nz);light=.54+.46*fabs((nx*.4+ny*.82+nz*.4)/(n*sqrt(.9924)));}
            for(k=0;k<3;++k)p[k]=project(s,&v[k],width,height);
            /* All authored geometry stays in front of the near plane in scored cameras. */
            if(p[0].w<=.1||p[1].w<=.1||p[2].w<=.1)continue;
            area=edge(p[0],p[1],p[2].x,p[2].y);if(fabs(area)<1e-10)continue;
            minx=fmin(p[0].x,fmin(p[1].x,p[2].x));maxx=fmax(p[0].x,fmax(p[1].x,p[2].x));
            miny=fmin(p[0].y,fmin(p[1].y,p[2].y));maxy=fmax(p[0].y,fmax(p[1].y,p[2].y));
            x0=(int)floor(minx*PAN_PROBES_X/width-.5)-1;x1=(int)ceil(maxx*PAN_PROBES_X/width-.5)+1;
            y0=(int)floor((miny-16)*PAN_PROBES_Y/(height-16)-.5)-1;y1=(int)ceil((maxy-16)*PAN_PROBES_Y/(height-16)-.5)+1;
            if(x0<0)x0=0;if(y0<0)y0=0;if(x1>=PAN_PROBES_X)x1=PAN_PROBES_X-1;if(y1>=PAN_PROBES_Y)y1=PAN_PROBES_Y-1;
            for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x){double xx=px(x,width)+.5,yy=py(y,height)+.5;
                double a=edge(p[1],p[2],xx,yy)/area,b=edge(p[2],p[0],xx,yy)/area,c=1-a-b,z;
                unsigned idx=y*PAN_PROBES_X+x;double near=1e9;int safe=1,tw=layer==3?256:64,th=layer==3?512:64;
                const unsigned char *tex=layer==3?s->stone:s->texture;
                z=a*p[0].z+b*p[1].z+c*p[2].z;if(z<0)continue;
                for(k=0;k<3;++k){Projected q=p[(k+1)%3];double length=hypot(q.x-p[k].x,q.y-p[k].y);double d=fabs(edge(p[k],q,xx,yy))/(length>0?length:1);if(d<near)near=d;}
                if(a<0||b<0||c<0){
                    double wa=fabs(area)*a/hypot(p[2].x-p[1].x,p[2].y-p[1].y),wb=fabs(area)*b/hypot(p[0].x-p[2].x,p[0].y-p[2].y),wc=fabs(area)*c/hypot(p[1].x-p[0].x,p[1].y-p[0].y);
                    if(wa>=-edge_guard && wb>=-edge_guard && wc>=-edge_guard && z<uncertain[idx])uncertain[idx]=z;
                    continue;}
                if(z>=o->depth[idx]){if(z-o->depth[idx]<depth_guard)o->safe[idx]=0;continue;}
                if(near<edge_guard || fabs(z-o->depth[idx])<depth_guard)safe=0;
                {double inv=a/p[0].w+b/p[1].w+c/p[2].w;
                 double u=(a*p[0].u/p[0].w+b*p[1].u/p[1].w+c*p[2].u/p[2].w)/inv;
                 double t=(a*p[0].v/p[0].w+b*p[1].v/p[1].w+c*p[2].v/p[2].w)/inv;
                 double tx=(u-floor(u))*tw,ty=(t-floor(t))*th;unsigned n=((unsigned)ty*tw+(unsigned)tx)*4;
                 if(layer!=2 && (tx-floor(tx)<.08||tx-floor(tx)>.92||ty-floor(ty)<.08||ty-floor(ty)>.92))safe=0;
                 for(k=0;k<3;++k){double value=v[2].c[k]*light*(layer==2?1:tex[n+k]/255.0),alpha=v[2].c[3]/255.0;
                    if(layer==2)value=value*alpha+o->color[idx][k]*(1-alpha);o->color[idx][k]=(unsigned char)floor(value+.5);}}
                if(layer!=2){o->depth[idx]=(float)z;o->safe[idx]=safe;o->actor[idx]=layer==1;}else o->safe[idx]=o->safe[idx]&&safe;
            }
        }
    }
    for(i=0;i<PAN_PROBES;++i)if(uncertain[i]<=o->depth[i]+depth_guard)o->safe[i]=0;
}
int pan_compare(PanOracle *o,const unsigned char *actual,int width,int height,unsigned char *ref,unsigned char *diff){
    int x,y,k;o->tested=o->bad=o->maximum=o->actor_tested=0;memset(ref,0,(size_t)width*height*4);memset(diff,0,(size_t)width*height*4);
    for(y=0;y<PAN_PROBES_Y;++y)for(x=0;x<PAN_PROBES_X;++x){unsigned i=y*PAN_PROBES_X+x,n=(py(y,height)*width+px(x,width))*4,bad=0;if(!o->safe[i])continue;++o->tested;if(o->actor[i])++o->actor_tested;
        for(k=0;k<3;++k){unsigned a=actual[n+k],b=o->color[i][k],d=a>b?a-b:b-a;if(d>o->maximum)o->maximum=d;if(d>4)bad=1;ref[n+k]=b;diff[n+k]=(unsigned char)(d>63?255:d*4);}ref[n+3]=diff[n+3]=255;o->bad+=bad;}
    return o->tested>=128 && o->actor_tested>=o->need_actors && !o->bad;
}
void pan_marker(unsigned char *rgba,int width,unsigned frame){int x,y;for(y=0;y<12;++y)for(x=0;x<width;++x){int cell=x*16/width;unsigned char v=((frame>>cell)&1)?255:0;size_t at=(size_t)(y*width+x)*4;rgba[at]=rgba[at+1]=rgba[at+2]=v;rgba[at+3]=255;}}
int pan_marker_check(const unsigned char *rgba,int width,unsigned frame){int i,k;for(i=0;i<16;++i){unsigned char v=((frame>>i)&1)?255:0;int x=(int)((i+.5)*width/16);for(k=0;k<3;++k){int d=(int)rgba[(6*width+x)*4+k]-v;if(d< -2||d>2)return 0;}}return 1;}
