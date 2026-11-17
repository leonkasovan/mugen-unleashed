#ifndef MUGEN_ZSS_PROGRAM_H
#define MUGEN_ZSS_PROGRAM_H
#include "zss_controller.h"
typedef struct ZssProgram ZssProgram;
/* Prototype profile: states, enabled controllers, nested if/else, assignments,
 * while, anonymous/named for ranges, single-name numeric let declarations,
 * locals in the audited controller parameters, short-circuit &&/||, switch,
 * functions/calls and single/multiple call-result bindings. Multiple ordinary
 * let values, locals in StateDef header properties and block attributes remain
 * unsupported. Explicit st/stN/stcommon stream installation is experimental. Compilation
 * owns all output; source can be released immediately. *out is unchanged on
 * failure. Limits/file: 64 states, 64 functions, 1024 nodes, 32 nested blocks,
 * 256 local slots per state/function (including args/results), 63-character
 * function names, 256 call arguments/results, 1 MiB source.
 * Switch guards: <128 combined lexical tokens and 2 MiB expansion per program.
 * Runtime guards: 2500 iterations per loop, 32 active function calls, 256
 * execution frames and 100000 shared control steps per state-body invocation.
 * Function local frames use bounded per-call heap storage; execution uses no C
 * recursion. A state without calls retains its allocation-free local frame. */
int Zss_CompileProgram(const char *source, size_t size, CharData *owner,
                       ZssProgram **out, ZssDiagnostic *diagnostic);
/* Release one owned reference; installed state bindings retain their own. */
void Zss_FreeProgram(ZssProgram *program);
size_t Zss_ProgramStateCount(const ZssProgram *program);
const StateDef *Zss_ProgramState(const ZssProgram *program, size_t index, int *number);
/* Callback dispatch boundary: the engine adapter must call StateCtrl_RunCtx, with
 * its StateRunCtx/AI/hitpause bookkeeping. This isolated executor does not apply
 * state entry properties or install itself in Player_RunState. Callback returns
 * original STATECTRL_* bits; it must not free or alter the program. The frame
 * is borrowed only during the callback; do not retain it. */
typedef int (*ZssControllerRun)(Player *, StateCtrl *, ExprEvalCtx *frame, void *context);
int Zss_RunBody(ZssProgram *program, size_t state, Player *player,
                ZssControllerRun run, void *context, int *result);
/* Standalone RunBody resolves only this program's functions. Installed bindings
 * resolve the owning collection's modules, including later function-only files. */
#endif
