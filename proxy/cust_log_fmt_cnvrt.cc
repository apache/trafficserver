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

#include <iostream>
#include <iomanip>
#include <fstream>
using namespace std;
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
//#include <iomanip.h>
//#include <unistd.h>
#include "SimpleTokenizer.h"
#include "ink_args.h"
#include "I_Version.h"
#include "ink_assert.h"

#define PROGRAM_NAME            "traffic_cust_log_fmt_cnvrt"

const int LINE_BUFFER_SIZE = 1024;
const int MAX_LOG_OBJECTS = 64;
const int MAX_FILTERS = 64;

// command line flags
//
static int version_flag = 0;
static int help = 0;
static char output_file[1024];
static int auto_filenames = 0;
static int overwrite_existing_file = 0;
static int annotate_output = 0;

static ArgumentDescription argument_descriptions[] = {

  {"auto_filenames", 'a', "Automatically generate output names",
   "T", &auto_filenames, NULL, NULL},
  {"help", 'h', "Give this help", "T", &help, NULL, NULL},
  {"annotate_output", 'n', "Add comments to output file(s)", "T",
   &annotate_output, NULL, NULL},
  {"output_file", 'o', "Specify output file", "S1023", &output_file, NULL, NULL},
  {"version", 'V', "Print Version Id", "T", &version_flag, NULL, NULL},
  {"overwrite_output", 'w', "Overwrite existing output file(s)", "T",
   &overwrite_existing_file, NULL, NULL}
};
static int n_argument_descriptions = SIZE(argument_descriptions);

static char *USAGE_LINE = "Usage: " PROGRAM_NAME " [-o output-file | -a] [-hnVw] [input-file ...]";

struct LogFormat
{
  char *name;
  char *fmt_string;
};

struct Filter
{
  char *name;
  char *objName;
  char *field;
  char *oper;
  char *value;
  char *action;
};

struct LogObj
{
  char *name;
  char *filename;
  char *header;
  int enabled;
  int binary;
  LogFormat *format;
  Filter *filters[MAX_FILTERS];
  int numFilters;

  void add_filter(Filter * f);
  void print_filters(ostream & _os);
};

void
LogObj::add_filter(Filter * f)
{
  ink_debug_assert(numFilters < MAX_FILTERS);

  filters[numFilters++] = f;
}

void
LogObj::print_filters(ostream & os)
{
  if (numFilters > 0) {
    for (int i = 0; i < (numFilters - 1); i++) {
      os << filters[i]->name << ", ";
    }
    os << filters[numFilters - 1]->name;
  }
}


char *
create_escaped_string(char *input)
{
  ink_debug_assert(input);
  char *output = new char[strlen(input) * 2];   // worst case scenario
  char *outptr = output;

  while (*input != 0) {
    if (*input == '"')
      *outptr++ = '\\';
    *outptr++ = *input++;
  }
  *outptr = 0;

  return output;
}


LogObj *
process_format(SimpleTokenizer * tok, LogFormat ** formats, int &numFormats)
{
  char *t;

  t = tok->getNext();
  if (!t)
    return 0;
  int enabled = (strcasecmp(t, "enabled") == 0 ? 1 : (strcasecmp(t, "disabled") == 0 ? 0 : -1));
  if (enabled < 0)
    return 0;

  t = tok->getNext();
  if (!t)
    return 0;                   // numeric id, don't need it
  char *name = tok->getNext();
  if (!name)
    return 0;
  char *fmt_string = tok->getNext();
  if (!fmt_string)
    return 0;
  char *filename = tok->getNext();
  if (!filename)
    return 0;

  t = tok->getNext();
  if (!t)
    return 0;
  int binary = (strcasecmp(t, "ASCII") == 0 ? 0 : (strcasecmp(t, "BINARY") == 0 ? 1 : -1));
  if (binary < 0)
    return 0;

  char *header = tok->getRest();


  // check if we have already defined a LogFormat we can use 

  char *escaped_fmt_string = create_escaped_string(fmt_string);

  LogFormat *logFmt = 0;
  for (int i = 0; i < numFormats; i++) {
    if (strcmp(formats[i]->fmt_string, escaped_fmt_string) == 0) {
      logFmt = formats[i];
      delete[]escaped_fmt_string;
      break;
    }
  }

  if (!logFmt && numFormats < MAX_LOG_OBJECTS) {
    logFmt = new LogFormat;
    char logFmtName[16];
    snprintf(logFmtName, sizeof(logFmtName), "format_%d", numFormats);
    logFmt->name = strdup(logFmtName);
    logFmt->fmt_string = escaped_fmt_string;

    formats[numFormats++] = logFmt;
  }

  LogObj *ob = new LogObj;
  ob->enabled = enabled;
  ob->name = create_escaped_string(name);
  ob->format = logFmt;
  ob->filename = create_escaped_string(filename);
  ob->binary = binary;
  ob->header = (!header || strcasecmp(header, "none") == 0 ? 0 : create_escaped_string(header));
  ob->numFilters = 0;

  // coverity[leaked_storage]
  return ob;
}


