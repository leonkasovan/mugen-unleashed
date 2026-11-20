/* GGPO direct UDP quick-versus sessions and local determinism checks. */
#ifdef MUGEN_ROLLBACK
#include "mugen.h"
#include "rollback.h"
#include "memprof.h"
#include "model.h"
#include "zss_assert.h"
#include "zss_binding.h"

#include <arpa/inet.h>
#include <stdbool.h>
#include <ggponet.h>
#include <zlib.h>

enum { MAX_SNAPSHOT_BYTES = 32 * 1024 * 1024, INPUT_BYTES = 6 };

typedef struct {
    void *address;
    size_t size;
    uint64_t allocation;
    const char *name;
} CheckpointRegion;

typedef struct {
    ScreenLoop *screen;
    GGPOSession *session;
    CheckpointRegion *regions;
    size_t region_count, region_capacity, size;
    const unsigned char *last_checkpoint;
    uint16_t held[2];
    uint32_t setup_key;
    unsigned replayed;
    int connected, disconnected, wait_frames;
} RollbackMatch;

static struct {
    int side;
    unsigned short local_port, remote_port;
    char address[INET_ADDRSTRLEN];
} peer;

static RollbackMatch *active_match;
static int requested_frames;
static int match_completed;

static int ParseNumber(const char *text, int upper)
{
    char *end;
    long count = strtol(text, &end, 10);
    return end != text && *end == '\0' && count >= 1 && count <= upper ? (int)count : 0;
}

int Rollback_ParsePeer(const char *side, const char *local_port, const char *ip, const char *remote_port)
{
    struct in_addr address;
    int player = ParseNumber(side, 2);
    int local = ParseNumber(local_port, 65535);
    int remote = ParseNumber(remote_port, 65535);
    if (requested_frames || peer.side || !player || !local || !remote ||
        inet_pton(AF_INET, ip, &address) != 1 ||
        ((ntohl(address.s_addr) >> 24) == 127 && local == remote))
        return 0;
    peer.side = player;
    peer.local_port = (unsigned short)local;
    peer.remote_port = (unsigned short)remote;
    memcpy(peer.address, ip, strlen(ip) + 1);
    return 1;
}

int Rollback_ParseFrames(const char *text)
{
    if (peer.side || requested_frames)
        return 0;
    requested_frames = ParseNumber(text, 100000);
    return requested_frames != 0;
}

int Rollback_Requested(void)
{
    return requested_frames != 0 || peer.side != 0;
}

int Rollback_Active(void)
{
    return active_match != NULL;
}

int Rollback_Configure(CmdLineArgs *args)
{
    if (!Rollback_Requested())
        return 1;
    if (!args->player[0][0] || !args->player[1][0] || args->player[2][0] || args->player[3][0] ||
        !args->stage[0] || args->storyboard[0] || args->speedtest || args->stresstest ||
        args->ai[0] > 0 || args->ai[1] > 0) {
        fputs("Rollback requires a human quick VS between two players and an explicit stage.\n", stderr);
        return 0;
    }
    args->nosound = 1;
    args->nomusic = 1;
    if (!args->session_seed)
        args->session_seed = 424242;
    return 1;
}

int Rollback_ExitCode(void)
{
    return Rollback_Requested() && !match_completed;
}

static int Region_Add(RollbackMatch *test, void *address, size_t size, const char *name)
{
    uint64_t allocation = 0;
    if (!address)
        return 1;
    if (size == 0 && !Memprof_BlockInfo(address, &size, &allocation)) {
        Log_Printf("Rollback: could not identify allocation for %s\n", name);
        return 0;
    }
    if (size > MAX_SNAPSHOT_BYTES - test->size) {
        Log_Printf("Rollback: checkpoint exceeds %d MiB\n", MAX_SNAPSHOT_BYTES >> 20);
        return 0;
    }
    if (test->region_count == test->region_capacity) {
        size_t capacity = test->region_capacity ? test->region_capacity * 2 : 64;
        CheckpointRegion *regions = realloc(test->regions, capacity * sizeof(*regions));
        if (!regions)
            return 0;
        test->regions = regions;
        test->region_capacity = capacity;
    }
    test->regions[test->region_count++] = (CheckpointRegion){address, size, allocation, name};
    test->size += size;
    return 1;
}

