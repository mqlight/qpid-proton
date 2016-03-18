/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <proton/type_compat.h>
#include <ctype.h>
#include <string.h>
#include <proton/error.h>
#include <proton/types.h>
#include "util.h"

ssize_t pn_quote_data(char *dst, size_t capacity, const char *src, size_t size)
{
  int idx = 0;
  for (unsigned i = 0; i < size; i++)
  {
    uint8_t c = src[i];
    if (isprint(c)) {
      if (idx < (int) (capacity - 1)) {
        dst[idx++] = c;
      } else {
        if (idx > 0) {
          dst[idx - 1] = '\0';
        }
        return PN_OVERFLOW;
      }
    } else {
      if (idx < (int) (capacity - 4)) {
        idx += sprintf(dst + idx, "\\x%.2x", c);
      } else {
        if (idx > 0) {
          dst[idx - 1] = '\0';
        }
        return PN_OVERFLOW;
      }
    }
  }

  dst[idx] = '\0';
  return idx;
}

int pn_quote(pn_string_t *dst, const char *src, size_t size)
{
  while (true) {
    size_t str_size = pn_string_size(dst);
    char *str = pn_string_buffer(dst) + str_size;
    size_t capacity = pn_string_capacity(dst) - str_size;
    ssize_t ssize = pn_quote_data(str, capacity, src, size);
    if (ssize == PN_OVERFLOW) {
      int err = pn_string_grow(dst, (str_size + capacity) ? 2*(str_size + capacity) : 16);
      if (err) return err;
    } else if (ssize >= 0) {
      return pn_string_resize(dst, str_size + ssize);
    } else {
      return ssize;
    }
  }
}

void pn_fprint_data(FILE *stream, const char *bytes, size_t size)
{
  char buf[256];
  ssize_t n = pn_quote_data(buf, 256, bytes, size);
  if (n >= 0) {
    fputs(buf, stream);
  } else {
    if (n == PN_OVERFLOW) {
      fputs(buf, stream);
      fputs("... (truncated)", stream);
    }
    else
      fprintf(stderr, "pn_quote_data: %s\n", pn_code(n));
  }
}

void pn_print_data(const char *bytes, size_t size)
{
  pn_fprint_data(stdout, bytes, size);
}

void pni_urldecode(const char *src, char *dst)
{
  const char *in = src;
  char *out = dst;
  while (*in != '\0')
  {
    if ('%' == *in)
    {
      if ((in[1] != '\0') && (in[2] != '\0'))
      {
        char esc[3];
        esc[0] = in[1];
        esc[1] = in[2];
        esc[2] = '\0';
        unsigned long d = strtoul(esc, NULL, 16);
        *out = (char)d;
        in += 3;
        out++;
      }
      else
      {
        *out = *in;
        in++;
        out++;
      }
    }
    else
    {
      *out = *in;
      in++;
      out++;
    }
  }
  *out = '\0';
}

// Parse URL syntax:
// [ <scheme> :// ] [ <user> [ : <password> ] @ ] <host> [ : <port> ] [ / <path> ]
// <scheme>, <user>, <password>, <port> cannot contain any of '@', ':', '/'
// If the first character of <host> is '[' then it can contain any character up to ']' (this is to allow IPv6
// literal syntax). Otherwise it also cannot contain '@', ':', '/'
// <host> is not optional but it can be null! If it is not present an empty string will be returned
// <path> can contain any character
void pni_parse_url(char *url, char **scheme, char **user, char **pass, char **host, char **port, char **path)
{
  if (!url) return;

  char *slash = strchr(url, '/');

  if (slash && slash>url) {
    char *scheme_end = strstr(slash-1, "://");

    if (scheme_end && scheme_end<slash) {
      *scheme_end = '\0';
      *scheme = url;
      url = scheme_end + 3;
      slash = strchr(url, '/');
    }
  }

  if (slash) {
    *slash = '\0';
    *path = slash + 1;
  }

  char *at = strchr(url, '@');
  if (at) {
    *at = '\0';
    char *up = url;
    *user = up;
    url = at + 1;
    char *colon = strchr(up, ':');
    if (colon) {
      *colon = '\0';
      *pass = colon + 1;
    }
  }

  *host = url;
  char *open = (*url == '[') ? url : 0;
  if (open) {
    char *close = strchr(open, ']');
    if (close) {
        *host = open + 1;
        *close = '\0';
        url = close + 1;
    }
  }

  char *colon = strchr(url, ':');
  if (colon) {
    *colon = '\0';
    *port = colon + 1;
  }

  if (*user) pni_urldecode(*user, *user);
  if (*pass) pni_urldecode(*pass, *pass);
}

