#ifndef MUGEN_CONFIGMERGE_H
#define MUGEN_CONFIGMERGE_H

#include "ini.h"

/*
 * Port addition: layers one parsed text file over another, per key.
 *
 * The engine reads a single configuration file, so a platform port that needs
 * different device settings has either to replace the game's data/mugen.cfg or
 * to layer its own file over it. This is the second: the port's keys win, every
 * key its file does not mention comes from the game's file.
 *
 * A group in the engine's write-back set is never forced (see the skip tables
 * in configmerge.c). Options_Save, the FirstRun clear and
 * Config_WriteMissingGamepadGroups rewrite data/mugen.cfg at runtime, so a
 * platform value that outranked them would be restored on the next boot and the
 * options screen would appear not to save.
 *
 * *text receives the merged file (malloc'd, the caller frees it), *text_len its
 * length in bytes, *keys the number of keys the overlay contributed, and
 * *skipped the number of overlay groups ignored because they are written back.
 *
 * Returns 1 when a merged file was produced, 0 when there is nothing to merge
 * (either file parses to no group at all) or on allocation failure; the caller
 * then uses the base Ini unmodified, which is the original behaviour.
 */
int __cdecl ConfigMerge_Text(const Ini *base, const Ini *over,
                             char **text, int *text_len, int *keys, int *skipped);

#endif /* MUGEN_CONFIGMERGE_H */