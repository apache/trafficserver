/** @file

  Contains main function definition for logcat

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

#include "tscore/Version.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_args.h"
#include "tscore/Layout.h"
#include "tscore/runroot.h"

#define PROGRAM_NAME       "traffic_logcat"
#define MAX_LOGBUFFER_SIZE 524288 // 512KB

#include <poll.h>

#include "../proxy/logging/LogStandalone.cc"

#include "proxy/logging/LogAccess.h"
#include "proxy/logging/LogField.h"
#include "proxy/logging/LogFilter.h"
#include "proxy/logging/LogFormat.h"
#include "proxy/logging/LogFile.h"
#include "proxy/logging/LogObject.h"
#include "proxy/logging/LogConfig.h"
#include "proxy/logging/LogBuffer.h"
#include "proxy/logging/LogUtils.h"
#include "proxy/logging/Log.h"

#include "LogEntryJson.h"

#include <cinttypes>

namespace
{

// logcat-specific command-line flags
int  squid_flag              = 0;
int  follow_flag             = 0;
int  clf_flag                = 0;
int  elf_flag                = 0;
int  elf2_flag               = 0;
int  json_flag               = 0;
int  header_flag             = 0;
int  auto_filenames          = 0;
int  overwrite_existing_file = 0;
char output_file[1024];

const ArgumentDescription argument_descriptions[] = {

  {"output_file",      'o', "Specify output file",                 "S1023", &output_file,             NULL, NULL},
  {"auto_filenames",   'a', "Automatically generate output names", "T",     &auto_filenames,          NULL, NULL},
  {"follow",           'f', "Follow the log file as it grows",     "T",     &follow_flag,             NULL, NULL},
  {"clf",              'C', "Convert to Common Logging Format",    "T",     &clf_flag,                NULL, NULL},
  {"elf",              'E', "Convert to Extended Logging Format",  "T",     &elf_flag,                NULL, NULL},
  {"squid",            'S', "Convert to Squid Logging Format",     "T",     &squid_flag,              NULL, NULL},
  {"debug_tags",       'T', "Colon-Separated Debug Tags",          "S1023", error_tags,               NULL, NULL},
  {"overwrite_output", 'w', "Overwrite existing output file(s)",   "T",     &overwrite_existing_file, NULL, NULL},
  {"elf2",             '2', "Convert to Extended2 Logging Format", "T",     &elf2_flag,               NULL, NULL},
  {"json",             'j', "Convert v3 binary logs to JSON",      "T",     &json_flag,               NULL, NULL},
  {"header",           'H', "Print binary log header info only",   "T",     &header_flag,             NULL, NULL},
  HELP_ARGUMENT_DESCRIPTION(),
  VERSION_ARGUMENT_DESCRIPTION(),
  RUNROOT_ARGUMENT_DESCRIPTION()
};

DbgCtl dbg_ctl_logcat{"logcat"};

/*
 * Gets the inode number of a given file
 *
 * @param filename name of the file
 * @returns -1 on failure, otherwise inode number
 */
ino_t
get_inode_num(const char *filename)
{
  struct stat sb;

  if (stat(filename, &sb) != 0) {
    perror("stat");
    return -1;
  }

  return sb.st_ino;
}

/*
 * Checks if a log file has been rotated, and if so, opens the rotated file
 * and returns the new file descriptor
 *
 * @param input_file name of log file we want to follow
 * @param old_inode_num the most recently known inode number of `input_name`
 * @returns -1 on failure, 0 on noop, otherwise the open fd of rotated file
 */
int
follow_rotate(const char *input_file, ino_t old_inode_num)
{
  // check if file has been rotated
  if (get_inode_num(input_file) != old_inode_num) {
    int new_fd = open(input_file, O_RDONLY);
    if (new_fd < 0) {
      fprintf(stderr, "Error while trying to follow rotated input file %s: %s\n", input_file, strerror(errno));
      return -1;
    }

    return new_fd;
  } else { // file has not been rotated
    return 0;
  }
}