void pni_vfatal(const char *fmt, va_list ap)
{
  vfprintf(stderr, fmt, ap);
  abort();
}

void pni_fatal(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  pni_vfatal(fmt, ap);
  va_end(ap);
}

int pn_strcasecmp(const char *a, const char *b)
{
  int diff;
  while (*b) {
    diff = tolower(*a++)-tolower(*b++);
    if ( diff!=0 ) return diff;
  }
  return *a;
}

int pn_strncasecmp(const char* a, const char* b, size_t len)
{
  int diff = 0;
  while (*b && len > 0) {
    diff = tolower(*a++)-tolower(*b++);
    if ( diff!=0 ) return diff;
    --len;
  };
  return len==0 ? diff : *a;
}

bool pn_env_bool(const char *name)
{
  char *v = getenv(name);
  return v && (!pn_strcasecmp(v, "true") || !pn_strcasecmp(v, "1") ||
               !pn_strcasecmp(v, "yes")  || !pn_strcasecmp(v, "on"));
}

char *pn_strdup(const char *src)
{
  if (src) {
    char *dest = (char *) malloc((strlen(src)+1)*sizeof(char));
    if (!dest) return NULL;
    return strcpy(dest, src);
  } else {
    return NULL;
  }
}

char *pn_strndup(const char *src, size_t n)
{
  if (src) {
    unsigned size = 0;
    for (const char *c = src; size < n && *c; c++) {
      size++;
    }

    char *dest = (char *) malloc(size + 1);
    if (!dest) return NULL;
    strncpy(dest, src, n);
    dest[size] = '\0';
    return dest;
  } else {
    return NULL;
  }
}

// which timestamp will expire next, or zero if none set
pn_timestamp_t pn_timestamp_min( pn_timestamp_t a, pn_timestamp_t b )
{
  if (a && b) return pn_min(a, b);
  if (a) return a;
  return b;
}

#define PN_DOUBLE_TO_STRING(_val)  char rcString[40];  sprintf(rcString, "%.18g", _val)
#define PN_FLOAT_TO_STRING(_val)   char rcString[30];  sprintf(rcString, "%.12g", _val)
#define PN_INT16_TO_STRING(_val)   char rcString[32];  sprintf(rcString, "%hd", _val)
#define PN_INT32_TO_STRING(_val)   char rcString[64];  sprintf(rcString, "%d", _val)
#if defined(_WIN32)
#define PN_INT64_TO_STRING(_val)   char rcString[128]; sprintf(rcString, "%I64d", _val)
#define PN_UINT64_TO_STRING(_val)  char rcString[128]; sprintf(rcString, "%I64u", _val)
#else
#define PN_INT64_TO_STRING(_val)   char rcString[128]; sprintf(rcString, "%ld", _val)
#define PN_UINT64_TO_STRING(_val)  char rcString[128]; sprintf(rcString, "%lu", _val)
#endif
#define PN_POINTER_TO_STRING(_val) char rcString[32];  sprintf(rcString, "0x%p", _val)
#define PN_UINT16_TO_STRING(_val)  char rcString[32];  sprintf(rcString, "%hu", _val)
#define PN_UINT32_TO_STRING(_val)  char rcString[64];  sprintf(rcString, "%u", _val)

static pn_fnc_tracer_t pn_fnc_entry_tracer = NULL;
static pn_fnc_tracer_t pn_fnc_data_tracer  = NULL;
static pn_fnc_tracer_t pn_fnc_exit_tracer  = NULL;

void pn_set_fnc_entry_tracer(pn_fnc_tracer_t tracer)
{
  pn_fnc_entry_tracer = tracer;
}

void pn_set_fnc_data_tracer(pn_fnc_tracer_t tracer)
{
  pn_fnc_data_tracer = tracer;
}

void pn_set_fnc_exit_tracer(pn_fnc_tracer_t tracer)
{
  pn_fnc_exit_tracer = tracer;
}

void pn_fnc_entry(const char* name)
{
  if(pn_fnc_entry_tracer)
    pn_fnc_entry_tracer(name, NULL);
}

static void pn_fnc_data(const char* prefix, char* data)
{
  if(pn_fnc_data_tracer)
    pn_fnc_data_tracer(prefix, data);
}

void pn_fnc_data_string(const char* prefix, char* data)
{
  char rcString[17] = "<null>";
  if (data)
  {
    strncpy(rcString, data, 16);
    rcString[16] = 0;
  }

  pn_fnc_data(prefix, rcString);
}

void pn_fnc_data_bool(const char* prefix, bool data)
{
  pn_fnc_data(prefix, data ? (char*)"true" : (char*)"false");
}

void pn_fnc_data_double(const char* prefix, double data)
{
  PN_DOUBLE_TO_STRING(data);
  pn_fnc_data(prefix, rcString);
}

