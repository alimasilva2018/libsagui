/*                         _
 *   ___  __ _  __ _ _   _(_)
 *  / __|/ _` |/ _` | | | | |
 *  \__ \ (_| | (_| | |_| | |
 *  |___/\__,_|\__, |\__,_|_|
 *             |___/
 *
 * Cross-platform library which helps to develop web servers or frameworks.
 *
 * Copyright (C) 2016-2019 Silvio Clecio <silvioprog@gmail.com>
 *
 * Sagui library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Sagui library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Sagui library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef SG_MACROS_H
#define SG_MACROS_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef NDEBUG
#include <unistd.h>
#include <errno.h>
#endif
#include "sagui.h"

#ifdef SG_PATH_ROUTING
# ifndef PCRE2_CODE_UNIT_WIDTH
#  define PCRE2_CODE_UNIT_WIDTH 8
# endif
#include "pcre2.h"
#endif

#ifndef SG__EXTERN
# if defined(_WIN32) && defined(BUILD_TESTING)
#  define SG__EXTERN __declspec(dllexport) extern
# else
#  define SG__EXTERN
# endif
#endif

#define _(String) (String) /* macro to make it easy to mark text for translation */

#define xstr(a) str(a) /* stringify the result of expansion of a macro argument */
#define str(a) #a

#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

#ifndef SG__BLOCK_SIZE
#ifdef _WIN32
#define SG__BLOCK_SIZE 16384 /* 16k */
#else
#define SG__BLOCK_SIZE 4096 /* 4k */
#endif
#endif

#ifndef sg__off_t
#ifdef _WIN64
#define sg__off_t off64_t
#else
#define sg__off_t off_t
#endif
#endif

#ifndef sg__lseek
#ifdef _WIN32
#ifdef _WIN64
#define sg__lseek _lseeki64
#else
#define sg__lseek _lseek
#endif
#else
#define sg__lseek lseek
#endif
#endif

#ifndef SG__ZLIB_CHUNK
#define SG__ZLIB_CHUNK 16384 /* 16k */
#endif

/* used by utstring library */
#ifndef utstring_oom
#define utstring_oom() (void) 0
#endif

/* used by uthash library */
#ifndef HASH_NONFATAL_OOM
#define HASH_NONFATAL_OOM 1
#endif
#ifndef uthash_malloc
#define uthash_malloc(sz) sg_malloc(sz)
#endif
#ifndef uthash_free
#define uthash_free(ptr, sz) sg_free(ptr)
#endif

#endif /* SG_MACROS_H */
