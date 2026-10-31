/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compile the production vertex-fetch generator as a compute fixture. No test-only backend API. */
#include "../src/optics/renderer.mm"
extern "C" {
#include "../src/optics/draw.h"
}
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
static uint32_t word(unsigned v,unsigned c,bool floating){
    static const uint32_t f[]={0,0x80000000,0x3f800000,0xbf800000,0x3e800000,0xc1000000,0x47000000,0x3f000000};
    if(floating)return f[(v+c)%8];
    return ((v+c*1337)&65535)|((65535-v+c*977)&65535)<<16;
}
static unsigned lane(unsigned c,unsigned kind){return kind==1&&c<3?2-c:c;}
static float expected(unsigned format,unsigned c,unsigned v){
    if(format==OPTICS_RAW_DEFAULT)return c==3?1.0f:0.0f;
    bool floating=format>=OPTICS_RAW_FLOAT&&format<OPTICS_RAW_DEFAULT,method2=format>=OPTICS_RAW_NORM2;
    unsigned code=method2?format-OPTICS_RAW_NORM2:floating?format-OPTICS_RAW_FLOAT:format-OPTICS_RAW_INT;
    unsigned kind=code&3,components=floating?kind+1:kind==2?2:4;
    if(c>=components)return c==3?1.0f:0.0f;
    if(floating){uint32_t x=word(v,c,true);float f;memcpy(&f,&x,4);return f;}
    unsigned k=lane(c,kind),bits=kind<2?8:16;
    uint32_t u=word(v,kind<2?0:k/2,false)>>(bits*(kind<2?k:k&1))&((1u<<bits)-1);
    bool sign=method2||(code&4),norm=method2||(code&8);int value=sign&&u>=(1u<<(bits-1))?(int)u-(int)(1u<<bits):(int)u;
    if(!norm)return (float)value;
    double f=method2?(2.0*value+1.0)/((1u<<bits)-1):(double)value/((1u<<(bits-sign))-1);
    if(!method2&&(code&16)&&f<-1)f=-1;
    return (float)f;
}
static bool close(float a,float b,bool normalized){
    uint32_t x,y;memcpy(&x,&a,4);memcpy(&y,&b,4);if(x==y)return true;
    return normalized&&((x^y)&0x80000000)==0&&(x>y?x-y:y-x)<=1;
}
int main(int argc,char **argv){@autoreleasepool{
    assert(argc==1||(argc==2&&!strcmp(argv[1],"--compile-only")));
    id<MTLDevice> dev=MTLCreateSystemDefaultDevice();assert(dev);
    std::vector<unsigned> formats;for(unsigned f=OPTICS_RAW_INT;f<OPTICS_RAW_NORM2+16;f++)formats.push_back(f);
    const unsigned group=8,count=65536,stride=128;
    NSMutableString *source=[NSMutableString stringWithString:@"#include <metal_stdlib>\nusing namespace metal;struct PtIO{float4 input[16];};\n"];
    [source appendString:rawLoadSource()];
    for(unsigned base=0;base<formats.size();base+=group){
        [source appendFormat:@"kernel void test%u(uint id [[thread_position_in_grid]],device const uchar *v [[buffer(0)]],device float4 *out [[buffer(1)]]){PtIO io;\n",base];
        PtR2Route r={};r.stride=stride;
        for(unsigned j=0;j<group&&base+j<formats.size();j++){
            unsigned f=formats[base+j],kind=f>=OPTICS_RAW_NORM2?(f-OPTICS_RAW_NORM2)&3:f>=OPTICS_RAW_FLOAT?(f-OPTICS_RAW_FLOAT)&3:(f-OPTICS_RAW_INT)&3;
            PtR2Attribute a={(uint16_t)(j*16),(uint8_t)(f==OPTICS_RAW_DEFAULT?4:f>=OPTICS_RAW_FLOAT&&f<OPTICS_RAW_DEFAULT?kind+1:kind==2?2:4),(uint8_t)f};
            for(unsigned selection=0;selection<3;selection++){
                r.vapSelect[j]=f==OPTICS_RAW_DEFAULT?0:selection==0?OPTICS_RAW_IDENTITY:selection==1?0xb000|1u|5u<<6|2u<<9:0xf000|5u|4u<<3|1u<<6;
                [source appendString:@"{\n"];rawInput(source,j,a,r);[source appendFormat:@"out[id*%uu+%u]=io.input[%u];}\n",3*group,selection*group+j,j];
            }
        }[source appendString:@"}\n"];
    }
    MTLCompileOptions *options=[MTLCompileOptions new];options.mathMode=MTLMathModeSafe;NSError *error=nil;
    id<MTLLibrary> lib=[dev newLibraryWithSource:source options:options error:&error];if(!lib)fprintf(stderr,"%s\n",error.description.UTF8String);assert(lib);
    if(argc==2){puts("PASS: production VAP input MSL compiles; no GPU work submitted");return 0;}
    id<MTLCommandQueue> queue=[dev newCommandQueue];assert(queue);
    id<MTLBuffer> input=[dev newBufferWithLength:(NSUInteger)count*stride options:MTLResourceStorageModeShared];
    id<MTLBuffer> output=[dev newBufferWithLength:(NSUInteger)count*3*group*16 options:MTLResourceStorageModeShared];assert(input&&output);
    uint8_t *bytes=(uint8_t *)input.contents;float *got=(float *)output.contents;
    static const uint8_t perm[4][4]={{0,1,2,3},{1,0,3,2},{3,2,1,0},{2,3,0,1}};uint64_t checked=0;
    for(unsigned base=0;base<formats.size();base+=group){
        for(unsigned j=0;j<group&&base+j<formats.size();j++){
            unsigned f=formats[base+j];bool floating=f>=OPTICS_RAW_FLOAT&&f<OPTICS_RAW_DEFAULT;
            unsigned swap=f==OPTICS_RAW_DEFAULT?0:f>=OPTICS_RAW_NORM2?(f-OPTICS_RAW_NORM2)/4:floating?(f-OPTICS_RAW_FLOAT)/4:(f-OPTICS_RAW_INT)/32;
            for(unsigned v=0;v<count;v++)for(unsigned c=0;c<4;c++){
                uint32_t w=word(v,c,floating);for(unsigned b=0;b<4;b++)bytes[(size_t)v*stride+j*16+c*4+b]=(uint8_t)(w>>(8*perm[swap][b]));
            }
        }
        if(getenv("OPTICS_TEST_WRONG_SWAP")&&base==0)bytes[0]^=1;
        id<MTLFunction> fn=[lib newFunctionWithName:[NSString stringWithFormat:@"test%u",base]];
        id<MTLComputePipelineState> pipeline=[dev newComputePipelineStateWithFunction:fn error:&error];assert(pipeline);
        id<MTLCommandBuffer> cb=[queue commandBuffer];id<MTLComputeCommandEncoder> enc=[cb computeCommandEncoder];
        [enc setComputePipelineState:pipeline];[enc setBuffer:input offset:0 atIndex:0];[enc setBuffer:output offset:0 atIndex:1];
        [enc dispatchThreads:MTLSizeMake(count,1,1) threadsPerThreadgroup:MTLSizeMake(pipeline.threadExecutionWidth,1,1)];[enc endEncoding];[cb commit];[cb waitUntilCompleted];assert(cb.status==MTLCommandBufferStatusCompleted);
        for(unsigned selection=0;selection<3;selection++)for(unsigned j=0;j<group&&base+j<formats.size();j++)for(unsigned v=0;v<count;v++)for(unsigned c=0;c<4;c++){
            unsigned f=formats[base+j];float ref=expected(f,c,v);
            if(f!=OPTICS_RAW_DEFAULT&&selection==1)ref=c==2?0:expected(f,c==0?1:c==1?0:2,v);
            if(f!=OPTICS_RAW_DEFAULT&&selection==2)ref=c==0?1:c==1?0:expected(f,c==2?1:0,v);
            float actual=got[(((size_t)v*3+selection)*group+j)*4+c];bool norm=f>=OPTICS_RAW_NORM2||(f<OPTICS_RAW_FLOAT&&((f-OPTICS_RAW_INT)&8));
            if(!close(ref,actual,norm)){fprintf(stderr,"format=%u vertex=%u component=%u expected=%a actual=%a\n",f,v,c,ref,actual);assert(false);}checked++;
        }
    }
    printf("PASS: %zu native VAP formats; %llu GPU components; exact integer/float/defaults, <=1 ULP normalized; exhaustive signed-short method 2\n",formats.size(),(unsigned long long)checked);
}}
