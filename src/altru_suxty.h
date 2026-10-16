/*
 * mupen64plus-audio-altru - altru_suxty.h
 * The optional, one-way interface that the suxty in-game menu (mupen64plus-input-suxty) uses to list and change altru's options.
 * altru never calls back into suxty and behaves identically when nobody asks for the interface. It mirrors foir_suxty.h.
 *
 * Discovery: the shared object exports one function,
 *
 *     const struct altru_suxty_interface *AltruSuxtyGetInterface(void);
 *
 * which returns a pointer to a static, const table (valid for as long as the plugin is loaded). A client must check `abi` before using
 * anything else; `size` allows later versions to append fields to the end of the struct.
 *
 * Every option is a plain int: bool 0/1, an int range, or an enum index (the labels are in the descriptor). See docs/options.md.
 *
 * THREADING. get() and set() must be called on the emulation thread (the thread that calls RomOpen and AiLenChanged), not from inside
 * one of those calls. The suxty menu runs inside the video-extension buffer swap on that thread, so nothing else touches the live
 * options while the menu is open. Once the ROM is open a call from another thread is refused (ALTRU_SUXTY_E_THREAD). Before RomOpen
 * any thread may call, one at a time.
 *
 * Copyright (C) 2026 altru contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ALTRU_SUXTY_H
#define ALTRU_SUXTY_H

#include <stdint.h>

#define ALTRU_SUXTY_ABI 1

enum altru_suxty_type { ALTRU_OPT_BOOL = 0, ALTRU_OPT_INT = 1, ALTRU_OPT_ENUM = 2 };

/* flags */
#define ALTRU_OPTF_LIVE 1u    /* a change takes effect at once; without it the option is read when the ROM is opened */

/* set() results (>= 0) and errors (< 0) */
#define ALTRU_SUXTY_APPLIED      0   /* in effect now */
#define ALTRU_SUXTY_NEXT_LAUNCH  1   /* remembered, but the running game keeps its value; used by the next RomOpen */
#define ALTRU_SUXTY_E_INDEX     (-1) /* no such option */
#define ALTRU_SUXTY_E_RANGE     (-2)
#define ALTRU_SUXTY_E_THREAD    (-3) /* not the emulation thread */
#define ALTRU_SUXTY_E_STATE     (-4) /* plugin not started */
#define ALTRU_SUXTY_E_CONFIG    (-5) /* save_default: the core's config could not be written */

struct altru_suxty_option {
    const char *key;                  /* stable identifier = the parameter name in [Audio-Altru] ("FF_MODE") */
    const char *label;                /* short menu label */
    const char *help;                 /* one line */
    int         type;                 /* enum altru_suxty_type */
    unsigned    flags;                /* ALTRU_OPTF_* */
    int         min, max, step;       /* INT: range and step; ENUM: 0 .. max; BOOL: 0 .. 1 */
    const char *const *enum_labels;   /* ENUM: max + 1 labels */
    int         def;                  /* the built-in default */
};

struct altru_suxty_interface {
    uint32_t abi;                     /* ALTRU_SUXTY_ABI */
    uint32_t size;                    /* sizeof(struct altru_suxty_interface) */
    uint32_t altru_version;           /* ALTRU_PLUGIN_VERSION */
    uint32_t option_count;
    const struct altru_suxty_option *options;    /* option_count descriptors; the index is the option id */

    /* The value in effect: for a running game its running value; before RomOpen what the game will start with
     * (a set() value if there is one, else [Audio-Altru]). 0 = ok. */
    int (*get)(unsigned index, int *value);
    /* Sets a value. Never writes mupen64plus.cfg. Before RomOpen it overrides the config for the coming launch (what suxty does at game
     * start for its per-game values). ALTRU_SUXTY_APPLIED / ALTRU_SUXTY_NEXT_LAUNCH, or an error. */
    int (*set)(unsigned index, int value);
    /* Writes the value to [Audio-Altru] and saves that section, i.e. makes it the user's global default. Does not touch the running
     * game or the values set() holds. */
    int (*save_default)(unsigned index, int value);
};

#ifdef __cplusplus
extern "C" {
#endif
const struct altru_suxty_interface *AltruSuxtyGetInterface(void);
#ifdef __cplusplus
}
#endif

#endif
