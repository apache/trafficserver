/** @file

  Routines to construct and manipulate message bodies and format error responses

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

#include "ink_unused.h"  /* MAGIC_EDITING_TAG */
#include "HttpMessageBody.h"
#include "HttpConfig.h"

/** This routine returns a constant string name for the status_code. */
const char *
HttpMessageBody::StatusCodeName(HTTPStatus status_code)
{
  return http_hdr_reason_lookup(status_code);
}

/**
  This routine creates an HTTP error message body for the status code and
  printf format string format and args va, allocates a response buffer
  (using malloc), and places the result body in the buffer. The body
  will be NUL terminated.

  The caller must ats_free() the returned object when done.

  The reason string allows you to override the default reason phrase for
  the status code. If it is NULL, the default is used. If format is NULL
  or "", no additional text is added.

  NULL is returned if the resulting length exceeds max_buffer_length.

*/
char *
HttpMessageBody::MakeErrorBodyVA(int64_t max_buffer_length,
                                 int64_t *resulting_buffer_length,
                                 const HttpConfigParams * config,
                                 HTTPStatus status_code, const char *reason, const char *format, va_list va)
{
  NOWARN_UNUSED(config);
  char *p, *outbuf = NULL;
  char error_title[128];
  int pass;
  int64_t l, output_length;

  if (reason == NULL)
    reason = (char *) (StatusCodeName(status_code));

  output_length = 0;
  *resulting_buffer_length = 0;

  for (pass = 1; pass <= 2; pass++) {
    if (pass == 2) {
      ats_free(outbuf);
      if (output_length > max_buffer_length)
        return (NULL);
      else
        outbuf = (char *)ats_malloc(output_length);
    }

    l = 0;
    p = outbuf;

    ink_strlcpy(error_title, reason, sizeof(error_title));

    p = (pass == 1 ? (char *) NULL : &(outbuf[l]));
    l += ink_bsprintf(p, "<HEAD><TITLE>%s</TITLE></HEAD>\n", error_title) - 1;

    p = (pass == 1 ? (char *) NULL : &(outbuf[l]));
    l += ink_bsprintf(p, "<BODY BGCOLOR=\"white\" FGCOLOR=\"black\">") - 1;

    p = (pass == 1 ? (char *) NULL : &(outbuf[l]));
    //l += ink_bsprintf(p,"<H1>%s</H1><HR>\n",error_title) - 1;
    l += ink_bsprintf(p, "\n") - 1;

    p = (pass == 1 ? (char *) NULL : &(outbuf[l]));
    l += ink_bsprintf(p, "<FONT FACE=\"Helvetica,Arial\"><B>\n") - 1;

    if (format && *format) {
      p = (pass == 1 ? (char *) NULL : &(outbuf[l]));
      //l += ink_bsprintf(p,"Description: ") - 1;
      l += ink_bsprintf(p, " ") - 1;

      p = (pass == 1 ? (char *) NULL : &(outbuf[l]));
      l += ink_bvsprintf(p, format, va) - 1;
    }

    p = (pass == 1 ? (char *) NULL : &(outbuf[l]));
    l += ink_bsprintf(p, "</B></FONT>\n") - 1;

    p = (pass == 1 ? (char *) NULL : &(outbuf[l]));
    //l += ink_bsprintf(p,"<HR>\n") - 1;
    l += ink_bsprintf(p, "\n") - 1;

    // Moved trailing info into a comment
    p = (pass == 1 ? (char *) NULL : &(outbuf[l]));
    l += ink_bsprintf(p, "<!-- default \"%s\" response (%d) -->\n", reason, status_code) - 1;

    p = (pass == 1 ? (char *) NULL : &(outbuf[l]));
    l += ink_bsprintf(p, "</BODY>\n") - 1;

    l++;                        // leave room for trailing NUL

    if (pass == 2) {
      ink_release_assert(l == output_length);
    }
    output_length = l;
  }

  *resulting_buffer_length = output_length;
  return (outbuf);
}
