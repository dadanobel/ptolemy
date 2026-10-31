/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "primitive.h"
#include <stddef.h>

int pt_r2_triangle_count(unsigned primitive,uint32_t n,uint64_t *count)
{
    if(!count)return 0;
    uint64_t c;
    switch(primitive) {
    case 4: c=n/3;break;
    case 5:case 6:case 15:c=n>2?n-2:0;break;
    case 13:c=(uint64_t)(n/4)*2;break;
    case 14:c=n>3?(uint64_t)((n-2)/2)*2:0;break;
    default:return 0;
    }
    *count=c;return 1;
}
int pt_r2_triangle(unsigned prim,uint32_t n,uint64_t ordinal,PtR2Triangle *out)
{
    uint64_t count;
    if(!out || !pt_r2_triangle_count(prim,n,&count) || ordinal>=count)return 0;
    uint32_t k=(uint32_t)ordinal;
    PtR2Triangle t={.edge_mask=7};
    switch(prim) {
    case 4:
        k*=3;t=(PtR2Triangle){{k,k+1,k+2},{k,k+1,k+2},k+2,7};break;
    case 5:
        t=(PtR2Triangle){{0,k+1,k+2},{0,k+1,k+2},k+2,7};break;
    case 6:
        t=(PtR2Triangle){{k+(k&1),k+!(k&1),k+2},{k,k+1,k+2},k+2,7};break;
    case 15:
        t=(PtR2Triangle){{0,k+1,k+2},{0,1,2},n-1,
                           (uint8_t)((k==0?1:0)|2|(k+3==n?4:0))};break;
    case 13:
        k=(k/2)*4;
        t=(PtR2Triangle){{k,k+1,k+2},{k,k+1,k+2},k+3,3};
        if(ordinal&1){t.vertex[1]=k+2;t.vertex[2]=k+3;t.edge_mask=6;}break;
    case 14:
        k=(k/2)*2;
        t=(PtR2Triangle){{k,k+1,k+3},{k,k+1,k+2},k+3,3};
        if(ordinal&1){t.vertex[1]=k+3;t.vertex[2]=k+2;t.edge_mask=6;}break;
    default:return 0;
    }
    *out=t;return 1;
}
