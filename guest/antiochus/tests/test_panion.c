/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/panion.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
int main(void){PanScene *s=calloc(1,sizeof *s);PanOracle *o=malloc(sizeof *o);unsigned char *a=calloc(PAN_WIDTH*PAN_HEIGHT,4),*r=malloc(PAN_WIDTH*PAN_HEIGHT*4),*d=malloc(PAN_WIDTH*PAN_HEIGHT*4);int k;assert(s&&o&&a&&r&&d);
    for(k=0;k<5;++k){unsigned i;assert(pan_create(s,k));assert(s->count<=PAN_MAX_AGENTS);for(i=0;i<4;++i)assert(pan_frame(s,i,PAN_WIDTH,PAN_HEIGHT));assert(pan_state_check(s));
        printf("%s: %u agents; %lu triangles; state %08x\n",pan_ids[k],s->count,(unsigned long)(s->mesh[0].n+s->mesh[1].n+s->mesh[2].n+s->mesh[3].n)/3,s->state_hash);
        if(s->count){int32_t saved=s->agent[0].x;s->agent[0].x=129*65536;assert(!pan_state_check(s));s->agent[0].x=saved;}
        pan_reference(s,o,PAN_WIDTH,PAN_HEIGHT);for(i=0;i<PAN_PROBES;++i){int x=(int)((i%PAN_PROBES_X+.5)*PAN_WIDTH/PAN_PROBES_X),y=16+(int)((i/PAN_PROBES_X+.5)*(PAN_HEIGHT-16)/PAN_PROBES_Y);memcpy(a+(y*PAN_WIDTH+x)*4,o->color[i],4);}
        assert(pan_compare(o,a,PAN_WIDTH,PAN_HEIGHT,r,d));for(i=0;i<PAN_PROBES;++i)if(o->safe[i]){int x=(int)((i%PAN_PROBES_X+.5)*PAN_WIDTH/PAN_PROBES_X),y=16+(int)((i/PAN_PROBES_X+.5)*(PAN_HEIGHT-16)/PAN_PROBES_Y);a[(y*PAN_WIDTH+x)*4]^=255;break;}assert(!pan_compare(o,a,PAN_WIDTH,PAN_HEIGHT,r,d));pan_destroy(s);
    }
    pan_marker(a,PAN_WIDTH,128);assert(pan_marker_check(a,PAN_WIDTH,128));assert(!pan_marker_check(a,PAN_WIDTH,129));a[(6*PAN_WIDTH+PAN_WIDTH/32)*4]^=255;assert(!pan_marker_check(a,PAN_WIDTH,128));
    free(s);free(o);free(a);free(r);free(d);puts("Panion: bounds, independent neighbor search, corrupt pixel and stale frame rejected.");return 0;
}
