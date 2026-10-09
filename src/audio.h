/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - audio.h
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

/* The one audio option suxty offers: the core's own volume (M64CORE_AUDIO_VOLUME), which every audio plugin implements.
 * [audio] volume = 0-100 in suxty.cfg or the game file; absent means whatever the audio plugin starts with. */

#ifndef AUDIO_H
#define AUDIO_H

int  audio_volume_now(void);          /* 0-100, -1 if the core / audio plugin can't tell (the menu then hides the row) */
void audio_apply_volume(void);        /* the configured volume (this game's, else the global one) -> the core */

#endif /* AUDIO_H */
