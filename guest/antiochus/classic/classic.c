/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <Types.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <Dialogs.h>
#include <AppleEvents.h>
#include <Files.h>
#include <Gestalt.h>
#include <Timer.h>
#include <TextEdit.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
/* Multiversal exports Microseconds but omits its declaration. */
typedef struct { uint32_t hi,lo; } AntMicroseconds;
extern pascal void Microseconds(AntMicroseconds *value);
static WindowPtr window;
static int done;
static unsigned char input[65536];
static uint32_t table[256];
static volatile uint32_t observed;
static void pascal_string(Str255 out,const char *in) {
    size_t n=strlen(in);if(n>255)n=255;out[0]=(unsigned char)n;memcpy(out+1,in,n);
}
static double seconds(void) { AntMicroseconds t;Microseconds(&t);return ((double)t.hi*4294967296.0+t.lo)*1e-6; }
static uint32_t crc(int slow) {
    uint32_t c=~0u;unsigned i,j;
    for(i=0;i<sizeof input;++i) {
        if(slow){c^=input[i];for(j=0;j<8;++j)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}
        else c=(c>>8)^table[(c^input[i])&255];
    }
    return ~c;
}
static int sibling(const FSSpec *request,const char *name,FSSpec *out) {
    Str255 p;OSErr err;pascal_string(p,name);err=FSMakeFSSpec(request->vRefNum,request->parID,p,out);
    return err==noErr || err==fnfErr;
}
static int write_bytes(FSSpec *spec,const void *bytes,long count) {
    short file;OSErr err=FSpCreate(spec,'AnCl','TEXT',smSystemScript);long written=count;
    if(err!=noErr && err!=dupFNErr)return 0;
    if(FSpOpenDF(spec,fsWrPerm,&file)!=noErr)return 0;
    err=SetEOF(file,0);if(!err)err=FSWrite(file,&written,bytes);if(FSClose(file)!=noErr)err=ioErr;
    return err==noErr && written==count;
}
static int request_nonce(FSSpec *spec,char *nonce) {
    short file;long n=255;char data[256],*p,*end;
    if(FSpOpenDF(spec,fsRdPerm,&file)!=noErr)return 0;
    OSErr err=FSRead(file,&n,data);FSClose(file);if(err!=noErr && err!=eofErr)return 0;data[n]=0;
    const char *prefix="ANTIOCHUS_REQUEST_1\nnonce\t";
    if(strncmp(data,prefix,strlen(prefix)))return 0;
    p=data+strlen(prefix);end=strchr(p,'\n');if(!end || end-p!=36)return 0;
    memcpy(nonce,p,36);nonce[36]=0;return 1;
}
static void pattern(GWorldPtr source,GWorldPtr target) {
    Rect r,bounds;unsigned x,y;RGBColor color;
    SetRect(&bounds,0,0,257,129);SetGWorld(source,NULL);
    for(y=0;y<129;y+=16)for(x=0;x<257;x+=16) {
        unsigned red=(x/16*13)&255,green=(y/16*29)&255,blue=((x/16)^(y/16))*11;
        color.red=red*257;color.green=green*257;color.blue=(blue&255)*257;RGBForeColor(&color);
        SetRect(&r,x,y,x+16>257?257:x+16,y+16>129?129:y+16);PaintRect(&r);
    }
    SetGWorld(target,NULL);color.red=color.green=color.blue=0;RGBForeColor(&color);color.red=color.green=color.blue=65535;RGBBackColor(&color);
    CopyBits((BitMap *)*GetGWorldPixMap(source),(BitMap *)*GetGWorldPixMap(target),&bounds,&bounds,srcCopy,NULL);
}
static int check_pixels(GWorldPtr target) {
    unsigned x,y;PixMapHandle pixels=GetGWorldPixMap(target);
    unsigned char *base=(unsigned char *)GetPixBaseAddr(pixels);unsigned stride=(**pixels).rowBytes&0x3fff;
    for(y=0;y<129;++y)for(x=0;x<257;++x) {
        const unsigned char *p=base+y*stride+x*4;
        if(p[1]!=((x/16*13)&255) || p[2]!=((y/16*29)&255) || p[3]!=((((x/16)^(y/16))*11)&255))return 0;
    }
    return 1;
}
static void execute(FSSpec *request) {
    char nonce[64],text[2048];unsigned i,j,s;uint32_t seed=0x416e7439u,expected;double cpu[5],qdtime[5];
    int pass=1;SInt32 version=0;Rect bounds;GWorldPtr a=NULL,b=NULL;CGrafPtr old;GDHandle device;
    FSSpec temporary,result;Str255 resultName;
    if(!request_nonce(request,nonce))return;
    if(Gestalt(gestaltSystemVersion,&version)!=noErr || version<0x0900 || version>=0x1000)pass=0;
    for(i=0;i<sizeof input;++i){seed=seed*1664525u+1013904223u;input[i]=seed>>24;}
    for(i=0;i<256;++i){uint32_t c=i;for(j=0;j<8;++j)c=(c>>1)^(0xedb88320u&(0u-(c&1)));table[i]=c;}
    expected=crc(1);
    for(s=0;s<5;++s){double start=seconds();for(i=0;i<32;++i)observed=crc(0);cpu[s]=(seconds()-start)/32;if(observed!=expected || cpu[s]<=0)pass=0;}
    GetGWorld(&old,&device);SetRect(&bounds,0,0,257,129);
    if(NewGWorld(&a,32,&bounds,NULL,NULL,0)!=noErr || NewGWorld(&b,32,&bounds,NULL,NULL,0)!=noErr)pass=0;
    if(a && b && LockPixels(GetGWorldPixMap(a)) && LockPixels(GetGWorldPixMap(b))) {
        for(s=0;s<5;++s){double start=seconds();for(i=0;i<8;++i)pattern(a,b);qdtime[s]=(seconds()-start)/8;
            if(!check_pixels(b) || qdtime[s]<=0)pass=0;}
        SetPort(window);CopyBits((BitMap *)*GetGWorldPixMap(b),(BitMap *)*((CGrafPtr)window)->portPixMap,&bounds,&bounds,srcCopy,NULL);
        UnlockPixels(GetGWorldPixMap(a));UnlockPixels(GetGWorldPixMap(b));
    }else {pass=0;for(s=0;s<5;++s)qdtime[s]=0;}
    SetGWorld(old,device);if(a)DisposeGWorld(a);if(b)DisposeGWorld(b);
    int n=sprintf(text,"protocol\tantiochus-classic-1\nnonce\t%s\nsystem_version\t%04lx\nstatus\t%s\n",nonce,(unsigned long)version,pass?"pass":"fail");
    for(s=0;s<5;++s)n+=sprintf(text+n,"cpu_sample_%u\t%.9g\nquickdraw_sample_%u\t%.9g\n",s,cpu[s],s,qdtime[s]);
    n+=sprintf(text+n,"quickdraw_scope\t32-bit offscreen fill, copy and checked pixels; visible copy not read back\n");
    if(sibling(request,"ClassicResult.tmp",&temporary) && sibling(request,"ClassicResult.tsv",&result) && write_bytes(&temporary,text,n)) {
        pascal_string(resultName,"ClassicResult.tsv");FSpRename(&temporary,resultName);
    }
    done=1;
}
static pascal OSErr open_documents(const AppleEvent *event,AppleEvent *reply,long refcon) {
    AppleEvent inputEvent=*event;AEDescList list;long count=0;OSErr err;FSSpec request;AEKeyword key;DescType type;Size size;
    (void)reply;(void)refcon;
    err=AEGetParamDesc(&inputEvent,keyDirectObject,typeAEList,&list);if(err)return err;
    AECountItems(&list,&count);
    if(count==1 && AEGetNthPtr(&list,1,typeFSS,&key,&type,&request,sizeof request,&size)==noErr && size==sizeof request)execute(&request);
    AEDisposeDesc(&list);return noErr;
}
static pascal OSErr quit_application(const AppleEvent *event,AppleEvent *reply,long refcon) {
    (void)event;(void)reply;(void)refcon;done=1;return noErr;
}
int main(void) {
    Rect bounds;EventRecord event;Str255 title;AEEventHandlerUPP openUPP,quitUPP;
    InitGraf(&qd.thePort);InitFonts();InitWindows();InitMenus();TEInit();InitDialogs(NULL);InitCursor();
    SetRect(&bounds,60,60,400,250);pascal_string(title,"Antiochus Classic");
    window=NewCWindow(NULL,&bounds,title,true,documentProc,(WindowPtr)-1L,true,0);
    if(!window)return 1;
    SetPort(window);MoveTo(16,160);pascal_string(title,"Waiting for Antiochus...");DrawString(title);
    openUPP=NewAEEventHandlerUPP(open_documents);quitUPP=NewAEEventHandlerUPP(quit_application);
    AEInstallEventHandler(kCoreEventClass,kAEOpenDocuments,openUPP,0,false);
    AEInstallEventHandler(kCoreEventClass,kAEQuitApplication,quitUPP,0,false);
    while(!done)if(WaitNextEvent(everyEvent,&event,30,NULL)) {
        if(event.what==kHighLevelEvent)AEProcessAppleEvent(&event);
        else if(event.what==mouseDown){WindowPtr hit;short part=FindWindow(event.where,&hit);if(part==inGoAway && TrackGoAway(hit,event.where))done=1;}
        else if(event.what==updateEvt){BeginUpdate((WindowPtr)event.message);EndUpdate((WindowPtr)event.message);}
    }
    DisposeWindow(window);DisposeAEEventHandlerUPP(openUPP);DisposeAEEventHandlerUPP(quitUPP);return 0;
}
