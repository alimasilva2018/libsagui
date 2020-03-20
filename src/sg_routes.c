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

#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include "sg_macros.h"
#include "utlist.h"
#ifdef _WIN32
#include "sg_utils.h"
#endif
#include "sg_routes.h"
#include "sg_utils.h"
#include "sagui.h"

static void sg__route_free(struct sg_route *route);

static struct sg_route *sg__route_new(const char *pattern, char *errmsg,
                                      size_t errlen, int *errnum,
                                      sg_route_cb cb, void *cls) {
  struct sg_route *route;
  PCRE2_UCHAR err[SG_ERR_SIZE >> 1];
  size_t off;
  *errnum = 0;
  if (strstr(pattern, "\\K")) {
    strncpy(errmsg, _("\\K is not not allowed.\n"), errlen);
    *errnum = EINVAL;
    return NULL;
  }
  route = sg_alloc(sizeof(struct sg_route));
  if (!route) {
    *errnum = ENOMEM;
    return NULL;
  }
  off = strlen(pattern) + 3;
  route->pattern = sg_malloc(off);
  if (!route->pattern) {
    *errnum = ENOMEM;
    goto error;
  }
  snprintf(route->pattern, off, ((*pattern == '(') ? "%s" : "^%s$"), pattern);
  route->re = pcre2_compile((PCRE2_SPTR) route->pattern, PCRE2_ZERO_TERMINATED,
                            PCRE2_CASELESS, errnum, &off, NULL);
  if (!route->re) {
    pcre2_get_error_message(*errnum, err, sizeof(err));
    snprintf(errmsg, errlen,
             _("Pattern compilation failed at offset %d: %s.\n"),
             (unsigned int) off, err);
    *errnum = EINVAL;
    goto error;
  }
#ifdef PCRE2_JIT_SUPPORT
  *errnum = pcre2_jit_compile(route->re, PCRE2_JIT_COMPLETE);
  if (*errnum < 0) {
    pcre2_get_error_message(*errnum, err, sizeof(err));
    snprintf(errmsg, errlen, _("JIT compilation failed: %s.\n"), err);
    *errnum = EINVAL;
    goto error;
  }
#endif
  route->match = pcre2_match_data_create_from_pattern(route->re, NULL);
  if (!route->match) {
    strncpy(errmsg, _("Cannot allocate match data from the pattern.\n"),
            errlen);
    *errnum = EINVAL;
    goto error;
  }
  route->cb = cb;
  route->cls = cls;
  return route;
error:
  sg__route_free(route);
  return NULL;
}

static void sg__route_free(struct sg_route *route) {
  pcre2_match_data_free(route->match);
  pcre2_code_free(route->re);
  sg_free(route->pattern);
  sg_free(route);
}

void *sg_route_handle(struct sg_route *route) {
  if (route)
    return route->re;
  errno = EINVAL;
  return NULL;
}

void *sg_route_match(struct sg_route *route) {
  if (route)
    return route->match;
  errno = EINVAL;
  return NULL;
}

const char *sg_route_rawpattern(struct sg_route *route) {
  if (route)
    return route->pattern;
  errno = EINVAL;
  return NULL;
}

char *sg_route_pattern(struct sg_route *route) {
  char *pattern;
  if (!route) {
    errno = EINVAL;
    return NULL;
  }
  if (!route->pattern) {
    errno = 0;
    return NULL;
  }
  pattern = strndup(route->pattern + 1, strlen(route->pattern) - 2);
  if (!pattern) {
    errno = ENOMEM;
    return NULL;
  }
  return pattern;
}

const char *sg_route_path(struct sg_route *route) {
  if (route)
    return route->path;
  errno = EINVAL;
  return NULL;
}

int sg_route_segments_iter(struct sg_route *route, sg_segments_iter_cb cb,
                           void *cls) {
  char *segment;
  size_t off;
  int r;
  if (!route || !cb)
    return EINVAL;
  if (route->rc < 0)
    return 0;
  route->ovector = pcre2_get_ovector_pointer(route->match);
  for (int i = 1; i < route->rc; i++) {
    r = i << 1;
    off = route->ovector[r];
    segment = strdup(route->path + off);
    if (!segment)
      return ENOMEM;
    segment[route->ovector[r + 1] - off] = '\0';
    r = cb(cls, (unsigned int) i - 1, segment);
    sg_free(segment);
    if (r != 0)
      return r;
  }
  return 0;
}

