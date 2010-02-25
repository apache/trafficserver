/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/* varargs declarations: */
# include <stdarg.h>
# define VA_START(f)     va_start(ap, f)
# define VA_SHIFT(v,t)  ;       /* no-op for ANSI */

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "inktomi++.h"


int
ink_vsnprintf(char *str, size_t count, const char *fmt, va_list args)
{
  return vsnprintf(str, count, fmt, args);
}

int
ink_snprintf(char *str, size_t count, const char *fmt, ...)
{
  int len;
  va_list ap;

  VA_START(fmt);
  VA_SHIFT(str, char *);
  VA_SHIFT(count, size_t);
  VA_SHIFT(fmt, char *);
  len = (str && count > 0) ? ink_vsnprintf(str, count, fmt, ap) : 0;

  va_end(ap);
  return (len);
}

int
ink_vsprintf(char *str, const char *fmt, va_list args)
{
  return vsprintf(str, fmt, args);
}

int
ink_sprintf(char *str, const char *fmt, ...)
{
  va_list ap;

  VA_START(fmt);
  VA_SHIFT(str, char *);
  VA_SHIFT(count, size_t);
  VA_SHIFT(fmt, char *);
  int len = str ? ink_vsprintf(str, fmt, ap) : 0;

  va_end(ap);
  return (len);
}

int
ink_vfprintf(FILE * file, const char *fmt, va_list args)
{
  return vfprintf(file, fmt, args);
}

int
ink_fprintf(FILE * file, const char *fmt, ...)
{
  va_list ap;

  VA_START(fmt);
  VA_SHIFT(str, char *);
  VA_SHIFT(count, size_t);
  VA_SHIFT(fmt, char *);
  int len = file ? ink_vfprintf(file, fmt, ap) : 0;

  va_end(ap);
  return (len);
}

int
ink_printf(const char *fmt, ...)
{
  va_list ap;

  VA_START(fmt);
  VA_SHIFT(str, char *);
  VA_SHIFT(count, size_t);
  VA_SHIFT(fmt, char *);
  int len = ink_vfprintf(stdout, fmt, ap);

  va_end(ap);
  return (len);
}
