/** @file

  MimeTableEntry and MimeTable definitions

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

#include "libts.h" /* MAGIC_EDITING_TAG */

MimeTableEntry MimeTable::m_table[] = {{"ai", "application/postscript", "8bit", "text"},
                                       {"aif", "audio/x-aiff", "binary", "sound"},
                                       {"aifc", "audio/x-aiff", "binary", "sound"},
                                       {"aiff", "audio/x-aiff", "binary", "sound"},
                                       {"arj", "application/x-arj-compressed", "binary", "binary"},
                                       {"au", "audio/basic", "binary", "sound"},
                                       {"avi", "video/x-msvideo", "binary", "movie"},
                                       {"bcpio", "application/x-bcpio", "binary", "binary"},
                                       {"bin", "application/macbinary", "macbinary", "binary"},
                                       {"c", "text/plain", "7bit", "text"},
                                       {"cc", "text/plain", "7bit", "text"},
                                       {"cdf", "application/x-netcdf", "binary", "binary"},
                                       {"cpio", "application/x-cpio", "binary", "binary"},
                                       {"csh", "application/x-csh", "7bit", "text"},
                                       {"doc", "application/msword", "binary", "binary"},
                                       {"dvi", "application/x-dvi", "binary", "binary"},
                                       {"eps", "application/postscript", "8bit", "text"},
                                       {"etx", "text/x-setext", "7bit", "text"},
                                       {"exe", "application/octet-stream", "binary", "binary"},
                                       {"f90", "text/plain", "7bit", "text"},
                                       {"gif", "image/gif", "binary", "image"},
                                       {"gtar", "application/x-gtar", "binary", "binary"},
                                       {"gz", "application/x-gzip", "x-gzip", "binary"},
                                       {"h", "text/plain", "7bit", "text"},
                                       {"hdf", "application/x-hdf", "binary", "binary"},
                                       {"hh", "text/plain", "7bit", "text"},
                                       {"hqx", "application/mac-binhex40", "mac-binhex40", "binary"},
                                       {"htm", "text/html", "8bit", "text"},
                                       {"html", "text/html", "8bit", "text"},
                                       {"ief", "image/ief", "binary", "image"},
                                       {"jpe", "image/jpeg", "binary", "image"},
                                       {"jpeg", "image/jpeg", "binary", "image"},
                                       {"jpg", "image/jpeg", "binary", "image"},
                                       {"latex", "application/x-latex", "8bit", "text"},
                                       {"lha", "application/x-lha-compressed", "binary", "binary"},
                                       {"lsm", "text/plain", "7bit", "text"},
                                       {"lzh", "application/x-lha-compressed", "binary", "binary"},
                                       {"m", "text/plain", "7bit", "text"},
                                       {"man", "application/x-troff-man", "7bit", "text"},
                                       {"me", "application/x-troff-me", "7bit", "text"},
                                       {"mif", "application/x-mif", "binary", "binary"},
                                       {"mime", "www/mime", "8bit", "text"},
                                       {"mov", "video/quicktime", "binary", "movie"},
                                       {"movie", "video/x-sgi-movie", "binary", "movie"},
                                       {"mp2", "audio/mpeg", "binary", "sound"},
                                       {"mp3", "audio/mpeg", "binary", "sound"},
                                       {"mpe", "video/mpeg", "binary", "movie"},
                                       {"mpeg", "video/mpeg", "binary", "movie"},
                                       {"mpg", "video/mpeg", "binary", "movie"},
                                       {"ms", "application/x-troff-ms", "7bit", "text"},
                                       {"msw", "application/msword", "binary", "binary"},
                                       {"mwrt", "application/macwriteii", "binary", "binary"},
                                       {"nc", "application/x-netcdf", "binary", "binary"},
                                       {"oda", "application/oda", "binary", "binary"},
                                       {"pbm", "image/x-portable-bitmap", "binary", "image"},
                                       {"pdf", "application/pdf", "binary", "binary"},
                                       {"pgm", "image/x-portable-graymap", "binary", "image"},
                                       {"pic", "application/pict", "binary", "image"},
                                       {"pict", "application/pict", "binary", "image"},
                                       {"pnm", "image/x-portable-anymap", "binary", "image"},
                                       {"ppm", "image/x-portable-pixmap", "binary", "image"},
                                       {"ps", "application/postscript", "8bit", "text"},
                                       {"qt", "video/quicktime", "binary", "movie"},
                                       {"ras", "image/cmu-raster", "binary", "image"},
                                       {"rgb", "image/x-rgb", "binary", "image"},
                                       {"roff", "application/x-troff", "7bit", "text"},
                                       {"rpm", "application/x-rpm", "binary", "binary"},
                                       {"rtf", "application/x-rtf", "7bit", "binary"},
                                       {"rtx", "text/richtext", "7bit", "text"},
                                       {"sh", "application/x-sh", "7bit", "text"},
                                       {"shar", "application/x-shar", "8bit", "text"},
                                       {"sit", "application/stuffit", "binary", "binary"},
                                       {"snd", "audio/basic", "binary", "sound"},
                                       {"src", "application/x-wais-source", "7bit", "text"},
                                       {"sv4cpio", "application/x-sv4cpio", "binary", "binary"},
                                       {"sv4crc", "application/x-sv4crc", "binary", "binary"},
                                       {"t", "application/x-troff", "7bit", "text"},
                                       {"tar", "application/x-tar", "binary", "binary"},
                                       {"tcl", "application/x-tcl", "7bit", "text"},
                                       {"tex", "application/x-tex", "8bit", "text"},
                                       {"texi", "application/x-texinfo", "7bit", "text"},
                                       {"texinfo", "application/x-texinfo", "7bit", "text"},
                                       {"tgz", "application/x-tar", "x-gzip", "binary"},
                                       {"tif", "image/tiff", "binary", "image"},
                                       {"tiff", "image/tiff", "binary", "image"},
                                       {"tr", "application/x-troff", "7bit", "text"},
                                       {"tsv", "text/tab-separated-values", "7bit", "text"},
                                       {"txt", "text/plain", "7bit", "text"},
                                       {"ustar", "application/x-ustar", "binary", "binary"},
                                       {"wav", "audio/x-wav", "binary", "sound"},
                                       {"xbm", "image/x-xbitmap", "binary", "image"},
                                       {"xpm", "image/x-xpixmap", "binary", "image"},
                                       {"xwd", "image/x-xwindowdump", "binary", "image"},
                                       {"Z", "application/x-compressed", "x-compress", "binary"},
                                       {"zip", "application/x-zip-compressed", "zip", "binary"}};
