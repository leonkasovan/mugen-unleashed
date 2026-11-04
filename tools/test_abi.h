/*
 * Pointer-width assertion shared by the render/qualification harnesses.
 *
 * RENDERING_PLAN.md 13.2: a `win64` qualification run must build and run a
 * 64-bit binary, not merely relabel a 32-bit one. Each harness prints its own
 * width and its .sh wrapper compares that line with the target's expected
 * width, so a harness that silently kept -m32 fails instead of producing
 * evidence for the wrong ABI.
 *
 * Include after <stdio.h> and call TEST_REPORT_ABI() first thing in main().
 */
#ifndef MUGEN_TEST_ABI_H
#define MUGEN_TEST_ABI_H

#include <stdio.h>

/* Human-readable pointer width: "abi: 32-bit build" / "abi: 64-bit build". */
static void Test_ReportAbi(void)
{
    printf("abi: %u-bit build\n", (unsigned)(sizeof(void *) * 8));
}

#define TEST_REPORT_ABI() Test_ReportAbi()

#endif /* MUGEN_TEST_ABI_H */