/* Only owned mutable storage enters the checkpoint. Read-only art, code and
 * renderer/mixer resources stay where they were loaded. */
#define OWNED(ptr) Region_Add(test, (ptr), 0, #ptr)
#define INLINE(value) Region_Add(test, &(value), sizeof(value), #value)

static int CaptureList(RollbackMatch *test, KeyedArray *list)
{
    return !list || (OWNED(list) && OWNED(list->data) && OWNED(list->slots));
}

static int CaptureExplods(RollbackMatch *test, SysDrawList *list)
{
    if (!list)
        return 1;
    if (!OWNED(list) || !OWNED(list->items))
        return 0;
    for (int i = 0; i < list->capacity; ++i) {
        SysDrawItem *item = &list->items[i];
        SpriteSet *scratch = item->sprites;
        /* Explods_Create reserves an empty sprite set for each slot. An
         * inactive slot never draws this set and expiry frees it. Drop the
         * unused reservation before taking any checkpoint. */
        if (scratch) {
            if (item->used || scratch->sprites || scratch->shared || scratch->data || scratch->refs ||
                (scratch->palettes && scratch->palettes->count) || item->draw_sprites == scratch ||
                (item->anim && item->anim->sprites == scratch)) {
                Log_Printf("Rollback: explod slot %d owns active sprite storage at setup\n", i);
                return 0;
            }
            SpriteSet_Free(scratch);
            item->sprites = NULL;
        }
        if (!OWNED(item->anim))
            return 0;
    }
    return 1;
}

static int CaptureBackground(RollbackMatch *test, Bg *bg)
{
    if (!bg)
        return 1;
    if (!OWNED(bg) || !OWNED(bg->ctrls) || !CaptureList(test, bg->elems))
        return 0;
    KeyedArray *list = bg->elems;
    if (!list)
        return 1;
    for (int i = list->first; i <= list->last && list->count; ++i) {
        if (!list->slots[i].used)
            continue;
        BgElem *elem = (BgElem *)(list->data + (size_t)list->elem_size * i);
        if (elem->type == BG_TYPE_VIDEO) {
            Log_Printf("Rollback: video stage backgrounds are unsupported\n");
            return 0;
        }
        if (!OWNED(elem->anim))
            return 0;
    }
    return 1;
}

static int CapturePlayer(RollbackMatch *test, Player *player)
{
    if (!player)
        return 1;
    ProjectileList *projectiles = player->projectiles;
    if (!OWNED(player) || !OWNED(player->anim) || !OWNED(player->hit_pending) ||
        !OWNED(player->hit_ids) || !CaptureList(test, player->targets) ||
        !OWNED(projectiles) || (projectiles && !CaptureList(test, projectiles->list)))
        return 0;
    TextRing *warnings = player->warnings;
    return !warnings || (OWNED(warnings) && OWNED(warnings->buf));
}

static int CaptureWorld(RollbackMatch *test)
{
    GameVars *game = g_game;
    if (g_stage3d.model) {
        Log_Printf("Rollback: 3D stages are unsupported\n");
        return 0;
    }
    size_t assert_size;
    void *assertions = ZssAssert_RollbackStorage(&assert_size);
    if (!OWNED(game) || !INLINE(*test->screen) || !INLINE(g_palFxSerial) ||
        !INLINE(g_palFx) || !INLINE(g_palFxApplied) || !INLINE(g_palFxStatic) ||
        !INLINE(g_palFxCur) || !INLINE(g_palRemapStatic) || !INLINE(g_palRemapCur) ||
        !INLINE(g_sysElemSerial) || !INLINE(g_nokoLatch) ||
        !Region_Add(test, assertions, assert_size, "actor assertions") ||
        !INLINE(g_quitRequested) || !CaptureBackground(test, game->stage_bg) ||
        !CaptureExplods(test, game->explods) || !CaptureExplods(test, game->sysexplods))
        return 0;
    PalGroups *groups = game->palgroups;
    if (!OWNED(groups) || (groups && !OWNED(groups->entries)) || !OWNED(game->anims) ||
        (game->anims && !CaptureList(test, game->anims->list)) ||
        !CaptureList(test, game->afterimages))
        return 0;
    KeyedArray *images = game->afterimages;
    if (images) {
        for (int i = images->first; i <= images->last && images->count; ++i) {
            if (!images->slots[i].used)
                continue;
            AfterImage *image = (AfterImage *)(images->data + (size_t)images->elem_size * i);
            if (!OWNED(image->frames))
                return 0;
        }
    }
    for (int i = 1; i <= game->player_slots; ++i) {
        if (!CapturePlayer(test, game->players[i]))
            return 0;
    }
    return 1;
}

