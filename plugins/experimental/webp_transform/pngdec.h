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
  PngDec() : _init(false), _input_img(NULL), _info(NULL), _end_info(NULL) {}
  ~PngDec() {}
  bool init(std::stringstream *img);
  int readImage(struct WebPPicture *const pic, struct Metadata *const metadata);
  void finalize();

private:
  struct PNGMetadataMap {
    const char *name;
    int (*process)(const char *profile, size_t profile_len, MetadataPayload *const payload);
    size_t storage_offset;
  };

  void _read(png_structp pngPtr, png_bytep data, png_size_t length);
  int _extractMetadataFromPNG(Metadata *const metadata);

  static uint8_t *_hexStringToBytes(const char *hexstring, size_t expected_length);
  static int _processRawProfile(const char *profile, size_t profile_len, MetadataPayload *const payload);
  static void _readFunction(png_structp pngPtr, png_bytep data, png_size_t length);
  static void _errorFunction(png_structp png, png_const_charp error);
  bool _readData(png_bytep data, png_size_t len);


  bool _init;
  std::stringstream *_input_img;
  volatile png_structp _png;
  volatile png_infop _info;
  volatile png_infop _end_info;
  static PNGMetadataMap _png_metadata_map[];
};
#endif // PNGDEC_H_