// Emit every entry of a v3 segment as a line of JSON. v2 segments lack the
// field-type schema needed for self-describing decode, so they are skipped
// with a note. Returns the number of entries written.
int
write_json_logbuffer(LogBufferHeader *header, int out_fd)
{
  if (header->fmt_fieldtypes() == nullptr) {
    fprintf(stderr, "JSON output requires a v3 binary log with a field-type schema; skipping segment.\n");
    return 0;
  }

  LogBufferIterator iter(header);
  LogEntryHeader   *entry;
  char              line[LOG_MAX_FORMATTED_LINE];
  int               count = 0;

  while ((entry = iter.next()) != nullptr) {
    int n = log_entry_to_json(entry, header, line, sizeof(line) - 1);
    if (n > 0) {
      line[n] = '\n';
      if (write(out_fd, line, n + 1) > 0) {
        ++count;
      }
    }
  }
  return count;
}

// Human-readable name for a LogBufferHeader::format_type value.
const char *
log_format_type_name(uint32_t format_type)
{
  switch (format_type) {
  case LOG_FORMAT_CUSTOM:
    return "CUSTOM";
  case LOG_FORMAT_TEXT:
    return "TEXT";
  default:
    return "UNKNOWN";
  }
}

// Human-readable name for a v3 wire type code (LogField::Type).
const char *
log_field_type_name(uint8_t code)
{
  switch (static_cast<LogField::Type>(code)) {
  case LogField::Type::sINT:
    return "sINT";
  case LogField::Type::dINT:
    return "dINT";
  case LogField::Type::STRING:
    return "STRING";
  case LogField::Type::IP:
    return "IP";
  case LogField::Type::INVALID:
    return "INVALID";
  default:
    return "UNKNOWN";
  }
}

// Return the NUL-terminated string living at @a offset within the segment, or
// nullptr if the offset is zero, lands outside the segment, or is not
// terminated before the segment ends. A .blog read here may be untrusted, so
// every offset is validated against byte_count.
const char *
bounded_header_str(LogBufferHeader *header, uint32_t offset)
{
  if (offset == 0) {
    return nullptr;
  }

  char *seg_start = reinterpret_cast<char *>(header);
  char *seg_end   = seg_start + header->byte_count;
  char *addr      = seg_start + offset;

  if (addr < seg_start || addr >= seg_end) {
    return nullptr;
  }
  if (memchr(addr, '\0', static_cast<size_t>(seg_end - addr)) == nullptr) {
    return nullptr;
  }
  return addr;
}

// Print the v3 field-type schema: the field_count and, for each field, its
// fmt_fieldlist symbol paired with its framing type. Every read is bounded
// against the segment (mirrors the bounds checks in log_entry_to_json).
void
print_field_type_schema(LogBufferHeader *header, int out_fd)
{
  char *seg_start   = reinterpret_cast<char *>(header);
  char *seg_end     = seg_start + header->byte_count;
  char *schema_blob = header->fmt_fieldtypes();

  if (schema_blob == nullptr) {
    dprintf(out_fd, "  %-22s%s\n", "field_type_schema:", "(none)");
    return;
  }
  if (schema_blob < seg_start || schema_blob + sizeof(LogFieldTypeSchema) > seg_end) {
    dprintf(out_fd, "  %-22s%s\n", "field_type_schema:", "(out of bounds)");
    return;
  }

  // field_count is written right after the NUL-terminated header strings, so it
  // may be unaligned; read it byte-wise.
  uint16_t fc16 = 0;
  memcpy(&fc16, schema_blob, sizeof(fc16));
  const unsigned field_count = fc16;
  const uint8_t *codes       = reinterpret_cast<const uint8_t *>(schema_blob) + sizeof(LogFieldTypeSchema);

  if (reinterpret_cast<const char *>(codes) + field_count > seg_end) {
    dprintf(out_fd, "  %-22sfield_count=%u (truncated)\n", "field_type_schema:", field_count);
    return;
  }

  dprintf(out_fd, "  %-22sfield_count=%u\n", "field_type_schema:", field_count);

  // Pair each type code with its symbol from fmt_fieldlist (comma/space
  // separated), if the symbol list is present and well-formed.
  const char *sym = bounded_header_str(header, header->fmt_fieldlist_offset);
  for (unsigned i = 0; i < field_count; ++i) {
    const char *sym_start = "?";
    int         sym_len   = 1;
    if (sym != nullptr) {
      while (*sym == ',' || *sym == ' ') {
        ++sym;
      }
      const char *tok = sym;
      while (*sym != '\0' && *sym != ',' && *sym != ' ') {
        ++sym;
      }
      if (sym != tok) {
        sym_start = tok;
        sym_len   = static_cast<int>(sym - tok);
      }
    }
    dprintf(out_fd, "    [%2u] %-20.*s %s\n", i, sym_len, sym_start, log_field_type_name(codes[i]));
  }
}

