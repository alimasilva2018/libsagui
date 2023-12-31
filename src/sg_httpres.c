/*                         _
 *   ___  __ _  __ _ _   _(_)
 *  / __|/ _` |/ _` | | | | |
 *  \__ \ (_| | (_| | |_| | |
 *  |___/\__,_|\__, |\__,_|_|
 *             |___/
 *
 * Cross-platform library which helps to develop web servers or frameworks.
 *
 * Copyright (C) 2016-2021 Silvio Clecio <silvioprog@gmail.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "sg_macros.h"
#ifdef SG_HTTP_COMPRESSION
#include "zlib.h"
#endif /* SG_HTTP_COMPRESSION */
#include "microhttpd.h"
#include "sagui.h"
#include "sg_utils.h"
#include "sg_strmap.h"
#include "sg_extra.h"
#include "sg_httpres.h"

static void sg__httpres_openfile(struct sg_httpres *res, const char *filename,
                                 const char *disposition, uint64_t max_size,
                                 int *fd, struct stat *sbuf, int *errnum) {
  const char *base_name;
  size_t base_name_size;
  char *header;
#ifdef _WIN32
#define SG__HTTPRES_OPENFILE_ERROR(e)                                          \
  do {                                                                         \
    *errnum = (e);                                                             \
    goto error;                                                                \
  } while (0)
  wchar_t *file_name = stow(filename);
#define SG__FILENAME file_name
#else /* _WIN32 */
#define SG__HTTPRES_OPENFILE_ERROR(e)                                          \
  do {                                                                         \
    *errnum = (e);                                                             \
    return;                                                                    \
  } while (0)
#define SG__FILENAME filename
#endif /* _WIN32 */
#ifdef _WIN32
  if (SG__STAT(SG__FILENAME, sbuf))
    SG__HTTPRES_OPENFILE_ERROR(errno);
  if (S_ISDIR(sbuf->st_mode))
    SG__HTTPRES_OPENFILE_ERROR(EISDIR);
#endif /* _WIN32 */
  *fd = SG__OPEN(SG__FILENAME, O_RDONLY);
  if ((*fd == -1) || fstat(*fd, sbuf))
    SG__HTTPRES_OPENFILE_ERROR(errno);
#ifndef _WIN32
  if (S_ISDIR(sbuf->st_mode))
    SG__HTTPRES_OPENFILE_ERROR(EISDIR);
#endif /* _WIN32 */
  if (!S_ISREG(sbuf->st_mode))
    SG__HTTPRES_OPENFILE_ERROR(EBADF);
  if ((max_size > 0) && ((uint64_t) sbuf->st_size > max_size))
    SG__HTTPRES_OPENFILE_ERROR(EFBIG);
  if (disposition) {
#define SG__BASENAME_FMT "%s; filename=\"%s\""
    base_name = sg__basename(filename);
    base_name_size =
      (size_t) snprintf(NULL, 0, SG__BASENAME_FMT, disposition, base_name) + 1;
    header = sg_malloc(base_name_size);
    if (!header)
      SG__HTTPRES_OPENFILE_ERROR(ENOMEM);
    snprintf(header, base_name_size, SG__BASENAME_FMT, disposition, base_name);
#undef SG__BASENAME_FMT /* SG__BASENAME_FMT */
    *errnum =
      sg_strmap_set(&res->headers, MHD_HTTP_HEADER_CONTENT_DISPOSITION, header);
    sg_free(header);
  }
#ifdef _WIN32
error:
  sg_free(SG__FILENAME);
#endif /* _WIN32 */
#undef SG__FILENAME /* SG__FILENAME */
#undef SG__HTTPRES_OPENFILE_ERROR /* SG__HTTPRES_OPENFILE_ERROR */
}

#ifdef SG_HTTP_COMPRESSION

