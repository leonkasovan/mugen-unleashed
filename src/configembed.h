/*
 * Port addition: the platform configuration every target ships.
 *
 * res/configs/ holds the base mugen.cfg and one overlay per platform; both are
 * embedded in the executable (src/configembed.c) and written into data/ when
 * absent. This header owns the mapping from the compiled target to its overlay,
 * which config.c also uses as CONFIG_PORT_PATH for the per-key merge.
 *
 * CONFIG_PORT_PATH can be predefined from the command line, which is how a host
 * test drives the merge; CONFIG_PORT_EMBEDDED is then absent, so only the base
 * file is extracted for that build.
 */
#ifndef MUGEN_CONFIGEMBED_H
#define MUGEN_CONFIGEMBED_H

#ifndef CONFIG_PORT_PATH
#if defined(MUGEN_VITA_GL)
#define CONFIG_PORT_PATH "data/mugen.psvita.cfg"
#define CONFIG_PORT_EMBEDDED g_config_mugen_psvita_cfg
#elif defined(ANDROID_PORT)
#define CONFIG_PORT_PATH "data/mugen.android.cfg"
#define CONFIG_PORT_EMBEDDED g_config_mugen_android_cfg
#elif defined(_WIN64)
#define CONFIG_PORT_PATH "data/mugen.win64.cfg"
#define CONFIG_PORT_EMBEDDED g_config_mugen_win64_cfg
#elif defined(_WIN32)
#define CONFIG_PORT_PATH "data/mugen.win32.cfg"
#define CONFIG_PORT_EMBEDDED g_config_mugen_win32_cfg
#elif defined(__aarch64__) || defined(_M_ARM64)
#define CONFIG_PORT_PATH "data/mugen.linux_arm64.cfg"
#define CONFIG_PORT_EMBEDDED g_config_mugen_linux_arm64_cfg
#elif defined(__x86_64__) || defined(_M_X64)
#define CONFIG_PORT_PATH "data/mugen.linux_x64.cfg"
#define CONFIG_PORT_EMBEDDED g_config_mugen_linux_x64_cfg
#endif
#endif

/* Writes data/mugen.cfg and the platform overlay when each is absent. */
void Config_Embedded_Extract(void);

#endif
