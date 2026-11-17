#ifndef MUGEN_ZSS_MAP_H
#define MUGEN_ZSS_MAP_H

/* Port addition: bounded int32/float32 ZSS maps. These sidecars preserve the
 * original Player/CharData layouts. Callers own parsing/evaluation; storage
 * never evaluates expressions while holding its lock. */
#define ZSS_MAP_KEY_MAX 63
#define ZSS_MAP_KEYS_MAX 256
#define ZSS_MAP_PLAYERS_MAX 64 /* engine: four players plus at most 56 helpers */
#define ZSS_MAP_DEFAULTS_MAX 128

int ZssMap_ParseKey(const char *source, char key[ZSS_MAP_KEY_MAX+1], const char **end);
int ZssMap_Read(Player *player, const char *key, ExprValue *value);
int ZssMap_Write(Player *player, const char *key, ExprValue value, int type, int add);
void ZssMap_ResetPlayer(Player *player);
void ZssMap_FreeDefaults(CharData *owner);
/* Load before exposing the character to players; defaults remain immutable
 * during play. The character cache owns/free them, not installed programs. */
int ZssMap_LoadDefaults(Ini *definition, CharData *owner, const char **error);
int ZssMap_Apply(Player *player, StateCtrl *controller, ExprEvalCtx *frame);
float Zss_AiLevelF(const Player *player);

#endif
