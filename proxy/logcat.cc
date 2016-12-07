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

#include "ts/ink_platform.h"
#include "ts/ink_args.h"
#include "ts/I_Layout.h"

#define PROGRAM_NAME "traffic_logcat"
#define MAX_LOGBUFFER_SIZE 65536

#include <poll.h>

#include "LogStandalone.cc"

#include "LogAccess.h"
#include "LogField.h"
#include "LogFilter.h"
#include "LogFormat.h"
#include "LogFile.h"
#include "LogHost.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogBuffer.h"
#include "LogUtils.h"
#include "LogSock.h"
#include "Log.h"

// logcat-specific command-line flags
static int squid_flag              = 0;
static int follow_flag             = 0;
static int clf_flag                = 0;
static int elf_flag                = 0;
static int elf2_flag               = 0;
static int auto_filenames          = 0;
static int overwrite_existing_file = 0;
static char output_file[1024];
int auto_clear_cache_flag = 0;

static const ArgumentDescription argument_descriptions[] = {

  {"output_file", 'o', "Specify output file", "S1023", &output_file, NULL, NULL},
  {"auto_filenames", 'a', "Automatically generate output names", "T", &auto_filenames, NULL, NULL},
  {"follow", 'f', "Follow the log file as it grows", "T", &follow_flag, NULL, NULL},
  {"clf", 'C', "Convert to Common Logging Format", "T", &clf_flag, NULL, NULL},
  {"elf", 'E', "Convert to Extended Logging Format", "T", &elf_flag, NULL, NULL},
  {"squid", 'S', "Convert to Squid Logging Format", "T", &squid_flag, NULL, NULL},
  {"debug_tags", 'T', "Colon-Separated Debug Tags", "S1023", error_tags, NULL, NULL},
  {"overwrite_output", 'w', "Overwrite existing output file(s)", "T", &overwrite_existing_file, NULL, NULL},
  {"elf2", '2', "Convert to Extended2 Logging Format", "T", &elf2_flag, NULL, NULL},
  HELP_ARGUMENT_DESCRIPTION(),
  VERSION_ARGUMENT_DESCRIPTION()};

/*
 * Gets the inode number of a given file
 *
 * @param filename name of the file
 * @returns -1 on failure, otherwise inode number
 */
static ino_t
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
static int
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

static int
process_file(int in_fd, int out_fd)
{
  char buffer[MAX_LOGBUFFER_SIZE];
  int nread, buffer_bytes;
  unsigned bytes = 0;

  while (true) {
    // read the next buffer from file descriptor
    //
    Debug("logcat", "Reading buffer ...");
    memset(buffer, 0, sizeof(buffer));

    // read the first 8 bytes of the header, which will give us the
    // cookie and the version number.
    //
    unsigned first_read_size = sizeof(uint32_t) + sizeof(uint32_t);
    unsigned header_size     = sizeof(LogBufferHeader);
    LogBufferHeader *header  = (LogBufferHeader *)&buffer[0];

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
      fprintf(stderr, "Buffer too large!\n");
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
      int rc = read(in_fd, &buffer[header_size] + nread, buffer_bytes - nread);

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
    const char *alt_format = NULL;
    // convert the buffer to ascii entries and place onto stdout
    //
    if (header->fmt_fieldlist()) {
      bytes += LogFile::write_ascii_logbuffer(header, out_fd, ".", alt_format);
    } else {
      // TODO investigate why this buffer goes wonky
    }
  }
}

static int
open_output_file(char *output_file)
{
  int file_desc = 0;

  if (!overwrite_existing_file) {
    if (access(output_file, F_OK)) {
      if (errno != ENOENT) {
        fprintf(stderr, "Error accessing output file %s: ", output_file);
        perror(0);
        file_desc = -1;
      }
    } else {
      fprintf(stderr, "Error, output file %s already exists.\n"
                      "Select a different filename or use the -w flag\n",
              output_file);
      file_desc = -1;
    }
  }

  if (file_desc == 0) {
    file_desc = open(output_file, O_WRONLY | O_TRUNC | O_CREAT, 0640);

    if (file_desc < 0) {
      fprintf(stderr, "Error while opening output file %s: ", output_file);
      perror(0);
    }
  }

  return file_desc;
}

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
  appVersionInfo.setup(PACKAGE_NAME, PROGRAM_NAME, PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // Before accessing file system initialize Layout engine
  Layout::create();
  // process command-line arguments
  //
  output_file[0] = 0;
  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv);

  // check that only one of the -o and -a options was specified
  //
  if (output_file[0] != 0 && auto_filenames) {
    fprintf(stderr, "Error: specify only one of -o <file> and -a\n");
    ::exit(CMD_LINE_OPTION_ERROR);
  }
  // initialize this application for standalone logging operation
  //
  init_log_standalone_basic(PROGRAM_NAME);

  Log::init(Log::NO_REMOTE_MANAGEMENT | Log::LOGCAT);

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
        perror(0);
        error = DATA_PROCESSING_ERROR;
      } else {
#if HAVE_POSIX_FADVISE
        // If we don't plan on following the log file, we should let the kernel know
        // that we plan on reading the entire file so the kernel can do
        // some fancy optimizations.
        if (!follow_flag)
          posix_fadvise(in_fd, 0, 0, POSIX_FADV_WILLNEED);

        // We're always reading the file sequentially so this will always help
        posix_fadvise(in_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
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
                Debug("logcat", "Detected logfile rotation. Following to new file");
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
      posix_fadvise(in_fd, 0, 0, POSIX_FADV_DONTNEED);
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
