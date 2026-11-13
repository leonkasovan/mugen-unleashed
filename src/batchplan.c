/*
 * Port addition: batch boundaries and texture-slot budgeting (batchplan.h).
 * Not part of the original binary; there is no address to name.
 *
 * Kept free of GL and of the engine so the rules can be tested on the host: the
 * part that goes wrong silently here is a wrong boundary, which shows up as a
 * layer drawn in the wrong order or a sprite sampled from its neighbour's
 * texture -- neither of which a "looks fine" check catches.
 */
#include <stddef.h>

#include "batchplan.h"

int GlesBatch_KeyEqual(const GlesBatchKey *a, const GlesBatchKey *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (a->program != b->program || a->blend_enabled != b->blend_enabled ||
        a->blend_src != b->blend_src || a->blend_dst != b->blend_dst ||
        a->blend_eq != b->blend_eq || a->mask != b->mask ||
        a->has_scissor != b->has_scissor) {
        return 0;
    }
    if (a->has_scissor &&
        (a->scissor[0] != b->scissor[0] || a->scissor[1] != b->scissor[1] ||
         a->scissor[2] != b->scissor[2] || a->scissor[3] != b->scissor[3])) {
        return 0;
    }
    /* Texture identity is NOT compared: a batch may span several units. */
    return 1;
}

/*
 * The slot of keys[i] within [start, i]: the ordinal of the first draw in that
 * range with the same (tex, pal) pair, counting pairs in order of first
 * appearance. *slots receives how many distinct pairs the range used.
 *
 * Getting this wrong does not crash: two sprites would be handed the same unit
 * and one would sample the other's pixels.
 */
static int GlesBatch_SlotInRange(const GlesBatchKey *keys, int start, int i, int *slots)
{
    int total = 0;
    int found = -1;
    int j;

    for (j = start; j <= i; j++) {
        int first = 1;
        int m;

        for (m = start; m < j; m++) {
            if (keys[m].tex == keys[j].tex && keys[m].pal == keys[j].pal) {
                first = 0;
                break;
            }
        }
        if (!first) {
            continue;
        }
        if (found < 0 && keys[j].tex == keys[i].tex && keys[j].pal == keys[i].pal) {
            found = total;      /* j is the first draw with this pair */
        }
        total++;
    }
    if (slots != NULL) {
        *slots = total;
    }
    return found < 0 ? 0 : found;
}

int GlesBatch_End(const GlesBatchKey *keys, int count, int start, int max_slots)
{
    int used = 0;
    int next;

    if (keys == NULL || start < 0 || start >= count) {
        return start;
    }
    if (max_slots < 1) {
        max_slots = 1;
    }
    /* The batch always contains the call at `start`. */
    next = start + 1;
    used = 1;
    while (next < count) {
        int j;
        int is_new = 1;

        if (!GlesBatch_KeyEqual(&keys[start], &keys[next])) {
            break;
        }
        for (j = start; j < next; j++) {
            if (keys[j].tex == keys[next].tex && keys[j].pal == keys[next].pal) {
                is_new = 0;
                break;
            }
        }
        if (is_new) {
            if (used == max_slots) {
                break;          /* would need another texture unit */
            }
            used++;
        }
        next++;
    }
    return next;
}

int GlesBatch_SlotOf(const GlesBatchKey *keys, int start, int end, int i, int *slots)
{
    if (keys == NULL || i < start || i >= end) {
        if (slots != NULL) {
            *slots = 0;
        }
        return 0;
    }
    return GlesBatch_SlotInRange(keys, start, i, slots);
}
