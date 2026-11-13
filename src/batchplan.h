#ifndef MUGEN_BATCHPLAN_H
#define MUGEN_BATCHPLAN_H

/*
 * Port addition: batch boundaries and texture-slot budgeting for the GLES
 * sprite path, apart from the renderer so they can be tested on the host.
 *
 * This is the decision half of Ikemen-GO's Phase 5 batching (render_gles32.go,
 * sameBatchKey and the slot loop in flushSpriteBatches):
 *
 *  - two consecutive draws may share one instanced draw call when their batch
 *    keys match: same program, same blend factors, same mask, same scissor.
 *    Texture identity is deliberately NOT part of the key;
 *  - a batch may span at most `max_slots` distinct (sprite, palette) pairs,
 *    because each is bound to its own texture unit (Ikemen uses 7 sprite + 7
 *    palette units, Mali-G31's limit);
 *  - a run that would need an extra slot is split there;
 *  - only consecutive draws are ever grouped, so painter's order is preserved.
 *
 * The renderer keeps the GL part; src/gles_batchplan.c is pure arithmetic over
 * the recorded keys, and tools/test_batchplan.sh checks the rules below.
 */

typedef struct GlesBatchKey {
    int program;                /* indexed or rgba program */
    unsigned int blend_enabled;
    unsigned int blend_src, blend_dst, blend_eq;
    int mask;
    int has_scissor;
    int scissor[4];
    /* Which textures this draw samples. Identifies a slot, not a batch break. */
    unsigned int tex;
    unsigned int pal;
} GlesBatchKey;

/* Do these two draws allow sharing a draw call? (Texture excluded on purpose.) */
int GlesBatch_KeyEqual(const GlesBatchKey *a, const GlesBatchKey *b);

/*
 * The end (exclusive) of the batch that starts at `start` in keys[0..count):
 * the longest run whose keys match `keys[start]` and which uses at most
 * `max_slots` distinct (tex, pal) pairs. A run is never empty, so the result is
 * always > start. max_slots < 1 is treated as 1.
 */
int GlesBatch_End(const GlesBatchKey *keys, int count, int start, int max_slots);

/* The slot index of keys[i] within the batch [start, end), assigned in first
 * appearance order. *slots receives the slot count when it is not NULL. */
int GlesBatch_SlotOf(const GlesBatchKey *keys, int start, int end, int i, int *slots);

#endif /* MUGEN_BATCHPLAN_H */
