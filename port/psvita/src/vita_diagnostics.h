/* Optional on-device tests; native presentation logging is always available. */
#ifndef MUGEN_VITA_DIAGNOSTICS_H
#define MUGEN_VITA_DIAGNOSTICS_H
void VitaDiag_Boot(void);
void VitaDiag_Log(const char *format, ...);
int VitaDiag_CheckPresentation(int width, int height, const char *phase);
int VitaDiag_StartupSmoke(int width, int height);
void VitaDiag_GamePresent(int width, int height);
#endif