int sg_route_vars_iter(struct sg_route *route, sg_vars_iter_cb cb, void *cls) {
  PCRE2_SPTR tbl, rec;
  char *val;
  size_t off;
  uint32_t cnt, len;
  int n, r;
  if (!route || !cb)
    return EINVAL;
  if (route->rc < 0)
    return 0;
  route->ovector = pcre2_get_ovector_pointer(route->match);
  pcre2_pattern_info(route->re, PCRE2_INFO_NAMECOUNT, &cnt);
  if (cnt == 0)
    return 0;
  pcre2_pattern_info(route->re, PCRE2_INFO_NAMETABLE, &tbl);
  pcre2_pattern_info(route->re, PCRE2_INFO_NAMEENTRYSIZE, &len);
  rec = tbl;
  for (uint32_t i = 0; i < cnt; i++) {
    n = (rec[0] << 8) | rec[1];
    r = n << 1;
    off = route->ovector[r];
    val = strndup(route->path + off, route->ovector[r + 1] - off);
    if (!val)
      return ENOMEM;
    r = cb(cls, (const char *) rec + 2, val);
    sg_free(val);
    if (r != 0)
      return r;
    rec += len;
  }
  return 0;
}

void *sg_route_user_data(struct sg_route *route) {
  if (route)
    return route->user_data;
  errno = EINVAL;
  return NULL;
}

int sg_routes_add2(struct sg_route **routes, struct sg_route **route,
                   const char *pattern, char *errmsg, size_t errlen,
                   sg_route_cb cb, void *cls) {
  int errnum;
  if (!routes || !route || !pattern || !errmsg || (errlen < 1) || !cb)
    return EINVAL;
  LL_FOREACH(*routes, *route) {
    if (strncmp(pattern, (*route)->pattern + 1, strlen(pattern)) == 0)
      return EALREADY;
  }
  *route = sg__route_new(pattern, errmsg, errlen, &errnum, cb, cls);
  if (errnum != 0)
    return errnum;
  LL_APPEND(*routes, *route);
  return 0;
}

bool sg_routes_add(struct sg_route **routes, const char *pattern,
                   sg_route_cb cb, void *cls) {
  struct sg_route *route;
  char err[SG_ERR_SIZE];
  int ret = sg_routes_add2(routes, &route, pattern, err, sizeof(err), cb, cls);
  if (ret == 0)
    return true;
#ifndef SG_TESTING
  if (ret == EINVAL || ret == EALREADY)
    sg_strerror(ret, err, sizeof(err));
  sg__err_cb(NULL, err);
#endif
  errno = ret;
  return false;
}

int sg_routes_rm(struct sg_route **routes, const char *pattern) {
  struct sg_route *route, *tmp;
  if (!routes || !pattern)
    return EINVAL;
  LL_FOREACH_SAFE(*routes, route, tmp) {
    if (strncmp(pattern, route->pattern + 1, strlen(pattern)) != 0)
      continue;
    LL_DELETE(*routes, route);
    sg__route_free(route);
    return 0;
  }
  return ENOENT;
}

int sg_routes_iter(struct sg_route *routes, sg_routes_iter_cb cb, void *cls) {
  struct sg_route *tmp;
  int ret;
  if (!cb)
    return EINVAL;
  if (routes)
    LL_FOREACH(routes, tmp) {
      ret = cb(cls, tmp);
      if (ret != 0)
        return ret;
    }
  return 0;
}

int sg_routes_next(struct sg_route **route) {
  if (!route)
    return EINVAL;
  *route = (*route) ? (*route)->next : NULL;
  return 0;
}

unsigned int sg_routes_count(struct sg_route *routes) {
  struct sg_route *tmp;
  unsigned int count = 0;
  if (routes)
    LL_COUNT(routes, tmp, count);
  return count;
}

int sg_routes_cleanup(struct sg_route **routes) {
  struct sg_route *route, *tmp;
  if (!routes)
    return EINVAL;
  LL_FOREACH_SAFE(*routes, route, tmp) {
    LL_DELETE(*routes, route);
    sg__route_free(route);
  }
  *routes = NULL;
  return 0;
}
