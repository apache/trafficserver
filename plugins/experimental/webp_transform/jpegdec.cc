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

#include <stdio.h>

#include <jpeglib.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <atscppapi/Logger.h>
#include "webp/encode.h"
#include "metadata.h"
#include "jpegdec.h"
#include "Common.h"

#define JPEG_APP1 (JPEG_APP0 + 1)
#define JPEG_APP2 (JPEG_APP0 + 2)


JpegDec::JpegMetadataMap JpegDec::_jpeg_metadata_map[] = {
  // Exif 2.2 Section 4.7.2 Interoperability Structure of APP1 ...
  {JPEG_APP1, "Exif\0", 6, METADATA_OFFSET(exif)},
  // XMP Specification Part 3 Section 3 Embedding XMP Metadata ... #JPEG
  // TODO(jzern) Add support for 'ExtendedXMP'
  {JPEG_APP1, "http://ns.adobe.com/xap/1.0/", 29, METADATA_OFFSET(xmp)},
  {0, NULL, 0, 0},
};

void
JpegDec::_saveMetadataMarkers()
{
  const unsigned int max_marker_length = 0xffff;
  jpeg_save_markers((j_decompress_ptr)&_dinfo, JPEG_APP1, max_marker_length); // Exif/XMP
  jpeg_save_markers((j_decompress_ptr)&_dinfo, JPEG_APP2, max_marker_length); // ICC profile
}

int
JpegDec::_compareICCPSegments(const void *a, const void *b)
{
  const ICCPSegment *s1 = (const ICCPSegment *)a;
  const ICCPSegment *s2 = (const ICCPSegment *)b;
  return s1->seq - s2->seq;
}


int
JpegDec::_storeICCP(MetadataPayload *const iccp)
{
  // ICC.1:2010-12 (4.3.0.0) Annex B.4 Embedding ICC Profiles in JPEG files
  static const char kICCPSignature[] = "ICC_PROFILE";
  static const size_t kICCPSignatureLength = 12; // signature includes '\0'
  static const size_t kICCPSkipLength = 14;      // signature + seq & count
  int expected_count = 0;
  int actual_count = 0;
  int seq_max = 0;
  size_t total_size = 0;
  ICCPSegment iccp_segments[255];
  jpeg_saved_marker_ptr marker;

  memset(iccp_segments, 0, sizeof(iccp_segments));
  for (marker = _dinfo.marker_list; marker != NULL; marker = marker->next) {
    if (marker->marker == JPEG_APP2 && marker->data_length > kICCPSkipLength &&
        !memcmp(marker->data, kICCPSignature, kICCPSignatureLength)) {
      // ICC_PROFILE\0<seq><count>; 'seq' starts at 1.
      const int seq = marker->data[kICCPSignatureLength];
      const int count = marker->data[kICCPSignatureLength + 1];
      const size_t segment_size = marker->data_length - kICCPSkipLength;
      ICCPSegment *segment;
      if (segment_size == 0 || count == 0 || seq == 0) {
        TS_DEBUG(TAG, "[ICCP] size (%d) / count (%d) / sequence number (%d) cannot be 0!", (int)segment_size, seq, count);
        return 0;
      }

      if (expected_count == 0) {
        expected_count = count;
      } else if (expected_count != count) {
        TS_DEBUG(TAG, "[ICCP] Inconsistent segment count (%d / %d)!\n", expected_count, count);
        return 0;
      }

      segment = iccp_segments + seq - 1;
      if (segment->data_length != 0) {
        TS_DEBUG(TAG, "[ICCP] Duplicate segment number (%d)!\n", seq);
        return 0;
      }

      segment->data = marker->data + kICCPSkipLength;
      segment->data_length = segment_size;
      segment->seq = seq;
      total_size += segment_size;
      if (seq > seq_max)
        seq_max = seq;
      ++actual_count;
    }
  }

  if (actual_count == 0)
    return 1;
  if (seq_max != actual_count) {
    TS_DEBUG(TAG, "[ICCP] Discontinuous segments, expected: %d actual: %d!\n", actual_count, seq_max);
    return 0;
  }
  if (expected_count != actual_count) {
    TS_DEBUG(TAG, "[ICCP] Segment count: %d does not match expected: %d!\n", actual_count, expected_count);
    return 0;
  }

  // The segments may appear out of order in the file, sort them based on
  // sequence number before assembling the payload.
  qsort(iccp_segments, actual_count, sizeof(*iccp_segments), JpegDec::_compareICCPSegments);

  iccp->bytes = (uint8_t *)malloc(total_size);
  if (iccp->bytes == NULL)
    return 0;
  iccp->size = total_size;

  {
    int i;
    size_t offset = 0;
    for (i = 0; i < seq_max; ++i) {
      memcpy(iccp->bytes + offset, iccp_segments[i].data, iccp_segments[i].data_length);
      offset += iccp_segments[i].data_length;
    }
  }
  return 1;
}