static ssize_t sg__httpres_zread_cb(void *handle, __SG_UNUSED uint64_t offset,
                                    char *mem, size_t size) {
  struct sg__httpres_zholder *holder = handle;
  z_size_t have;
  int flush;
  if (holder->status == SG__HTTPRES_ZFINISHED)
    return MHD_CONTENT_READER_END_OF_STREAM;
  if (holder->status != SG__HTTPRES_ZWRITING) {
    have =
      (z_size_t) holder->read_cb(holder->handle, holder->offset_in, mem, size);
    if (have == (z_size_t) MHD_CONTENT_READER_END_WITH_ERROR)
      return MHD_CONTENT_READER_END_WITH_ERROR;
    if (have == (z_size_t) MHD_CONTENT_READER_END_OF_STREAM) {
      holder->status = SG__HTTPRES_ZFINISHED;
      have = 0;
      flush = Z_FINISH;
    } else {
      holder->offset_in += have;
      if ((holder->size_in > 0) && (holder->offset_in > holder->size_in)) {
        holder->status = SG__HTTPRES_ZFINISHED;
        holder->offset_in -= have;
        have = 0;
        flush = Z_FINISH;
      } else
        flush = Z_NO_FLUSH;
    }
    if (sg__zdeflate(&holder->stream, holder->buf_in, flush, (Bytef *) mem,
                     (uInt) have, &holder->buf_out, &have) != 0)
      return MHD_CONTENT_READER_END_WITH_ERROR;
    if (have > size) {
      holder->status = SG__HTTPRES_ZWRITING;
      holder->size_out = have;
      holder->offset_out = 0;
      return 0;
    } else
      memcpy(mem, holder->buf_out, have);
  } else {
    have = size;
    holder->offset_out += have;
    if (holder->offset_out >= holder->size_out) {
      holder->status = SG__HTTPRES_ZPROCESSING;
      if (holder->offset_out > holder->size_out) {
        holder->offset_out -= have;
        have = holder->size_out - holder->offset_out;
        memcpy(mem, holder->buf_out + holder->offset_out, have);
        goto done;
      }
    }
    memcpy(mem, holder->buf_out + (holder->offset_out - have), have);
  }
done:
  if (holder->status != SG__HTTPRES_ZWRITING)
    sg_free(holder->buf_out);
  return have;
}

static void sg__httpres_zfree_cb(void *handle) {
  struct sg__httpres_zholder *holder = handle;
  if (!holder)
    return;
  deflateEnd(&holder->stream);
  sg_free(holder->buf_in);
  if (holder->free_cb)
    holder->free_cb(holder->handle);
  sg_free(holder);
}

static ssize_t sg__httpres_gzread_cb(void *handle, __SG_UNUSED uint64_t offset,
                                     char *mem, size_t size) {
  struct sg__httpres_gzholder *holder = handle;
  z_size_t have;
  int flush;
  if (holder->status == SG__HTTPRES_GZNONE) {
    holder->status = SG__HTTPRES_GZPROCESSING;
    mem[0] = (char) 0x1f;
    mem[1] = (char) 0x8b;
    mem[2] = (char) 0x08;
    mem[3] = (char) 0x00;
    mem[4] = (char) 0x00;
    mem[5] = (char) 0x00;
    mem[6] = (char) 0x00;
    mem[7] = (char) 0x00;
    mem[8] = (char) 0x00;
#ifdef _WIN32
    mem[9] = (char) 0x0b;
#else /* _WIN32 */
    mem[9] = (char) 0x03;
#endif /* _WIN32 */
    holder->crc = crc32(0L, Z_NULL, 0);
    return 10;
  }
  if (holder->status == SG__HTTPRES_GZFINISHING) {
    holder->status = SG__HTTPRES_GZFINISHED;
    mem[0] = (char) (holder->crc & 0xff);
    mem[1] = (char) ((holder->crc >> 8) & 0xff);
    mem[2] = (char) ((holder->crc >> 16) & 0xff);
    mem[3] = (char) ((holder->crc >> 24) & 0xff);
    mem[4] = (char) ((holder->offset_in) & 0xff);
    mem[5] = (char) ((holder->offset_in >> 8) & 0xff);
    mem[6] = (char) ((holder->offset_in >> 16) & 0xff);
    mem[7] = (char) ((holder->offset_in >> 24) & 0xff);
    return 8;
  }
  if (holder->status == SG__HTTPRES_GZFINISHED)
    return MHD_CONTENT_READER_END_OF_STREAM;
  if (holder->status != SG__HTTPRES_GZWRITING) {
    have = (size_t) read(*holder->handle, mem, size);
    if ((ssize_t) have < 0)
      return MHD_CONTENT_READER_END_WITH_ERROR;
    if (have == 0) {
      holder->status = SG__HTTPRES_GZFINISHING;
      flush = Z_FINISH;
    } else {
      holder->offset_in += have;
      if ((holder->size_in > 0) && (holder->offset_in > holder->size_in)) {
        holder->status = SG__HTTPRES_GZFINISHING;
        holder->offset_in -= have;
        have = 0;
        flush = Z_FINISH;
      } else {
        holder->crc = crc32(holder->crc, (z_const Bytef *) mem, (uInt) have);
        flush = Z_NO_FLUSH;
      }
    }
    if (sg__zdeflate(&holder->stream, holder->buf_in, flush, (Bytef *) mem,
                     (uInt) have, &holder->buf_out, &have) != 0)
      return MHD_CONTENT_READER_END_WITH_ERROR;
    if (have > size) {
      holder->status = SG__HTTPRES_GZWRITING;
      holder->size_out = have;
      holder->offset_out = 0;
      return 0;
    } else
      memcpy(mem, holder->buf_out, have);
  } else {
    have = size;
    holder->offset_out += have;
    if (holder->offset_out >= holder->size_out) {
      holder->status = SG__HTTPRES_GZPROCESSING;
      if (holder->offset_out > holder->size_out) {
        holder->offset_out -= have;
        have = holder->size_out - holder->offset_out;
        memcpy(mem, holder->buf_out + holder->offset_out, have);
        goto done;
      }
    }
    memcpy(mem, holder->buf_out + (holder->offset_out - have), have);
  }
done:
  if (holder->status != SG__HTTPRES_GZWRITING)
    sg_free(holder->buf_out);
  return have;
}

