/** @file

    ATSCPPAPI plugin to do webp transform.

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

#ifndef WEBPTRANSFORM_H_
#define WEBPTRANSFORM_H_

#include <stdio.h>
#include <sstream>
#include <string>
#include <vector>

#include <webp/encode.h>
#include "jpegdec.h"
#include "pngdec.h"
#include "metadata.h"


class WebpTransform
{
public:
  WebpTransform() : _init(false), _png_dec(), _jpeg_dec() {}

  ~WebpTransform() {}

  bool init();
  bool transform(std::stringstream &stream);
  void finalize();
  std::stringstream &
  getTransformedImage()
  {
    return _stream;
  }

  void writeImage(const char *data, size_t data_size);

private:
  typedef enum { PNG_ = 0, JPEG_, WEBP_, UNSUPPORTED } InputFileFormat;

  enum {
    METADATA_EXIF = (1 << 0),
    METADATA_ICC = (1 << 1),
    METADATA_XMP = (1 << 2),
    METADATA_ALL = METADATA_EXIF | METADATA_ICC | METADATA_XMP
  };

  InputFileFormat _getImageType(std::stringstream &input_msg);
  int _readImage(std::stringstream &input_img);
  void _allocExtraInfo();
  void _webpMemoryWriterClear();

  static const std::string _errors[];
  bool _init;
  WebPMemoryWriter _writer;
  std::stringstream _stream;
  WebPPicture _picture;
  WebPConfig _config;
  Metadata _metadata;
  std::string _debug_tag;
  PngDec _png_dec;
  JpegDec _jpeg_dec;
};

#endif // WEBPTRANSFORM_H_
