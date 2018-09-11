/** @file

  Print block device geometry.

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

#include "tscore/ink_platform.h"
#include "tscore/ink_defs.h"
#include "tscore/ink_file.h"

// This isn't really a unit test. It's just a dumb little program to probe the disk
// geometry of an arbitrary device file. That's useful when figuring out how ATS will
// perceive different devices on differen operating systems.

int
main(int argc, const char **argv)
{
  for (int i = 1; i < argc; ++i) {
    int fd;
    ink_device_geometry geometry;

    fd = open(argv[i], O_RDONLY);
    if (fd == -1) {
      fprintf(stderr, "open(%s): %s\n", argv[i], strerror(errno));
      continue;
    }

    if (ink_file_get_geometry(fd, geometry)) {
      printf("%s:\n", argv[i]);
      printf("\ttotalsz: %" PRId64 "\n", geometry.totalsz);
      printf("\tblocksz: %u\n", geometry.blocksz);
      printf("\talignsz: %u\n", geometry.alignsz);
    } else {
      printf("%s: %s (%d)\n", argv[i], strerror(errno), errno);
    }

    close(fd);
  }

  return 0;
}