static void sg__httpres_gzfree_cb(void *handle) {
  struct sg__httpres_gzholder *holder = handle;
  if (!holder)
    return;
  deflateEnd(&holder->stream);
  sg_free(holder->buf_in);
  close(*holder->handle);
  sg_free(holder->handle);
  sg_free(holder);
}

#endif /* SG_HTTP_COMPRESSION */

struct sg_httpres *sg__httpres_new(struct MHD_Connection *con) {
  struct sg_httpres *res = sg_alloc(sizeof(struct sg_httpres));
  if (!res)
    return NULL;
  res->con = con;
  res->status = MHD_HTTP_INTERNAL_SERVER_ERROR;
  return res;
}

void sg__httpres_free(struct sg_httpres *res) {
  if (!res)
    return;
  sg_strmap_cleanup(&res->headers);
  MHD_destroy_response(res->handle);
  sg_free(res);
}

int sg__httpres_dispatch(struct sg_httpres *res) {
  sg_strmap_iter(res->headers, sg__strmap_iter, res->handle);
  res->ret = MHD_queue_response(res->con, res->status, res->handle);
  return res->ret;
}

struct sg_strmap **sg_httpres_headers(struct sg_httpres *res) {
  if (res)
    return &res->headers;
  errno = EINVAL;
  return NULL;
}

int sg_httpres_set_cookie(struct sg_httpres *res, const char *name,
                          const char *val) {
  char *str;
  size_t len;
  int ret;
  if (!res || !name || !val || !sg__is_cookie_name(name) ||
      !sg__is_cookie_val(val))
    return EINVAL;
  len = strlen(name) + strlen("=") + strlen(val) + 1;
  str = sg_malloc(len);
  if (!str)
    return ENOMEM;
  snprintf(str, len, "%s=%s", name, val);
  ret = sg_strmap_add(&res->headers, MHD_HTTP_HEADER_SET_COOKIE, str);
  sg_free(str);
  return ret;
}

int sg_httpres_sendbinary(struct sg_httpres *res, void *buf, size_t size,
                          const char *content_type, unsigned int status) {
  int ret;
  if (!res || !buf || ((ssize_t) size < 0) || (status < 100) || (status > 599))
    return EINVAL;
  if (res->handle)
    return EALREADY;
  if (content_type) {
    ret =
      sg_strmap_set(&res->headers, MHD_HTTP_HEADER_CONTENT_TYPE, content_type);
    if (ret != 0)
      return ret;
  }
  res->handle =
    MHD_create_response_from_buffer(size, buf, MHD_RESPMEM_MUST_COPY);
  if (!res->handle)
    return ENOMEM;
  res->status = status;
  return 0;
}

int sg_httpres_sendfile2(struct sg_httpres *res, uint64_t size,
                         uint64_t max_size, uint64_t offset,
                         const char *filename, const char *disposition,
                         unsigned int status) {
  struct stat sbuf;
  int fd, errnum = 0;
  if (!res || ((int64_t) size < 0) || ((int64_t) max_size < 0) ||
      ((int64_t) offset < 0) || !filename || (status < 100) || (status > 599))
    return EINVAL;
  if (res->handle)
    return EALREADY;
  sg__httpres_openfile(res, filename, disposition, max_size, &fd, &sbuf,
                       &errnum);
  if (errnum != 0)
    goto error;
  if (size == 0)
    size = ((uint64_t) sbuf.st_size) - offset;
  res->handle = MHD_create_response_from_fd_at_offset64(size, fd, offset);
  if (!res->handle) {
    errnum = ENOMEM;
    goto error;
  }
  res->status = status;
  return 0;
error:
  if (fd != -1)
    close(fd);
  return errnum;
}

