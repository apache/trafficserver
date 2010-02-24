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

#ifndef _CLI_LINEBUFFER_H_
#define _CLI_LINEBUFFER_H_

#include "inktomi++.h"
#include "DynArray.h"

/* Class to handle formatted line output from CLI */
class CLIlineBuffer
{
private:
  int n_fields;                 /* available number of fields */
  intptr_t c_fields;                 /* low water mark on used fields */
    DynArray < const char *>*f_format;  /* array of format fields */
    DynArray < const char *>*o_string;  /* array of output strings */
    DynArray<int>*f_widths;  /* array of field widths */

  /* copy constructor and assignment operator are private 
   *  to prevent their use */
    CLIlineBuffer(const CLIlineBuffer & rhs);
    CLIlineBuffer & operator=(const CLIlineBuffer & rhs);

  /* NO default constructor */
    CLIlineBuffer();

public:
  /* constructor */
    CLIlineBuffer(int num_fields);

  /* destructor */
   ~CLIlineBuffer();

  /* Member fcns */

  /* Add field with asssociated string/widths */
  int addField(const char *field_format,        /* IN: field format string */
               const char *out_string,  /* IN: output string */
               int field_width /* IN: width of output field */ );

  void reset();                 /* reset the buffer */
  int numFields();              /* number of fields in buffer */
  int getLineSize();            /* length of an output line */
  int getStringSize();          /* length of all output strings */
  int getDepth();               /* maximum depth of output i.e. num of output lines */
  char *getline();              /* return formatted line */
};

#endif /* _CLI_LINEBUFFER_H_ */
