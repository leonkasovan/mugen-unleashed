/* Port addition: parse a standalone ZSS controller into the existing CNS
 * controller representation. Parameters retain string case and delimiters;
 * comments are removed by tokens, never by Ini_Parse. */
#include "mugen.h"
#include "zss_params.h"
#include "zss_lex.h"
#include "zss_controller.h"
#include <limits.h>


/* Initial leaf schemas; block attributes belong to the future block parser. */
static int allowed_key(const char *type, const char *key)
{
    if (!_stricmp(type,"velset") || !_stricmp(type,"veladd") ||
        !_stricmp(type,"posset") || !_stricmp(type,"posadd"))
        return !_stricmp(key,"x") || !_stricmp(key,"y") || !_stricmp(key,"z");
    if (!_stricmp(type,"hitby") || !_stricmp(type,"nothitby"))
        return !_stricmp(key,"value") || !_stricmp(key,"value2") || !_stricmp(key,"time");
    if (!_stricmp(type,"mapset") || !_stricmp(type,"mapadd"))
        return !_stricmp(key,"map") || !_stricmp(key,"value");
    if (!_stricmp(type,"assertspecial"))
        return !_stricmp(key,"flag") || !_stricmp(key,"flag2") || !_stricmp(key,"flag3");
    if (!_stricmp(type,"posfreeze")) return !_stricmp(key,"value");
    if (!_stricmp(type,"ctrlset")) return !_stricmp(key,"value");
    if (!_stricmp(type,"changestate"))
        return !_stricmp(key,"value") || !_stricmp(key,"ctrl") || !_stricmp(key,"anim");
    if (!_stricmp(type,"changeanim")) return !_stricmp(key,"value") || !_stricmp(key,"elem");
    if (!_stricmp(type,"playsnd"))
        return !_stricmp(key,"value") || !_stricmp(key,"channel") || !_stricmp(key,"volume") ||
               !_stricmp(key,"volumescale") || !_stricmp(key,"lowpriority") || !_stricmp(key,"freqmul") ||
               !_stricmp(key,"loop") || !_stricmp(key,"pan") || !_stricmp(key,"abspan");
    if (!_stricmp(type,"displaytoclipboard") || !_stricmp(type,"appendtoclipboard") || !_stricmp(type,"printtoconsole"))
        return !_stricmp(key,"text") || !_stricmp(key,"params");
    if (!_stricmp(type,"palfx") || !_stricmp(type,"bgpalfx") || !_stricmp(type,"allpalfx"))
        return !_stricmp(key,"time") || !_stricmp(key,"color") || !_stricmp(key,"add") ||
               !_stricmp(key,"mul") || !_stricmp(key,"sinadd") || !_stricmp(key,"invertall");
    if (!_stricmp(type,"afterimage")) return !_stricmp(key,"framegap") || !_stricmp(key,"length") || !_stricmp(key,"paladd") || !_stricmp(key,"palbright") || !_stricmp(key,"palcontrast") || !_stricmp(key,"palmul") || !_stricmp(key,"time") || !_stricmp(key,"timegap") || !_stricmp(key,"trans");
    if (!_stricmp(type,"afterimagetime")) return !_stricmp(key,"time");
    if (!_stricmp(type,"changeanim2")) return !_stricmp(key,"value");
    if (!_stricmp(type,"defencemulset")) return !_stricmp(key,"value");
    if (!_stricmp(type,"dialogue")) return !_stricmp(key,"text1") || !_stricmp(key,"text2") || !_stricmp(key,"text3");
    if (!_stricmp(type,"envshake")) return !_stricmp(key,"ampl") || !_stricmp(key,"freq") || !_stricmp(key,"time");
    if (!_stricmp(type,"explod")) return !_stricmp(key,"accel") || !_stricmp(key,"anim") || !_stricmp(key,"facing") || !_stricmp(key,"pos") || !_stricmp(key,"postype") || !_stricmp(key,"removetime") || !_stricmp(key,"sprpriority") || !_stricmp(key,"velocity");
    if (!_stricmp(type,"forcefeedback")) return !_stricmp(key,"ampl") || !_stricmp(key,"time") || !_stricmp(key,"waveform");
    if (!_stricmp(type,"gamemakeanim")) return !_stricmp(key,"pos") || !_stricmp(key,"under") || !_stricmp(key,"value");
    if (!_stricmp(type,"hitdef")) return !_stricmp(key,"air.fall") || !_stricmp(key,"air.hittime") || !_stricmp(key,"air.type") || !_stricmp(key,"air.velocity") || !_stricmp(key,"airguard.velocity") || !_stricmp(key,"animtype") || !_stricmp(key,"attr") || !_stricmp(key,"damage") || !_stricmp(key,"down.hittime") || !_stricmp(key,"down.velocity") || !_stricmp(key,"envshake.ampl") || !_stricmp(key,"envshake.freq") || !_stricmp(key,"envshake.time") || !_stricmp(key,"fall") || !_stricmp(key,"fall.animtype") || !_stricmp(key,"fall.damage") || !_stricmp(key,"fall.envshake.ampl") || !_stricmp(key,"fall.envshake.freq") || !_stricmp(key,"fall.envshake.time") || !_stricmp(key,"fall.recover") || !_stricmp(key,"fall.recovertime") || !_stricmp(key,"forcenofall") || !_stricmp(key,"forcestand") || !_stricmp(key,"getpower") || !_stricmp(key,"ground.cornerpush.veloff") || !_stricmp(key,"ground.hittime") || !_stricmp(key,"ground.slidetime") || !_stricmp(key,"ground.type") || !_stricmp(key,"ground.velocity") || !_stricmp(key,"guard.ctrltime") || !_stricmp(key,"guard.dist") || !_stricmp(key,"guard.velocity") || !_stricmp(key,"guardflag") || !_stricmp(key,"guardsound") || !_stricmp(key,"hitflag") || !_stricmp(key,"hitsound") || !_stricmp(key,"maxdist") || !_stricmp(key,"mindist") || !_stricmp(key,"p1facing") || !_stricmp(key,"p1sprpriority") || !_stricmp(key,"p1stateno") || !_stricmp(key,"p2facing") || !_stricmp(key,"p2stateno") || !_stricmp(key,"pausetime") || !_stricmp(key,"priority") || !_stricmp(key,"sparkno") || !_stricmp(key,"sparkxy") || !_stricmp(key,"yaccel");
    if (!_stricmp(type,"hitfallset")) return !_stricmp(key,"value");
    if (!_stricmp(type,"hitoverride")) return !_stricmp(key,"attr") || !_stricmp(key,"stateno") || !_stricmp(key,"time");
    if (!_stricmp(type,"hitvelset")) return !_stricmp(key,"x") || !_stricmp(key,"y");
    if (!_stricmp(type,"makedust")) return !_stricmp(key,"pos") || !_stricmp(key,"spacing");
    if (!_stricmp(type,"nothitby")) return !_stricmp(key,"time") || !_stricmp(key,"value") || !_stricmp(key,"value2");
    if (!_stricmp(type,"pause")) return !_stricmp(key,"endcmdbuftime") || !_stricmp(key,"pausebg") || !_stricmp(key,"time");
    if (!_stricmp(type,"remappal")) return !_stricmp(key,"dest") || !_stricmp(key,"source");
    if (!_stricmp(type,"reversaldef")) return !_stricmp(key,"hitsound") || !_stricmp(key,"p1sprpriority") || !_stricmp(key,"p1stateno") || !_stricmp(key,"p2sprpriority") || !_stricmp(key,"pausetime") || !_stricmp(key,"reversal.attr") || !_stricmp(key,"sparkno") || !_stricmp(key,"sparkxy");
    if (!_stricmp(type,"screenbound")) return !_stricmp(key,"movecamera") || !_stricmp(key,"value");
    if (!_stricmp(type,"selfstate")) return !_stricmp(key,"value");
    if (!_stricmp(type,"sprpriority")) return !_stricmp(key,"value");
    if (!_stricmp(type,"statetypeset")) return !_stricmp(key,"movetype") || !_stricmp(key,"physics") || !_stricmp(key,"statetype");
    if (!_stricmp(type,"superpause")) return !_stricmp(key,"anim") || !_stricmp(key,"pos") || !_stricmp(key,"poweradd") || !_stricmp(key,"sound");
    if (!_stricmp(type,"targetbind")) return !_stricmp(key,"pos");
    if (!_stricmp(type,"targetfacing")) return !_stricmp(key,"value");
    if (!_stricmp(type,"targetlifeadd")) return !_stricmp(key,"value");
    if (!_stricmp(type,"targetstate")) return !_stricmp(key,"value");
    if (!_stricmp(type,"varrangeset")) return !_stricmp(key,"fvalue") || !_stricmp(key,"value");
    if (!_stricmp(type,"velmul")) return !_stricmp(key,"x") || !_stricmp(key,"y") || !_stricmp(key,"z");
    if (!_stricmp(type,"width")) return !_stricmp(key,"edge") || !_stricmp(key,"value");
    return 0;
}

