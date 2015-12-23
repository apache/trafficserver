/**
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

#ifndef JPEGDEC_H_
#define JPEGDEC_H_

#include <stdio.h>
#include <string>
#include <sstream>
#include <jpeglib.h>
#include <setjmp.h>
#include <webp/types.h>
#include "jpegdec.h"
#include "metadata.h"

struct WebPPicture;

typedef struct {
  const uint8_t *data;
  size_t data_length;
  int seq; // this segment's sequence number [1, 255] for use in reassembly.
} ICCPSegment;

class JpegDec
{
public:
  JpegDec() : init_(false) {}
  ~JpegDec() {}
  bool Init(std::stringstream *img);
  int ReadImage(struct WebPPicture *const pic, struct Metadata *const metadata);
  void Finalize();

private:
  struct ErrorMgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
  };

  struct JpegMetadataMap {
    int marker;
    const char *signature;
    size_t signature_length;
    size_t storage_offset;
  };

  static int CompareICCPSegments(const void *a, const void *b);
  int StoreICCP(MetadataPayload *const iccp);
  int ExtractMetadataFromJPEG(Metadata *const metadata);
  void SaveMetadataMarkers();

  static void Error(j_common_ptr dinfo);

  bool init_;
  std::stringstream *input_img_;
  volatile struct jpeg_decompress_struct dinfo_;
  struct ErrorMgr jerr_;
  static JpegMetadataMap jpeg_metadata_map_[];
};

#endif // JPEGDEC_H_
