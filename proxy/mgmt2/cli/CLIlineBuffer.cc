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

/***************************************/
/****************************************************************************
 *
 *  Module: handles creating formated line output for the CLI
 *
 *
 ****************************************************************************/

#include "inktomi++.h"
#include "ink_platform.h"
#include "ink_unused.h"      /* MAGIC_EDITING_TAG */

/* local includes */

#include "DynArray.h"
#include "WebMgmtUtils.h"
#include "FileManager.h"
#include "MgmtUtils.h"
#include "LocalManager.h"
#include "CLIlineBuffer.h"

// No default constructor

// Constructor
CLIlineBuffer::CLIlineBuffer(int num_fields)
  :
n_fields(num_fields),
c_fields(0),
f_format(NULL),
o_string(NULL),
f_widths(NULL)
{
  static int i = 1;
  static char *cptr = '\0';

  ink_assert(num_fields);
  f_format = new DynArray < const char *>(&cptr, num_fields);
  o_string = new DynArray < const char *>(&cptr, num_fields);
  f_widths = new DynArray<int>(&i, num_fields);
}

// Destructor
CLIlineBuffer::~CLIlineBuffer()
{
  if (f_format)
    delete f_format;
  if (o_string)
    delete o_string;
  if (f_widths)
    delete f_widths;
}

void
CLIlineBuffer::reset()
{
  //n_fields = 0;
  c_fields = 0;
}

int
CLIlineBuffer::numFields()
{
  return c_fields;
}

// returns the size of a line i.e. sum of all the field widths
int
CLIlineBuffer::getLineSize()
{
  intptr_t i;
  int line_size = 0;

  for (i = 0; i < c_fields; i++)
    line_size += (*f_widths)[i];

  return line_size;
}                               // end getLineSize()

// returns sum of the sizes of the output strings
int
CLIlineBuffer::getStringSize()
{
  intptr_t i;
  int s_size = 0;

  for (i = 0; i < c_fields; i++)
    s_size += strlen((*o_string)[i]);

  return s_size;
}                               // end getStringSize()

//
// determines and returns the maximum possible depth(i.e. number of lines)
// the formatted output will consume.
// Two heuristics are used: one based on the one calculating the
// maximum number of words across all the output strings and the
// other based on maximum of the string length/field width across
// all the output strings.
//
int
CLIlineBuffer::getDepth()
{
  intptr_t i;
  int cdepth = 1;
  int depth = 1;
  int spdepth = 0;
  int cspdepth = 0;
  const char *cptr = NULL;

  for (i = 0; i < c_fields; i++) {      // calculate depth based on maximum words across all the output strings
    ink_assert((*o_string)[i]);
    cptr = (*o_string)[i] + strlen((*o_string)[i]);
    cspdepth = 0;
    while (cptr && cptr != (*o_string)[i]) {
      if (isspace(*cptr))
        cspdepth++;
      cptr--;
    }
    if (cspdepth > spdepth)
      spdepth = cspdepth;

    // calculate depth based on string and field width
    ink_assert((*f_widths)[i]);
    cdepth = (strlen((*o_string)[i]) / (*f_widths)[i]);
    cdepth += (strlen((*o_string)[i]) % (*f_widths)[i]) ? 1 : 0;
    if (cdepth > depth)
      depth = cdepth;
  }

  spdepth++;                    // words are one more than number of spaces found in string
  return (depth > spdepth ? depth : spdepth);
}                               // end getDepth()

//
// Add a field with corresponding format, output string and field width
// to be outputed in. Note that entries are appended in order
// they are added which determine the order in which they are outputed.
//
// field_format entries must have only have width specifiers e.g. '%*s'
//
int
CLIlineBuffer::addField(const char *field_format,       /* IN: field format string */
                        const char *out_string, /* IN: output string */
                        int field_width /* IN: width of output field */ )
{
  if (field_format && out_string && field_width > 0) {
    (*f_format)[c_fields] = field_format;
    (*o_string)[c_fields] = out_string;
    (*f_widths)[c_fields] = field_width;

    c_fields++;

    if (c_fields > n_fields) {  // get new resized length
      n_fields = (*f_format).length();
    }

    return c_fields;
  } else {
    return -1;
  }
}                               // end addField()

