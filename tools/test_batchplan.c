/*
 * Gate for the sprite batch planner (src/batchplan.c).
 *
 * The planner decides draw-call boundaries and texture-unit slots. Both are
 * silent when wrong: a bad boundary reorders layers, and a bad slot samples a
 * sprite from its neighbour's texture. Neither shows up as a crash, so the rules
 * are pinned here on the host instead.
 *
 * What is asserted:
 *   - draws with equal keys butt together into one batch;
 *   - a texture is NOT a batch break: alternating two textures stays one batch;
 *   - a run needing more than max_slots distinct pairs is split exactly at the
 *     call that would need the extra unit;
 *   - a repeated pair does not consume a second slot;
 *   - each key field individually ends a batch (program, blend src/dst/eq,
 *     enabled flag, mask, scissor presence and rect) -- the values are changed
 *     one at a time from a matching baseline;
 *   - slots are numbered in first-appearance order and reused for repeats;
 *   - degenerate input (count 0, start past the end, max_slots < 1) is safe.
 *
 * Usage: sh tools/test_batchplan.sh
 */
#include <stdio.h>
#include <string.h>

#include "batchplan.h"

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

/* A baseline key; callers vary one field to prove it breaks a batch. */
static GlesBatchKey base_key(void)
{
    GlesBatchKey k;

    memset(&k, 0, sizeof(k));
    k.program = 7;
    k.blend_enabled = 1;
    k.blend_src = 0x0302;       /* GL_SRC_ALPHA */
    k.blend_dst = 0x0303;       /* GL_ONE_MINUS_SRC_ALPHA */
    k.blend_eq = 0x8006;        /* GL_FUNC_ADD */
    k.mask = 1;
    k.has_scissor = 1;
    k.scissor[0] = 0; k.scissor[1] = 0; k.scissor[2] = 640; k.scissor[3] = 480;
    k.tex = 100;
    k.pal = 200;
    return k;
}

static void test_key_equality(void)
{
    GlesBatchKey a = base_key();
    GlesBatchKey b = base_key();

    check(GlesBatch_KeyEqual(&a, &b), "identical keys are equal");

    b.tex = 101;
    check(GlesBatch_KeyEqual(&a, &b), "a different sprite texture does not break a batch");
    b = base_key();
    b.pal = 201;
    check(GlesBatch_KeyEqual(&a, &b), "a different palette does not break a batch");

    b = base_key(); b.program = 8;
    check(!GlesBatch_KeyEqual(&a, &b), "a different program breaks a batch");
    b = base_key(); b.blend_enabled = 0;
    check(!GlesBatch_KeyEqual(&a, &b), "blend on/off breaks a batch");
    b = base_key(); b.blend_src = 1;
    check(!GlesBatch_KeyEqual(&a, &b), "a different blend source breaks a batch");
    b = base_key(); b.blend_dst = 1;
    check(!GlesBatch_KeyEqual(&a, &b), "a different blend destination breaks a batch");
    b = base_key(); b.blend_eq = 1;
    check(!GlesBatch_KeyEqual(&a, &b), "a different blend equation breaks a batch");
    b = base_key(); b.mask = 0;
    check(!GlesBatch_KeyEqual(&a, &b), "a different mask breaks a batch");
    b = base_key(); b.has_scissor = 0;
    check(!GlesBatch_KeyEqual(&a, &b), "scissor presence breaks a batch");
    b = base_key(); b.scissor[2] = 639;
    check(!GlesBatch_KeyEqual(&a, &b), "a different scissor rectangle breaks a batch");

    check(!GlesBatch_KeyEqual(NULL, &a), "a NULL key is never equal");
}