// Print the LogBufferHeader of one segment. Works for both v2 and v3; the
// v3-only field-type schema is printed only when the segment is v3, since the
// v2 on-disk header stops before fmt_fieldtypes_offset.
void
print_logbuffer_header(LogBufferHeader *header, int out_fd)
{
  static int segment_index = 0;

  dprintf(out_fd, "==== LogBuffer segment %d ====\n", segment_index++);
  dprintf(out_fd, "  %-22s0x%08x\n", "cookie:", header->cookie);
  dprintf(out_fd, "  %-22s%u\n", "version:", header->version);
  dprintf(out_fd, "  %-22s%u (%s)\n", "format_type:", header->format_type, log_format_type_name(header->format_type));
  dprintf(out_fd, "  %-22s%u\n", "byte_count:", header->byte_count);
  dprintf(out_fd, "  %-22s%u\n", "entry_count:", header->entry_count);
  dprintf(out_fd, "  %-22s%u\n", "low_timestamp:", header->low_timestamp);
  dprintf(out_fd, "  %-22s%u\n", "high_timestamp:", header->high_timestamp);
  dprintf(out_fd, "  %-22s0x%08x\n", "log_object_flags:", header->log_object_flags);
  dprintf(out_fd, "  %-22s%" PRIu64 "\n", "log_object_signature:", header->log_object_signature);
  dprintf(out_fd, "  %-22s%u\n", "data_offset:", header->data_offset);

  auto print_str = [&](const char *label, const char *value) {
    dprintf(out_fd, "  %-22s%s\n", label, value != nullptr ? value : "(none)");
  };
  print_str("format_name:", bounded_header_str(header, header->fmt_name_offset));
  print_str("fieldlist:", bounded_header_str(header, header->fmt_fieldlist_offset));
  print_str("printf:", bounded_header_str(header, header->fmt_printf_offset));
  print_str("src_hostname:", bounded_header_str(header, header->src_hostname_offset));
  print_str("log_filename:", bounded_header_str(header, header->log_filename_offset));

  // v3 appended fmt_fieldtypes_offset after data_offset; the v2 on-disk header
  // does not contain it, so only touch it for v3 segments.
  if (header->version >= 3) {
    print_field_type_schema(header, out_fd);
  }
}

