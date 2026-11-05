#!/usr/bin/env python3
"""Require semantic tests to reject deliberate faults in private source copies."""

from pathlib import Path
import re
import os, shutil, subprocess, tempfile


def anchor(text, s):  # the one span of text that s matches, whitespace aside (the sources are clang-formatted)
    hits = re.findall(r'\s*'.join(map(re.escape, ''.join(s.split()))), text)
    return hits[0] if len(hits) == 1 else None


root = Path(__file__).resolve().parent.parent
mutations = [
    (
        'USPRG3 reads independent storage',
        'epicycle/interp970.c',
        'case 259:if(write)return -1;*v=ppc_spr64(c,275);',
        'case 259:if(write)return -1;*v=ppc_spr64(c,259);',
    ),
    (
        'duplicate SLB silently selects a mapping',
        'epicycle/mmu970.c',
        'if(segment.valid){c->halted=1;c->exit_code=70;return 0x40000000u;}',
        'if(segment.valid)break;',
    ),
    ('SLBMTE skips 970 operand normalization', 'epicycle/interp970.c', 'R(b)&~UINT64_C(0x07ffffc0)', 'R(b)'),
    (
        'HPT read bypasses GPU ownership',
        'experimental/g5/src/g5_mmu.c',
        'm->prepare_pte(m->prepare_opaque,pa,16,false)',
        'PT_G5_OK',
    ),
    (
        'HPT R/C write bypasses GPU ownership',
        'experimental/g5/src/g5_mmu.c',
        'm->prepare_pte(m->prepare_opaque,pa+8,8,true)',
        'PT_G5_OK',
    ),
    (
        'agent bypasses GPU ownership',
        'src/agent_guest.c',
        'c->is970&&ppc970_agent_prepare(c,ra,k,write)',
        '0&&ppc970_agent_prepare(c,ra,k,write)',
    ),
    (
        'agent leaves a CPU reservation live',
        'epicycle/mmu970.c',
        'return write?ppc970_owner_write(b,pa,n):0;',
        'return 0;',
    ),
    (
        'DSEG keeps stale DSI reason',
        'epicycle/mmu970.c',
        'c->spr[SPR_DSISR]=why==PT970_SEGMENT?0:why;',
        'if(why!=PT970_SEGMENT)c->spr[SPR_DSISR]=why;',
    ),
    ('high GPR truncation', 'epicycle/ppc.h', 'return (uint64_t)c->gpr_hi[r]<<32|c->gpr[r];', 'return c->gpr[r];'),
    ('SRR0 truncation', 'epicycle/interp970.c', 'ppc_ea64(c,pc)&~UINT64_C(3)', '(uint32_t)pc&~UINT64_C(3)'),
    ('PR does not force translation', 'epicycle/mmu970.c', '(c->msr&MSR_PR)||(c->msr&', '(c->msr&'),
    (
        'wrong change-bit update',
        'experimental/g5/src/g5_mmu.c',
        'updated=(updated|0x90)&~UINT64_C(0x40)',
        'updated=updated|0x80',
    ),
    (
        'legacy caller silently narrows physical address',
        'epicycle/mmu.c',
        'if(physical>>32)return 0x40000000u|(acc==1?0x02000000u:0);',
        '',
    ),
    (
        'wrong reservation granule',
        'experimental/g5/src/g5.h',
        '#define PT_G5_RESERVATION 128',
        '#define PT_G5_RESERVATION 32',
    ),
    (
        'fcfid ignores rounding mode',
        'epicycle/interp.c',
        'int64_t integer=(int64_t)bb;fp_begin(c->fpscr&3);',
        'int64_t integer=(int64_t)bb;fp_begin(0);',
    ),
    (
        'load updates base before its page fault',
        'epicycle/interp970.c',
        'if(op==46||op==47){',
        'if(update)SET(a,address);\n        if(op==46||op==47){',
    ),
    (
        'dcbz trap uses the wrong exception',
        'epicycle/interp970.c',
        'if(!count)return illegal(c);',
        'if(!count){ppc970_alignment(c,RA(a)+R(b),1);return 0;}',
    ),
    ('alignment loses the store bit', 'epicycle/mmu970.c', '|(write?0x02000000u:0);', '|0;'),
    (
        'TLB barrier completes during an older access',
        'epicycle/interp970.c',
        'ppc970_bus_enter(c->bus970,exclusive);',
        'ppc970_bus_enter(c->bus970,0);',
    ),
    (
        'SCOM read/write reversed',
        'epicycle/interp970.c',
        'if(*v&0x8000)ppc_set_spr64(c,277,c->biu_mode970);',
        'if(!(*v&0x8000))ppc_set_spr64(c,277,c->biu_mode970);',
    ),
    ('SCOM unknown address succeeds', 'epicycle/interp970.c', 'ppc_set_spr64(c,276,0x21);', 'ppc_set_spr64(c,276,0);'),
    ('SCOM unsupported mode silently succeeds', 'epicycle/interp970.c', 'if(mode&~UINT64_C(0x8004))return -1;', ''),
    ('RAM data bypasses GPU ownership', 'epicycle/mmu970.c', 'if(ram){', 'if(ram){c->env_ram970=NULL;'),
    (
        'instruction fetch bypasses GPU ownership',
        'epicycle/mmu970.c',
        'if(translate(c,pc,2,&ra,NULL))return 1;uint8_t p[4];',
        'if(translate(c,pc,2,&ra,NULL))return 1;uint8_t p[4];c->env_ram970=NULL;',
    ),
    (
        'reservation checks GPU ownership twice',
        'epicycle/mmu970.c',
        'bool ok=false;PtG5Result r=write?',
        'if(c->env_ram970)c->env_ram970(c,ra,n,write);bool ok=false;PtG5Result r=write?',
    ),
    (
        'HSPRG truncates the upper half',
        'epicycle/interp970.c',
        'case 304:case 305:break;',
        'case 304:case 305:if(write)*v=(uint32_t)*v;break;',
    ),
]
for name, file, old, new in mutations:
    with tempfile.TemporaryDirectory(prefix='pt970-negative-') as tmp:
        dst = Path(tmp)
        for d in ('epicycle', 'experimental/g5/src', 'include'):
            shutil.copytree(root / d, dst / d, ignore=shutil.ignore_patterns('__pycache__', 'build*'))
        for f in (
            'src/agent_guest.c',
            'src/optics/diagnostics.h',
            'tests/g5-core.c',
            'tests/g5-bridge.c',
            'src/g5_bridge.c',
            'src/platform.c',
            'tools/check-g5-core.sh',
            'tools/check-pci-firmware.py',
            'firmware/openbios/arch/ppc/qemu/init.c',
        ):
            (dst / f).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(root / f, dst / f)
        p = dst / file
        s = p.read_text()
        a = anchor(s, old)
        assert a, (name, 'mutation anchor')
        p.write_text(s.replace(a, new))
        env = os.environ.copy()
        env.pop('PT_970_TEST_DIR', None)
        env.pop('PT_970_SANITIZER', None)
        r = subprocess.run(['sh', 'tools/check-g5-core.sh'], cwd=dst, env=env, text=True, capture_output=True)
        # A compiler failure or sanitizer fault is not a successful semantic control.
        assert (dst / 'build/g5-core/core').exists(), (name, r.stderr)
        assert r.returncode and 'Assertion failed:' in r.stderr, (name, r.returncode, r.stdout, r.stderr)
        print('PASS negative:', name, flush=True)
print(f'PASS: {len(mutations)} semantic mutations rejected; source tree unchanged')
