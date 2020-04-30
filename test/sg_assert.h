/*                         _
 *   ___  __ _  __ _ _   _(_)
 *  / __|/ _` |/ _` | | | | |
 *  \__ \ (_| | (_| | |_| | |
 *  |___/\__,_|\__, |\__,_|_|
 *             |___/
 *
 * Cross-platform library which helps to develop web servers or frameworks.
 *
 * Copyright (C) 2016-2020 Silvio Clecio <silvioprog@gmail.com>
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

#ifndef SG_ASSERT_H
#define SG_ASSERT_H

#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */
#endif /* _WIN32 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#ifdef _WIN32
#define __progname __argv[0]
#else /* _WIN32 */
#if defined(__USE_GNU) && !defined(__i386__) && !defined(__ANDROID__)
#define __progname program_invocation_short_name
#else /* __USE_GNU && !__i386__ && !__ANDROID__ */
extern const char *__progname;
#endif /* __USE_GNU && !__i386__ && !__ANDROID__ */
#endif /* _WIN32 */

#ifdef NDEBUG
#define ASSERT(expr) ((void) 0)
#else /* NDEBUG */
#define ASSERT(expr)                                                           \
  do {                                                                         \
    if (!(expr)) {                                                             \
      if ((fprintf(stderr, "%s: %s:%d: %s: Assertion `%s' failed.\n",          \
                   __progname, __FILE__, __LINE__, __extension__ __FUNCTION__, \
                   #expr) > 0))                                                \
        fflush(stderr);                                                        \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)
#endif /* NDEBUG */

#endif /* SG_ASSERT_H */