Filter *
process_filter(SimpleTokenizer * tok)
{
  char *action = "REJECT";

  char *objName = tok->getNext();
  if (!objName)
    return 0;
  char *field = tok->getNext();
  if (!field)
    return 0;

  char *oper = tok->getNext();
  if (!oper)
    return 0;
  if (strcasecmp(oper, "NOMATCH") == 0) {
    action = "ACCEPT";
    oper = "MATCH";
  }

  char *value = tok->getNext();
  if (!value)
    return 0;

  Filter *filter = new Filter;
  filter->objName = create_escaped_string(objName);
  filter->field = create_escaped_string(field);
  filter->oper = create_escaped_string(oper);
  filter->value = create_escaped_string(value);
  filter->action = create_escaped_string(action);

  return filter;
}


void
add_filter_to_objects(Filter * filter, LogObj ** objects, int numObjects)
{
  if (strcmp(filter->objName, "_global_") == 0) {
    for (int i = 0; i < numObjects; i++) {
      objects[i]->add_filter(filter);
    }
  } else {
    for (int i = 0; i < numObjects; i++) {
      if (strcmp(objects[i]->name, filter->objName) == 0) {
        objects[i]->add_filter(filter);
      }
    }
  }
}


void
output_xml(ostream & os,
           LogFormat ** formats, int numFormats, Filter ** filters, int numFilters, LogObj ** objects, int numObjects)
{
  int i;

  for (i = 0; i < numFormats; i++) {
    LogFormat *format = formats[i];

    os << "<LogFormat>\n"
      << "  <Name      = \"" << format->name << "\"/>\n"
      << "  <Format    = \"" << format->fmt_string << "\"/>\n" << "</LogFormat>\n" << endl;
  }

  for (i = 0; i < numFilters; i++) {
    Filter *filter = filters[i];

    os << "<LogFilter>\n"
      << "  <Name      = \"" << filter->name << "\"/>\n"
      << "  <Action    = \"" << filter->action << "\"/>\n"
      << "  <Condition = \"" << filter->field << " "
      << filter->oper << " " << filter->value << "\"/>\n" << "</LogFilter>\n" << endl;
  }

  for (i = 0; i < numObjects; i++) {
    LogObj *obj = objects[i];

    if (!obj->enabled) {
      os << "<!--- object created from a disabled logs.config format\n";
    }

    os << "<LogObject>\n"
      << "  <Format    = \"" << obj->format->name << "\"/>\n"
      << "  <Filename  = \"" << obj->filename << "\"/>\n" << "  <Mode      = \"" << (obj->binary ? "binary" : "ascii")
      << "\"/>\n";

    if (obj->header) {
      os << "  <Header    = \"" << obj->header << "\"/>\n";
    }

    if (obj->numFilters > 0) {
      os << "  <Filters   = \"";
      obj->print_filters(os);
      os << "\"/>\n";
    }

    os << "</LogObject>\n";

    if (!obj->enabled) {
      os << "object created from a disabled logs.config format ---!>\n";
    }

    os << endl;
  }
}