//
// NOTE: This routine allocates memory which should be deleted by the user
//       using delete []
//
// This class assumes that format strings have width specifiers in them
// i.e they look like '%*s' or '%-*s', the last one specifying left justifcation
//
// This stuff is not pretty
//
char *
CLIlineBuffer::getline()
{
  char tmpbuf[1024];            // assume no text is greater than
  // this for now
  DynArray < const char *>*f_ptrs = NULL;
  char *line_buf = NULL;
  int line_size = getLineSize();
  int buf_size = 0;
  int buf_depth = getDepth();
  int iters = 0;
  static char *cptr = '\0';
  bool done = false;
  intptr_t i;

  // calculate output line buffer size
  buf_size = buf_depth * (line_size + strlen("\n") + 1) + 1;
  line_buf = new char[buf_size];        // this is returned the user

  f_ptrs = new DynArray < const char *>(&cptr, c_fields);

  iters = buf_depth;            // number of lines in output

  //  Debug("cli","getline: iters=%d, line_size=%d, buf_size=%d, c_fields=%d, depth=%d \n",
  //         iters,line_size,buf_size,c_fields,buf_depth);

  // clear line buffer
  memset(line_buf, '\0', buf_size);

  // assign running ptrs to the output strings
  for (i = 0; i < c_fields; i++)
    (*f_ptrs)[i] = (*o_string)[i];

  while (iters && !done) {      // for each line
    for (i = 0; i < c_fields; i++) {
      memset(tmpbuf, '\0', sizeof(tmpbuf));
      if ((*f_ptrs)[i] && strlen((*f_ptrs)[i]) <= (size_t) (*f_widths)[i]) {
        // string fits into field
        ink_assert(strlen((*f_ptrs)[i]) < sizeof(tmpbuf));
        snprintf(tmpbuf, sizeof(tmpbuf), (*f_format)[i], (*f_widths)[i], (*f_ptrs)[i]);
        // now add it to the line buffer
        strncat(line_buf, tmpbuf, (sizeof(line_buf) - strlen(line_buf) - 1));
        // done with the field now
        (*f_ptrs)[i] = NULL;
      } else if ((*f_ptrs)[i]) {
        // string does not fit in field width
        // take part of string that fits
        char cbuf[256];         // assume no field is bigger than this for now
        char *sptr = NULL;
        int clen = 0;

        // first copy a field width portion
        memset(cbuf, '\0', sizeof(cbuf));
        ink_assert(strlen((*f_ptrs)[i]) < sizeof(cbuf));
        ink_strncpy(cbuf, (*f_ptrs)[i], (*f_widths)[i]);

        // find last word seperated by a space in field string.
        // this tries not break a word across a line if possible
        sptr = strrchr(cbuf, ' ');
        if (sptr) {
          sptr++;               // include space to copy over
          clen = sptr - cbuf;
          *sptr = '\0';
        } else {
          clen = strlen(cbuf);
        }

        ink_assert(strlen(cbuf) < sizeof(tmpbuf));
        snprintf(tmpbuf, sizeof(tmpbuf), (*f_format)[i], (*f_widths)[i], cbuf);
        // now add it to the line buffer
        strncat(line_buf, tmpbuf, (sizeof(line_buf) - strlen(line_buf) - 1));

        // adjust running ptrs
        if (sptr) {
          (*f_ptrs)[i] += clen;
        } else {
          (*f_ptrs)[i] += (*f_widths)[i];
        }
      } else {                  // empty field
        // copy empty spaces
        snprintf(tmpbuf, sizeof(tmpbuf), (*f_format)[i], (*f_widths)[i], " ");
        // now add it to the line buffer
        strncat(line_buf, tmpbuf, (sizeof(line_buf) - strlen(line_buf) - 1));
      }
      //Debug("cli","adding field(%d)%s\n",strlen(tmpbuf),tmpbuf);
    }                           // end for ()

    // add new line
    strncat(line_buf, "\n", (sizeof(line_buf) - strlen(line_buf) - 1));
    //Debug("cli","getline: line_buf(%d)=\n%s",strlen(line_buf),line_buf);

    // check if done
    done = true;
    for (i = 0; i < c_fields; i++) {
      if ((*f_ptrs)[i])
        done = false;
    }

    // decrement iteration
    iters--;
  }                             // end while()

  // cleanup
  if (f_ptrs)
    delete f_ptrs;

  return line_buf;
}                               // end getline()
