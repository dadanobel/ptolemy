/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
int main(int argc,const char **argv){@autoreleasepool{
    if(argc!=2)return 2;NSError *error=nil;NSString *source=[NSString stringWithContentsOfFile:@(argv[1]) encoding:NSUTF8StringEncoding error:&error];
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(!source||!device){NSLog(@"Compiler unavailable: %@",error);return 2;}
    MTLCompileOptions *options=[MTLCompileOptions new];options.mathMode=MTLMathModeSafe;
    id<MTLLibrary> library=[device newLibraryWithSource:source options:options error:&error];
    if(!library||![library newFunctionWithName:@"optics_unpack"]){NSLog(@"MSL rejected: %@",error);return 1;}
    puts("PASS: product pixel-unpack MSL including YUV compiles on the host Metal driver; no command queue or GPU work submitted");return 0;
}}
