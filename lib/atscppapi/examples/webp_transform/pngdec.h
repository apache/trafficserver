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

#ifndef PNGDEC_H_
#define PNGDEC_H_

#include <stdio.h>
#include <string>
#include <sstream>
#include <png.h>
#include <webp/types.h>
#include "pngdec.h"
#include "metadata.h"

struct WebPPicture;

class PngDec
{
public:
  PngDec() : init_(false), input_img_(NULL), info_(NULL), end_info_(NULL) {}
  ~PngDec() {}
  bool Init(std::stringstream *img);
  int ReadImage(struct WebPPicture *const pic, struct Metadata *const metadata);
  void Finalize();

private:
  struct PNGMetadataMap {
    const char *name;
    int (*process)(const char *profile, size_t profile_len, MetadataPayload *const payload);
    size_t storage_offset;
  };

  void Read(png_structp pngPtr, png_bytep data, png_size_t length);
  int ExtractMetadataFromPNG(Metadata *const metadata);

  static uint8_t *HexStringToBytes(const char *hexstring, size_t expected_length);
  static int ProcessRawProfile(const char *profile, size_t profile_len, MetadataPayload *const payload);
  static void ReadFunction(png_structp pngPtr, png_bytep data, png_size_t length);
  static void ErrorFunction(png_structp png, png_const_charp error);
  bool ReadData(png_bytep data, png_size_t len);


  bool init_;
  std::stringstream *input_img_;
  volatile png_structp png_;
  volatile png_infop info_;
  volatile png_infop end_info_;
  static PNGMetadataMap png_metadata_map_[];
};
#endif // PNGDEC_H_
