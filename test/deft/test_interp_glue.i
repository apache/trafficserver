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

% module TestExec % {
#include "test_interp_glue.h"
%}

%include pointer.i % typemap(perl5, in)
char **
{
  AV *
    av = SvRV($input);
  char **
    result = NULL;

  if (SvTYPE(av) == SVt_PVAV)
{
int array_len = av_len(av);

if (array_len < 0)
{
  result = NULL;
} else
{
  int i;
  STRLEN str_len;
  char *str;
  SV **cur_sv;
  result = (char **) malloc((sizeof(char *) * (array_len + 2)));

  for (i = 0; i <= array_len; i++) {
    cur_sv = av_fetch(av, i, 0);
    str = SvPV(*cur_sv, str_len);
    result[i] = (char *) malloc(str_len + 1);
    strcpy(result[i], str);
  }

  result[array_len + 1] = NULL;
}
  } else {
    result = NULL;
  }

  $1 = result;
}

%typemap(perl5, out)
char **
{
  char **
    from_c = $1;
  char **
    tmp;
  int
    i;
  int
    num_el = 0;

  if (from_c)
  {
    tmp = from_c;
    while (*tmp != NULL)
    {
      num_el++;
      tmp++;
    }

    EXTEND(sp, num_el);

    tmp = from_c;
    for (i = 0; i < num_el; i++) {
      ST(argvi) = sv_newmortal();
      sv_setpv(ST(argvi++), tmp[i]);
      free(tmp[i]);
    }
    free(from_c);
  }
}

%typemap(perl5, out)
char *
{
  char *
    from_c = $1;
  int
    i;
  int
    num_el = 0;

  if (from_c)
  {
    ST(argvi) = sv_newmortal();
    sv_setpv(ST(argvi++), from_c);
    free(from_c);
  }
}

 %include test_interp_glue.h