#undef OWNED
#undef INLINE

static int LayoutIsLive(const RollbackMatch *test)
{
    for (size_t i = 0; i < test->region_count; ++i) {
        const CheckpointRegion *region = &test->regions[i];
        size_t size;
        uint64_t identity;
        if (region->allocation &&
            (!Memprof_BlockInfo(region->address, &size, &identity) ||
             identity != region->allocation || size != region->size)) {
            Log_Printf("Rollback: %s storage changed during simulation\n", region->name);
            return 0;
        }
    }
    return 1;
}

static void WriteCheckpoint(const RollbackMatch *match, unsigned char *out)
{
    for (size_t i = 0; i < match->region_count; ++i) {
        const CheckpointRegion *region = &match->regions[i];
        memcpy(out, region->address, region->size);
        out += region->size;
    }
}

static void ReadCheckpoint(const RollbackMatch *match, const unsigned char *buffer)
{
    for (size_t i = 0; i < match->region_count; ++i) {
        const CheckpointRegion *region = &match->regions[i];
        memcpy(region->address, buffer, region->size);
        buffer += region->size;
    }
}

static bool GgpoSave(unsigned char **buffer, int *size, int *checksum, int frame)
{
    RollbackMatch *test = active_match;
    (void)frame;
    if (!LayoutIsLive(test))
        return false;
    unsigned char *saved = malloc(test->size);
    if (!saved)
        return false;
    WriteCheckpoint(test, saved);
    *buffer = saved;
    *size = (int)test->size;
    test->last_checkpoint = saved;
    /* Addresses are stable within this process. Raw snapshots are never sent
     * across the wire or compared across address spaces. */
    *checksum = requested_frames ? (int)crc32(0, saved, (uInt)test->size) : 0;
    return true;
}

static bool GgpoLoad(unsigned char *buffer, int size)
{
    RollbackMatch *test = active_match;
    if (size != (int)test->size || !LayoutIsLive(test))
        return false;
    ReadCheckpoint(test, buffer);
    return true;
}

static bool GgpoLog(char *path, unsigned char *buffer, int size)
{
    (void)path;
    (void)buffer;
    (void)size;
    return true;
}

static bool GgpoBegin(const char *name)
{
    (void)name;
    return true;
}

static void GgpoFree(void *buffer)
{
    if (active_match && active_match->last_checkpoint == buffer)
        active_match->last_checkpoint = NULL;
    free(buffer);
}

static bool GgpoEvent(GGPOEvent *event)
{
    RollbackMatch *match = active_match;
    switch (event->code) {
    case GGPO_EVENTCODE_RUNNING:
        match->connected = 1;
        if (peer.side)
            Log_Printf("Rollback: synchronized as P%d\n", peer.side);
        break;
    case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER:
        match->disconnected = 1;
        break;
    case GGPO_EVENTCODE_TIMESYNC:
        match->wait_frames = event->u.timesync.frames_ahead;
        if (match->wait_frames > 4)
            match->wait_frames = 4;
        break;
    default:
        break;
    }
    return true;
}

/* Same absolute controls are reapplied after a rewind, independent of SDL's
 * current input state. The player-facing transform is part of simulation. */
int Rollback_ApplyInput(Player *player)
{
    if (!active_match)
        return 0;
    if (player->ai || player->team_side < 1 || player->team_side > 2)
        return 1;
    PlayerInput_ApplyHeldBits(&player->input, active_match->held[player->team_side - 1]);
    return 1;
}