int sg_httpres_sendfile(struct sg_httpres *res, uint64_t size,
                        uint64_t max_size, uint64_t offset,
                        const char *filename, bool downloaded,
                        unsigned int status) {
  return sg_httpres_sendfile2(res, size, max_size, offset, filename,
                              (downloaded ? "attachment" : NULL), status);
}

int sg_httpres_sendstream(struct sg_httpres *res, uint64_t size,
                          sg_read_cb read_cb, void *handle, sg_free_cb free_cb,
                          unsigned int status) {
  int errnum;
  if (!res || ((int64_t) size < 0) || !read_cb || (status < 100) ||
      (status > 599)) {
    errnum = EINVAL;
    goto error;
  }
  if (res->handle) {
    errnum = EALREADY;
    goto error;
  }
  res->handle =
    MHD_create_response_from_callback((size > 0 ? size : MHD_SIZE_UNKNOWN),
                                      SG__BLOCK_SIZE, read_cb, handle, free_cb);
  if (!res->handle)
    return ENOMEM;
  res->status = status;
  return 0;
error:
  if (free_cb)
    free_cb(handle);
  return errnum;
}

#ifdef SG_HTTP_COMPRESSION

int sg_httpres_zsendbinary2(struct sg_httpres *res, int level, void *buf,
                            size_t size, const char *content_type,
                            unsigned int status) {
  size_t zsize;
  void *zbuf;
  int ret;
  if (!res || ((level < -1) || (level > 9)) || !buf || ((ssize_t) size < 0) ||
      (status < 100) || (status > 599))
    return EINVAL;
  if (res->handle)
    return EALREADY;
  if (size > 0) {
    zsize = compressBound(size);
    zbuf = sg_malloc(zsize);
    if (!zbuf)
      return ENOMEM;
    if ((sg__zcompress(buf, size, zbuf, (uLongf *) &zsize, level) != Z_OK) ||
        (zsize >= size)) {
      zsize = size;
      memcpy(zbuf, buf, zsize);
    } else {
      ret = sg_strmap_set(&res->headers, MHD_HTTP_HEADER_CONTENT_ENCODING,
                          "deflate");
      if (ret != 0)
        goto error;
    }
  } else {
    zbuf = strdup("");
    zsize = 0;
  }
  if (content_type) {
    ret =
      sg_strmap_set(&res->headers, MHD_HTTP_HEADER_CONTENT_TYPE, content_type);
    if (ret != 0)
      goto error;
  }
  res->handle =
    MHD_create_response_from_buffer(zsize, zbuf, MHD_RESPMEM_MUST_FREE);
  if (!res->handle)
    return ENOMEM;
  res->status = status;
  return 0;
error:
  sg_free(zbuf);
  return ret;
}

int sg_httpres_zsendbinary(struct sg_httpres *res, void *buf, size_t size,
                           const char *content_type, unsigned int status) {
  return sg_httpres_zsendbinary2(res, Z_BEST_COMPRESSION, buf, size,
                                 content_type, status);
}

