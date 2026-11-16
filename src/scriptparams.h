#ifndef MUGEN_SCRIPTPARAMS_H
#define MUGEN_SCRIPTPARAMS_H
/* Port addition: adapter for already parsed script parameters. Strings are
 * borrowed during construction, then copied. No INI comment stripping occurs.
 * Keys must be unique (ASCII case-insensitive); line numbers are 1-based. */
typedef struct ScriptParam {
    const char *key;
    const char *value;
    int line;
} ScriptParam;
/* Returns an ordinary owned Ini selected at its only group; Ini_Free releases
 * it. Diagnostic text is static, optional, and never owns an allocation. */
Ini *ScriptParams_ToIni(const ScriptParam *pairs, size_t count, const char **error);
#endif
