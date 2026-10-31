/* SPDX-License-Identifier: GPL-2.0-or-later */
#import "io_metal.h"
#include <string.h>
static void io_error(NSError **error,PtR2IoResult result) {
    if(error)*error=[NSError errorWithDomain:@"Ptolemy.Radeon2.IO" code:result
        userInfo:@{NSLocalizedDescriptionKey:[NSString stringWithUTF8String:pt_r2_io_result(result)]}];
}
@interface PtR2Export : NSObject {
@public PtR2ExportDescriptor descriptor;PtR2Readback *readback;PtR2ExportData *collected;
}
@end
@implementation PtR2Export @end
@interface PtR2ExportData () {@public PtR2ExportDescriptor descriptor;NSData *data;} @end
@implementation PtR2ExportData
- (size_t)byteCount {return data.length;}
@end
PtR2IoResult pt_r2_export_reserve(PtR2Io *io,uint64_t address,uint32_t w,uint32_t h,size_t pitch,const uint8_t lanes[4],PtR2ExportDescriptor *out) {
    uint64_t row=(uint64_t)w*4;unsigned mask=0;
    if(lanes)for(unsigned i=0;i<4;i++)if(lanes[i]<4)mask|=1u<<lanes[i];
    if(!out||!w||!h||w>16384||h>16384||row>pitch||mask!=15||(h>1&&pitch>(UINT64_MAX-row)/(h-1)))return PT_R2_IO_ARGUMENT;
    uint64_t span=(uint64_t)(h-1)*pitch+row;PtR2IoToken token;
    PtR2IoResult result=pt_r2_io_write_begin(io,address,(size_t)span,&token);if(result!=PT_R2_IO_OK)return result;
    *out=(PtR2ExportDescriptor){token,w,h,pitch,{0,0,0,0}};memcpy(out->lanes,lanes,4);return PT_R2_IO_OK;
}
PtR2Export *pt_r2_export_encode(PtR2Renderer *renderer,PtR2Target *target,PtR2ExportDescriptor descriptor,uint32_t control,NSError **error) {
    if(!renderer||!target||!descriptor.token.id||target.width!=descriptor.width||target.height!=descriptor.height){io_error(error,PT_R2_IO_ARGUMENT);return nil;}
    PtR2Readback *readback=[renderer readback:target resolveControl:control error:error];if(!readback)return nil;
    PtR2Export *result=[PtR2Export new];result->descriptor=descriptor;result->readback=readback;return result;
}
PtR2ExportData *pt_r2_export_collect(PtR2Renderer *renderer,PtR2Export *encoded,NSError **error) {
    if(!renderer||!encoded){io_error(error,PT_R2_IO_ARGUMENT);return nil;}
    if(encoded->collected)return encoded->collected;
    NSData *bytes=[renderer copyReadback:encoded->readback error:error];if(!bytes)return nil;
    PtR2ExportData *result=[PtR2ExportData new];result->descriptor=encoded->descriptor;result->data=bytes;encoded->collected=result;return result;
}
PtR2IoResult pt_r2_export_apply(PtR2Io *io,PtR2ExportData *result) {
    if(!result)return PT_R2_IO_ARGUMENT;PtR2ExportDescriptor d=result->descriptor;
    return pt_r2_io_write_rgba_rows(io,d.token,(const uint8_t *)result->data.bytes,result->data.length,d.width,d.height,d.pitch,d.lanes);
}
NSData *pt_r2_snapshot_detach(PtR2Io *io,PtR2IoToken token,NSError **error) {
    uint8_t *bytes=NULL;size_t n=0;PtR2IoResult r=pt_r2_io_snapshot_take_data(io,token,&bytes,&n);
    if(r!=PT_R2_IO_OK){io_error(error,r);return nil;}
    return [NSData dataWithBytesNoCopy:bytes length:n freeWhenDone:YES];
}
