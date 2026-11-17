/* Port addition: native ZSS numeric maps and AI-level access. No original
 * structure grows. Reads allocate nothing; mutations publish only complete
 * entries. Explicit map reset, helper reuse, player free and character-cache
 * destruction own the sidecars; ordinary round resets preserve maps.
 * The lock also covers defaults loaded/freed by the precache thread. */
#include "mugen.h"
#include "zss_map.h"

typedef struct MapEntry {
    char key[ZSS_MAP_KEY_MAX+1];
    ExprValue value;
    char type;
} MapEntry;
typedef struct MapValues {
    MapEntry *entries;
    size_t count, capacity;
} MapValues;
typedef struct PlayerMap {
    Player *player;
    int id;
    MapValues values;
} PlayerMap;
typedef struct DefaultMap {
    CharData *owner;
    MapValues values;
} DefaultMap;

static PlayerMap player_maps[ZSS_MAP_PLAYERS_MAX];
static DefaultMap default_maps[ZSS_MAP_DEFAULTS_MAX];
static SDL_SpinLock map_lock;

static int map_ident(unsigned char c)
{
    return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
           (c>='0' && c<='9') || c=='_' || c=='.';
}
static int normalize_key(const char *s, size_t size, char key[ZSS_MAP_KEY_MAX+1])
{
    size_t i;
    unsigned char c;
    if (size==0 || size>ZSS_MAP_KEY_MAX) return 0;
    for (i=0;i<size;i++) {
        c=(unsigned char)s[i];
        /* Static ASCII keys only: no escapes, strings computed at runtime,
         * Unicode case folding or indexed-map suffix encoding in this profile. */
        if (c<32 || c>126 || c=='\\' || c=='"' || c=='[' || c==']') return 0;
        key[i]=(char)(c>='A' && c<='Z' ? c+('a'-'A') : c);
    }
    key[size]=0;return 1;
}
int ZssMap_ParseKey(const char *s, char key[ZSS_MAP_KEY_MAX+1], const char **end)
{
    const char *first;
    size_t n;
    if (s==NULL) return 0;
    while (*s==' ' || *s=='\t') s++;
    if (*s=='"') {
        first=++s;
        while (*s && *s!='"') s++;
        n=(size_t)(s-first);
        if (*s!='"') return 0;
        s++;
    } else {
        first=s;
        if (!map_ident((unsigned char)*s) || (*s>='0' && *s<='9') || *s=='.') return 0;
        while (map_ident((unsigned char)*s)) s++;
        n=(size_t)(s-first);
    }
    if (!normalize_key(first,n,key)) return 0;
    while (*s==' ' || *s=='\t') s++;
    if (end!=NULL) *end=s;
    return 1;
}
static MapEntry *find_entry(MapValues *values, const char *key)
{
    size_t i;
    for (i=0;i<values->count;i++)
        if (!strcmp(values->entries[i].key,key)) return &values->entries[i];
    return NULL;
}
static MapValues *defaults_for(CharData *owner)
{
    size_t i;
    if (owner==NULL) return NULL;
    for (i=0;i<ZSS_MAP_DEFAULTS_MAX;i++)
        if (default_maps[i].owner==owner) return &default_maps[i].values;
    return NULL;
}
static MapValues *player_defaults(Player *p)
{
    /* Ikemen newHelper/init creates an empty map/default bank. Sharing the
     * parent's CharData in this engine must not imply inheriting its maps. */
    return p->helper==0 ? defaults_for(p->data) : NULL;
}
static void clear_values(MapValues *values)
{
    free(values->entries);memset(values,0,sizeof(*values));
}
static PlayerMap *player_slot(Player *p, int create)
{
    PlayerMap *empty=NULL;
    size_t i;
    for (i=0;i<ZSS_MAP_PLAYERS_MAX;i++) {
        PlayerMap *slot=&player_maps[i];
        if (slot->player==p) {
            if (slot->id==p->id) return slot;
            /* Helper pool addresses survive destruction; IDs do not. */
            clear_values(&slot->values);memset(slot,0,sizeof(*slot));
        }
        if (slot->player==NULL && empty==NULL) empty=slot;
    }
    return create ? empty : NULL;
}
static MapEntry *read_entry(PlayerMap *slot, MapValues *defaults, const char *key)
{
    MapEntry *entry=slot!=NULL ? find_entry(&slot->values,key) : NULL;
    return entry!=NULL || defaults==NULL ? entry : find_entry(defaults,key);
}
static MapEntry *append_entry(MapValues *values)
{
    MapEntry *entries;
    size_t capacity;
    if (values->count==ZSS_MAP_KEYS_MAX) return NULL;
    if (values->count==values->capacity) {
        capacity=values->capacity==0 ? 8 : values->capacity*2;
        entries=calloc(capacity,sizeof(*entries));
        if (entries==NULL) {g_outOfMemory=1;return NULL;}
        if (values->count!=0) memcpy(entries,values->entries,values->count*sizeof(*entries));
        free(values->entries);values->entries=entries;values->capacity=capacity;
    }
    return &values->entries[values->count++];
}
int ZssMap_Read(Player *p, const char *source, ExprValue *value)
{
    PlayerMap *slot;
    MapEntry *entry;
    char key[ZSS_MAP_KEY_MAX+1];
    int type=EXPR_INT;
    memset(value,0,sizeof(*value));
    if (p==NULL || source==NULL || !normalize_key(source,strlen(source),key)) return EXPR_BOTTOM;
    SDL_AtomicLock(&map_lock);
    slot=player_slot(p,0);entry=read_entry(slot,player_defaults(p),key);
    if (entry!=NULL) {*value=entry->value;type=entry->type;}
    SDL_AtomicUnlock(&map_lock);return type;
}
int ZssMap_Write(Player *p, const char *source, ExprValue value, int type, int add)
{
    PlayerMap *slot;
    MapValues *defaults;
    MapEntry *entry, *old;
    ExprValue sum={0};
    uint32_t wrapped;
    char key[ZSS_MAP_KEY_MAX+1];
    size_t i, visible;
    int ok=0;
    if (p==NULL || source==NULL || !normalize_key(source,strlen(source),key) ||
        (type!=EXPR_INT && type!=EXPR_FLOAT && type!=EXPR_BOTTOM)) return 0;
    SDL_AtomicLock(&map_lock);
    slot=player_slot(p,1);if (slot==NULL) goto done;
    defaults=player_defaults(p);entry=find_entry(&slot->values,key);
    if (entry==NULL) {
        visible=defaults!=NULL ? defaults->count : 0;
        for (i=0;i<slot->values.count;i++)
            if (defaults==NULL || find_entry(defaults,slot->values.entries[i].key)==NULL) visible++;
        if (visible>=ZSS_MAP_KEYS_MAX && (defaults==NULL || find_entry(defaults,key)==NULL)) goto done;
    }
    old=read_entry(slot,defaults,key);
    if (add) {
        /* The supported numeric profile uses the same int32 wrap and float32
         * promotion as expression ADD. Bottom remains bottom, not float bits. */
        if (type==EXPR_BOTTOM || (old!=NULL && old->type==EXPR_BOTTOM)) {
            type=EXPR_BOTTOM;memset(&value,0,sizeof(value));
        } else if (type==EXPR_FLOAT || (old!=NULL && old->type==EXPR_FLOAT)) {
            sum.f=(old==NULL ? 0.0f : old->type==EXPR_FLOAT ? old->value.f : (float)old->value.i) +
                  (type==EXPR_FLOAT ? value.f : (float)value.i);
            value=sum;type=EXPR_FLOAT;
        } else {
            wrapped=(uint32_t)(old!=NULL ? old->value.i : 0)+(uint32_t)value.i;
            memcpy(&sum.i,&wrapped,sizeof(wrapped));value=sum;
        }
    }
    if (entry==NULL) {
        entry=append_entry(&slot->values);if (entry==NULL) goto done;
        strcpy(entry->key,key);
    }
    entry->value=value;entry->type=(char)type;
    slot->player=p;slot->id=p->id;ok=1;
done:
    SDL_AtomicUnlock(&map_lock);return ok;
}
void ZssMap_ResetPlayer(Player *p)
{
    size_t i;
    if (p==NULL) return;
    SDL_AtomicLock(&map_lock);
    for (i=0;i<ZSS_MAP_PLAYERS_MAX;i++) if (player_maps[i].player==p) {
        clear_values(&player_maps[i].values);memset(&player_maps[i],0,sizeof(player_maps[i]));
    }
    SDL_AtomicUnlock(&map_lock);
}
void ZssMap_FreeDefaults(CharData *owner)
{
    size_t i;
    if (owner==NULL) return;
    SDL_AtomicLock(&map_lock);
    for (i=0;i<ZSS_MAP_DEFAULTS_MAX;i++) if (default_maps[i].owner==owner) {
        clear_values(&default_maps[i].values);memset(&default_maps[i],0,sizeof(default_maps[i]));
    }
    SDL_AtomicUnlock(&map_lock);
}
int ZssMap_LoadDefaults(Ini *ini, CharData *owner, const char **error)
{
    MapValues values={0};
    MapEntry *entry;
    Expr expression={0};
    ExprCompileCtx numeric={0};
    const char *line, *equals, *end, *why="Invalid numeric [Map] defaults";
    char key[ZSS_MAP_KEY_MAX+1];
    size_t i, slot=ZSS_MAP_DEFAULTS_MAX;
    int ok=0;
    if (error!=NULL) *error=NULL;
    if (ini==NULL || owner==NULL) goto done;
    if (Ini_SelectGroup(ini,"map")==-1) return 1;
    for (line=Ini_FirstLine(ini);line!=NULL;line=Ini_NextLine(ini)) {
        equals=strchr(line,'=');
        if (equals==NULL || !ZssMap_ParseKey(line,key,&end) || end!=equals || find_entry(&values,key)!=NULL) goto done;
        end=NULL;
        if (!Expr_CompileCtx(equals+1,&expression,owner,&end,EXPR_WANT_ANY,&numeric) ||
            end==NULL || *end!=0 || expression.count!=EXPR_CONST ||
            (expression.t.type!=EXPR_INT && expression.t.type!=EXPR_FLOAT)) goto done;
        entry=append_entry(&values);if (entry==NULL) {why="[Map] key budget or allocation failed";goto done;}
        strcpy(entry->key,key);entry->value=expression.u.value;entry->type=expression.t.type;
        Expr_Free(&expression);
    }
    if (values.count==0) {ZssMap_FreeDefaults(owner);ok=1;goto done;}
    SDL_AtomicLock(&map_lock);
    for (i=0;i<ZSS_MAP_DEFAULTS_MAX;i++) {
        if (default_maps[i].owner==owner) {slot=i;break;}
        if (default_maps[i].owner==NULL && slot==ZSS_MAP_DEFAULTS_MAX) slot=i;
    }
    if (slot<ZSS_MAP_DEFAULTS_MAX) {
        clear_values(&default_maps[slot].values);
        default_maps[slot].owner=owner;default_maps[slot].values=values;
        memset(&values,0,sizeof(values));ok=1;
    } else why="[Map] character-default budget exhausted";
    SDL_AtomicUnlock(&map_lock);
done:
    Expr_Free(&expression);clear_values(&values);
    if (!ok && error!=NULL) *error=why;
    return ok;
}
int ZssMap_Apply(Player *p, StateCtrl *sc, ExprEvalCtx *frame)
{
    ExprValue value={0};
    int type, result, integer=0;
    float real=0.0f;
    if (sc->arg[1].u.value.i==2) {ZssMap_ResetPlayer(p);return 0;}
    result=Expr_EvalCtx(p,&sc->arg[0],&integer,&real,frame);
    /* Expr_Eval writes one output only; use separate temporaries because its
     * integer/float outputs must not alias the union's two representations. */
    if (result==0) {Player_Warn(p,"Map value could not be evaluated");return STATECTRL_STOP;}
    type=result==1 ? EXPR_INT : result==2 ? EXPR_FLOAT : EXPR_BOTTOM;
    if (type==EXPR_INT) value.i=integer;
    else if (type==EXPR_FLOAT) value.f=real;
    if (!ZssMap_Write(p,(const char *)sc->params,value,type,sc->arg[1].u.value.i)) {
        Player_Warn(p,"Map write exceeded its storage budget or allocation failed");return STATECTRL_STOP;
    }
    return 0;
}
float Zss_AiLevelF(const Player *p)
{
    if (p==NULL) return 0.0f;
    if (p->helper!=0) {
        if (p->data!=NULL && (p->data->mugenversion>>24)==1) return 0.0f;
        if (p->root==NULL) return 0.0f;
        p=p->root;
    }
    if (p->ai==0 || (g_game!=NULL && g_game->team_variant==3 && p->dummy_control==0)) return 0.0f;
    /* The native engine's ramped AI level is integral; no invented fractional
     * AI setting or AILevelSet support is implied by the float return type. */
    return (float)p->ai_level;
}
