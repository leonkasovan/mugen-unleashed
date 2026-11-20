#ifndef MUGEN_ROLLBACK_H
#define MUGEN_ROLLBACK_H

#ifdef MUGEN_ROLLBACK
int Rollback_ParsePeer(const char *side, const char *local_port, const char *ip, const char *remote_port);
int Rollback_ParseFrames(const char *text);
int Rollback_Requested(void);
int Rollback_Configure(CmdLineArgs *args);
int Rollback_ExitCode(void);
void Rollback_Run(ScreenLoop *screen);
int Rollback_Active(void);
void Rollback_DrawFrame(ScreenLoop *screen);
int Rollback_ApplyInput(Player *player);
uint16_t PlayerInput_ReadHeldBits(const int *keys);
void PlayerInput_ApplyHeldBits(InputBuf *buf, uint16_t buttons);
#endif

#endif
