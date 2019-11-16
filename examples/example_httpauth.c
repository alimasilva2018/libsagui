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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sagui.h>

/* NOTE: Error checking has been omitted to make it clear. */

static bool strmatch(const char *s1, const char *s2) {
  if (!s1 || !s2)
    return false;
  return strcmp(s1, s2) == 0;
}

static bool auth_cb(__SG_UNUSED void *cls, struct sg_httpauth *auth,
                    __SG_UNUSED struct sg_httpreq *req,
                    __SG_UNUSED struct sg_httpres *res) {
  bool pass;
  sg_httpauth_set_realm(auth, "My realm");
  pass = strmatch(sg_httpauth_usr(auth), "abc") &&
         strmatch(sg_httpauth_pwd(auth), "123");
  if (!pass)
    sg_httpauth_deny(auth,
                     "<html><head><title>Denied</title></head><body><font "
                     "color=\"red\">Go away</font></body></html>",
                     "text/html; charset=utf-8");
  return pass;
}

static void err_cb(__SG_UNUSED void *cls, const char *err) {
  fprintf(stderr, "%s", err);
  fflush(stderr);
}

static void req_cb(__SG_UNUSED void *cls, __SG_UNUSED struct sg_httpreq *req,
                   struct sg_httpres *res) {
  sg_httpres_send(res,
                  "<html><head><title>Secret</title></head><body><font "
                  "color=\"green\">Secret page</font></body></html>",
                  "text/html; charset=utf-8", 200);
}

int main(int argc, const char *argv[]) {
  struct sg_httpsrv *srv;
  uint16_t port;
  if (argc != 2) {
    printf("%s <PORT>\n", argv[0]);
    return EXIT_FAILURE;
  }
  port = strtol(argv[1], NULL, 10);
  srv = sg_httpsrv_new2(auth_cb, req_cb, err_cb, NULL);
  if (!sg_httpsrv_listen(srv, port, false)) {
    sg_httpsrv_free(srv);
    return EXIT_FAILURE;
  }
  fprintf(stdout, "Server running at http://localhost:%d\n",
          sg_httpsrv_port(srv));
  fflush(stdout);
  getchar();
  sg_httpsrv_free(srv);
  return EXIT_SUCCESS;
}