int
process_file(int in_fd, int out_fd)
{
  char    buffer[MAX_LOGBUFFER_SIZE];
  ssize_t nread, buffer_bytes;

  while (true) {
    // read the next buffer from file descriptor
    //
    Dbg(dbg_ctl_logcat, "Reading buffer ...");
    memset(buffer, 0, sizeof(buffer));

    // read the first 8 bytes of the header, which will give us the
    // cookie and the version number.
    //
    unsigned         first_read_size = sizeof(uint32_t) + sizeof(uint32_t);
    LogBufferHeader *header          = (LogBufferHeader *)&buffer[0];

    nread = read(in_fd, buffer, first_read_size);
    if (!nread || nread == EOF) {
      return 0;
    }

    // ensure that this is a valid logbuffer header
    //
    if (header->cookie != LOG_SEGMENT_COOKIE) {
      fprintf(stderr, "Bad LogBuffer!\n");
      return 1;
    }

    // The header size on disk depends on the segment version (v3 grew the
    // struct), so size the read from the version we just read rather than from
    // sizeof(LogBufferHeader). This keeps v2 files readable by a v3 build.
    //
    unsigned header_size = static_cast<unsigned>(log_buffer_header_size(header->version));
    if (header_size == 0) {
      fprintf(stderr, "Unsupported LogBuffer version %u!\n", header->version);
      return 1;
    }

    // read the rest of the header
    //
    unsigned second_read_size = header_size - first_read_size;

    nread = read(in_fd, &buffer[first_read_size], second_read_size);
    if (!nread || nread == EOF) {
      if (follow_flag) {
        return 0;
      }

      fprintf(stderr, "Bad LogBufferHeader read!\n");
      return 1;
    }
    // read the rest of the buffer
    //
    uint32_t byte_count = header->byte_count;

    if (byte_count > sizeof(buffer)) {
      fprintf(stderr, "Buffer too large! byte_count=%d\n", byte_count);
      return 1;
    }
    // Guard the unsigned subtraction below: a corrupt/truncated segment whose
    // byte_count is smaller than its own header would otherwise wrap to a huge
    // read size.
    if (byte_count < header_size) {
      fprintf(stderr, "Bad LogBuffer! byte_count=%u < header_size=%u\n", byte_count, header_size);
      return 1;
    }
    buffer_bytes = byte_count - header_size;
    if (buffer_bytes == 0) {
      return 0;
    }
    if (buffer_bytes < 0) {
      fprintf(stderr, "No buffer body!\n");
      return 1;
    }
    // Read the next full buffer (allowing for "partial" reads)
    nread = 0;
    while (nread < buffer_bytes) {
      auto rc = read(in_fd, &buffer[header_size] + nread, buffer_bytes - nread);

      if ((rc == EOF) && (!follow_flag)) {
        fprintf(stderr, "Bad LogBuffer read!\n");
        return 1;
      }

      if (rc > 0) {
        nread += rc;
      }
    }

    if (nread > buffer_bytes) {
      fprintf(stderr, "Read too many bytes!\n");
      return 1;
    }
    // see if there is an alternate format request from the command
    // line
    //
    const char *alt_format = nullptr;
    // convert the buffer to ascii (or JSON) entries and place onto stdout
    //
    if (header_flag) {
      print_logbuffer_header(header, out_fd);
    } else if (json_flag) {
      write_json_logbuffer(header, out_fd);
    } else if (header->fmt_fieldlist()) {
      LogFile::write_ascii_logbuffer(header, out_fd, ".", alt_format);
    } else {
      // TODO investigate why this buffer goes wonky
    }
  }
}

int
open_output_file(char *output_file_p)
{
  int file_desc = 0;

  if (!overwrite_existing_file) {
    if (access(output_file_p, F_OK)) {
      if (errno != ENOENT) {
        fprintf(stderr, "Error accessing output file %s: ", output_file_p);
        perror(nullptr);
        file_desc = -1;
      }
    } else {
      fprintf(stderr,
              "Error, output file %s already exists.\n"
              "Select a different filename or use the -w flag\n",
              output_file_p);
      file_desc = -1;
    }
  }

  if (file_desc == 0) {
    file_desc = open(output_file_p, O_WRONLY | O_TRUNC | O_CREAT, 0640);

    if (file_desc < 0) {
      fprintf(stderr, "Error while opening output file %s: ", output_file_p);
      perror(nullptr);
    }
  }

  return file_desc;
}

} // end anonymous namespace

/*-------------------------------------------------------------------------
  main
  -------------------------------------------------------------------------*/

