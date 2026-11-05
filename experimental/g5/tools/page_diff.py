#!/usr/bin/env python3
"""Read-only page identity report. Emits hashes/offsets, never file contents."""

import argparse, hashlib, json, pathlib


def compare(reference, candidate, page_size=4096, details=32):
    if page_size < 4 or page_size & (page_size - 1) or details < 0:
        raise ValueError('invalid page size/detail limit')
    for path in [reference, candidate]:
        if not path.is_file() or path.stat().st_size > 1 << 30:
            raise ValueError('regular files up to 1 GiB only')
    index = {}
    refhash = hashlib.sha256()
    candhash = hashlib.sha256()
    total = bad = identity = swaps = zeros = 0
    shown = []
    with reference.open('rb') as f:
        while data := f.read(page_size):
            refhash.update(data)
            h = hashlib.sha256(data).digest()
            entry = index.setdefault(h, [0, []])
            entry[0] += 1
            if len(entry[1]) < 8:
                entry[1].append(total * page_size)
            total += 1
    pages = 0
    with reference.open('rb') as ref, candidate.open('rb') as cand:
        while True:
            a = ref.read(page_size)
            b = cand.read(page_size)
            if not a and not b:
                break
            candhash.update(b)
            offset = pages * page_size
            pages += 1
            if a == b:
                continue
            bad += 1
            h = hashlib.sha256(b).digest()
            matches = index.get(h, [0, []]) if b else [0, []]
            other = matches[0] > 0 and (matches[0] > 1 or offset not in matches[1])
            identity += bool(other)
            swap = (
                bool(a)
                and len(a) == len(b)
                and len(a) % 4 == 0
                and b == b''.join(a[i : i + 4][::-1] for i in range(0, len(a), 4))
            )
            swaps += swap
            zero = bool(b) and not any(b)
            zeros += zero
            if len(shown) < details:
                shown.append(
                    {
                        'offset': offset,
                        'reference_bytes': len(a),
                        'candidate_bytes': len(b),
                        'candidate_sha256': h.hex(),
                        'matching_reference_pages': matches[0],
                        'reference_offsets_first8': matches[1],
                        'word32_byte_swap': swap,
                        'all_zero': zero,
                    }
                )
    return {
        'page_size': page_size,
        'reference_sha256': refhash.hexdigest(),
        'candidate_sha256': candhash.hexdigest(),
        'reference_pages': total,
        'compared_pages': pages,
        'different_pages': bad,
        'different_pages_matching_reference_elsewhere': identity,
        'word32_swapped_pages': swaps,
        'zero_pages': zeros,
        'details': shown,
        'limit': 'Page identities narrow a hypothesis; they do not prove which MMU/cache/CPU path caused corruption.',
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference', type=pathlib.Path)
    p.add_argument('candidate', type=pathlib.Path)
    p.add_argument('--page-size', type=int, default=4096)
    p.add_argument('--details', type=int, default=32)
    a = p.parse_args()
    print(json.dumps(compare(a.reference, a.candidate, a.page_size, a.details), indent=2))


if __name__ == '__main__':
    main()