static bool Advance(bool replay)
{
    RollbackMatch *test = active_match;
    unsigned char inputs[2][INPUT_BYTES];
    int disconnected = 0;
    if (g_outOfMemory ||
        ggpo_synchronize_input(test->session, inputs, sizeof(inputs), &disconnected) != GGPO_OK ||
        disconnected)
        return false;
    for (int i = 0; i < 2; ++i) {
        const unsigned char *input = inputs[i];
        uint32_t key = (uint32_t)input[2] | (uint32_t)input[3] << 8 |
                       (uint32_t)input[4] << 16 | (uint32_t)input[5] << 24;
        if (key != test->setup_key) {
            Log_Printf("Rollback: mismatched match setup from P%d\n", i + 1);
            return false;
        }
        test->held[i] = (uint16_t)(input[0] | (input[1] << 8));
    }

    ScreenLoop *screen = test->screen;
    PalFx_ResetGlobal();
    g_game->key_codes.read = g_game->key_codes.write = 0;
    if (g_game->round_reset_pending) {
        Round_Reset();
        g_game->round_reset_pending = 0;
    }
    Screen_Tick(screen);
    Game_Tick(screen);
    Match_Tick(screen, screen->state);
    if (!g_game->defocus_paused) {
        SysDrawList *list = g_game->sysexplods;
        for (int i = 0; i < list->capacity; ++i)
            SysDraw_Start(list, i);
        Lifebars_Tick();
        if (!g_game->slow_counter) {
            Combo_Tick(1);
            Combo_Tick(2);
        }
        ++g_game->frames;
        ++screen->ticks;
        ++screen->idle_ticks;
    }
    g_game->no_sounds = 0;
    if (replay)
        ++test->replayed;
    return !g_outOfMemory && ggpo_advance_frame(test->session) == GGPO_OK;
}

static bool GgpoAdvance(int flags)
{
    (void)flags;
    return Advance(true);
}

static int IsCnsMatch(void)
{
    if (!g_game->quick_vs || g_game->match_members[0].count != 1 || g_game->match_members[1].count != 1)
        return 0;
    for (int i = 0; i < 2; ++i) {
        Player *player = g_game->players[g_game->match_members[i].slots[0].player];
        if (!player || player->ai || !player->data || !player->data->states)
            return 0;
        KeyedArray *states = player->data->states->list;
        for (int j = states->first; j <= states->last && states->count; ++j) {
            if (states->slots[j].used &&
                Zss_IsBoundState((StateDef *)(states->data + (size_t)states->elem_size * j)))
                return 0;
        }
    }
    return 1;
}

static uint16_t TestInput(unsigned frame, int player)
{
    unsigned phase = (frame + (unsigned)player * 17u) % 96u;
    uint16_t direction = (uint16_t)(1u << (player == 0 ? KEY_RIGHT : KEY_LEFT));
    if (phase < 48)
        return direction;
    if (phase < 68)
        return (uint16_t)(1u << KEY_CROUCH);
    if (phase < 76)
        return (uint16_t)(direction | (1u << KEY_JUMP));
    return phase % 4 == 0 ? (uint16_t)(1u << KEY_X) : 0;
}

/* Compare the basic configuration at the first synchronized input. Asset
 * packs and build revisions must also match on the two peers. */
static uint32_t SetupKey(void)
{
    const GameVars *g = g_game;
    const uint32_t rules[] = {1, g->match_rng_seed, g->round_time,
                              (uint32_t)g->game_coord[0], (uint32_t)g->game_coord[1],
                              (uint32_t)g_cmdline->rounds};
    uint32_t key = (uint32_t)crc32(0, (const unsigned char *)rules, sizeof(rules));
    key = (uint32_t)crc32(key, (const unsigned char *)g_cmdline->stage, (uInt)strlen(g_cmdline->stage));
    for (int i = 0; i < 2; ++i) {
        const Player *p = g->players[g->match_members[i].slots[0].player];
        const uint32_t actor[] = {(uint32_t)p->life, (uint32_t)p->life_max,
                                  (uint32_t)p->power, (uint32_t)p->pal_no,
                                  (uint32_t)p->data->expr_count};
        key = (uint32_t)crc32(key, (const unsigned char *)actor, sizeof(actor));
        key = (uint32_t)crc32(key, (const unsigned char *)p->name, (uInt)strlen(p->name));
    }
    return key ? key : 1;
}

