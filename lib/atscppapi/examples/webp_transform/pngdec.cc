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

#include <iostream>
#include <sstream>
#include <stdio.h>
#include <png.h>
#include <setjmp.h> // note: this must be included *after* png.h
#include <stdlib.h>
#include <string.h>
#include <atscppapi/Logger.h>

#include "webp/encode.h"
#include "metadata.h"
#include "pngdec.h"
#include "Common.h"

void
PngDec::ErrorFunction(png_structp png, png_const_charp error)
{
  if (error != NULL)
    TS_DEBUG("img_transform_png", "libpng error: %s\n", error);
  longjmp(png_jmpbuf(png), 1);
}

void
PngDec::ReadFunction(png_structp pngPtr, png_bytep data, png_size_t length)
{
  PngDec *png_dec = reinterpret_cast<PngDec *>(png_get_io_ptr(pngPtr));
  png_dec->ReadData(data, length);
}

PngDec::PNGMetadataMap PngDec::png_metadata_map_[] = {
  // http://www.sno.phy.queensu.ca/~phil/exiftool/TagNames/PNG.html#TextualData
  // See also: ExifTool on CPAN.
  {"Raw profile type exif", PngDec::ProcessRawProfile, METADATA_OFFSET(exif)},
  {"Raw profile type xmp", PngDec::ProcessRawProfile, METADATA_OFFSET(xmp)},
  // Exiftool puts exif data in APP1 chunk, too.
  {"Raw profile type APP1", PngDec::ProcessRawProfile, METADATA_OFFSET(exif)},
  // XMP Specification Part 3, Section 3 #PNG
  {"XML:com.adobe.xmp", MetadataCopy, METADATA_OFFSET(xmp)},
  {NULL, NULL, 0},
};

int
PngDec::ProcessRawProfile(const char *profile, size_t profile_len, MetadataPayload *const payload)
{
  const char *src = profile;
  char *end;
  int expected_length;

  if (profile == NULL || profile_len == 0)
    return 0;

  // ImageMagick formats 'raw profiles' as
  // '\n<name>\n<length>(%8lu)\n<hex payload>\n'.
  if (*src != '\n') {
    TS_DEBUG(TAG, "Malformed raw profile, expected '\\n' got '\\x%.2X'\n", *src);
    return 0;
  }
  ++src;
  // skip the profile name and extract the length.
  while (*src != '\0' && *src++ != '\n') {
  }
  expected_length = (int)strtol(src, &end, 10);
  if (*end != '\n') {
    TS_DEBUG(TAG, "Malformed raw profile, expected '\\n' got '\\x%.2X'\n", *end);
    return 0;
  }
  ++end;

  // 'end' now points to the profile payload.
  payload->bytes = HexStringToBytes(end, expected_length);
  if (payload->bytes == NULL) {
    TS_DEBUG(TAG, "HexStringToBytes failed");
    return 0;
  }
  payload->size = expected_length;
  return 1;
}

// Converts the NULL terminated 'hexstring' which contains 2-byte character
// representations of hex values to raw data.
// 'hexstring' may contain values consisting of [A-F][a-f][0-9] in pairs,
// e.g., 7af2..., separated by any number of newlines.
// 'expected_length' is the anticipated processed size.
// On success the raw buffer is returned with its length equivalent to
// 'expected_length'. NULL is returned if the processed length is less than
// 'expected_length' or any character aside from those above is encountered.
// The returned buffer must be freed by the caller.
uint8_t *
PngDec::HexStringToBytes(const char *hexstring, size_t expected_length)
{
  const char *src = hexstring;
  size_t actual_length = 0;
  uint8_t *const raw_data = (uint8_t *)malloc(expected_length);
  uint8_t *dst;

  if (raw_data == NULL)
    return NULL;

  for (dst = raw_data; actual_length < expected_length && *src != '\0'; ++src) {
    char *end;
    char val[3];
    if (*src == '\n')
      continue;
    val[0] = *src++;
    val[1] = *src;
    val[2] = '\0';
    *dst++ = (uint8_t)strtol(val, &end, 16);
    if (end != val + 2)
      break;
    ++actual_length;
  }
  if (actual_length != expected_length) {
    free(raw_data);
    return NULL;
  }
  return raw_data;
}


