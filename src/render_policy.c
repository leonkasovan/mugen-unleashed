/*
 * Renderer selection policy -- see render_policy.h for the contract.
 */
#include <stddef.h>

#include "render_policy.h"

void RenderPolicy_Resolve(int requested, const RenderCaps *caps, RenderDecision *out)
{
    RenderCaps have;

    if (out == NULL) {
        return;
    }
    if (caps != NULL) {
        have = *caps;
    } else {
        /* No capability information at all: the only safe answer is the
         * software renderer. */
        have.system = 1;
        have.gl_screen = 0;
        have.gl_sprite = 0;
    }

    out->effective = RENDER_POLICY_SYSTEM;
    out->fell_back = 0;
    out->reason = NULL;

    switch (requested) {
    case RENDER_POLICY_SYSTEM:
        /* Nothing to check: System is the recovery path everywhere. */
        return;

    case RENDER_POLICY_OPENGL_SCREEN:
        if (have.gl_screen) {
            out->effective = RENDER_POLICY_OPENGL_SCREEN;
            return;
        }
        out->fell_back = 1;
        out->reason = "no GL presenter for the screen renderer";
        return;

    case RENDER_POLICY_OPENGL:
        /* The GPU sprite renderer needs both halves: the sprite programs and
         * the ability to present. Selecting it with only one of them wired
         * would draw through NULL/stub table entries (plan section 12.2). */
        if (have.gl_sprite && have.gl_screen) {
            out->effective = RENDER_POLICY_OPENGL;
            return;
        }
        out->fell_back = 1;
        out->reason = have.gl_sprite ? "no GL presenter for the GPU renderer"
                                     : "GPU sprite renderer not available";
        return;

    default:
        out->fell_back = 1;
        out->reason = "unknown render mode";
        return;
    }
}