// Returns true on success and false for memory errors and corrupt profiles.
// The caller must use MetadataFree() on 'metadata' in all cases.
int
JpegDec::_extractMetadataFromJPEG(Metadata *const metadata)
{
  jpeg_saved_marker_ptr marker;
  // Treat ICC profiles separately as they may be segmented and out of order.
  if (!_storeICCP(&metadata->iccp))
    return 0;

  for (marker = _dinfo.marker_list; marker != NULL; marker = marker->next) {
    int i;
    for (i = 0; _jpeg_metadata_map[i].marker != 0; ++i) {
      if (marker->marker == _jpeg_metadata_map[i].marker && marker->data_length > _jpeg_metadata_map[i].signature_length &&
          !memcmp(marker->data, _jpeg_metadata_map[i].signature, _jpeg_metadata_map[i].signature_length)) {
        MetadataPayload *const payload = (MetadataPayload *)((uint8_t *)metadata + _jpeg_metadata_map[i].storage_offset);

        if (payload->bytes == NULL) {
          const char *marker_data = (const char *)marker->data + _jpeg_metadata_map[i].signature_length;
          const size_t marker_data_length = marker->data_length - _jpeg_metadata_map[i].signature_length;
          if (!MetadataCopy(marker_data, marker_data_length, payload))
            return 0;
        } else {
          TS_DEBUG(TAG, "Ignoring additional '%s' marker\n", _jpeg_metadata_map[i].signature);
        }
      }
    }
  }
  return 1;
}

#undef JPEG_APP1
#undef JPEG_APP2

// -----------------------------------------------------------------------------
// JPEG decoding


void
JpegDec::_error(j_common_ptr dinfo)
{
  struct JpegDec::ErrorMgr *err = (struct JpegDec::ErrorMgr *)dinfo->err;
  dinfo->err->output_message(dinfo);
  longjmp(err->setjmp_buffer, 1);
}

bool
JpegDec::init(std::stringstream *img)
{
  _input_img = img;

  memset((j_decompress_ptr)&_dinfo, 0, sizeof(_dinfo)); // for setjmp sanity
  _dinfo.err = jpeg_std_error(&_jerr.pub);
  _jerr.pub.error_exit = JpegDec::_error;

  if (setjmp(_jerr.setjmp_buffer)) {
    TS_DEBUG(TAG, " setjmp failed");
    jpeg_destroy_decompress((j_decompress_ptr)&_dinfo);
    return false;
  }

  jpeg_create_decompress((j_decompress_ptr)&_dinfo);
  _init = true;
  return true;
}

int
JpegDec::readImage(WebPPicture *const pic, Metadata *const metadata)
{
  int ok = 0;
  int stride, width, height;
  uint8_t *volatile rgb = NULL;
  JSAMPROW buffer[1];

  std::string img_str = _input_img->str();
  // TODO: Can this copy be avoided.
  jpeg_mem_src((j_decompress_ptr)&_dinfo, (unsigned char *)img_str.data(), img_str.size());

  if (metadata != NULL)
    _saveMetadataMarkers();
  jpeg_read_header((j_decompress_ptr)&_dinfo, TRUE);

  _dinfo.out_color_space = JCS_RGB;
  _dinfo.do_fancy_upsampling = TRUE;

  jpeg_start_decompress((j_decompress_ptr)&_dinfo);

  if (_dinfo.output_components != 3) {
    TS_DEBUG(TAG, "not enought output componenets available.");
    return 0;
  }

  width = _dinfo.output_width;
  height = _dinfo.output_height;
  stride = _dinfo.output_width * _dinfo.output_components * sizeof(*rgb);

  rgb = (uint8_t *)malloc(stride * height);
  if (rgb == NULL) {
    TS_DEBUG(TAG, "unable to alloc memory for rgb.");
    return 0;
  }
  buffer[0] = (JSAMPLE *)rgb;

  while (_dinfo.output_scanline < _dinfo.output_height) {
    if (jpeg_read_scanlines((j_decompress_ptr)&_dinfo, buffer, 1) != 1) {
      free(rgb);
      return 0;
    }
    buffer[0] += stride;
  }

  if (metadata != NULL) {
    ok = _extractMetadataFromJPEG(metadata);
    if (!ok) {
      TS_DEBUG(TAG, "Error extracting JPEG metadata!");
      free(rgb);
      return 0;
    }
  }

  jpeg_finish_decompress((j_decompress_ptr)&_dinfo);
  jpeg_destroy_decompress((j_decompress_ptr)&_dinfo);

  // WebP conversion.
  pic->width = width;
  pic->height = height;
  pic->use_argb = 1; // store raw RGB samples
  ok = WebPPictureImportRGB(pic, rgb, stride);
  if (!ok) {
    TS_DEBUG(TAG, "Unable to import inot webp");
  }

  free(rgb);
  return ok;
}

void
JpegDec::finalize()
{
  if (_init)
    jpeg_destroy_decompress((j_decompress_ptr)&_dinfo);
}