// Looks for metadata at both the beginning and end of the PNG file, giving
// preference to the head.
// Returns true on success. The caller must use MetadataFree() on 'metadata' in
// all cases.
int
PngDec::ExtractMetadataFromPNG(Metadata *const metadata)
{
  int p;
  for (p = 0; p < 2; ++p) {
    png_infop const info = (p == 0) ? info_ : end_info_;
    png_textp text = NULL;
    const int num = png_get_text(png_, info, &text, NULL);
    int i;
    // Look for EXIF / XMP metadata.
    for (i = 0; i < num; ++i, ++text) {
      int j;
      for (j = 0; png_metadata_map_[j].name != NULL; ++j) {
        if (!strcmp(text->key, png_metadata_map_[j].name)) {
          MetadataPayload *const payload = (MetadataPayload *)((uint8_t *)metadata + png_metadata_map_[j].storage_offset);
          png_size_t text_length;
          switch (text->compression) {
#ifdef PNG_iTXt_SUPPORTED
          case PNG_ITXT_COMPRESSION_NONE:
          case PNG_ITXT_COMPRESSION_zTXt:
            text_length = text->itxt_length;
            break;
#endif
          case PNG_TEXT_COMPRESSION_NONE:
          case PNG_TEXT_COMPRESSION_zTXt:
          default:
            text_length = text->text_length;
            break;
          }
          if (payload->bytes != NULL) {
            TS_DEBUG(TAG, "Ignoring additional '%s'\n", text->key);
          } else if (!png_metadata_map_[j].process(text->text, text_length, payload)) {
            TS_DEBUG(TAG, "Failed to process: '%s'\n", text->key);
            return 0;
          }
          break;
        }
      }
    }
    // Look for an ICC profile.
    {
      png_charp name;
      int comp_type;
#if ((PNG_LIBPNG_VER_MAJOR << 8) | PNG_LIBPNG_VER_MINOR << 0) < ((1 << 8) | (5 << 0))
      png_charp profile;
#else // >= libpng 1.5.0
      png_bytep profile;
#endif
      png_uint_32 len;

      if (png_get_iCCP(png_, info, &name, &comp_type, &profile, &len) == PNG_INFO_iCCP) {
        if (!MetadataCopy((const char *)profile, len, &metadata->iccp))
          return 0;
      }
    }
  }

  return 1;
}

bool
PngDec::Init(std::stringstream *img)
{
  input_img_ = img;
  png_ = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
  if (png_ == NULL) {
    TS_DEBUG(TAG, "Error! Unable to create read structure");
    return false;
  }

  png_set_error_fn(png_, 0, PngDec::ErrorFunction, NULL);
  if (setjmp(png_jmpbuf(png_))) {
    TS_DEBUG(TAG, "Error! setjmp failed");
    return false;
  }

  info_ = png_create_info_struct(png_);
  if (info_ == NULL) {
    TS_DEBUG(TAG, "Error! could not create info struct for info_");
    return false;
  }
  end_info_ = png_create_info_struct(png_);
  if (end_info_ == NULL) {
    TS_DEBUG(TAG, "Error! could not create info struct for info_");
    return false;
  }

  // png_init_io(png, in_file);
  png_set_read_fn(png_, (void *)this, PngDec::ReadFunction);
  png_read_info(png_, info_);
  init_ = true;
  return true;
}

void
PngDec::Finalize()
{
  if (init_ && png_ != NULL) {
    png_destroy_read_struct((png_structpp)&png_, (png_infopp)&info_, (png_infopp)&end_info_);
    png_ = NULL;
    info_ = end_info_ = NULL;
  }
}

int
PngDec::ReadImage(WebPPicture *const pic, Metadata *const metadata)
{
  int color_type, bit_depth, interlaced;
  int has_alpha;
  int num_passes;
  int p;
  int ok = 0;
  png_uint_32 width, height, y;
  int stride;
  uint8_t *volatile rgb = NULL;


  if (!png_get_IHDR(png_, info_, &width, &height, &bit_depth, &color_type, &interlaced, NULL, NULL)) {
    TS_DEBUG(TAG, "failed to get IHDR");
    return false;
  }

  png_set_strip_16(png_);
  png_set_packing(png_);
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png_);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    if (bit_depth < 8) {
      png_set_expand_gray_1_2_4_to_8(png_);
    }
    png_set_gray_to_rgb(png_);
  }
  if (png_get_valid(png_, info_, PNG_INFO_tRNS)) {
    png_set_tRNS_to_alpha(png_);
    has_alpha = 1;
  } else {
    has_alpha = !!(color_type & PNG_COLOR_MASK_ALPHA);
  }

  num_passes = png_set_interlace_handling(png_);
  png_read_update_info(png_, info_);
  stride = (has_alpha ? 4 : 3) * width * sizeof(*rgb);
  rgb = (uint8_t *)malloc(stride * height);
  if (rgb == NULL)
    return false;
  for (p = 0; p < num_passes; ++p) {
    for (y = 0; y < height; ++y) {
      png_bytep row = (png_bytep)(rgb + y * stride);
      png_read_rows(png_, &row, NULL, 1);
    }
  }
  png_read_end(png_, end_info_);

  if (metadata != NULL && !ExtractMetadataFromPNG(metadata)) {
    TS_DEBUG(TAG, "Error!! extracting PNG metadata!");
    free(rgb);
    return false;
  }

  pic->width = width;
  pic->height = height;
  pic->use_argb = 1;
  ok = has_alpha ? WebPPictureImportRGBA(pic, rgb, stride) : WebPPictureImportRGB(pic, rgb, stride);

  free(rgb);
  return ok;
}


bool
PngDec::ReadData(png_bytep data, png_size_t length)
{
  input_img_->read((char *)data, length);
  return true;
}