int
check_output_file(char *output_file)
{
  int error = 0;

  if (!overwrite_existing_file) {
    if (access(output_file, F_OK)) {
      if (errno != ENOENT) {
        cerr << "Error accessing output file " << output_file << ": ";
        perror(0);
        error = 1;
      }
    } else {
      cerr << "Error, output file " << output_file << " already exists."
        "\nSelect a different filename or use the -w flag\n";
      error = 1;
    }
  }

  return error;
}

#if 0
int
open_output_file(char *output_file)
{
  int fd = -1;
  int error = 0;

  if (!overwrite_existing_file) {
    if (access(output_file, F_OK)) {
      if (errno != ENOENT) {
        cerr << "Error accessing output file " << output_file << ": ";
        perror(0);
        error = 1;
      }
    } else {
      cerr << "Error, output file " << output_file << " already exists."
        "\nSelect a different filename or use the -w flag\n";
      error = 1;
    }
  }

  if (!error) {
    fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
      cerr << "Error opening output file " << output_file << ": ";
      perror(0);
    }
  }

  return fd;
}
#endif

int
process_file(const char *in_filename, istream & is, ostream & os)
{
  int retVal = 0;

  char line_buffer[LINE_BUFFER_SIZE];

  LogObj *objects[MAX_LOG_OBJECTS] = { 0 };
  Filter *filters[MAX_FILTERS] = { 0 };
  LogFormat *formats[MAX_LOG_OBJECTS] = { 0 };
  int nonComments = 0;
  int numObjects = 0;
  int numFilters = 0;
  int numFormats = 0;

  if (annotate_output) {
    os << "<!-----------------------------------------------------------"
      "-------------\n"
      "This file (or file section) was generated automatically from \""
      << in_filename << "\"." <<
      "\nThe following is a summary of the translation process:\n"
      "\nline #    type  status" "\n----------------------\n";
  }

  SimpleTokenizer tok(':', SimpleTokenizer::OVERWRITE_INPUT_STRING);

  int line_num = 0;
  while (is.good() && !is.eof()) {
    ++line_num;
    is.getline(line_buffer, LINE_BUFFER_SIZE);
    tok.setString(line_buffer);

    char *t = tok.getNext();

    if (t) {
      if (t[0] == '#' || t[0] == '\n') {
        continue;
      } else {
        if (annotate_output) {
          nonComments++;
          os << setw(6) << line_num << "  " << setw(6) << t;
        }

        if (strcasecmp(t, "format") == 0) {
          LogObj *obj = process_format(&tok, formats, numFormats);
          if (obj && numObjects < MAX_LOG_OBJECTS) {
            objects[numObjects++] = obj;
            if (annotate_output) {
              os << "  success\n";
            }
          } else {
            if (annotate_output) {
              os << "  failure, ";
              if (obj) {
                os << "maximum number of formats in input file" " (" << MAX_LOG_OBJECTS << ") exceeded\n";
              } else {
                os << "syntax error in format definition\n";
              }
            }

            delete obj;
            retVal = 1;
          }
        } else if (strcasecmp(t, "filter") == 0) {
          Filter *filter = process_filter(&tok);
          if (filter && numFilters < MAX_FILTERS) {
            char filter_name[16];
            snprintf(filter_name, sizeof(filter_name), "filter_%d", numFilters);
            filter->name = strdup(filter_name);

            filters[numFilters++] = filter;

            add_filter_to_objects(filter, objects, numObjects);

            if (annotate_output) {
              os << "  success\n";
            }
          } else {
            if (annotate_output) {
              os << "  failure, ";
              if (filter) {
                os << "maximum number of filters in input file" " (" << MAX_FILTERS << ") exceeded\n";
              } else {
                os << "syntax error in filter definition\n";
              }
            }

            delete filter;
            retVal = 1;
          }
        } else {
          if (annotate_output) {
            os << "  failure, unknown keyword \"" << t << "\" should be \"format\" or \"filter\"\n";
          }
          retVal = 1;
        }
      }
    }
  }

  if (annotate_output) {
    if (nonComments == 0) {
      os << "                " "input file does not define any formats or filters\n";
    }

    os << "-------------------------------------------------------------" "-----------!>\n" << endl;
  }

  output_xml(os, formats, numFormats, filters, numFilters, objects, numObjects);

  return retVal;
}


