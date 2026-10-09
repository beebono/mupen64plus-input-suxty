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


/* This file contains the routines for attaching to the Mupen64Plus core
 * library and pointers to the core functions.  It is a trimmed-down version
 * of the one in mupen64plus-ui-console (suxty needs no debugger functions).
 */

#include <stdio.h>
#include <string.h>

#include "core_interface.h"
#include "m64p_common.h"
#include "m64p_config.h"
#include "m64p_frontend.h"
#include "m64p_types.h"
#include "main.h"
#include "osal_dynamiclib.h"
#include "osal_preproc.h"
#include "version.h"

/* global data definitions */
int g_CoreCapabilities;
int g_CoreAPIVersion;

/* definitions of pointers to Core common functions */
ptr_CoreErrorMessage    CoreErrorMessage = NULL;

/* definitions of pointers to Core front-end functions */
ptr_CoreStartup         CoreStartup = NULL;
ptr_CoreShutdown        CoreShutdown = NULL;
ptr_CoreAttachPlugin    CoreAttachPlugin = NULL;
ptr_CoreDetachPlugin    CoreDetachPlugin = NULL;
ptr_CoreDoCommand       CoreDoCommand = NULL;
ptr_CoreOverrideVidExt  CoreOverrideVidExt = NULL;

/* definitions of pointers to Core config functions */
ptr_ConfigListSections     ConfigListSections = NULL;
ptr_ConfigOpenSection      ConfigOpenSection = NULL;
ptr_ConfigDeleteSection    ConfigDeleteSection = NULL;
ptr_ConfigSaveSection      ConfigSaveSection = NULL;
ptr_ConfigListParameters   ConfigListParameters = NULL;
ptr_ConfigSaveFile         ConfigSaveFile = NULL;
ptr_ConfigSetParameter     ConfigSetParameter = NULL;
ptr_ConfigGetParameter     ConfigGetParameter = NULL;
ptr_ConfigGetParameterType ConfigGetParameterType = NULL;
ptr_ConfigGetParameterHelp ConfigGetParameterHelp = NULL;
ptr_ConfigSetDefaultInt    ConfigSetDefaultInt = NULL;
ptr_ConfigSetDefaultFloat  ConfigSetDefaultFloat = NULL;
ptr_ConfigSetDefaultBool   ConfigSetDefaultBool = NULL;
ptr_ConfigSetDefaultString ConfigSetDefaultString = NULL;
ptr_ConfigGetParamInt      ConfigGetParamInt = NULL;
ptr_ConfigGetParamFloat    ConfigGetParamFloat = NULL;
ptr_ConfigGetParamBool     ConfigGetParamBool = NULL;
ptr_ConfigGetParamString   ConfigGetParamString = NULL;

ptr_ConfigHasUnsavedChanges    ConfigHasUnsavedChanges = NULL;

ptr_ConfigGetSharedDataFilepath ConfigGetSharedDataFilepath = NULL;
ptr_ConfigGetUserConfigPath     ConfigGetUserConfigPath = NULL;
ptr_ConfigGetUserDataPath       ConfigGetUserDataPath = NULL;
ptr_ConfigGetUserCachePath      ConfigGetUserCachePath = NULL;

/* global variables */
m64p_dynlib_handle CoreHandle = NULL;
static int l_core_borrowed;            /* SUXTY_PLUGIN: the handle belongs to the front-end; never dlclose it */

/* The set of functions suxty cannot work without.  Table-driven so attach and
 * detach cannot get out of sync. */