void pn_fnc_data_float(const char* prefix, float data)
{
  PN_FLOAT_TO_STRING(data);
  pn_fnc_data(prefix, rcString);
}

void pn_fnc_data_int8_t(const char* prefix, int8_t data)
{
  pn_fnc_data_int32_t(prefix, (int32_t)data);
}

void pn_fnc_data_int16_t(const char* prefix, int16_t data)
{
  PN_INT16_TO_STRING(data);
  pn_fnc_data(prefix, rcString);
}

void pn_fnc_data_int32_t(const char* prefix, int32_t data)
{
  PN_INT32_TO_STRING(data);
  pn_fnc_data(prefix, rcString);
}

void pn_fnc_data_int64_t(const char* prefix, int64_t data)
{
  PN_INT64_TO_STRING(data);
  pn_fnc_data(prefix, rcString);
}

void pn_fnc_data_pointer(const char* prefix, void* data)
{
  PN_POINTER_TO_STRING(data);
  pn_fnc_data(prefix, rcString);
}

void pn_fnc_data_uint8_t(const char* prefix, uint8_t data)
{
  pn_fnc_data_uint32_t(prefix, (uint32_t)data);
}

void pn_fnc_data_uint16_t(const char* prefix, uint16_t data)
{
  PN_UINT16_TO_STRING(data);
  pn_fnc_data(prefix, rcString);
}

void pn_fnc_data_uint32_t(const char* prefix, uint32_t data)
{
  PN_UINT32_TO_STRING(data);
  pn_fnc_data(prefix, rcString);
}

void pn_fnc_data_uint64_t(const char* prefix, uint64_t data)
{
  PN_UINT64_TO_STRING(data);
  pn_fnc_data(prefix, rcString);
}

char* pn_fnc_exit_string(const char* name, char* rc)
{
  if(pn_fnc_exit_tracer)
    pn_fnc_exit_tracer(name, rc);
  return rc;
}

const char* pn_fnc_exit_const_string(const char* name, const char* rc)
{
  pn_fnc_exit_string(name, (char*)rc);
  return rc;
}

bool pn_fnc_exit_bool(const char* name, bool rc)
{
  pn_fnc_exit_const_string(name, rc ? "true" : "false");
  return rc;
}

double pn_fnc_exit_double(const char* name, double rc)
{
  PN_DOUBLE_TO_STRING(rc);
  pn_fnc_exit_string(name, rcString);
  return rc;
}

float pn_fnc_exit_float(const char* name, float rc)
{
  PN_FLOAT_TO_STRING(rc);
  pn_fnc_exit_string(name, rcString);
  return rc;
}

int8_t pn_fnc_exit_int8_t(const char* name, int8_t rc)
{
  pn_fnc_exit_int32_t(name, (int32_t)rc);
  return rc;
}

int16_t pn_fnc_exit_int16_t(const char* name, int16_t rc)
{
  PN_INT16_TO_STRING(rc);
  pn_fnc_exit_string(name, rcString);
  return rc;
}

int32_t pn_fnc_exit_int32_t(const char* name, int32_t rc)
{
  PN_INT32_TO_STRING(rc);
  pn_fnc_exit_string(name, rcString);
  return rc;
}

int64_t pn_fnc_exit_int64_t(const char* name, int64_t rc)
{
  PN_INT64_TO_STRING(rc);
  pn_fnc_exit_string(name, rcString);
  return rc;
}

void* pn_fnc_exit_pointer(const char* name, void* rc)
{
  PN_POINTER_TO_STRING(rc);
  pn_fnc_exit_string(name, rcString);
  return rc;
}

const void* pn_fnc_exit_const_pointer(const char* name, const void* rc)
{
  pn_fnc_exit_pointer(name, (void*)rc);
  return rc;
}

uint8_t pn_fnc_exit_uint8_t(const char* name, uint8_t rc)
{
  pn_fnc_exit_uint32_t(name, (uint32_t)rc);
  return rc;
}

uint16_t pn_fnc_exit_uint16_t(const char* name, uint16_t rc)
{
  PN_UINT16_TO_STRING(rc);
  pn_fnc_exit_string(name, rcString);
  return rc;
}

uint32_t pn_fnc_exit_uint32_t(const char* name, uint32_t rc)
{
  PN_UINT32_TO_STRING(rc);
  pn_fnc_exit_string(name, rcString);
  return rc;
}

uint64_t pn_fnc_exit_uint64_t(const char* name, uint64_t rc)
{
  PN_UINT64_TO_STRING(rc);
  pn_fnc_exit_string(name, rcString);
  return rc;
}

void pn_fnc_exit_void(const char* name)
{
  pn_fnc_exit_const_string(name, "");
}

