#!/usr/bin/env python3
"""Small mutations must be rejected by existing, unchanged contract tests."""

import re
import json
import pathlib
import resource
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def anchor(text, s):  # the one span of text that s matches, whitespace aside (the sources are clang-formatted)
    hits = re.findall(r'\s*'.join(map(re.escape, ''.join(s.split()))), text)
    return hits[0] if len(hits) == 1 else None


resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
SOURCES = ['g5_memory.c', 'g5_mmu.c', 'g5_integer.c', 'g5_fabric.c', 'g5_of.c']
MUTATIONS = [
    ('reservation_32_instead_of_128', 'g5.h', '#define PT_G5_RESERVATION 128', '#define PT_G5_RESERVATION 32'),
    (
        'truncate_physical_address',
        'g5_memory.c',
        'static uint8_t *resolve(PtG5Memory *m, uint64_t pa, size_t n, bool store) {',
        'static uint8_t *resolve(PtG5Memory *m, uint64_t pa, size_t n, bool store) { pa=(uint32_t)pa;',
    ),
    ('no_pte_compare', 'g5_memory.c', 'if(get_be(p,8)==old[0] && get_be(p+8,8)==old[1]) {', 'if((void)old, true) {'),
    (
        'early_shootdown_ack',
        'g5_fabric.c',
        'f->pending[c].flags|=PT_G5_JOB_TLB;',
        'f->ack[c]=ticket; f->pending[c].flags|=PT_G5_JOB_TLB;',
    ),
    ('truncate_gpr_sf0', 'g5_integer.c', 'c->gpr[dst]=r;', 'c->gpr[dst]=wide?r:(uint32_t)r;'),
    ('native_write_bypasses_pte_c', 'g5_mmu.c', 'writable && (updated&0x80)', 'writable'),
]
results = []
CPU_SOURCES = ['g5_cpu.c', 'g5_integer.c', 'g5_mmu.c', 'g5_memory.c', 'g5_dec.c']
CASES = [(*m, 'contracts.c', SOURCES) for m in MUTATIONS] + [
    (
        'cpu_truncates_high_ea',
        'g5_cpu.c',
        'return c->r.msr&PT_G5_MSR_SF?address:(uint32_t)address;',
        'return (void)c,(uint32_t)address;',
        'cpu-runtime.c',
        CPU_SOURCES,
    ),
    (
        'rfid_truncates_srr0',
        'g5_cpu.c',
        'c->r.pc=ea(c,c->srr0)&~UINT64_C(3);',
        'c->r.pc=(uint32_t)c->srr0&~UINT64_C(3);',
        'cpu-runtime.c',
        CPU_SOURCES,
    ),
    (
        'tlbsync_ignores_pending_ticket',
        'g5_cpu.c',
        'if(c->tlb_ticket){if(!b->synced)',
        'if(false){if(!b->synced)',
        'cpu-runtime.c',
        CPU_SOURCES,
    ),
    (
        'load_updates_base_before_fault',
        'g5_cpu.c',
        'uint8_t bytes[8];if(write)put(bytes,n,c->r.gpr[t]);',
        'if(update)c->r.gpr[a]=address;uint8_t bytes[8];if(write)put(bytes,n,c->r.gpr[t]);',
        'cpu-runtime.c',
        CPU_SOURCES,
    ),
    (
        'external_exception_clears_me',
        'g5_cpu.c',
        '(vector==0x200?0:c->r.msr&PT_G5_MSR_ME)',
        '(vector==0x200?0:0)',
        'cpu-runtime.c',
        CPU_SOURCES,
    ),
    (
        'stop_ack_before_coordinator',
        'g5_fabric.c',
        'bool busy=(f->online&(1u<<c))||f->pending[c].flags||f->active[c].flags;',
        'bool busy=false;',
        'contracts.c',
        SOURCES,
    ),
    (
        'stale_work_after_stop',
        'g5_fabric.c',
        '/* Work published while STOP was borrowed cannot survive a new CPU reset. */\n            memset(&f->pending[c],0,sizeof f->pending[c]);',
        '/* Deliberate mutation: retain obsolete work after reset. */',
        'contracts.c',
        SOURCES,
    ),
    (
        'masked_timer_still_pends',
        'g5_pic.c',
        'if(!(s->vpr&MASK))s->pending|=s->dest;',
        's->pending|=s->dest;',
        'pic.c',
        ['g5_pic.c'],
    ),
    ('dec_only_first_negative_tick', 'g5_dec.c', 'ee && (value>>31)', 'ee && value==UINT32_MAX', 'dec.c', ['g5_dec.c']),
    (
        'dart_ignores_write_protection',
        'g5_dart.c',
        'write?0x20000000u:0x40000000u',
        'write?0u:0x40000000u',
        'dart.c',
        ['g5_memory.c', 'g5_dart.c'],
    ),
    (
        'owner_ack_before_apply',
        'g5_owners.c',
        '            r=t->ops.apply(context,&job);\n            pthread_mutex_lock(&t->gate);\n            if(r!=PT_G5_OK){fail_locked(t,r);break;}\n            if(job.flags&PT_G5_JOB_START)running=true;\n            if(job.flags&PT_G5_JOB_STOP)running=false;\n            r=pt_g5_complete(&t->fabric,o->cpu,&job);',
        '            (void)pt_g5_complete(&t->fabric,o->cpu,&job);\n            r=t->ops.apply(context,&job);\n            pthread_mutex_lock(&t->gate);\n            if(r!=PT_G5_OK){fail_locked(t,r);break;}\n            if(job.flags&PT_G5_JOB_START)running=true;\n            if(job.flags&PT_G5_JOB_STOP)running=false;\n            r=PT_G5_OK;',
        'owners.c',
        ['g5_fabric.c', 'g5_owners.c'],
    ),
]
with tempfile.TemporaryDirectory(prefix='pt-g5-negative-') as tmp:
    folder = pathlib.Path(tmp)
    for name, filename, before, after, test, sources in CASES:
        src = folder / name
        shutil.copytree(ROOT / 'src', src)
        p = src / filename
        text = p.read_text()
        span = anchor(text, before)
        assert span, (name, 'anchor changed')
        p.write_text(text.replace(span, after))
        exe = src / 'test'
        subprocess.run(
            [
                'clang',
                '-std=c11',
                '-O1',
                '-Wall',
                '-Wextra',
                '-Werror',
                '-I',
                str(src),
                *[str(src / s) for s in sources],
                str(ROOT / 'tests' / test),
                '-pthread',
                '-o',
                str(exe),
            ],
            check=True,
            capture_output=True,
        )
        run = subprocess.run([str(exe)], capture_output=True, timeout=30)
        # Compilation failure, a timeout, or a signal unrelated to assert is not a successful control.
        assert run.returncode == -6 and b'Assertion failed' in run.stderr, (name, run.returncode, run.stderr)
        results.append({'mutation': name, 'rejected_by_unchanged_tests': True})
print(json.dumps(results, indent=2))
