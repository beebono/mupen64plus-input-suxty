/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - osal_files.h                              *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2009 Richard Goedeken                                   *
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

#if !defined(OSAL_FILES_H)
#define OSAL_FILES_H

#include "m64p_types.h"
#include "osal_preproc.h"

#if defined(WIN32) && !defined(__MINGW32__)
  #include <io.h>

  #define unlink _unlink
  #define snprintf _snprintf
  #define strdup _strdup

  #define OSAL_DIR_SEPARATORS           "\\/"
#else  /* Not WIN32 */
  #include <unistd.h>

  #define OSAL_DIR_SEPARATORS           "/"
#endif

typedef struct _osal_lib_search {
  char                     filepath[PATH_MAX];
  char                    *filename;
  m64p_plugin_type         plugin_type;
  struct _osal_lib_search *next;
  } osal_lib_search;

extern const int   osal_libsearchdirs;
extern const char *osal_libsearchpath[];

extern osal_lib_search *osal_library_search(const char *searchpath);
extern void             osal_free_lib_list(osal_lib_search *head);

extern int osal_mkdir_p(const char *path);
extern int osal_file_exists(const char *path);

#endif /* #define OSAL_FILES_H */