int
main(int /* argc ATS_UNUSED */, const char *argv[])
{
  enum {
    NO_ERROR              = 0,
    CMD_LINE_OPTION_ERROR = 1,
    DATA_PROCESSING_ERROR = 2,
  };

  // build the application information structure
  //
  auto &version = AppVersionInfo::setup_version(PROGRAM_NAME);

  runroot_handler(argv);
  // Before accessing file system initialize Layout engine
  Layout::create();
  // process command-line arguments
  //
  output_file[0] = 0;
  process_args(&version, argument_descriptions, countof(argument_descriptions), argv);

  // check that only one of the -o and -a options was specified
  //
  if (output_file[0] != 0 && auto_filenames) {
    fprintf(stderr, "Error: specify only one of -o <file> and -a\n");
    ::exit(CMD_LINE_OPTION_ERROR);
  }
  // initialize this application for standalone logging operation
  //
  init_log_standalone_basic(PROGRAM_NAME);

  Log::init(Log::LOGCAT);

  // setup output file
  //
  int out_fd = STDOUT_FILENO;
  if (output_file[0] != 0) {
    out_fd = open_output_file(output_file);

    if (out_fd < 0) {
      ::exit(DATA_PROCESSING_ERROR);
    }
  } else if (!auto_filenames) {
    out_fd = STDOUT_FILENO;
  }
  // process file arguments
  //
  int error = NO_ERROR;

  if (n_file_arguments) {
    int bin_ext_len   = strlen(LOG_FILE_BINARY_OBJECT_FILENAME_EXTENSION);
    int ascii_ext_len = strlen(LOG_FILE_ASCII_OBJECT_FILENAME_EXTENSION);

    for (unsigned i = 0; i < n_file_arguments; ++i) {
      int in_fd = open(file_arguments[i], O_RDONLY);
      if (in_fd < 0) {
        fprintf(stderr, "Error opening input file %s: ", file_arguments[i]);
        perror(nullptr);
        error = DATA_PROCESSING_ERROR;
      } else {
#if HAVE_POSIX_FADVISE
        // If we don't plan on following the log file, we should let the kernel know
        // that we plan on reading the entire file so the kernel can do
        // some fancy optimizations.
        if (!follow_flag) {
          if (posix_fadvise(in_fd, 0, 0, POSIX_FADV_WILLNEED) != 0) {
            fprintf(stderr, "Error while trying to advise kernel about file access pattern: %s\n", strerror(errno));
          }
        }

        // We're always reading the file sequentially so this will always help
        if (posix_fadvise(in_fd, 0, 0, POSIX_FADV_SEQUENTIAL) != 0) {
          fprintf(stderr, "Error while trying to advise kernel about file access pattern: %s\n", strerror(errno));
        }
#endif
        if (auto_filenames) {
          // change .blog to .log
          //
          int n = strlen(file_arguments[i]);
          int copy_len =
            (n >= bin_ext_len ?
               (strcmp(&file_arguments[i][n - bin_ext_len], LOG_FILE_BINARY_OBJECT_FILENAME_EXTENSION) == 0 ? n - bin_ext_len : n) :
               n);

          char *out_filename = (char *)ats_malloc(copy_len + ascii_ext_len + 1);

          memcpy(out_filename, file_arguments[i], copy_len);
          memcpy(&out_filename[copy_len], LOG_FILE_ASCII_OBJECT_FILENAME_EXTENSION, ascii_ext_len);
          out_filename[copy_len + ascii_ext_len] = 0;

          out_fd = open_output_file(out_filename);
          ats_free(out_filename);

          if (out_fd < 0) {
            error = DATA_PROCESSING_ERROR;
            continue;
          }
        }
        if (follow_flag) {
          lseek(in_fd, 0, SEEK_END);
        }

        ino_t inode_num = get_inode_num(file_arguments[i]);
        while (true) {
          if (process_file(in_fd, out_fd) != 0) {
            error = DATA_PROCESSING_ERROR;
            break;
          }
          if (!follow_flag) {
            break;
          } else {
            usleep(10000); // This avoids burning CPU, using poll() would have been nice, but doesn't work I think.

            // see if the file we're following has been rotated
            if (access(file_arguments[i], F_OK) == 0) { // Sometimes there's a gap between logfile rotation and the actual presence
                                                        // of a fresh file on disk. We must make sure we don't get caught in that
                                                        // gap.
              int fd = follow_rotate(file_arguments[i], inode_num);
              if (fd == -1) {
                error = DATA_PROCESSING_ERROR;
                break;
              } else if (fd > 0) {
                // we got a new fd to use
                Dbg(dbg_ctl_logcat, "Detected logfile rotation. Following to new file");
                close(in_fd);
                in_fd = fd;

                // update the inode number for the log file
                inode_num = get_inode_num(file_arguments[i]);
              }
            }
          }
        }
      }
#if HAVE_POSIX_FADVISE
      // Now that we're done reading a potentially large log file, we can tell the kernel that it's OK to evict
      // the associated log file pages from cache
      if (posix_fadvise(in_fd, 0, 0, POSIX_FADV_DONTNEED) != 0) {
        fprintf(stderr, "Error while trying to advise kernel about file access pattern: %s\n", strerror(errno));
      }
#endif
    }
  } else {
    // read from stdin, allow STDIN to go EOF a few times until we get synced
    //
    int tries = 3;
    while (--tries >= 0) {
      if (process_file(STDIN_FILENO, out_fd) != 0) {
        tries = -1;
      }
    }
  }

  ::exit(error);
}
