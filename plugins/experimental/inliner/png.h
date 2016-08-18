/** @file

  Inlines base64 images from the ATS cache

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
#ifndef PNG_H
#define PNG_H

#include <algorithm>
#include <exception>
#include <string>
#include <vector>

namespace ats
{
namespace inliner
{
  struct PNG {
    typedef std::vector<char> Content;
    typedef Content::const_iterator Iterator;

    static const uint32_t HEADER_SIZE = 8;

    Content content;

    template <class C>
    static bool
    verifySignature(const C &content)
    {
      const char SIGNATURE[] = {static_cast<char>(0x89), 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

      return content.size() >= HEADER_SIZE && std::equal(SIGNATURE, SIGNATURE + HEADER_SIZE, content.begin());
    }

    PNG(const Content &c) : content(c)
    {
      if (!verifySignature(content)) {
        throw std::exception();
      }
    }

    class ChunkHeader
    {
      unsigned char length_[4];
      char type_[4];

    public:
      uint32_t
      length(void) const
      {
        return (length_[0] << 24) | (length_[1] << 16) | (length_[2] << 8) | length_[3];
      }

      std::string
      type(void) const
      {
        return std::string(type_, 4);
      }
    };

    // REFERENCE: http://en.wikipedia.org/wiki/Portable_Network_Graphics#.22Chunks.22_within_the_file
    void
    stripMetaData(Content &output) const
    {
      output.clear();
      output.reserve(content.size());

      // chunk length (4) + chunk type (4) + chunk crc (4)
      const int32_t N = 12;

      const char *iterator  = content.data();
      const char *const end = iterator + content.size();

      if (std::distance(iterator, end) > HEADER_SIZE) {
        std::copy(iterator, iterator + HEADER_SIZE, std::back_inserter(output));

        iterator += HEADER_SIZE;

        while (iterator < end) {
          const ChunkHeader *const header = reinterpret_cast<const ChunkHeader *>(iterator);
          const std::string type          = header->type();
          const uint32_t length           = header->length();

          // iterator cannot go backwards
          if (iterator >= iterator + (length + N)) {
            output.clear();
            break;
          }

          if (type == "IDAT" || type == "IEND" || type == "IHDR" || type == "PLTE" || type == "tRNS") {
            // skip chunk in case it is bigger than the whole image to prevent overflows
            if (std::distance(iterator, end) >= length + N) {
              std::copy(iterator, iterator + length + N, std::back_inserter(output));
            }
          }

          iterator += length + N;
        }
      }
    }
  };

  // const uint32_t PNG::HEADER_SIZE = 8;
} // end of inliner namespace
} // end of ats namespace

#endif // PNG_H
