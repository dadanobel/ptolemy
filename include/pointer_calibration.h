/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_POINTER_CALIBRATION_H
#define PT_POINTER_CALIBRATION_H
#include <math.h>
#include <string.h>
/* Settled observations only. Keep linear mapping until both axes have spread,
 * bounded affine coefficients and a two-pixel residual across eight points. */
typedef struct {double input[8][2],output[8][2],scale[2],bias[2];unsigned count,next,valid;} PtPointerCalibration;
static inline void pt_pointer_calibration_reset(PtPointerCalibration *p){memset(p,0,sizeof *p);p->scale[0]=p->scale[1]=1;}
static inline void pt_pointer_calibration_add(PtPointerCalibration *p,double x,double y,double gx,double gy,unsigned w,unsigned h){
    double in[2]={x,y},out[2]={gx,gy};if(w<320||h<200)return;
    for(unsigned j=0;j<2;j++)if(!isfinite(in[j])||!isfinite(out[j])||in[j]<=0.01||in[j]>=0.99||out[j]<=0.01||out[j]>=0.99)return;
    if(p->count){unsigned k=(p->next+7)%8;if(fabs(p->input[k][0]-x)+fabs(p->input[k][1]-y)<0.08)return;}
    unsigned k=p->next++%8;memcpy(p->input[k],in,sizeof in);memcpy(p->output[k],out,sizeof out);if(p->count<8)p->count++;
    if(p->count<8)return;p->valid=0;double scale[2],bias[2];
    for(unsigned j=0;j<2;j++){double sx=0,sy=0,sxx=0,sxy=0,lo=1,hi=0;
        for(unsigned i=0;i<8;i++){double a=p->input[i][j],b=p->output[i][j];sx+=a;sy+=b;sxx+=a*a;sxy+=a*b;if(a<lo)lo=a;if(a>hi)hi=a;}
        double denominator=8*sxx-sx*sx;if(hi-lo<0.25||denominator<0.01)return;
        scale[j]=(8*sxy-sx*sy)/denominator;bias[j]=(sy-scale[j]*sx)/8;
        if(scale[j]<0.75||scale[j]>1.25||fabs(bias[j])>0.15)return;
        for(unsigned i=0;i<8;i++)if(fabs(scale[j]*p->input[i][j]+bias[j]-p->output[i][j])*(j?h:w)>2)return;
    }memcpy(p->scale,scale,sizeof scale);memcpy(p->bias,bias,sizeof bias);p->valid=1;
}
static inline double pt_pointer_calibration_apply(PtPointerCalibration *p,unsigned axis,double value){double v=p->valid?(value-p->bias[axis])/p->scale[axis]:value;return fmax(0,fmin(1,v));}
#endif
