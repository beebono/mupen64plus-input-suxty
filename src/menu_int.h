/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - menu_int.h
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* Shared between menu.c (the page stack) and menu_ctl.c (the controller and hotkey pages). */

#ifndef MENU_INT_H
#define MENU_INT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PAGE_MAIN,
    PAGE_STATES,
    PAGE_CONFIRM_OVERWRITE,
    PAGE_CONFIRM_DELETE,
    PAGE_SETTINGS,
    PAGE_CONFIRM_RESET,
    PAGE_CONFIRM_QUIT,
    PAGE_CONTROLS,           /* menu_ctl.c from here on */
    PAGE_PRESETS,
    PAGE_CONFIRM_PRESET,
    PAGE_BUTTONS,
    PAGE_MODLAYER,
    PAGE_CAPTURE,
    PAGE_TEST,
    PAGE_HOTKEYS,
    PAGE_COMBO_CAPTURE,
    PAGE_COMBO_CONFIRM,
    PAGE_CONFIRM_CTL_RESET,
    PAGE_VIDEO,              /* menu_av.c from here on: foir's options, the volume and the game profile */
    PAGE_AUDIO,
    PAGE_PROFILE,
    PAGE_CONFIRM_PROFILE_RESET
} menu_page;

/* "Controls" or "Controls - this game": the edit scope (g_cfg.scope) is part of every settings page's title */
const char *menu_scoped_title(char *buf, size_t size, const char *base);
/* the marker a settings row gets: 1 = this game has its own value (and you are editing the game), 2 = this game has
 * its own value but you are looking at the global one, 0 = none */
int         menu_mark_kind(int overridden);

void      menu_push(menu_page p);
void      menu_pop(void);

/* menu_av.c */
int  av_owns_page(menu_page p);
int  av_video_available(void);        /* foir is the video plugin: the Video page exists */
int  av_audio_available(void);        /* the core can report the volume: the Audio page exists */
void av_frame(menu_page p, int back);
void av_reset(void);                  /* the menu was (re)opened */
void av_leave_page(menu_page p);      /* page `p` was just left */

/* menu_ctl.c */
int  ctl_owns_page(menu_page p);
void ctl_frame(menu_page p, uint32_t pressed, uint32_t held, int back);
void ctl_reset(void);                 /* the menu was (re)opened */
int  ctl_combo_pressed(uint32_t held);   /* the menu combo fired inside the menu; 1 = consumed (a pending change was confirmed) */
void ctl_menu_closing(void);          /* the menu is about to close: revert anything unconfirmed */
void ctl_leave_page(menu_page p);     /* page `p` was just left */

#endif /* MENU_INT_H */
