/* Port addition: append newly installed loose characters/stages to select.def. */
#ifndef MUGEN_SELECTUPDATE_H
#define MUGEN_SELECTUPDATE_H

void SelectUpdate_Configure(int characters, int stages);
int SelectUpdate_Enabled(void);
void SelectUpdate_Auto(const char *path);
/* Caller frees the merged bytes. NULL means unchanged, disabled, or failed. */
char *SelectUpdate_BuildAuto(const char *path, int *size);
/* Return the number of additions, or -1 on failure; unchanged files are not written. */
int Select_UpdateSelectDef(const char *path, int characters, int stages);
int Select_UpdateCharInSelectDef(const char *path);
int Select_UpdateStageInSelectDef(const char *path);

#endif
