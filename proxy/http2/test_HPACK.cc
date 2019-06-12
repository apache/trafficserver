/** @file

    Test runner for HPACK encoding and decoding.

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

#include "HuffmanCodec.h"
#include "HPACK.h"
#include "I_EventSystem.h"
#include <sys/stat.h>
#include <dirent.h>
#include <string>
#include <iostream>
#include <fstream>
#include "tscore/ink_args.h"
#include "tscore/TestBox.h"

const static int MAX_REQUEST_HEADER_SIZE = 131072;
const static int MAX_TABLE_SIZE          = 4096;

using namespace std;

AppVersionInfo appVersionInfo;

static int cmd_disable_freelist = 0;
static char cmd_input_dir[512]  = "";
static char cmd_output_dir[512] = "";

static const ArgumentDescription argument_descriptions[] = {
  {"disable_freelist", 'f', "Disable the freelist memory allocator", "T", &cmd_disable_freelist, nullptr, nullptr},
  {"disable_pfreelist", 'F', "Disable the freelist memory allocator in ProxyAllocator", "T", &cmd_disable_pfreelist,
   "PROXY_DPRINTF_LEVEL", nullptr},
  {"input_dir", 'i', "input dir", "S511", &cmd_input_dir, nullptr, nullptr},
  {"output_dir", 'o', "output dir", "S511", &cmd_output_dir, nullptr, nullptr},
  HELP_ARGUMENT_DESCRIPTION(),
  VERSION_ARGUMENT_DESCRIPTION()};

const static uint32_t INITIAL_TABLE_SIZE = 4096;

string input_dir  = "./hpack-tests/";
string output_dir = "./hpack-tests/results/";
string filename_in;
string filename_out;
int offset_in  = input_dir.length() + 6;
int offset_out = output_dir.length() + 6;
int first      = 0;
int last       = 0;

int
unpack(string &packed, uint8_t *unpacked)
{
  int n = packed.length() / 2;
  for (int i = 0; i < n; ++i) {
    int u       = packed[i * 2];
    int l       = packed[i * 2 + 1];
    unpacked[i] = (((u >= 'a') ? u - 'a' + 10 : u - '0') << 4) + ((l >= 'a') ? l - 'a' + 10 : l - '0');
  }
  return n;
}

void
pack(uint8_t *unpacked, int unpacked_len, string &packed)
{
  packed = "";
  for (int i = 0; i < unpacked_len; ++i) {
    packed += (unpacked[i] >> 4) >= 0x0A ? (unpacked[i] >> 4) - 0x0A + 'a' : (unpacked[i] >> 4) + '0';
    packed += (unpacked[i] & 0x0F) >= 0x0A ? (unpacked[i] & 0x0F) - 0x0A + 'a' : (unpacked[i] & 0x0F) + '0';
  }
}

string
unescape(string &str)
{
  // It's not a real unescape. But it works enough.
  for (size_t p = str.find_first_of('\\'); p != string::npos; p = str.find_first_of('\\', p)) {
    str.erase(p, 1);
  }
  return str;
}

string
escape(string &str)
{
  for (size_t p = str.find_first_of('"'); p != string::npos; p = str.find_first_of('"', p + 2)) {
    str.insert(p, 1, '\\');
  }
  return str;
}

void
parse_line(string &line, int offset, string &name, string &value)
{
  int son = offset + 1;               // start of name
  int eon = line.find("\": \"", son); // end of name
  int sov = eon + 4;                  // start of value
  int eov = line.find_last_of('"');   // end of value
  name    = line.substr(son, eon - son);
  value   = line.substr(sov, eov - sov);
  unescape(value);
}

void
print_difference(const char *a_str, const int a_str_len, const char *b_str, const int b_str_len)
{
  fprintf(stderr, "%.*s", b_str_len, b_str);
  fprintf(stderr, " <-> ");
  fprintf(stderr, "%.*s", a_str_len, a_str);
  fprintf(stderr, "\n");
}

int
compare_header_fields(HTTPHdr *a, HTTPHdr *b)
{
  // compare fields count
  if (a->fields_count() != b->fields_count()) {
    return -1;
  }

  MIMEFieldIter a_iter, b_iter;

  const MIMEField *a_field = a->iter_get_first(&a_iter);
  const MIMEField *b_field = b->iter_get_first(&b_iter);

  while (a_field != nullptr && b_field != nullptr) {
    int a_str_len, b_str_len;
    // compare header name
    const char *a_str = a_field->name_get(&a_str_len);
    const char *b_str = b_field->name_get(&b_str_len);
    if (a_str_len != b_str_len) {
      if (memcmp(a_str, b_str, a_str_len) != 0) {
        print_difference(a_str, a_str_len, b_str, b_str_len);
        return -1;
      }
    }
    // compare header value
    a_str = a_field->value_get(&a_str_len);
    b_str = b_field->value_get(&b_str_len);
    if (a_str_len != b_str_len) {
      if (memcmp(a_str, b_str, a_str_len) != 0) {
        print_difference(a_str, a_str_len, b_str, b_str_len);
        return -1;
      }
    }

    a_field = a->iter_get_next(&a_iter);
    b_field = b->iter_get_next(&b_iter);
  }

  return 0;
}

// Returns -1 if test passes, or returns the failed sequence number
int
test_decoding(const string &filename)
{
  HpackIndexingTable indexing_table(INITIAL_TABLE_SIZE);
  string line, name, value;
  uint8_t unpacked[8192];
  size_t unpacked_len;
  int result = -1;
  HTTPHdr original, decoded;
  MIMEField *field;

  decoded.create(HTTP_TYPE_REQUEST);
  original.create(HTTP_TYPE_REQUEST);

  int seqnum = -1;
  ifstream ifs(filename);
  while (ifs && getline(ifs, line) && result == -1) {
    switch (line.find_first_of('"')) {
    case 6:
      switch (line[6 + 1]) {
      case 's':
        // This line should be the start of new case
        // Check the last case
        if (compare_header_fields(&decoded, &original) != 0) {
          result = seqnum;
          break;
        }
        // Prepare for next sequence
        ++seqnum;
        decoded.fields_clear();
        original.fields_clear();
        break;
      case 'w':
        parse_line(line, 6, name, value);
        unpacked_len = unpack(value, unpacked);
        hpack_decode_header_block(indexing_table, &decoded, unpacked, unpacked_len, MAX_REQUEST_HEADER_SIZE, MAX_TABLE_SIZE);
        break;
      }
      break;
    case 10:
      // This line should be a header field
      parse_line(line, 10, name, value);
      field = original.field_create(name.c_str(), name.length());
      field->value_set(original.m_heap, original.m_mime, value.c_str(), value.length());
      original.field_attach(field);
      break;
    }
  }
  decoded.destroy();
  original.destroy();
  return result;
}

int
test_encoding(const string &filename_in, const string &filename_out)
{
  HpackIndexingTable indexing_table_for_encoding(INITIAL_TABLE_SIZE), indexing_table_for_decoding(INITIAL_TABLE_SIZE);
  string line, name, value;
  uint8_t encoded[8192];
  const uint64_t encoded_len = sizeof(encoded);
  string packed;
  int64_t written;
  int result = -1;
  HTTPHdr original, decoded;
  MIMEField *field;

  decoded.create(HTTP_TYPE_REQUEST);
  original.create(HTTP_TYPE_REQUEST);

  ofstream ofs(filename_out);
  ofs << "{" << endl;
  ofs << "  \"cases\": [" << endl;

  int seqnum = -1;
  ifstream ifs(filename_in);
  while (ifs && getline(ifs, line) && result == -1) {
    switch (line.find_first_of('"')) {
    case 6:
      switch (line[6 + 1]) {
      case 's':
        // This line should be the start of new case
        // Check the last sequence
        if (seqnum != -1) {
          ofs << "        }" << endl; // end of last header
          ofs << "      ]," << endl;  // end of headers
          written = hpack_encode_header_block(indexing_table_for_encoding, encoded, encoded_len, &original);
          if (written == -1) {
            result = seqnum;
            break;
          }
          hpack_decode_header_block(indexing_table_for_decoding, &decoded, encoded, written, MAX_REQUEST_HEADER_SIZE,
                                    MAX_TABLE_SIZE);
          if (compare_header_fields(&decoded, &original) != 0) {
            result = seqnum;
            break;
          }
          pack(encoded, written, packed);
          ofs << R"(      "wire": ")" << packed << "\"" << endl;
          ofs << "    }," << endl;
        }
        // Prepare for next sequence
        ++seqnum;
        decoded.fields_clear();
        original.fields_clear();
        name  = "";
        value = "";
        ofs << "    {" << endl;
        ofs << "      \"seqnum\": " << seqnum << "," << endl;
        ofs << "      \"headers\": [" << endl;
        break;
      case 'w':
        break;
      }
      break;
    case 10:
      // This line should be a header field
      if (name != "") {
        ofs << "        }," << endl;
      }
      parse_line(line, 10, name, value);
      field = original.field_create(name.c_str(), name.length());
      field->value_set(original.m_heap, original.m_mime, value.c_str(), value.length());
      original.field_attach(field);
      ofs << "        {" << endl;
      ofs << "          \"" << name << "\": \"" << escape(value) << "\"" << endl;
      break;
    }
  }
  ofs << "        }" << endl; // end of last header
  ofs << "      ]," << endl;  // end of headers
  // Check the final sequence
  written = hpack_encode_header_block(indexing_table_for_encoding, encoded, encoded_len, &original);
  if (written == -1) {
    result = seqnum;
    return result;
  }
  hpack_decode_header_block(indexing_table_for_decoding, &decoded, encoded, written, MAX_REQUEST_HEADER_SIZE, MAX_TABLE_SIZE);
  if (compare_header_fields(&decoded, &original) != 0) {
    result = seqnum;
    return result;
  }
  pack(encoded, written, packed);
  ofs << R"(      "wire": ")" << packed << "\"" << endl;
  ofs << "    }" << endl;
  decoded.destroy();
  original.destroy();

  ofs << "  ]," << endl;
  ofs << R"(  "description": "Apache Traffic Server")" << endl;
  ofs << "}" << endl;

  return result;
}

int
prepare()
{
  filename_in  = input_dir + "story_00.json";
  filename_out = output_dir + "story_00.json";
  offset_in    = input_dir.length() + 6;
  offset_out   = output_dir.length() + 6;

  struct dirent *d;
  DIR *dir = opendir(input_dir.c_str());

  if (dir == nullptr) {
    cerr << "Cannot open " << input_dir << endl;
    return 1;
  }
  struct stat st;
  char name[PATH_MAX + 1] = "";
  strcat(name, input_dir.c_str());
  while ((d = readdir(dir)) != nullptr) {
    name[input_dir.length()] = '\0';
    ink_strlcat(name, d->d_name, sizeof(name));
    stat(name, &st);
    if (!S_ISDIR(st.st_mode)) {
      ++last;
    }
  }
  closedir(dir);
  cerr << last << " test stories" << endl;

  if (mkdir(output_dir.c_str(), 0755) < 0 && errno != EEXIST) {
    cerr << "Cannot create output directory" << endl;
    return 1;
  }
  return 0;
}

REGRESSION_TEST(HPACK_Decoding)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  int result = -1;

  for (int i = first; i < last; ++i) {
    filename_in[offset_in + 0] = '0' + i / 10;
    filename_in[offset_in + 1] = '0' + i % 10;
    result                     = test_decoding(filename_in);
    box.check(result == -1, "Story %d sequence %d failed.", i, result);
    if (result != -1) {
      break;
    }
  }
}

REGRESSION_TEST(HPACK_Encoding)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  int result = -1;

  for (int i = first; i < last; ++i) {
    filename_in[offset_in + 0]   = '0' + i / 10;
    filename_in[offset_in + 1]   = '0' + i % 10;
    filename_out[offset_out + 0] = '0' + i / 10;
    filename_out[offset_out + 1] = '0' + i % 10;
    result                       = test_encoding(filename_in, filename_out);
    box.check(result == -1, "Story %d sequence %d failed.", i, result);
    if (result != -1) {
      break;
    }
  }
}

int
main(int argc, const char **argv)
{
  appVersionInfo.setup(PACKAGE_NAME, "test_HPACK", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");
  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv);

  ink_freelist_init_ops(cmd_disable_freelist, cmd_disable_pfreelist);

  if (*cmd_input_dir) {
    input_dir = cmd_input_dir;
    if (*input_dir.end() != '/') {
      input_dir += '/';
    }
  }
  if (*cmd_output_dir) {
    output_dir = cmd_output_dir;
    if (*output_dir.end() != '/') {
      output_dir += '/';
    }
  }

  Thread *main_thread = new EThread;
  main_thread->set_specific();
  url_init();
  mime_init();
  http_init();
  hpack_huffman_init();

  prepare();
  int status = RegressionTest::main(argc, argv, REGRESSION_TEST_QUICK);

  hpack_huffman_fin();
  return status;
}
