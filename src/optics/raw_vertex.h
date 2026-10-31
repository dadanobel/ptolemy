/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Internal immutable vertex formats, independent of guest register encodings. */
#ifndef OPTICS_RAW_VERTEX_H
#define OPTICS_RAW_VERTEX_H
#include <stdint.h>
#include <limits.h>
enum { OPTICS_RAW_INT=3,OPTICS_RAW_FLOAT=131,OPTICS_RAW_DEFAULT=147,OPTICS_RAW_NORM2=148,OPTICS_RAW_IDENTITY=0xf688 };
/* INT: type BYTE/D3DCOLOR/SHORT2/SHORT4 in bits 1:0, signed 2, normalized 3,
 * signed-normalization method 4, VAP dword byte permutation 6:5. */
static inline unsigned optics_raw_width(unsigned format,unsigned components) {
    if(format<3)return components>=1&&components<=4?components*(format==2?1:4):UINT_MAX;
    if(format<OPTICS_RAW_FLOAT){unsigned k=format-OPTICS_RAW_INT,type=k&3;return components==(type==2?2u:4u)?(type==3?8u:4u):UINT_MAX;}
    if(format<OPTICS_RAW_DEFAULT)return components==((format-OPTICS_RAW_FLOAT)&3)+1?components*4:UINT_MAX;
    if(format==OPTICS_RAW_DEFAULT)return components==4?0:UINT_MAX;
    if(format<OPTICS_RAW_NORM2+16){unsigned type=(format-OPTICS_RAW_NORM2)&3;return components==(type==2?2u:4u)?(type==3?8u:4u):UINT_MAX;}
    return UINT_MAX;
}
#endif
