/* Port addition: restore bundled Lua helpers before Lua_RunInit. */
#ifndef MUGEN_LUASCRIPTS_H
#define MUGEN_LUASCRIPTS_H
/* -1: failure, 0: vector.lua already exists, 1: missing helpers restored. */
int LuaScripts_Ensure(void);
#endif
