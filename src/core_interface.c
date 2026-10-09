/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - core_interface.c
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

/* The core functions suxty uses.  The front-end loaded the core and hands us its library handle in PluginStartup(),
 * so this only resolves the function pointers (and never closes the handle). */

#include <stdio.h>
#include <string.h>

#include <dlfcn.h>

#include "core_interface.h"
#include "m64p_common.h"
#include "m64p_config.h"
#include "m64p_frontend.h"
#include "m64p_types.h"
#include "main.h"
#include "version.h"

ptr_CoreDoCommand       CoreDoCommand = NULL;
ptr_CoreOverrideVidExt  CoreOverrideVidExt = NULL;

ptr_ConfigOpenSection      ConfigOpenSection = NULL;
ptr_ConfigGetParamInt      ConfigGetParamInt = NULL;
ptr_ConfigGetParamString   ConfigGetParamString = NULL;
ptr_ConfigGetUserConfigPath     ConfigGetUserConfigPath = NULL;
ptr_ConfigGetUserDataPath       ConfigGetUserDataPath = NULL;

m64p_dynlib_handle CoreHandle = NULL;

#define CORE_FUNC(name) { #name, (void **) &name }
static const struct { const char *name; void **ptr; } l_CoreFuncs[] = {
    CORE_FUNC(CoreDoCommand), CORE_FUNC(CoreOverrideVidExt),
    CORE_FUNC(ConfigOpenSection), CORE_FUNC(ConfigGetParamInt), CORE_FUNC(ConfigGetParamString),
    CORE_FUNC(ConfigGetUserConfigPath), CORE_FUNC(ConfigGetUserDataPath),
};
#define NUM_CORE_FUNCS (sizeof(l_CoreFuncs) / sizeof(l_CoreFuncs[0]))

m64p_error AttachCoreHandle(m64p_dynlib_handle handle)
{
    size_t i;
    ptr_PluginGetVersion get_version;
    int api = 0;

    if (handle == NULL)
        return M64ERR_INPUT_INVALID;
    CoreHandle = handle;

    /* the core is a plugin of type CORE: check its API major version */
    *(void **) (&get_version) = dlsym(handle, "PluginGetVersion");
    if (get_version != NULL)
    {
        m64p_plugin_type type = (m64p_plugin_type) 0;
        (*get_version)(&type, NULL, &api, NULL, NULL);
        if (type != M64PLUGIN_CORE || (api & 0xffff0000) != (CORE_API_VERSION & 0xffff0000))
        {
            DebugMessage(M64MSG_ERROR, "the core's API version %i.%i.%i is not supported (need %i.x.x)",
                         VERSION_PRINTF_SPLIT(api), (CORE_API_VERSION >> 16) & 0xffff);
            CoreHandle = NULL;
            return M64ERR_INCOMPATIBLE;
        }
    }
    for (i = 0; i < NUM_CORE_FUNCS; i++)
    {
        *l_CoreFuncs[i].ptr = dlsym(CoreHandle, l_CoreFuncs[i].name);
        if (*l_CoreFuncs[i].ptr == NULL)
        {
            DebugMessage(M64MSG_ERROR, "the core lacks the function %s()", l_CoreFuncs[i].name);
            DetachCoreLib();
            return M64ERR_INCOMPATIBLE;
        }
    }
    return M64ERR_SUCCESS;
}

void DetachCoreLib(void)
{
    size_t i;
    for (i = 0; i < NUM_CORE_FUNCS; i++)
        *l_CoreFuncs[i].ptr = NULL;
    CoreHandle = NULL;                       /* not ours to dlclose */
}