#define CORE_FUNC(name) { #name, (void **) &name }
static const struct { const char *name; void **ptr; } l_CoreFuncs[] = {
    CORE_FUNC(CoreErrorMessage),
    CORE_FUNC(CoreStartup), CORE_FUNC(CoreShutdown), CORE_FUNC(CoreAttachPlugin),
    CORE_FUNC(CoreDetachPlugin), CORE_FUNC(CoreDoCommand), CORE_FUNC(CoreOverrideVidExt),
    CORE_FUNC(ConfigListSections), CORE_FUNC(ConfigOpenSection), CORE_FUNC(ConfigDeleteSection),
    CORE_FUNC(ConfigSaveSection), CORE_FUNC(ConfigListParameters), CORE_FUNC(ConfigSaveFile),
    CORE_FUNC(ConfigSetParameter), CORE_FUNC(ConfigGetParameter), CORE_FUNC(ConfigGetParameterType),
    CORE_FUNC(ConfigGetParameterHelp), CORE_FUNC(ConfigSetDefaultInt), CORE_FUNC(ConfigSetDefaultFloat),
    CORE_FUNC(ConfigSetDefaultBool), CORE_FUNC(ConfigSetDefaultString), CORE_FUNC(ConfigGetParamInt),
    CORE_FUNC(ConfigGetParamFloat), CORE_FUNC(ConfigGetParamBool), CORE_FUNC(ConfigGetParamString),
    CORE_FUNC(ConfigHasUnsavedChanges), CORE_FUNC(ConfigGetSharedDataFilepath),
    CORE_FUNC(ConfigGetUserConfigPath), CORE_FUNC(ConfigGetUserDataPath), CORE_FUNC(ConfigGetUserCachePath),
};
#define NUM_CORE_FUNCS (sizeof(l_CoreFuncs) / sizeof(l_CoreFuncs[0]))

/* functions */
static m64p_error resolve_core_funcs(void)
{
    size_t i;
    for (i = 0; i < NUM_CORE_FUNCS; i++)
    {
        *l_CoreFuncs[i].ptr = osal_dynlib_getproc(CoreHandle, l_CoreFuncs[i].name);
        if (*l_CoreFuncs[i].ptr == NULL)
        {
            DebugMessage(M64MSG_ERROR, "AttachCoreLib() Error: core library lacks required function %s()", l_CoreFuncs[i].name);
            return M64ERR_INCOMPATIBLE;
        }
    }
    return M64ERR_SUCCESS;
}

/* Plugin mode: the front-end loaded the core and hands us its handle in PluginStartup(). */
m64p_error AttachCoreHandle(m64p_dynlib_handle handle)
{
    if (handle == NULL)
        return M64ERR_INPUT_INVALID;
    CoreHandle = handle;
    l_core_borrowed = 1;
    if (resolve_core_funcs() != M64ERR_SUCCESS)
    {
        DetachCoreLib();
        return M64ERR_INCOMPATIBLE;
    }
    return M64ERR_SUCCESS;
}