int Zss_CompileController(const char *source, size_t size, CharData *owner,
                          StateCtrl *out, ZssDiagnostic *diagnostic)
{
    return Zss_CompileControllerCtx(source,size,owner,out,diagnostic,NULL);
}
int Zss_CompileControllerCtx(const char *source, size_t size, CharData *owner,
                             StateCtrl *out, ZssDiagnostic *diagnostic, ExprCompileCtx *ctx)
{
    ZssLexer lex;
    ZssParamBlock block = {0};
    ScriptParam *pairs = block.pairs;
    char name[64];
    StateCtrl controller = {0};
    Ini *ini = NULL;
    const char *message = "Expected a controller name";
    size_t i;
    ExprCompileCtx numeric = {0};
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    memset(&lex, 0, sizeof(lex)); lex.token.line = lex.token.column = 1;
    if (source == NULL || owner == NULL || out == NULL || size > ZSS_MAX_SOURCE) {
        message = "Invalid or oversized controller source"; goto done;
    }
    if (!ZssLex_Init(&lex, source, size, 65536)) { message = lex.error; goto done; }
    if (ZssLex_Next(&lex) != ZSS_IDENT) goto lexical;
    /* The engine's capability table is shared with the runtime context guard. */
    if (lex.token.length >= sizeof(name)) {message="Controller name too long";goto done;}
    memcpy(name, source + lex.token.offset, lex.token.length);
    name[lex.token.length] = 0;
    if (StateCtrl_ScriptType(name)<0) {
        message = "Controller not yet enabled in the ZSS bridge"; goto done;
    }
    message = "Expected '{' after controller name";
    if (ZssLex_Next(&lex) != '{') goto lexical;
    if (!Zss_ReadParamsCtx(&lex, '}', name, &block, diagnostic, ctx)) goto cleanup;
    message = "Unexpected text after controller (no trailing ';')";
    if (ZssLex_Next(&lex) != ZSS_EOF) goto lexical;
    for (i = 1; i < block.count; i++) {
        if (!allowed_key(pairs[0].value, pairs[i].key)) {
            message = "Unknown parameter for enabled ZSS controller"; goto done;
        }
    }
    ini = ScriptParams_ToIni(pairs, block.count, &message);
    if (ini == NULL) goto done;
    /* Locals remain symbolic operands; never snapshot parameter values here. */
    /* Standalone ZSS leaves still use strict ZSS semantics, even without any
     * locals. The parameter reader above keeps rejecting unbound '$' names. */
    if (!StateCtrl_ParseParamsCtx(ini, &controller, owner, ctx!=NULL ? ctx : &numeric)) {
        message = "Controller parameters rejected by MUGEN compiler"; goto done;
    }
    *out = controller;
    Ini_Free(ini); free(block.storage);
    return 1;
lexical:
    if (lex.error != NULL) message = lex.error;
done:
    if (diagnostic != NULL) {
        diagnostic->message = message;
        diagnostic->line = lex.token.line; diagnostic->column = lex.token.column;
    }
cleanup:
    StateCtrl_Free(&controller);
    Ini_Free(ini); free(block.storage);
    return 0;
}
