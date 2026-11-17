/* The parser/runner and bounded assertion store are production code. The
 * separate input harness exercises Player_UpdateInput and its command boundary. */
static void test_zss_nocrouch(void)
{
    const char *source="AssertSpecial{flag:NoCrOuCh;flag2:nowalk;flag3:timerfreeze}";
    const char *error;
    ScriptParam pairs[]={{"type","AssertSpecial",1},{"flag","nocrouch",2}};
    StateCtrl sc={0}, legacy={0};
    ExprEvalCtx frame={0};
    ZssDiagnostic diagnostic;
    ZssProgram *program=NULL;
    Ini *ini;
    ExprValue value={0};
    static Player other, slots[ZSS_ASSERT_PLAYERS_MAX+1];
    int kind=77,index=88,i,result,before,warnings;
    unsigned char old_walk=player.nowalk,old_timer=game.assert_timerfreeze;
    assert(!AssertSpecial_ParseFlag("nocrouch",&kind,&index) && kind==77 && index==88);
    ini=ScriptParams_ToIni(pairs,2,&error);assert(ini);
    assert(!StateCtrl_ParseParams(ini,&legacy,&owner));StateCtrl_Free(&legacy);Ini_Free(ini);zss_no_allocs();
    assert(Zss_CompileController(source,strlen(source),&owner,&sc,&diagnostic));
    assert(sc.arg[0].u.value.i==ZSS_ASSERT_NO_CROUCH && sc.arg[3].u.value.i==ZSS_ASSERT_KIND);
    player.id=123;other.id=123;player.data=other.data=&owner;other.helper=1;
    ZssAssert_ClearPlayer(&player);ZssAssert_ClearPlayer(&other);
    assert(!ZssAssert_Test(&player,ZSS_ASSERT_NO_CROUCH));
    assert(assert_flag_ctx(&player,&sc,0,&frame)==1);
    assert(ZssAssert_Test(&player,ZSS_ASSERT_NO_CROUCH) && !ZssAssert_Test(&other,ZSS_ASSERT_NO_CROUCH));
    assert(player.nowalk==old_walk && game.assert_timerfreeze==old_timer);
    assert(assert_flag_ctx(&player,&sc,1,&frame)==1 && assert_flag_ctx(&player,&sc,2,&frame)==1);
    assert(player.nowalk==1 && game.assert_timerfreeze==1);
    ZssAssert_BeginTick(&player,0);assert(ZssAssert_Test(&player,ZSS_ASSERT_NO_CROUCH));
    player.hitpause_time=3;ZssAssert_BeginTick(&player,1);
    assert(!ZssAssert_Test(&player,ZSS_ASSERT_NO_CROUCH));player.hitpause_time=0;
    assert(ZssAssert_Set(&player,ZSS_ASSERT_NO_CROUCH));player.id++;
    assert(!ZssAssert_Test(&player,ZSS_ASSERT_NO_CROUCH));
    assert(ZssAssert_Set(&other,ZSS_ASSERT_NO_CROUCH));ZssAssert_ClearPlayer(&other);
    assert(!ZssAssert_Test(&other,ZSS_ASSERT_NO_CROUCH));
    assert(ZssAssert_Set(NULL,ZSS_ASSERT_NO_CROUCH)==0 && !ZssAssert_Test(NULL,ZSS_ASSERT_NO_CROUCH));
    assert(!ZssAssert_Set(&player,-1) && !ZssAssert_Set(&player,99) && !ZssAssert_Test(&player,99));
    ZssAssert_BeginTick(NULL,1);ZssAssert_ClearPlayer(NULL);
    StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    source="[StateDef 0]\ncall lock(1);\n[Function lock(enabled)]\nif $enabled {AssertSpecial{flag:nocrouch}}\n";
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&diagnostic));
    assert(Zss_RunBody(program,0,&player,run_assert_leaf,&owner,&result) && !result);
    assert(ZssAssert_Test(&player,ZSS_ASSERT_NO_CROUCH));Zss_FreeProgram(program);program=NULL;
    /* A map reset is unrelated to a tick assertion, and tick expiry is
     * unrelated to a map's round persistence. */
    value.i=5;assert(ZssMap_Write(&player,"kept",value,EXPR_INT,0));
    ZssAssert_BeginTick(&player,1);map_expect(&player,"kept",EXPR_INT,5);
    assert(ZssAssert_Set(&player,ZSS_ASSERT_NO_CROUCH));ZssMap_ResetPlayer(&player);
    assert(ZssAssert_Test(&player,ZSS_ASSERT_NO_CROUCH));ZssAssert_ClearPlayer(&player);zss_no_allocs();
    source="AssertSpecial{flag:nocrouch}";
    assert(Zss_CompileController(source,strlen(source),&owner,&sc,&diagnostic));
    before=zss_calloc_calls;
    for(i=0;i<ZSS_ASSERT_PLAYERS_MAX;i++) {slots[i].id=i;assert(ZssAssert_Set(&slots[i],ZSS_ASSERT_NO_CROUCH));}
    assert(zss_calloc_calls==before && !ZssAssert_Set(&slots[ZSS_ASSERT_PLAYERS_MAX],ZSS_ASSERT_NO_CROUCH));
    warnings=player_warnings;
    assert(assert_flag_ctx(&player,&sc,0,&frame)==-1 && player_warnings==warnings+1);
    ZssAssert_ClearPlayer(&slots[7]);assert(ZssAssert_Set(&player,ZSS_ASSERT_NO_CROUCH));
    for(i=0;i<=ZSS_ASSERT_PLAYERS_MAX;i++) ZssAssert_ClearPlayer(&slots[i]);
    ZssAssert_ClearPlayer(&player);StateCtrl_Free(&sc);zss_no_allocs();
    player.nowalk=old_walk;game.assert_timerfreeze=old_timer;player.data=NULL;g_outOfMemory=0;
    puts("ok: ZSS nocrouch metadata, actor/helper isolation, tick/pause lifetime, bounds and map independence");
}