static void test_boundaries(void)
{
    GlesBatchKey keys[16];
    int i;
    int end;

    /* One texture, matching keys: everything is one batch. */
    for (i = 0; i < 16; i++) {
        keys[i] = base_key();
    }
    check(GlesBatch_End(keys, 16, 0, 7) == 16, "matching keys and one texture form one batch");

    /* Alternating two textures: still one batch, slots reused. */
    for (i = 0; i < 16; i++) {
        keys[i] = base_key();
        keys[i].tex = (unsigned int)(100 + (i % 2));
    }
    check(GlesBatch_End(keys, 16, 0, 7) == 16, "alternating two textures stays one batch");

    /* Eight distinct textures with 7 slots: split at the eighth call. */
    for (i = 0; i < 16; i++) {
        keys[i] = base_key();
        keys[i].tex = (unsigned int)(100 + i);
    }
    end = GlesBatch_End(keys, 16, 0, 7);
    check(end == 7, "a run needing an eighth slot is split after seven");
    /* And the next batch starts where the first ended. */
    check(GlesBatch_End(keys, 16, 7, 7) == 14, "the following batch also takes seven");

    /* max_slots 1 degenerates to "one texture per batch". */
    check(GlesBatch_End(keys, 16, 0, 1) == 1, "one slot allows one call per batch");

    /* A key change ends the batch immediately. */
    for (i = 0; i < 16; i++) {
        keys[i] = base_key();
    }
    keys[3].blend_src = 1;
    check(GlesBatch_End(keys, 16, 0, 7) == 3, "a key change ends the batch before it");
}

static void test_slots(void)
{
    GlesBatchKey keys[8];
    int i;
    int slots = -1;

    /* A, B, A, C -> slots 0, 1, 0, 2 */
    for (i = 0; i < 4; i++) {
        keys[i] = base_key();
    }
    keys[0].tex = 10;
    keys[1].tex = 11;
    keys[2].tex = 10;
    keys[3].tex = 12;

    check(GlesBatch_SlotOf(keys, 0, 4, 0, &slots) == 0 && slots == 1, "first appearance is slot 0");
    check(GlesBatch_SlotOf(keys, 0, 4, 1, &slots) == 1 && slots == 2, "a new texture is slot 1");
    check(GlesBatch_SlotOf(keys, 0, 4, 2, &slots) == 0, "a repeated texture reuses slot 0");
    check(GlesBatch_SlotOf(keys, 0, 4, 3, &slots) == 2 && slots == 3, "the third distinct texture is slot 2");

    /* The palette half of the pair also identifies a slot. */
    keys[1].pal = 999;
    check(GlesBatch_SlotOf(keys, 0, 4, 1, &slots) == 1, "a different palette is a different slot");
    /* (10,200) (11,999) (10,200) (12,200): index 2 is the same pair as index 0,
     * so it keeps slot 0 even though a palette variant appeared in between. */
    check(GlesBatch_SlotOf(keys, 0, 4, 2, &slots) == 0 && slots == 2,
          "the earlier (texture, palette) pair keeps its own slot");
}

static void test_degenerate(void)
{
    GlesBatchKey keys[4];
    int i;
    int slots = -1;

    for (i = 0; i < 4; i++) {
        keys[i] = base_key();
    }
    check(GlesBatch_End(keys, 0, 0, 7) == 0, "an empty list ends immediately");
    check(GlesBatch_End(keys, 4, 4, 7) == 4, "a start past the end is returned unchanged");
    check(GlesBatch_End(keys, 4, -1, 7) == -1, "a negative start is returned unchanged");
    check(GlesBatch_End(NULL, 4, 0, 7) == 0, "a NULL list is safe");
    for (i = 0; i < 4; i++) {
        keys[i].tex = (unsigned int)(100 + i);   /* distinct, so the slot cap bites */
    }
    check(GlesBatch_End(keys, 4, 0, 0) == 1, "max_slots 0 is treated as 1 (never an empty batch)");
    check(GlesBatch_SlotOf(keys, 0, 4, 9, &slots) == 0 && slots == 0, "an index past the end is safe");
}

int main(void)
{
    test_key_equality();
    test_boundaries();
    test_slots();
    test_degenerate();

    if (failures != 0) {
        printf("test_batchplan: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_batchplan: OK\n");
    return 0;
}
