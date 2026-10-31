/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_INPUT_BUILTIN_H
#define PT_INPUT_BUILTIN_H
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
/* Tiger KL APIs, public 10.4 ABI. Dynamic lookup keeps an absent service a
 * reported incompatibility instead of an unresolved symbol or guessed layout. */
typedef struct {int16_t v,h;} PtGlobalPoint;
static struct {void *handle;int32_t (*count)(CFIndex *);int32_t (*at)(CFIndex,void **);int32_t (*property)(void *,uint32_t,const void **);int32_t (*select)(void *);void (*mouse)(PtGlobalPoint *);size_t (*width)(uint32_t);size_t (*height)(uint32_t);int (*visible)(void);uint32_t (*main_display)(void);} pt_input_api;
static void pt_input_guest_begin(unsigned caps){
    if(!(caps&PTA_CAP_POINTER))return;
    pt_input_api.handle=dlopen("/System/Library/Frameworks/Carbon.framework/Carbon",RTLD_LAZY|RTLD_LOCAL);
    void *q=dlopen("/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices",RTLD_LAZY|RTLD_LOCAL);
    if(pt_input_api.handle){*(void **)&pt_input_api.count=dlsym(pt_input_api.handle,"KLGetKeyboardLayoutCount");*(void **)&pt_input_api.at=dlsym(pt_input_api.handle,"KLGetKeyboardLayoutAtIndex");*(void **)&pt_input_api.property=dlsym(pt_input_api.handle,"KLGetKeyboardLayoutProperty");*(void **)&pt_input_api.select=dlsym(pt_input_api.handle,"KLSetCurrentKeyboardLayout");*(void **)&pt_input_api.mouse=dlsym(pt_input_api.handle,"GetGlobalMouse");}
    if(q){*(void **)&pt_input_api.main_display=dlsym(q,"CGMainDisplayID");*(void **)&pt_input_api.width=dlsym(q,"CGDisplayPixelsWide");*(void **)&pt_input_api.height=dlsym(q,"CGDisplayPixelsHigh");*(void **)&pt_input_api.visible=dlsym(q,"CGCursorIsVisible");}
}
static int pt_input_builtin(uint32_t token,uint32_t job,char **argv,unsigned *status){
    if(strcmp(argv[0],"/Ptolemy/input"))return 0;*status=2;
    if(!argv[1]||strcmp(argv[1],"layout")||!argv[2]||!argv[3]||argv[4]||!pt_input_api.count||!pt_input_api.at||!pt_input_api.property||!pt_input_api.select)return 1;
    const char *name=argv[2];const char *prefix="com.apple.keylayout.";if(!strncmp(name,prefix,strlen(prefix)))name+=strlen(prefix);
    if(!strcmp(name,"US")||!strcmp(name,"ABC"))name="U.S.";else if(!strcmp(name,"RussianWin"))name="Russian - PC";
    CFStringRef wanted=CFStringCreateWithCString(NULL,name,kCFStringEncodingUTF8),language=CFStringCreateWithCString(NULL,argv[3],kCFStringEncodingUTF8);
    CFIndex count=0;void *chosen=NULL,*fallback=NULL;unsigned matches=0;
    if(wanted&&language&&!pt_input_api.count(&count)&&count>0&&count<=4096)for(CFIndex i=0;i<count;i++){
        void *layout=NULL;const void *value=NULL;if(pt_input_api.at(i,&layout)||!layout)continue;
        if(!pt_input_api.property(layout,5,&value)&&value&&CFGetTypeID(value)==CFStringGetTypeID()&&CFStringCompare(value,wanted,kCFCompareCaseInsensitive)==kCFCompareEqualTo){chosen=layout;break;}
        value=NULL;if(!pt_input_api.property(layout,9,&value)&&value&&CFGetTypeID(value)==CFStringGetTypeID()){
            CFArrayRef parts=CFStringCreateArrayBySeparatingStrings(NULL,value,CFSTR("_"));if(parts&&CFArrayGetCount(parts)&&CFStringCompare(CFArrayGetValueAtIndex(parts,0),language,kCFCompareCaseInsensitive)==kCFCompareEqualTo){fallback=layout;matches++;}if(parts)CFRelease(parts);
        }
    }
    if(!chosen&&matches==1)chosen=fallback;if(chosen&&!pt_input_api.select(chosen))*status=0;
    if(wanted)CFRelease(wanted);if(language)CFRelease(language);uint32_t reply[3];const char *message=*status?"Input source is unavailable or ambiguous in Tiger\n":"Tiger input source selected\n";
    ptagent_call(PTA_OUTPUT,token,job,(uint32_t)(uintptr_t)message,(uint32_t)strlen(message),reply);return 1;
}
static void pt_input_guest_tick(uint32_t token,uint64_t now){
    static uint64_t last;if(!pt_input_api.visible||!pt_input_api.mouse||!pt_input_api.width||!pt_input_api.height||!pt_input_api.main_display||(last&&now>=last&&now-last<50))return;last=now;
    uint32_t width=(uint32_t)pt_input_api.width(pt_input_api.main_display()),height=(uint32_t)pt_input_api.height(pt_input_api.main_display());PtGlobalPoint point;pt_input_api.mouse(&point);
    uint8_t wire[24];uint32_t value[6]={1,pt_input_api.visible()?1u:2u,(uint32_t)(int32_t)point.h,(uint32_t)(int32_t)point.v,width,height},reply[3];
    for(unsigned i=0;i<6;i++){uint32_t v=value[i];wire[4*i]=v>>24;wire[4*i+1]=v>>16;wire[4*i+2]=v>>8;wire[4*i+3]=v;}
    (void)ptagent_call(PTA_POINTER,token,(uint32_t)(uintptr_t)wire,sizeof wire,0,reply);
}
#endif
