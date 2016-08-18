/** @file

  Record debug and logging

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

#include "P_RecUtils.h"
#include "P_RecCore.h"

static Diags *g_diags = NULL;

//-------------------------------------------------------------------------
// RecSetDiags
//-------------------------------------------------------------------------
int
RecSetDiags(Diags *_diags)
{
  // Warning! It's very dangerous to change diags on the fly!  This
  // function only exists so that we can boot-strap TM on startup.
  ink_atomic_swap(&g_diags, _diags);
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecLog
//-------------------------------------------------------------------------
void
RecLog(DiagsLevel dl, const char *format_string, ...)
{
  va_list ap;

  va_start(ap, format_string);
  if (g_diags) {
    g_diags->log_va(NULL, dl, NULL, format_string, ap);
  }
  va_end(ap);
}

//-------------------------------------------------------------------------
// RecDebug
//-------------------------------------------------------------------------
void
RecDebug(DiagsLevel dl, const char *format_string, ...)
{
  va_list ap;

  va_start(ap, format_string);
  if (g_diags) {
    g_diags->log_va("rec", dl, NULL, format_string, ap);
  }
  va_end(ap);
}

//-------------------------------------------------------------------------
// RecDebugOff
//-------------------------------------------------------------------------
void
RecDebugOff()
{
  g_diags = NULL;
}