int sg_httpres_zsendstream2(struct sg_httpres *res, int level, uint64_t size,
                            sg_read_cb read_cb, void *handle,
                            sg_free_cb free_cb, unsigned int status) {
  struct sg__httpres_zholder *holder;
  int errnum;
  if (!res || ((level < -1) || (level > 9)) || !read_cb ||
      ((int64_t) size < 0) || (status < 100) || (status > 599)) {
    errnum = EINVAL;
    goto error;
  }
  if (res->handle) {
    errnum = EALREADY;
    goto error;
  }
  holder = sg_alloc(sizeof(struct sg__httpres_zholder));
  if (!holder) {
    errnum = ENOMEM;
    goto error;
  }
  holder->stream.zalloc = sg__zalloc;
  holder->stream.zfree = sg__zfree;
  errnum = deflateInit2(&holder->stream, level, Z_DEFLATED, MAX_WBITS,
                        MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
  if (errnum != Z_OK)
    goto error_stream;
  holder->buf_in = sg_malloc(SG__ZLIB_CHUNK);
  if (!holder->buf_in) {
    errnum = ENOMEM;
    goto error_buf_in;
  }
  errnum =
    sg_strmap_set(&res->headers, MHD_HTTP_HEADER_CONTENT_ENCODING, "deflate");
  if (errnum != 0)
    goto error_res;
  holder->read_cb = read_cb;
  holder->free_cb = free_cb;
  holder->handle = handle;
  holder->size_in = size;
  res->handle = MHD_create_response_from_callback(
    MHD_SIZE_UNKNOWN, SG__BLOCK_SIZE, sg__httpres_zread_cb, holder,
    sg__httpres_zfree_cb);
  if (!res->handle) {
    errnum = ENOMEM;
    goto error_res;
  }
  res->status = status;
#ifdef SG_TESTING
  errnum = 0;
#else /* SG_TESTING */
  return 0;
#endif /* SG_TESTING */
error_res:
  sg_free(holder->buf_in);
error_buf_in:
  deflateEnd(&holder->stream);
error_stream:
  sg_free(holder);
error:
  if (free_cb)
    free_cb(handle);
  return errnum;
}

int sg_httpres_zsendstream(struct sg_httpres *res, sg_read_cb read_cb,
                           void *handle, sg_free_cb free_cb,
                           unsigned int status) {
  return sg_httpres_zsendstream2(res, Z_BEST_SPEED, 0, read_cb, handle, free_cb,
                                 status);
}

int sg_httpres_zsendfile2(struct sg_httpres *res, int level, uint64_t size,
                          uint64_t max_size, uint64_t offset,
                          const char *filename, const char *disposition,
                          unsigned int status) {
  struct sg__httpres_gzholder *holder;
  struct stat sbuf;
  int fd, errnum = 0;
  if (!res || ((level < -1) || (level > 9)) || ((int64_t) size < 0) ||
      ((int64_t) max_size < 0) || ((int64_t) offset < 0) || !filename ||
      (status < 100) || (status > 599))
    return EINVAL;
  if (res->handle)
    return EALREADY;
  sg__httpres_openfile(res, filename, disposition, max_size, &fd, &sbuf,
                       &errnum);
  if (errnum != 0)
    goto error;
  if (sg__lseek(fd, offset, SEEK_SET) != (sg__off_t) offset) {
    errnum = errno;
    goto error;
  }
  holder = sg_alloc(sizeof(struct sg__httpres_gzholder));
  if (!holder) {
    errnum = ENOMEM;
    goto error;
  }
  holder->stream.zalloc = sg__zalloc;
  holder->stream.zfree = sg__zfree;
  errnum = deflateInit2(&holder->stream, level, Z_DEFLATED, -MAX_WBITS, 8,
                        Z_DEFAULT_STRATEGY);
  if (errnum != Z_OK)
    goto error_stream;
  holder->buf_in = sg_malloc(SG__ZLIB_CHUNK);
  if (!holder->buf_in) {
    errnum = ENOMEM;
    goto error_buf_in;
  }
  holder->handle = sg_malloc(sizeof(int));
  if (!holder->handle) {
    errnum = ENOMEM;
    goto error_handle;
  }
  errnum =
    sg_strmap_set(&res->headers, MHD_HTTP_HEADER_CONTENT_ENCODING, "gzip");
  if (errnum != 0)
    goto error_res;
  *holder->handle = fd;
  holder->size_in = size;
  res->handle = MHD_create_response_from_callback(
    MHD_SIZE_UNKNOWN, SG__BLOCK_SIZE, sg__httpres_gzread_cb, holder,
    sg__httpres_gzfree_cb);
  if (!res->handle) {
    errnum = ENOMEM;
    goto error_res;
  }
  res->status = status;
#ifdef SG_TESTING
  errnum = 0;
#else /* SG_TESTING */
  return 0;
#endif /* SG_TESTING */
error_res:
  sg_free(holder->handle);
error_handle:
  sg_free(holder->buf_in);
error_buf_in:
  deflateEnd(&holder->stream);
error_stream:
  sg_free(holder);
error:
  if (fd != -1)
    close(fd);
  return errnum;
}

int sg_httpres_zsendfile(struct sg_httpres *res, uint64_t size,
                         uint64_t max_size, uint64_t offset,
                         const char *filename, bool downloaded,
                         unsigned int status) {
  return sg_httpres_zsendfile2(res, Z_BEST_SPEED, size, max_size, offset,
                               filename, (downloaded ? "attachment" : NULL),
                               status);
}

#endif /* SG_HTTP_COMPRESSION */

int sg_httpres_reset(struct sg_httpres *res) {
  if (!res)
    return EINVAL;
  MHD_destroy_response(res->handle);
  res->handle = NULL;
  res->status = MHD_HTTP_INTERNAL_SERVER_ERROR;
  return 0;
}

int sg_httpres_clear(struct sg_httpres *res) {
  int ret = sg_httpres_reset(res);
  if (ret == 0)
    sg_strmap_cleanup(&res->headers);
  return ret;
}

bool sg_httpres_is_empty(struct sg_httpres *res) {
  if (res)
    return !res->handle;
  errno = EINVAL;
  return false;
}