static void EncodeInput(unsigned char input[INPUT_BYTES], uint16_t buttons, uint32_t key)
{
    input[0] = (unsigned char)buttons;
    input[1] = (unsigned char)(buttons >> 8);
    for (unsigned int i = 0; i < 4; ++i)
        input[i + 2] = (unsigned char)(key >> (8 * i));
}

static bool RunPeer(RollbackMatch *match, GGPOPlayerHandle local_handle)
{
    ScreenLoop *screen = match->screen;
    /* ArkOS ships SDL 2.0.10. Subtracting unsigned ticks handles wraparound. */
    Uint32 sync_started = SDL_GetTicks();
    SpriteSet *target = g_game->screen;
    int width = g_targetWidth, height = g_targetHeight;
    bool completed = false;
    bool sent_first_input = false;
    bool starting = true;

    Log_Printf("Rollback: waiting for %s:%u on local UDP port %u\n",
               peer.address, peer.remote_port, peer.local_port);
    Timer_SetFps(60);
    screen->fps = 60;
    while (!match->disconnected) {
        Screen_ReadInput(screen);
        if (g_quitRequested || screen->key == SDLK_ESCAPE || g_keyState[SDLK_ESCAPE]) {
            Log_Printf("Rollback: local player left the match\n");
            completed = true;
            break;
        }
        if (g_rendererFallbackRequested || g_game->screen != target ||
            g_targetWidth != width || g_targetHeight != height) {
            Log_Printf("Rollback: render target changed during the match\n");
            break;
        }
        g_game->defocus_paused = 0;
        if (ggpo_idle(match->session, 0) != GGPO_OK)
            break;
        if (!match->connected) {
            if (SDL_GetTicks() - sync_started >= 10000u) {
                Log_Printf("Rollback: UDP synchronization timed out\n");
                break;
            }
            SDL_Delay(1);
            continue;
        }
        if (match->disconnected)
            break;
        if (screen->fade_out && screen->fade_ticks <= 0) {
            if (ggpo_last_confirmed_frame(match->session) >= screen->ticks - 1) {
                Log_Printf("Rollback: confirmed match end at frame %d, winner %d, rounds %d-%d\n",
                           screen->ticks, g_game->match_winner,
                           g_game->rounds_won[0], g_game->rounds_won[1]);
                completed = true;
                break;
            }
            SDL_Delay(1);
            continue;
        }
        if (match->wait_frames > 0) {
            --match->wait_frames;
            SDL_Delay(16);
            Timer_ResetFrame();
            continue;
        }

        if (!starting || !sent_first_input) {
            unsigned char input[INPUT_BYTES];
            uint16_t buttons = PlayerInput_ReadHeldBits(g_game->input_keys[0]);
            EncodeInput(input, buttons, match->setup_key);
            GGPOErrorCode status = ggpo_add_local_input(match->session, local_handle, input, sizeof(input));
            if (status == GGPO_ERRORCODE_PREDICTION_THRESHOLD) {
                SDL_Delay(1);
                Timer_ResetFrame();
                continue;
            }
            if (status != GGPO_OK)
                break;
            sent_first_input = true;
        }
        if (starting) {
            /* Require real input from both peers before checking the setup
             * keys and allowing GGPO to predict subsequent inputs. */
            if (ggpo_last_confirmed_frame(match->session) < 0) {
                if (SDL_GetTicks() - sync_started >= 10000u) {
                    Log_Printf("Rollback: timed out waiting for the peer's first input\n");
                    break;
                }
                SDL_Delay(1);
                continue;
            }
            Timer_ResetFrame();
            starting = false;
        }
        Perf_BeginFrame();
        if (!Advance(false) || !match->last_checkpoint)
            break;

        /* Drawing may update caches and presentation bookkeeping. Restore
         * GGPO's post-simulation checkpoint before polling it again. */
        int was_skipped = g_game->skip_draw;
        Rollback_DrawFrame(screen);
        int skip_draw = g_game->skip_draw;
        int skip_count = g_game->frame_skip_count;
        if (!LayoutIsLive(match))
            break;
        ReadCheckpoint(match, match->last_checkpoint);
        g_game->skip_draw = skip_draw;
        g_game->frame_skip_count = skip_count;
        screen->skipped_frames += was_skipped != 0;
    }
    return completed;
}

