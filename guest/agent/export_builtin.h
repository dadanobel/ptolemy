/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_EXPORT_BUILTIN_H
#define PT_EXPORT_BUILTIN_H
/* The only source is an explicit host UI request. Data never enters the text
 * command log; all pages are validated by the host adapter before enqueue. */
static int pt_export_builtin(uint32_t token,uint32_t job,char **argv,unsigned *status){
    if(strcmp(argv[0],"/Ptolemy/files"))return 0;
    *status=2;if(!argv[1]||argv[2]||argv[1][0]!='/')return 1;
    if(stopping){*status=125;return 1;}
    int fd=open(argv[1],O_RDONLY|O_NOFOLLOW);struct stat before,after;uint32_t reply[3];
    if(fd<0)return 1;if(fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_size<0){close(fd);return 1;}
    uint64_t size=(uint64_t)before.st_size,off=0;int rc=ptagent_call(PTA_EXPORT_BEGIN,token,job,(uint32_t)(size>>32),(uint32_t)size,reply);
    PtSha256 sha;pt_sha256_init(&sha);static uint8_t bytes[PTA_CHUNK];
    while(!rc&&off<size){input_tick(token,ms());if(stopping){rc=PTA_CANCELLED;break;}unsigned n=size-off>sizeof bytes-8?sizeof bytes-8:(unsigned)(size-off);unsigned got=0;
        while(got<n){ssize_t k=read(fd,bytes+8+got,n-got);if(k<0&&errno==EINTR)continue;if(k<=0){rc=PTA_INVALID;break;}got+=(unsigned)k;}
        if(rc)break;for(unsigned i=0;i<8;i++)bytes[i]=(uint8_t)(off>>(56-i*8));
        do{if(stopping){rc=PTA_CANCELLED;break;}rc=ptagent_call(PTA_EXPORT_CHUNK,token,job,(uint32_t)(uintptr_t)bytes,n+8,reply);
            /* Pages filled by read() may have no user PTE yet (a 16 MiB export failed at once, 1 byte passed):
             * touch every page from user mode and retry once, as the XCHUNK path does. */
            if(rc==PTA_FAULT){for(unsigned i=0;i<n+8;i+=4096)((volatile uint8_t *)bytes)[i]=bytes[i];((volatile uint8_t *)bytes)[n+7]=bytes[n+7];
                rc=ptagent_call(PTA_EXPORT_CHUNK,token,job,(uint32_t)(uintptr_t)bytes,n+8,reply);}
            if(rc==PTA_AGAIN){usleep(1000);uint32_t check[3];if(ptagent_call(PTA_CHECK,token,job,0,0,check)||check[0])rc=PTA_CANCELLED;}
        }while(rc==PTA_AGAIN);
        if(!rc){pt_sha256_update(&sha,bytes+8,n);off+=n;}
    }
    if(fstat(fd,&after)||after.st_size!=before.st_size||after.st_mtime!=before.st_mtime||after.st_mtimespec.tv_nsec!=before.st_mtimespec.tv_nsec||after.st_ctime!=before.st_ctime)rc=PTA_INVALID;close(fd);
    uint8_t hash[32];pt_sha256_final(&sha,hash);
    while(!rc){if(stopping){rc=PTA_CANCELLED;break;}rc=ptagent_call(PTA_EXPORT_COMMIT,token,job,(uint32_t)(uintptr_t)hash,0,reply);
        if(rc!=PTA_AGAIN)break;usleep(1000);uint32_t check[3];if(ptagent_call(PTA_CHECK,token,job,0,0,check)||check[0]){rc=PTA_CANCELLED;break;}rc=0;
    }
    *status=rc?10+rc:0;return 1;   /* 10+PTA_* names the failing step for the host */
}
#endif