m64p_error AttachCoreLib(const char *CoreLibFilepath)
{
    /* check if Core DLL is already attached */
    if (CoreHandle != NULL)
        return M64ERR_INVALID_STATE;

    /* load the DLL */
    m64p_error rval = M64ERR_INTERNAL;
    /* first, try a library path+name that was given on the command-line */
    if (CoreLibFilepath != NULL)
    {
        rval = osal_dynlib_open(&CoreHandle, CoreLibFilepath);
    }
    /* then try a library path that was given at compile time */
#if defined(COREDIR)
    if (rval != M64ERR_SUCCESS || CoreHandle == NULL)
    {
        rval = osal_dynlib_open(&CoreHandle, COREDIR OSAL_DEFAULT_DYNLIB_FILENAME);
    }
#endif
    /* then try just the filename of the shared library, to let dlopen() look through the system lib dirs */
    if (rval != M64ERR_SUCCESS || CoreHandle == NULL)
    {
        rval = osal_dynlib_open(&CoreHandle, OSAL_DEFAULT_DYNLIB_FILENAME);
    }
    /* as a last-ditch effort, try loading library in current directory */
    if (rval != M64ERR_SUCCESS || CoreHandle == NULL)
    {
        rval = osal_dynlib_open(&CoreHandle, OSAL_CURRENT_DIR OSAL_DEFAULT_DYNLIB_FILENAME);
    }
    /* if we haven't found a good core library by now, then we're screwed */
    if (rval != M64ERR_SUCCESS || CoreHandle == NULL)
    {
        DebugMessage(M64MSG_ERROR, "AttachCoreLib() Error: failed to find Mupen64Plus Core library");
        CoreHandle = NULL;
        return M64ERR_INPUT_NOT_FOUND;
    }

    /* attach and call the PluginGetVersion function, check the Core and API versions for compatibility with this front-end */
    ptr_PluginGetVersion CoreVersionFunc;
    CoreVersionFunc = (ptr_PluginGetVersion) osal_dynlib_getproc(CoreHandle, "PluginGetVersion");
    if (CoreVersionFunc == NULL)
    {
        DebugMessage(M64MSG_ERROR, "AttachCoreLib() Error: Shared library '%s' invalid; no PluginGetVersion() function found.", CoreLibFilepath ? CoreLibFilepath : "(default)");
        osal_dynlib_close(CoreHandle);
        CoreHandle = NULL;
        return M64ERR_INPUT_INVALID;
    }
    m64p_plugin_type PluginType = (m64p_plugin_type) 0;
    int Compatible = 0;
    int CoreVersion = 0;
    const char *CoreName = NULL;
    (*CoreVersionFunc)(&PluginType, &CoreVersion, &g_CoreAPIVersion, &CoreName, &g_CoreCapabilities);
    if (PluginType != M64PLUGIN_CORE)
        DebugMessage(M64MSG_ERROR, "AttachCoreLib() Error: Shared library is invalid; this is not the emulator core.");
    else if (CoreVersion < MINIMUM_CORE_VERSION)
        DebugMessage(M64MSG_ERROR, "AttachCoreLib() Error: core version %i.%i.%i is below minimum supported %i.%i.%i",
                VERSION_PRINTF_SPLIT(CoreVersion), VERSION_PRINTF_SPLIT(MINIMUM_CORE_VERSION));
    else if ((g_CoreAPIVersion & 0xffff0000) != (CORE_API_VERSION & 0xffff0000))
        DebugMessage(M64MSG_ERROR, "AttachCoreLib() Error: core API major version %i.%i.%i doesn't match with this application (%i.%i.%i)",
                VERSION_PRINTF_SPLIT(g_CoreAPIVersion), VERSION_PRINTF_SPLIT(CORE_API_VERSION));
    else
        Compatible = 1;
    /* exit if not compatible */
    if (Compatible == 0)
    {
        osal_dynlib_close(CoreHandle);
        CoreHandle = NULL;
        return M64ERR_INCOMPATIBLE;
    }

    /* attach and call the CoreGetAPIVersion function, check Config API version for compatibility */
    ptr_CoreGetAPIVersions CoreAPIVersionFunc;
    CoreAPIVersionFunc = (ptr_CoreGetAPIVersions) osal_dynlib_getproc(CoreHandle, "CoreGetAPIVersions");
    if (CoreAPIVersionFunc == NULL)
    {
        DebugMessage(M64MSG_ERROR, "AttachCoreLib() Error: Library broken; no CoreGetAPIVersions() function found.");
        osal_dynlib_close(CoreHandle);
        CoreHandle = NULL;
        return M64ERR_INPUT_INVALID;
    }
    int ConfigAPIVersion, DebugAPIVersion, VidextAPIVersion;
    (*CoreAPIVersionFunc)(&ConfigAPIVersion, &DebugAPIVersion, &VidextAPIVersion, NULL);
    if ((ConfigAPIVersion & 0xffff0000) != (CONFIG_API_VERSION & 0xffff0000) || ConfigAPIVersion < CONFIG_API_VERSION)
    {
        DebugMessage(M64MSG_ERROR, "AttachCoreLib() Error: Emulator core incompatible; Config API version %i.%i.%i doesn't match application: %i.%i.%i",
                VERSION_PRINTF_SPLIT(ConfigAPIVersion), VERSION_PRINTF_SPLIT(CONFIG_API_VERSION));
        osal_dynlib_close(CoreHandle);
        CoreHandle = NULL;
        return M64ERR_INCOMPATIBLE;
    }
    /* print some information about the core library */
    DebugMessage(M64MSG_INFO, "attached to core library '%s' version %i.%i.%i", CoreName, VERSION_PRINTF_SPLIT(CoreVersion));
    if (g_CoreCapabilities & M64CAPS_DYNAREC)
        DebugMessage(M64MSG_INFO, "            Includes support for Dynamic Recompiler.");

    /* get function pointers to the common, front-end and config functions */
    if (resolve_core_funcs() != M64ERR_SUCCESS)
    {
        DetachCoreLib();
        return M64ERR_INCOMPATIBLE;
    }

    return M64ERR_SUCCESS;
}

m64p_error DetachCoreLib(void)
{
    size_t i;

    if (CoreHandle == NULL)
        return M64ERR_INVALID_STATE;

    /* set the core function pointers to NULL */
    for (i = 0; i < NUM_CORE_FUNCS; i++)
        *l_CoreFuncs[i].ptr = NULL;

    /* detach the shared library (not one that the front-end owns) */
    if (!l_core_borrowed)
        osal_dynlib_close(CoreHandle);
    l_core_borrowed = 0;
    CoreHandle = NULL;

    return M64ERR_SUCCESS;
}