void Rollback_Run(ScreenLoop *screen)
{
    RollbackMatch test = {.screen = screen};
    GGPOSessionCallbacks callbacks = {
        GgpoBegin, GgpoSave, GgpoLoad, GgpoLog, GgpoFree, GgpoAdvance, GgpoEvent,
    };
    GGPOPlayerHandle handles[2];
    char game_name[] = "mugen-unleashed";
    int frames = 0;
    int distance = requested_frames % 4 == 0 ? 4 : 1;

    if (!IsCnsMatch() || g_bgmActive || g_sndActive || g_game->precache_enabled) {
        Log_Printf("Rollback: expected a silent 1v1 quick VS with CNS characters\n");
        return;
    }
    if (g_game->round_reset_pending) {
        Round_Reset();
        g_game->round_reset_pending = 0;
    }
    /* Learning is an offline AI side effect, not part of the human fight. */
    for (int i = 1; i <= g_game->player_slots; ++i) {
        if (g_game->players[i])
            g_game->players[i]->ai_learn = 0;
    }
    if (!CaptureWorld(&test))
        goto end;
    if (peer.side)
        test.setup_key = SetupKey();
    active_match = &test;
    GGPOErrorCode status = peer.side
        ? ggpo_start_session(&test.session, &callbacks, game_name, 2, INPUT_BYTES, peer.local_port)
        : ggpo_start_synctest(&test.session, &callbacks, game_name, 2, INPUT_BYTES, distance);
    if (status != GGPO_OK) {
        Log_Printf("Rollback: could not start the GGPO session (%d)\n", status);
        goto end;
    }
    for (int i = 0; i < 2; ++i) {
        GGPOPlayer player = {.size = sizeof(player), .type = GGPO_PLAYERTYPE_LOCAL, .player_num = i + 1};
        if (peer.side && peer.side != i + 1) {
            player.type = GGPO_PLAYERTYPE_REMOTE;
            memcpy(player.u.remote.ip_address, peer.address, strlen(peer.address) + 1);
            player.u.remote.port = peer.remote_port;
        }
        if (ggpo_add_player(test.session, &player, &handles[i]) != GGPO_OK)
            goto end;
    }
    if (peer.side) {
        ggpo_set_disconnect_timeout(test.session, 5000);
        ggpo_set_disconnect_notify_start(test.session, 1000);
        match_completed = RunPeer(&test, handles[peer.side - 1]);
        goto end;
    }
    if (ggpo_idle(test.session, 0) != GGPO_OK)
        goto end;
    Log_Printf("Rollback test: %zu bytes per checkpoint, replay every %d frame(s)\n", test.size, distance);
    while (frames < requested_frames) {
        for (int i = 0; i < 2; ++i) {
            uint16_t bits = TestInput((unsigned)frames, i);
            unsigned char input[INPUT_BYTES];
            EncodeInput(input, bits, 0);
            if (ggpo_add_local_input(test.session, handles[i], input, sizeof(input)) != GGPO_OK)
                goto end;
        }
        if (!Advance(false))
            goto end;
        ++frames;
        if (screen->fade_out && screen->fade_ticks <= 0 && frames < requested_frames)
            break;
    }
    match_completed = frames == requested_frames && test.replayed == (unsigned)frames;

end:
    if (test.session)
        ggpo_close_session(test.session);
    active_match = NULL;
    free(test.regions);
    if (peer.side) {
        Log_Printf("Rollback %s: %d frames, %u resimulated%s\n",
                   match_completed ? "finished" : "aborted", screen->ticks, test.replayed,
                   test.disconnected ? " (peer disconnected)" : "");
    } else {
        Log_Printf("Rollback test %s: %d/%d frames, %u resimulated\n",
                   match_completed ? "passed" : "failed", frames, requested_frames, test.replayed);
    }
}
#endif