char *
generate_filename(char *in_filename)
{
  // change .config to _xml.config
  //
  char in_extension[] = ".config";
  int in_extension_len = 7;

  char out_extension[] = "_xml.config";
  int out_extension_len = 11;

  int n = strlen(in_filename);
  int copy_len =
    (n >= in_extension_len ?
     (strcmp(&in_filename[n - in_extension_len], in_extension) == 0 ? n - in_extension_len : n) : n);

  char *out_filename = new char[copy_len + out_extension_len + 1];

  memcpy(out_filename, in_filename, copy_len);
  memcpy(&out_filename[copy_len], out_extension, out_extension_len);
  out_filename[copy_len + out_extension_len] = 0;

  return out_filename;
}


int
main(int argc, char **argv)
{
  enum
  {
    NO_ERROR = 0,
    CMD_LINE_OPTION_ERROR = 1,
    IO_ERROR = 2,
    DATA_PROCESSING_ERROR = 4
  };

  // build the application information structure
  //
  AppVersionInfo appVersionInfo;
  appVersionInfo.setup(PACKAGE_NAME, PROGRAM_NAME, PACKAGE_VERSION, __DATE__, 
                       __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // process command-line arguments
  //
  output_file[0] = 0;
  process_args(argument_descriptions, n_argument_descriptions, argv, USAGE_LINE);

  // check for the version number request
  //
  if (version_flag) {
    cerr << appVersionInfo.FullVersionInfoStr << endl;
    exit(NO_ERROR);
  }
  // check for help request
  //
  if (help) {
    usage(argument_descriptions, n_argument_descriptions, USAGE_LINE);
    exit(NO_ERROR);
  }
  // check that only one of the -o and -a options was specified
  //
  if (output_file[0] != 0 && auto_filenames) {
    cerr << "Error: specify only one of -o <file> and -a" << endl;
    exit(CMD_LINE_OPTION_ERROR);
  }


  ofstream *os = 0;

  // setup output file if no auto_filenames
  //
  if (output_file[0] != 0) {
    if (check_output_file(output_file) != 0) {
      exit(IO_ERROR);
    }
    os = new ofstream(output_file);
    if (!os || !(os->good())) {
      cerr << "Error creating output stream" << endl;
      exit(IO_ERROR);
    }
  }
  // process file arguments
  //
  int error = NO_ERROR;

  if (n_file_arguments) {
    ifstream *ifs;

    for (int i = 0; i < n_file_arguments; ++i) {
      char *in_filename = file_arguments[i];
      ifs = new ifstream(in_filename);
      if (!ifs || !*ifs) {
        cerr << "Error opening input file " << in_filename << ": ";
        perror(0);
        error |= IO_ERROR;
      } else {
        if (auto_filenames) {
          char *new_out_filename = generate_filename(in_filename);

          if (!new_out_filename) {
            cerr << "Memory allocation error" << endl;
            error |= IO_ERROR;
            continue;
          }
          if (check_output_file(new_out_filename) != 0) {
            error |= IO_ERROR;
            delete new_out_filename;
            continue;
          }
          os = new ofstream(new_out_filename);
          if (!os || !(os->good())) {
            cerr << "Error creating output stream" << endl;
            error |= IO_ERROR;
            delete new_out_filename;
            continue;
          }
        }

        ink_debug_assert(ifs);

        if (process_file(in_filename, *ifs, os ? *os : cout) != 0) {
          error |= DATA_PROCESSING_ERROR;
        }

        delete ifs;
        if (auto_filenames && os) {
          delete os;
          os = 0;
        }
      }
    }
  } else {
    // read from stdin
    //
    if (process_file("stdin", cin, os ? *os : cout) != 0) {
      error |= DATA_PROCESSING_ERROR;
    }
  }

  if (output_file[0] != 0 && os) {
    delete os;
  }

  exit(error);
}
