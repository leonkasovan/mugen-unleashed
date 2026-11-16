#ifndef MUGEN_ZSS_STATEDEF_H
#define MUGEN_ZSS_STATEDEF_H
#include "zss_controller.h"
/* Port addition: compile exactly one [StateDef n; key: value;] header.
 * out/number are untouched on failure. out must have empty expression fields.
 * Success owns properties only: free with StateDef_FreeProperties. No controller
 * list is created, and this object must not yet be installed in StateDefs. */
int Zss_CompileStateHeader(const char *source, size_t size, CharData *owner,
                           int *number, StateDef *out, ZssDiagnostic *diagnostic);
#endif
