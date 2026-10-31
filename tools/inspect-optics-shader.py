#!/usr/bin/env python3
"""Decode bounded OPTICS_SHADER_FAILURES records without running a guest or GPU."""
import argparse,json,pathlib,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('records',nargs='+',type=pathlib.Path);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];source=root/'src/optics'
cli=r'''#include "foundation/shader_ir.h"
#include <stdio.h>
#include <stdlib.h>
int main(void){unsigned stage,profile,flow,count;if(scanf("%u%u%u%u",&stage,&profile,&flow,&count)!=4||count>PT_R2_SHADER_WORDS)return 2;
uint32_t words[PT_R2_SHADER_WORDS];for(unsigned i=0;i<count;i++)if(scanf("%u",&words[i])!=1)return 2;
PtR2ShaderIR *ir=malloc(sizeof *ir);PtR2ShaderError e={0};if(!ir)return 2;unsigned s=pt_r2_shader_decode(stage,profile,words,count,flow,ir,&e);printf("%u %u %s\n",s,e.word,s?e.reason:"accepted");free(ir);return 0;}
'''
with tempfile.TemporaryDirectory(prefix='pt-optics-decode-') as tmp:
    c=pathlib.Path(tmp)/'decode.c';c.write_text(cli);exe=pathlib.Path(tmp)/'decode'
    subprocess.run(['clang','-std=c11','-O2','-I'+str(source),str(c),str(source/'foundation/shader_decode.c'),'-o',str(exe)],check=True)
    for file in a.records:
        if file.stat().st_size>1024*1024:raise SystemExit('record exceeds 1 MiB')
        record=json.loads(file.read_text());words=record['words']
        if record['schema']!=1 or record['stage'] not in [1,2] or record['profile'] not in [1,2] or not isinstance(words,list) or not words or len(words)>1026:raise SystemExit('invalid record')
        values=[record['stage'],record['profile'],record['flow'],len(words),*words]
        if any(not isinstance(x,int) or isinstance(x,bool) or x<0 or x>0xffffffff for x in values):raise SystemExit('invalid uint32')
        result=subprocess.run([str(exe)],input=' '.join(map(str,values)),text=True,capture_output=True,check=True).stdout.strip()
        status,word,reason=result.split(' ',2);at=int(word)
        print(json.dumps({'path':str(file),'stage':record['stage'],'status':int(status),'word':at,'reason':reason,'nearby_words':[f'{x:08x}' for x in words[max(0,at-2):at+4]]}))
