#!/usr/bin/env python3
"""Replay a MUGEN_SND_TRACE request/lifetime trace against PCM budgets.

Replay predicts cache residency for the recorded request sequence, not changed
frame timing, codec lock duration, source storage, or decoder scratch memory.
"""
import argparse
import collections
import csv
import json


def simulate(events, megabytes):
    budget = megabytes * 1024 * 1024
    resident = collections.OrderedDict()
    slots = {}
    hits = misses = evictions = peak = excess = 0
    latencies = []
    for row in events:
        kind = row[0]
        if kind == 'request':
            if len(row) != 9:
                raise ValueError('Trace uses an obsolete request format; record a new trace')
            key = (int(row[3]), int(row[4]))
            size = int(row[7])
            if key in resident:
                hits += 1
                resident.move_to_end(key)
            else:
                misses += 1
                resident[key] = size
            latencies.append(float(row[8]))
        elif kind == 'prepare':
            slots[int(row[3])] = (int(row[2]), int(row[4]))
        elif kind == 'release':
            slot = int(row[3])
            if slots.get(slot) == (int(row[2]), int(row[4])):
                slots.pop(slot)
        elif kind == 'free':
            bank = int(row[2])
            resident = collections.OrderedDict((k, v) for k, v in resident.items() if k[0] != bank)
            slots = {k: v for k, v in slots.items() if v[0] != bank}
        elif kind == 'invalidate':
            resident.clear()
            slots.clear()
        elif kind == 'tick':
            pinned = set(slots.values())
            for key in list(resident):
                if sum(resident.values()) <= budget:
                    break
                if key not in pinned:
                    resident.pop(key)
                    evictions += 1
            excess = max(excess, max(0, sum(resident.values()) - budget))
        peak = max(peak, sum(resident.values()))
    return dict(budget_MiB=megabytes, hits=hits, misses=misses, evictions=evictions,
                final_pcm_MiB=sum(resident.values()) / 2**20,
                peak_pcm_MiB=peak / 2**20, max_post_trim_excess_MiB=excess / 2**20,
                measured_max_request_decode_ms=max(latencies, default=0))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace')
    parser.add_argument('--budgets', nargs='+', type=int, default=[8, 16, 32])
    args = parser.parse_args()
    if any(n < 0 for n in args.budgets):
        parser.error('budgets must be nonnegative')
    with open(args.trace, newline='') as f:
        events = list(csv.reader(f))
    print(json.dumps([simulate(events, n) for n in args.budgets], indent=2))