int MimeTable::m_table_size = (sizeof(MimeTable::m_table)) / (sizeof(MimeTable::m_table[0]));
MimeTableEntry MimeTable::m_unknown = {"unknown", "application/x-unknown-content-type", "binary", "unknown"};
MimeTable mimeTable;
////////////////////////////////////////////////////////////////
//
//  class MimeTable
//
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
//
//  MimeTable::get_entry_path()
//
////////////////////////////////////////////////////////////////
MimeTableEntry *
MimeTable::get_entry_path(const char *path)
{
  const char *p = strrchr(path, '.');
  MimeTableEntry *e = 0;

  if (p)
    e = get_entry(p + 1);
  else {
    /////////////////////////////////////////
    // file has no extension. make a best  //
    // guess, or return null for unknown   //
    /////////////////////////////////////////
    if (ParseRules::strcasestr(path, "index") || ParseRules::strcasestr(path, "README") || ParseRules::strcasestr(path, "ls-lR") ||
        ParseRules::strcasestr(path, "config") || (path[0] == '\0') || (path[strlen(path) - 1] == '/'))
      e = get_entry("txt");
  }
  if (e == 0)
    e = &m_unknown;

  return (e);
}

////////////////////////////////////////////////////////////////
//
//  MimeTable::get_entry()
//
////////////////////////////////////////////////////////////////
MimeTableEntry *
MimeTable::get_entry(const char *name)
{
  MimeTableEntry key;

  key.name = name;

  // do a binary search. extensions are unique
  int low = 0;
  int high = m_table_size - 1;
  int mid = ((high - low) / 2) + low;
  int found = -1;

  if (!name[0])
    return (0);


  while (1) {
    if (m_table[mid] == key) {
      found = mid;
      break;
    } else if (m_table[mid] < key) {
      if (mid == high) {
        found = -1;
        break;
      } else {
        low = mid + 1;
        mid = ((high - low) / 2) + low;
      }
    } else {
      if (mid == low) {
        found = -1;
        break;
      } else {
        high = mid - 1;
        mid = ((high - low) / 2) + low;
      }
    }
  }

  return ((found >= 0) ? (&m_table[found]) : 0);
}
