/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - audio.c
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

#include "audio.h"
#include "cfg.h"
#include "core_interface.h"
#include "main.h"
#include "m64p_types.h"

int audio_volume_now(void)
{
    int v = -1;
    if ((*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_AUDIO_VOLUME, &v) != M64ERR_SUCCESS)
        return -1;
    return v;
}

static int l_vol_initial = -2;       /* the volume the audio plugin started with (-2: not asked yet) */

void audio_apply_volume(void)
{
    int v = cfg_get_int("audio", "volume", -1);
    if (l_vol_initial == -2)
        l_vol_initial = audio_volume_now();
    if (v < 0)
    {
        /* "plugin default": go back to what the plugin started with (after a reset in the menu) */
        v = l_vol_initial;
        if (v < 0 || v == audio_volume_now())
            return;
    }
    if (v > 100)
        v = 100;
    if ((*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_AUDIO_VOLUME, &v) == M64ERR_SUCCESS)
        DebugMessage(M64MSG_INFO, "audio volume set to %d%% (the audio plugin reports %d%%)", v, audio_volume_now());
}
