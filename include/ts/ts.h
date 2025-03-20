/** @file

  Traffic Server SDK API header file

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

  @section developers Developers

  Developers, when adding a new element to an enum, append it. DO NOT
  insert it.  Otherwise, binary compatibility of plugins will be broken!

 */

#pragma once

#if !defined(__cplusplus) || __cplusplus < 201703L
#error "Must compile ATS plugin code with C++ version 17 or later."
#endif

#include <type_traits>
#include <vector>

#include "tsutil/DbgCtl.h"
#include "ts/apidefs.h"

class DiagsConfigState;

/* --------------------------------------------------------------------------
   Memory */
void  *_TSmalloc(size_t size, const char *path);
void  *_TSrealloc(void *ptr, size_t size, const char *path);
char  *_TSstrdup(const char *str, int64_t length, const char *path);
size_t TSstrlcpy(char *dst, const char *str, size_t siz);
size_t TSstrlcat(char *dst, const char *str, size_t siz);
void   TSfree(void *ptr);

inline void *
TSmalloc(size_t s)
{
  return _TSmalloc(s, TS_RES_MEM_PATH);
}
inline void *
TSrealloc(void *p, size_t s)
{
  return _TSrealloc(p, s, TS_RES_MEM_PATH);
}
inline char *
TSstrdup(const char *p)
{
  return _TSstrdup(p, -1, TS_RES_MEM_PATH);
}
inline char *
TSstrndup(const char *p, int64_t n)
{
  return _TSstrdup(p, n, TS_RES_MEM_PATH);
}

/* --------------------------------------------------------------------------
   Component object handles */
/**
    Releases the TSMLoc mloc created from the TSMLoc parent.
    If there is no parent TSMLoc, use TS_NULL_MLOC.

    @param bufp marshal buffer containing the TSMLoc handle to be
      released.
    @param parent location of the parent object from which the handle
      was created.
    @param mloc location of the handle to be released.

 */
TSReturnCode TSHandleMLocRelease(TSMBuffer bufp, TSMLoc parent, TSMLoc mloc);

/* --------------------------------------------------------------------------
   Install and plugin locations */
/**
    Gets the path of the directory in which Traffic Server is installed.
    Use this function to specify the location of files that the
    plugin uses.

    @return pointer to Traffic Server install directory.

 */
const char *TSInstallDirGet(void);

/**
    Gets the path of the directory of Traffic Server configuration.

    @return pointer to Traffic Server configuration directory.

 */
const char *TSConfigDirGet(void);

/**
    Gets the path of the directory of Traffic Server runtime.

    @return pointer to Traffic Server runtime directory.

 */
const char *TSRuntimeDirGet(void);

/**
    Gets the path of the plugin directory relative to the Traffic Server
    install directory. For example, to open the file "config_ui.txt" in
    the plugin directory:

    @code
    TSfopen("TSPluginInstallDirGet()/TSPluginDirGet()/config_ui.txt");
    @endcode

    @return pointer to plugin directory relative to Traffic Server install
    directory.

 */
const char *TSPluginDirGet(void);

/* --------------------------------------------------------------------------
   Traffic Server Version */
/**
    Gets the version of Traffic Server currently running. Use this
    function to make sure that the plugin version and Traffic Server
    version are compatible. See the SDK sample code for usage.

    @return pointer to version of Traffic Server running the plugin.

 */
const char *TSTrafficServerVersionGet(void);

/**  Get the major version of Traffic Server currently running.
     This is the same as the first element of the string
     returned by @c TSTrafficServerVersionGet

     @return The major version as an integer.
 */
int TSTrafficServerVersionGetMajor(void);

/**  Get the minor version of Traffic Server currently running.
     This is the same as the second element of the string
     returned by @c TSTrafficServerVersionGet

     @return The minor version as an integer.
 */
int TSTrafficServerVersionGetMinor(void);

/**  Get the patch version of Traffic Server currently running.
     This is the same as the third element of the string
     returned by @c TSTrafficServerVersionGet

     @return The patch version as an integer.
 */
int TSTrafficServerVersionGetPatch(void);

/* --------------------------------------------------------------------------
   Plugin registration */

/**
    This function registers your plugin with a particular version
    of Traffic Server SDK. Use this function to make sure that the
    Traffic Server version currently running also supports your plugin.
    See the SDK sample code for usage.

    @param plugin_info contains registration information about your
      plugin. See TSPluginRegistrationInfo.
    @return TS_ERROR if the plugin registration failed.

 */
TSReturnCode TSPluginRegister(const TSPluginRegistrationInfo *plugin_info);

/**
   This function provides the ability to enable/disable programmatically
   the plugin dynamic reloading when the same Dynamic Shared Object (DSO)
   is also used as a remap plugin. This overrides `proxy.config.plugin.dynamic_reload_mode`
   configuration variable.

   @param enabled boolean flag. 0/false will disable the reload on the caller plugin.
   @return TS_ERROR if the function is not called from within TSPluginInit or if TS is
           unable to get the canonical path from the plugin's path. TS_SUCCESS otherwise.

   @note This function should be called from within TSPluginInit
 */
TSReturnCode TSPluginDSOReloadEnable(int enabled);

/* --------------------------------------------------------------------------
   Files */
/**
    Opens a file for reading or writing and returns a descriptor for
    accessing the file. The current implementation cannot open a file
    for both reading or writing. See the SDK Programmer's Guide for
    sample code.

    @param filename file to be opened.
    @param mode specifies whether to open the file for reading or
      writing. If mode is "r" then the file is opened for reading.
      If mode is "w" then the file is opened for writing. Currently
      "r" and "w" are the only two valid modes for opening a file.
    @return descriptor for the file that TSfopen opens. Descriptors of
      type TSFile can be greater than 256.

 */
TSFile TSfopen(const char *filename, const char *mode);

/**
    Closes the file to which filep points and frees the data structures
    and buffers associated with it. If the file was opened for writing,
    any pending data is flushed.

    @param filep file to be closed.

 */
void TSfclose(TSFile filep);

/**
    Attempts to read length bytes of data from the file pointed to by
    filep into the buffer buf.

    @param filep name of the file to read from.
    @param buf buffer to read into.
    @param length amount of data to read, in bytes.
    @return number of bytes read. If end of the file, it returns 0.
      If the file was not opened for reading or if an error occurs
      while reading the file, it returns -1.

 */
ssize_t TSfread(TSFile filep, void *buf, size_t length);

/**
    Attempts to write length bytes of data from the buffer buf
    to the file filep. Make sure that filep is open for writing.
    You might want to check the number of bytes written (TSfwrite()
    returns this value) against the value of length. If it is less,
    there might be insufficient space on disk, for example.

    @param filep file to write into.
    @param buf buffer containing the data to be written.
    @param length amount of data to write to filep, in bytes.
    @return number of bytes written to filep. If the file was not
      opened for writing, it returns -1. If an error occurs while
      writing, it returns the number of bytes successfully written.

 */
ssize_t TSfwrite(TSFile filep, const void *buf, size_t length);

/**
    Flushes pending data that has been buffered up in memory from
    previous calls to TSfwrite().

    @param filep file to flush.

 */
void TSfflush(TSFile filep);

/**
    Reads a line from the file pointed to by filep into the buffer buf.
    Lines are terminated by a line feed character, '\n'. The line
    placed in the buffer includes the line feed character and is
    terminated with a null. If the line is longer than length bytes
    then only the first length-minus-1 bytes are placed in buf.

    @param filep file to read from.
    @param buf buffer to read into.
    @param length size of the buffer to read into.
    @return pointer to the string read into the buffer buf.

 */
char *TSfgets(TSFile filep, char *buf, size_t length);

/* --------------------------------------------------------------------------
   Error logging */
/**
    Writes printf-style error messages to the Traffic Server error
    log. One advantage of TSError over printf is that each call is
    atomically placed into the error log and is not garbled with other
    error entries. This is not an issue in single-threaded programs
    but is a definite nuisance in multi-threaded programs.

    @param fmt printf format description.
    @param ... argument for the printf format description.

    Note: Your log monitoring (e.g. Splunk) needs to alert Ops of log
    messages that contain ' ALERT: ' or ' EMERGENCY: ', these require
    immediate attention.

*/
void TSStatus(const char *fmt, ...) TS_PRINTFLIKE(1, 2);    // Log information
void TSNote(const char *fmt, ...) TS_PRINTFLIKE(1, 2);      // Log significant information
void TSWarning(const char *fmt, ...) TS_PRINTFLIKE(1, 2);   // Log concerning information
void TSError(const char *fmt, ...) TS_PRINTFLIKE(1, 2);     // Log operational failure, fail CI
void TSFatal(const char *fmt, ...) TS_PRINTFLIKE(1, 2);     // Log recoverable crash, fail CI, exit & restart
void TSAlert(const char *fmt, ...) TS_PRINTFLIKE(1, 2);     // Log recoverable crash, fail CI, exit & restart, Ops attention
void TSEmergency(const char *fmt, ...) TS_PRINTFLIKE(1, 2); // Log unrecoverable crash, fail CI, exit, Ops attention

/* --------------------------------------------------------------------------
   Assertions */
void _TSReleaseAssert(const char *txt, const char *f, int l) TS_NORETURN;
int  _TSAssert(const char *txt, const char *f, int l);

#define TSReleaseAssert(EX) ((void)((EX) ? (void)0 : _TSReleaseAssert(#EX, __FILE__, __LINE__)))

#define TSAssert(EX) (void)((EX) || (_TSAssert(#EX, __FILE__, __LINE__)))

/* --------------------------------------------------------------------------
   Marshal buffers */
/**
    Creates a new marshal buffer and initializes the reference count
    to 1.

 */
TSMBuffer TSMBufferCreate(void);

/**
    Ignores the reference count and destroys the marshal buffer bufp.
    The internal data buffer associated with the marshal buffer is
    also destroyed if the marshal buffer allocated it.

    @param bufp marshal buffer to be destroyed.

 */
TSReturnCode TSMBufferDestroy(TSMBuffer bufp);

/* --------------------------------------------------------------------------
   URLs */
/**
    Creates a new URL within the marshal buffer bufp. Returns a
    location for the URL within the marshal buffer.

    @param bufp marshal buffer containing the new URL.
    @param locp pointer to a TSMLoc to store the MLoc into.

 */
TSReturnCode TSUrlCreate(TSMBuffer bufp, TSMLoc *locp);

/**
    Copies the URL located at src_url within src_bufp to a URL
    location within the marshal buffer dest_bufp, and returns the
    TSMLoc location of the copied URL. Unlike TSUrlCopy(), you do
    not have to create the destination URL before cloning. Release
    the returned TSMLoc handle with a call to TSHandleMLocRelease().

    @param dest_bufp marshal buffer containing the cloned URL.
    @param src_bufp marshal buffer containing the URL to be cloned.
    @param src_url location of the URL to be cloned, within the marshal
      buffer src_bufp.
    @param locp pointer to a TSMLoc to store the MLoc into.

 */
TSReturnCode TSUrlClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_url, TSMLoc *locp);

/**
    Copies the contents of the URL at location src_loc within the
    marshal buffer src_bufp to the location dest_loc within the marshal
    buffer dest_bufp. TSUrlCopy() works correctly even if src_bufp
    and dest_bufp point to different marshal buffers. Important: create
    the destination URL before copying into it. Use TSUrlCreate().

    @param dest_bufp marshal buffer to contain the copied URL.
    @param dest_offset location of the URL to be copied.
    @param src_bufp marshal buffer containing the source URL.
    @param src_offset location of the source URL within src_bufp.

 */
TSReturnCode TSUrlCopy(TSMBuffer dest_bufp, TSMLoc dest_offset, TSMBuffer src_bufp, TSMLoc src_offset);

/**
    Formats a URL stored in an TSMBuffer into an TSIOBuffer.

    @param bufp marshal buffer contain the URL to be printed.
    @param offset location of the URL within bufp.
    @param iobufp destination TSIOBuffer for the URL.

 */
void TSUrlPrint(TSMBuffer bufp, TSMLoc offset, TSIOBuffer iobufp);

/**
    Parses a URL. The start pointer is both an input and an output
    parameter and marks the start of the URL to be parsed. After
    a successful parse, the start pointer equals the end pointer.
    The end pointer must be one byte after the last character you
    want to parse. The URL parsing routine assumes that everything
    between start and end is part of the URL. It is up to higher level
    parsing routines, such as TSHttpHdrParseReq(), to determine the
    actual end of the URL. Returns TS_PARSE_ERROR if an error occurs,
    otherwise TS_PARSE_DONE is returned to indicate success.

    @param bufp marshal buffer containing the URL to be parsed.
    @param offset location of the URL to be parsed.
    @param start points to the start of the URL to be parsed AND at
      the end of a successful parse it will equal the end pointer.
    @param end must be one byte after the last character.
    @return TS_PARSE_ERROR or TS_PARSE_DONE.

 */
TSParseResult TSUrlParse(TSMBuffer bufp, TSMLoc offset, const char **start, const char *end);

/**
    Calculates the length of the URL located at url_loc within the
    marshal buffer bufp if it were returned as a string. This length
    is the same as the length returned by TSUrlStringGet().

    @param bufp marshal buffer containing the URL whose length you want.
    @param offset location of the URL within the marshal buffer bufp.
    @return string length of the URL.

 */
int TSUrlLengthGet(TSMBuffer bufp, TSMLoc offset);

/**
    Constructs a string representation of the URL located at url_loc
    within bufp. TSUrlStringGet() stores the length of the allocated
    string in the parameter length. This is the same length that
    TSUrlLengthGet() returns. The returned string is allocated by a
    call to TSmalloc(). It should be freed by a call to TSfree().
    The length parameter must be present, providing storage for the
    URL string length value.
    Note: To get the effective URL from a request, use the alternative
          TSHttpTxnEffectiveUrlStringGet or
          TSHttpHdrEffectiveUrlBufGet APIs.

    @param bufp marshal buffer containing the URL you want to get.
    @param offset location of the URL within bufp.
    @param length string length of the URL.
    @return The URL as a string.

 */
char *TSUrlStringGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Retrieves the scheme portion of the URL located at url_loc within
    the marshal buffer bufp. TSUrlSchemeGet() places the length of
    the string in the length argument. If the length is null then no
    attempt is made to dereference it.

    @param bufp marshal buffer storing the URL.
    @param offset location of the URL within bufp.
    @param length length of the returned string.
    @return The scheme portion of the URL, as a string.

 */
const char *TSUrlRawSchemeGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Retrieves the scheme portion of the URL located at url_loc within
    the marshal buffer bufp. TSUrlSchemeGet() places the length of
    the string in the length argument. If the length is null then no
    attempt is made to dereference it.  If there is no explicit scheme,
    a scheme of http is returned if the URL type is HTTP, and a scheme
    of https is returned if the URL type is HTTPS.

    @param bufp marshal buffer storing the URL.
    @param offset location of the URL within bufp.
    @param length length of the returned string.
    @return The scheme portion of the URL, as a string.

 */
const char *TSUrlSchemeGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Sets the scheme portion of the URL located at url_loc within
    the marshal buffer bufp to the string value. If length is -1
    then TSUrlSchemeSet() assumes that value is null-terminated.
    Otherwise, the length of the string value is taken to be length.
    TSUrlSchemeSet() copies the string to within bufp, so it is OK
    to modify or delete value after calling TSUrlSchemeSet().

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param value value to set the URL's scheme to.
    @param length string stored in value.

 */
TSReturnCode TSUrlSchemeSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

/* --------------------------------------------------------------------------
   Internet specific URLs */
/**
    Retrieves the user portion of the URL located at url_loc
    within bufp. Note: the returned string is not guaranteed to
    be null-terminated.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param length length of the returned string.
    @return user portion of the URL.

 */
const char *TSUrlUserGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Sets the user portion of the URL located at url_loc within bufp
    to the string value. If length is -1 then TSUrlUserSet() assumes
    that value is null-terminated. Otherwise, the length of the string
    value is taken to be length. TSUrlUserSet() copies the string to
    within bufp, so it is OK to modify or delete value after calling
    TSUrlUserSet().

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL whose user is to be set.
    @param value holds the new user name.
    @param length string length of value.

 */
TSReturnCode TSUrlUserSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

/**
    Retrieves the password portion of the URL located at url_loc
    within bufp. TSUrlPasswordGet() places the length of the returned
    string in the length argument. Note: the returned string is
    not guaranteed to be null-terminated.

    @param bufp marshal buffer containing the URL.
    @param offset
    @param length of the returned password string.
    @return password portion of the URL.

 */
const char *TSUrlPasswordGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Sets the password portion of the URL located at url_loc within
    bufp to the string value. If length is -1 then TSUrlPasswordSet()
    assumes that value is null-terminated. Otherwise, the length
    of value is taken to be length. TSUrlPasswordSet() copies the
    string to within bufp, so it is okay to modify or delete value
    after calling TSUrlPasswordSet().

    @param bufp marshal buffer containing the URL.
    @param offset
    @param value new password.
    @param length of the new password.

 */
TSReturnCode TSUrlPasswordSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

/**
    Retrieves the host portion of the URL located at url_loc
    within bufp. Note: the returned string is not guaranteed to be
    null-terminated.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param length of the returned string.
    @return Host portion of the URL.

 */
const char *TSUrlHostGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Sets the host portion of the URL at url_loc to the string value.
    If length is -1 then TSUrlHostSet() assumes that value is
    null-terminated. Otherwise, the length of the string value is
    taken to be length. The string is copied to within bufp, so you
    can modify or delete value after calling TSUrlHostSet().

    @param bufp marshal buffer containing the URL to modify.
    @param offset location of the URL.
    @param value new host name for the URL.
    @param length string length of the new host name of the URL.

 */
TSReturnCode TSUrlHostSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

/**
    Returns the port portion of the URL located at url_loc if explicitly present,
    otherwise the canonical port for the URL.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @return port portion of the URL.

 */
int TSUrlPortGet(TSMBuffer bufp, TSMLoc offset);

/**
    Returns the port portion of the URL located at url_loc if explicitly present,
    otherwise 0.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @return port portion of the URL.

 */
int TSUrlRawPortGet(TSMBuffer bufp, TSMLoc offset);

/**
    Sets the port portion of the URL located at url_loc.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param port new port setting for the URL.

 */
TSReturnCode TSUrlPortSet(TSMBuffer bufp, TSMLoc offset, int port);

/* --------------------------------------------------------------------------
   HTTP specific URLs */
/**
    Retrieves the path portion of the URL located at url_loc within
    bufp. TSUrlPathGet() places the length of the returned string in
    the length argument. Note: the returned string is not guaranteed to
    be null-terminated.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param length of the returned string.
    @return path portion of the URL.

 */
const char *TSUrlPathGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Sets the path portion of the URL located at url_loc within bufp
    to the string value. If length is -1 then TSUrlPathSet() assumes
    that value is null-terminated. Otherwise, the length of the value
    is taken to be length. TSUrlPathSet() copies the string into bufp,
    so you can modify or delete value after calling TSUrlPathSet().

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param value new path string for the URL.
    @param length of the new path string.

 */
TSReturnCode TSUrlPathSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

/* --------------------------------------------------------------------------
   FTP specific URLs */
/**
    Retrieves the FTP type of the URL located at url_loc within bufp.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @return FTP type of the URL.

 */
int TSUrlFtpTypeGet(TSMBuffer bufp, TSMLoc offset);

/**
    Sets the FTP type portion of the URL located at url_loc within
    bufp to the value type.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL to modify.
    @param type new FTP type for the URL.

 */
TSReturnCode TSUrlFtpTypeSet(TSMBuffer bufp, TSMLoc offset, int type);

/* --------------------------------------------------------------------------
   HTTP specific URLs */

/**
    Retrieves the HTTP query portion of the URL located at url_loc
    within bufp. The length of the returned string is in the length
    argument. Note: the returned string is not guaranteed to be
    null-terminated.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param length of the returned string.
    @return HTTP query portion of the URL.

 */
const char *TSUrlHttpQueryGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Sets the HTTP query portion of the URL located at url_loc within
    bufp to value. If length is -1, the string value is assumed to
    be null-terminated; otherwise, the length of value is taken to be
    length. TSUrlHttpQuerySet() copies the string to within bufp, so
    you can modify or delete value after calling TSUrlHttpQuerySet().

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL within bufp.
    @param value new HTTP query string for the URL.
    @param length of the new HTTP query string.

 */
TSReturnCode TSUrlHttpQuerySet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

/**
    Retrieves the HTTP fragment portion of the URL located at url_loc
    within bufp. The length of the returned string is in the length
    argument. Note: the returned string is not guaranteed to be
    null-terminated.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param length of the returned string.
    @return HTTP fragment portion of the URL.

 */
const char *TSUrlHttpFragmentGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Sets the HTTP fragment portion of the URL located at url_loc
    within bufp to value. If length is -1, the string value is
    assumed to be null-terminated; otherwise, the length of value
    is taken to be length. TSUrlHttpFragmentSet() copies the string
    to within bufp, so you can modify or delete value after calling
    TSUrlHttpFragmentSet().

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL within bufp.
    @param value new HTTP fragment string for the URL.
    @param length of the new HTTP query string.

 */
TSReturnCode TSUrlHttpFragmentSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

/**
   Perform percent-encoding of the string in the buffer, storing the
   new string in the destination buffer. The length parameter will be
   set to the new (encoded) string length, or 0 if the encoding failed.

   @param str the string buffer to encode.
   @param str_len length of the string buffer.
   @param dst destination buffer.
   @param dst_size size of the destination buffer.
   @param length amount of data written to the destination buffer.
   @param map optional (can be null) map of characters to encode.

*/
TSReturnCode TSStringPercentEncode(const char *str, int str_len, char *dst, size_t dst_size, size_t *length,
                                   const unsigned char *map);

/**
   Similar to TSStringPercentEncode(), but works on a URL object.

   @param bufp marshal buffer containing the URL.
   @param offset location of the URL within bufp.
   @param dst destination buffer.
   @param dst_size size of the destination buffer.
   @param length amount of data written to the destination buffer.
   @param map optional (can be null) map of characters to encode.

*/
TSReturnCode TSUrlPercentEncode(TSMBuffer bufp, TSMLoc offset, char *dst, size_t dst_size, size_t *length,
                                const unsigned char *map);

/**
   Perform percent-decoding of the string in the buffer, writing
   to the output buffer. The source and destination can be the same,
   in which case they overwrite. The decoded string is always
   guaranteed to be no longer than the source string.

   @param str the string to decode (and possibly write to).
   @param str_len length of the input string (or 0).
   @param dst output buffer (can be the same as src).
   @param dst_len size of the output buffer.
   @param length amount of data written to the destination buffer.

*/
TSReturnCode TSStringPercentDecode(const char *str, size_t str_len, char *dst, size_t dst_size, size_t *length);

/* --------------------------------------------------------------------------
   MIME headers */

/**
    Creates a MIME parser. The parser's data structure contains
    information about the header being parsed. A single MIME
    parser can be used multiple times, though not simultaneously.
    Before being used again, the parser must be cleared by calling
    TSMimeParserClear().

 */
TSMimeParser TSMimeParserCreate(void);

/**
    Clears the specified MIME parser so that it can be used again.

    @param parser to be cleared.

 */
void TSMimeParserClear(TSMimeParser parser);

/**
    Destroys the specified MIME parser and frees the associated memory.

    @param parser to destroy.
 */
void TSMimeParserDestroy(TSMimeParser parser);

/**
  Parse a MIME header date string. Candidate for deprecation in v10.0.0
 */
time_t TSMimeParseDate(char const *const value_str, int const value_len);

/**
    Creates a new MIME header within bufp. Release with a call to
    TSHandleMLocRelease().

    @param bufp marshal buffer to contain the new MIME header.
    @param locp buffer pointer to contain the MLoc

 */
TSReturnCode TSMimeHdrCreate(TSMBuffer bufp, TSMLoc *locp);

/**
    Destroys the MIME header located at hdr_loc within bufp.

    @param bufp marshal buffer containing the MIME header to destroy.
    @param offset location of the MIME header.

 */
TSReturnCode TSMimeHdrDestroy(TSMBuffer bufp, TSMLoc offset);

/**
    Copies a specified MIME header to a specified marshal buffer,
    and returns the location of the copied MIME header within the
    destination marshal buffer. Unlike TSMimeHdrCopy(), you do not
    have to create the destination MIME header before cloning. Release
    the returned TSMLoc handle with a call to TSHandleMLocRelease().

    @param dest_bufp destination marshal buffer.
    @param src_bufp source marshal buffer.
    @param src_hdr location of the source MIME header.
    @param locp where to store the location of the copied MIME header.

 */
TSReturnCode TSMimeHdrClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc *locp);

/**
    Copies the contents of the MIME header located at src_loc
    within src_bufp to the MIME header located at dest_loc within
    dest_bufp. TSMimeHdrCopy() works correctly even if src_bufp and
    dest_bufp point to different marshal buffers. Important: you must
    create the destination MIME header before copying into it--use
    TSMimeHdrCreate().

    @param dest_bufp is the destination marshal buffer.
    @param dest_offset
    @param src_bufp is the source marshal buffer.
    @param src_offset

 */
TSReturnCode TSMimeHdrCopy(TSMBuffer dest_bufp, TSMLoc dest_offset, TSMBuffer src_bufp, TSMLoc src_offset);

/**
    Formats the MIME header located at hdr_loc into the
    TSIOBuffer iobufp.

    @param offset The offset of the header to be copied to a TSIOBuffer.
    @param iobufp target TSIOBuffer.

 */
void TSMimeHdrPrint(TSMLoc offset, TSIOBuffer iobufp);

/**
    Parses a MIME header. The MIME header must have already been
    allocated and both bufp and hdr_loc must point within that header.
    It is possible to parse a MIME header a single byte at a time
    using repeated calls to TSMimeHdrParse(). As long as an error
    does not occur, TSMimeHdrParse() consumes each single byte and
    asks for more.

    @param parser parses the specified MIME header.
    @param bufp marshal buffer containing the MIME header to be parsed.
    @param offset
    @param start both an input and output. On input, the start
      argument points to the current position of the buffer being
      parsed. On return, start is modified to point past the last
      character parsed.
    @param end points to one byte after the end of the buffer.
    @return One of 3 possible int values:
      - TS_PARSE_ERROR if there is a parsing error.
      - TS_PARSE_DONE is returned when a "\r\n\r\n" pattern is
        encountered, indicating the end of the header.
      - TS_PARSE_CONT is returned if parsing of the header stopped
        because the end of the buffer was reached.

 */
TSParseResult TSMimeHdrParse(TSMimeParser parser, TSMBuffer bufp, TSMLoc offset, const char **start, const char *end);

/**
    Calculates the length of the MIME header located at hdr_loc if it
    were returned as a string. This the length of the MIME header in
    its unparsed form.

    @param bufp marshal buffer containing the MIME header.
    @param offset location of the MIME header.
    @return string length of the MIME header located at hdr_loc.

 */
int TSMimeHdrLengthGet(TSMBuffer bufp, TSMLoc offset);

/**
    Removes and destroys all the MIME fields within the MIME header
    located at hdr_loc within the marshal buffer bufp.

    @param bufp marshal buffer containing the MIME header.
    @param offset location of the MIME header.

 */
TSReturnCode TSMimeHdrFieldsClear(TSMBuffer bufp, TSMLoc offset);

/**
    Returns a count of the number of MIME fields within the MIME header
    located at hdr_loc within the marshal buffer bufp.

    @param bufp marshal buffer containing the MIME header.
    @param offset location of the MIME header within bufp.
    @return number of MIME fields within the MIME header located
      at hdr_loc.

 */
int TSMimeHdrFieldsCount(TSMBuffer bufp, TSMLoc offset);

/**
    Retrieves the location of a specified MIME field within the
    MIME header located at hdr_loc within bufp. The idx parameter
    specifies which field to retrieve. The fields are numbered from 0
    to TSMimeHdrFieldsCount(bufp, hdr_loc) - 1. If idx does not lie
    within that range then TSMimeHdrFieldGet returns 0. Release the
    returned handle with a call to TSHandleMLocRelease.

    @param bufp marshal buffer containing the MIME header.
    @param hdr location of the MIME header.
    @param idx index of the field to get with base at 0.
    @return location of the specified MIME field.

 */
TSMLoc TSMimeHdrFieldGet(TSMBuffer bufp, TSMLoc hdr, int idx);

/**
    Retrieves the TSMLoc location of a specified MIME field from within
    the MIME header located at hdr. The name and length parameters
    specify which field to retrieve. For each MIME field in the MIME
    header, a case insensitive string comparison is done between
    the field name and name. If TSMimeHdrFieldFind() cannot find the
    requested field, it returns TS_NULL_MLOC. Release the returned
    TSMLoc handle with a call to TSHandleMLocRelease().

    @param bufp marshal buffer containing the MIME header field to find.
    @param hdr location of the MIME header containing the field.
    @param name of the field to retrieve.
    @param length string length of the string name. If length is -1,
      then name is assumed to be null-terminated.
    @return location of the requested MIME field. If the field could
      not be found, returns TS_NULL_MLOC.

 */
TSMLoc TSMimeHdrFieldFind(TSMBuffer bufp, TSMLoc hdr, const char *name, int length);

/**
    Returns the TSMLoc location of a specified MIME field from within
    the MIME header located at hdr. The retrieved_str parameter
    specifies which field to retrieve. For each MIME field in the
    MIME header, a pointer comparison is done between the field name
    and retrieved_str. This is a much quicker retrieval function
    than TSMimeHdrFieldFind() since it obviates the need for a
    string comparison. However, retrieved_str must be one of the
    predefined field names of the form TS_MIME_FIELD_XXX for the
    call to succeed. Release the returned TSMLoc handle with a call
    to TSHandleMLocRelease().

    @param bufp marshal buffer containing the MIME field.
    @param hdr location of the MIME header containing the field.
    @param retrieved_str specifies the field to retrieve. Must be
      one of the predefined field names of the form TS_MIME_FIELD_XXX.
    @return location of the requested MIME field. If the requested
      field cannot be found, returns 0.

 */
TSReturnCode TSMimeHdrFieldAppend(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);

/**
    Removes the MIME field located at field within bufp from the
    header located at hdr within bufp. If the specified field cannot
    be found in the list of fields associated with the header then
    nothing is done.

    Note: removing the field does not destroy the field, it only
    detaches the field, hiding it from the printed output. The field
    can be reattached with a call to TSMimeHdrFieldAppend(). If you
    do not use the detached field you should destroy it with a call to
    TSMimeHdrFieldDestroy() and release the handle field with a call
    to TSHandleMLocRelease().

    @param bufp contains the MIME field to remove.
    @param hdr location of the header containing the MIME field to
      be removed. This header could be an HTTP header or MIME header.
    @param field is the location of the field to remove.

 */
TSReturnCode TSMimeHdrFieldRemove(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);

TSReturnCode TSMimeHdrFieldCreate(TSMBuffer bufp, TSMLoc hdr, TSMLoc *locp);

/****************************************************************************
 *  Create a new field and assign it a name all in one call
 ****************************************************************************/
TSReturnCode TSMimeHdrFieldCreateNamed(TSMBuffer bufp, TSMLoc mh_mloc, const char *name, int name_len, TSMLoc *locp);

/**
    Destroys the MIME field located at field within bufp. You must
    release the TSMLoc field with a call to TSHandleMLocRelease().

    @param bufp contains the MIME field to be destroyed.
    @param hdr location of the parent header containing the field
      to be destroyed. This could be the location of a MIME header or
      HTTP header.
    @param field location of the field to be destroyed.

 */
TSReturnCode TSMimeHdrFieldDestroy(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);

TSReturnCode TSMimeHdrFieldClone(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc src_field,
                                 TSMLoc *locp);
TSReturnCode TSMimeHdrFieldCopy(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMLoc dest_field, TSMBuffer src_bufp, TSMLoc src_hdr,
                                TSMLoc src_field);
TSReturnCode TSMimeHdrFieldCopyValues(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMLoc dest_field, TSMBuffer src_bufp, TSMLoc src_hdr,
                                      TSMLoc src_field);
TSMLoc       TSMimeHdrFieldNext(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);
TSMLoc       TSMimeHdrFieldNextDup(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);
int          TSMimeHdrFieldLengthGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);
const char  *TSMimeHdrFieldNameGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int *length);
TSReturnCode TSMimeHdrFieldNameSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, const char *name, int length);

TSReturnCode TSMimeHdrFieldValuesClear(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);
int          TSMimeHdrFieldValuesCount(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);

const char  *TSMimeHdrFieldValueStringGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int *value_len_ptr);
int          TSMimeHdrFieldValueIntGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx);
int64_t      TSMimeHdrFieldValueInt64Get(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx);
unsigned int TSMimeHdrFieldValueUintGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx);
time_t       TSMimeHdrFieldValueDateGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);
TSReturnCode TSMimeHdrFieldValueStringSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value, int length);
TSReturnCode TSMimeHdrFieldValueIntSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int value);
TSReturnCode TSMimeHdrFieldValueInt64Set(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int64_t value);
TSReturnCode TSMimeHdrFieldValueUintSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, unsigned int value);
TSReturnCode TSMimeHdrFieldValueDateSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, time_t value);

TSReturnCode TSMimeHdrFieldValueAppend(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value, int length);
/* These Insert() APIs should be considered. Use the corresponding Set() API instead */
TSReturnCode TSMimeHdrFieldValueStringInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value, int length);
TSReturnCode TSMimeHdrFieldValueIntInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int value);
TSReturnCode TSMimeHdrFieldValueUintInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, unsigned int value);
TSReturnCode TSMimeHdrFieldValueDateInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, time_t value);

TSReturnCode TSMimeHdrFieldValueDelete(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx);
const char  *TSMimeHdrStringToWKS(const char *str, int length);

/*
 * Print as a MIME header date string.
 */
TSReturnCode TSMimeFormatDate(time_t const value_time, char *const value_str, int *const value_len);

/* --------------------------------------------------------------------------
   HTTP headers */
TSHttpParser TSHttpParserCreate(void);
void         TSHttpParserClear(TSHttpParser parser);
void         TSHttpParserDestroy(TSHttpParser parser);

/**
    Parses an HTTP request header. The HTTP header must have already
    been created, and must reside inside the marshal buffer bufp.
    The start argument points to the current position of the string
    buffer being parsed. The end argument points to one byte after the
    end of the buffer to be parsed. On return, TSHttpHdrParseReq()
    modifies start to point past the last character parsed.

    It is possible to parse an HTTP request header a single byte at
    a time using repeated calls to TSHttpHdrParseReq(). As long as
    an error does not occur, the TSHttpHdrParseReq() function will
    consume that single byte and ask for more.

    @param parser parses the HTTP header.
    @param bufp marshal buffer containing the HTTP header to be parsed.
    @param offset location of the HTTP header within bufp.
    @param start both an input and output. On input, it points to the
      current position of the string buffer being parsed. On return,
      start is modified to point past the last character parsed.
    @param end points to one byte after the end of the buffer to be parsed.
    @return status of the parse:
      - TS_PARSE_ERROR means there was a parsing error.
      - TS_PARSE_DONE means that the end of the header was reached
        (the parser encountered a "\r\n\r\n" pattern).
      - TS_PARSE_CONT means that parsing of the header stopped because
        the parser reached the end of the buffer (large headers can
        span multiple buffers).

 */
TSParseResult TSHttpHdrParseReq(TSHttpParser parser, TSMBuffer bufp, TSMLoc offset, const char **start, const char *end);

TSParseResult TSHttpHdrParseResp(TSHttpParser parser, TSMBuffer bufp, TSMLoc offset, const char **start, const char *end);

TSMLoc TSHttpHdrCreate(TSMBuffer bufp);

/**
    Destroys the HTTP header located at hdr_loc within the marshal
    buffer bufp. Do not forget to release the handle hdr_loc with a
    call to TSHandleMLocRelease().

 */
void TSHttpHdrDestroy(TSMBuffer bufp, TSMLoc offset);

TSReturnCode TSHttpHdrClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc *locp);

/**
    Copies the contents of the HTTP header located at src_loc within
    src_bufp to the HTTP header located at dest_loc within dest_bufp.
    TSHttpHdrCopy() works correctly even if src_bufp and dest_bufp
    point to different marshal buffers. Make sure that you create the
    destination HTTP header before copying into it.

    Note: TSHttpHdrCopy() appends the port number to the domain
    of the URL portion of the header. For example, a copy of
    http://www.example.com appears as http://www.example.com:80 in
    the destination buffer.

    @param dest_bufp marshal buffer to contain the copied header.
    @param dest_offset location of the copied header.
    @param src_bufp marshal buffer containing the source header.
    @param src_offset location of the source header.

 */
TSReturnCode TSHttpHdrCopy(TSMBuffer dest_bufp, TSMLoc dest_offset, TSMBuffer src_bufp, TSMLoc src_offset);

void TSHttpHdrPrint(TSMBuffer bufp, TSMLoc offset, TSIOBuffer iobufp);

int TSHttpHdrLengthGet(TSMBuffer bufp, TSMLoc offset);

TSHttpType   TSHttpHdrTypeGet(TSMBuffer bufp, TSMLoc offset);
TSReturnCode TSHttpHdrTypeSet(TSMBuffer bufp, TSMLoc offset, TSHttpType type);

int          TSHttpHdrVersionGet(TSMBuffer bufp, TSMLoc offset);
TSReturnCode TSHttpHdrVersionSet(TSMBuffer bufp, TSMLoc offset, int ver);

const char  *TSHttpHdrMethodGet(TSMBuffer bufp, TSMLoc offset, int *length);
TSReturnCode TSHttpHdrMethodSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);
const char  *TSHttpHdrHostGet(TSMBuffer bufp, TSMLoc offset, int *length);
TSReturnCode TSHttpHdrUrlGet(TSMBuffer bufp, TSMLoc offset, TSMLoc *locp);
TSReturnCode TSHttpHdrUrlSet(TSMBuffer bufp, TSMLoc offset, TSMLoc url);

TSHttpStatus TSHttpHdrStatusGet(TSMBuffer bufp, TSMLoc offset);
TSReturnCode TSHttpHdrStatusSet(TSMBuffer bufp, TSMLoc offset, TSHttpStatus status);
const char  *TSHttpHdrReasonGet(TSMBuffer bufp, TSMLoc offset, int *length);
TSReturnCode TSHttpHdrReasonSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);
const char  *TSHttpHdrReasonLookup(TSHttpStatus status);

/* --------------------------------------------------------------------------
   Threads */
TSThread      TSThreadCreate(TSThreadFunc func, void *data);
TSThread      TSThreadInit(void);
void          TSThreadDestroy(TSThread thread);
void          TSThreadWait(TSThread thread);
TSThread      TSThreadSelf(void);
TSEventThread TSEventThreadSelf(void);

/* --------------------------------------------------------------------------
   Mutexes */
TSMutex      TSMutexCreate(void);
void         TSMutexDestroy(TSMutex mutexp);
void         TSMutexLock(TSMutex mutexp);
TSReturnCode TSMutexLockTry(TSMutex mutexp);

void TSMutexUnlock(TSMutex mutexp);

/* --------------------------------------------------------------------------
   cachekey */
/**
    Creates (allocates memory for) a new cache key.
 */
TSCacheKey TSCacheKeyCreate(void);

/**
    Generates a key for an object to be cached (written to the cache).

    @param key to be associated with the cached object. Before
      calling TSCacheKeySetDigest() you must create the key with
      TSCacheKeyCreate().
    @param input string that uniquely identifies the object. In most
      cases, it is the URL of the object.
    @param length of the string input.

 */
TSReturnCode TSCacheKeyDigestSet(TSCacheKey key, const char *input, int length);

TSReturnCode TSCacheKeyDigestFromUrlSet(TSCacheKey key, TSMLoc url);

/**
    Associates a host name to the cache key. Use this function if the
    cache has been partitioned by hostname. The hostname tells the
    cache which volume to use for the object.

    @param key of the cached object.
    @param hostname to associate with the cache key.
    @param host_len length of the string hostname.

 */
TSReturnCode TSCacheKeyHostNameSet(TSCacheKey key, const char *hostname, int host_len);

TSReturnCode TSCacheKeyPinnedSet(TSCacheKey key, time_t pin_in_cache);

/**
    Destroys a cache key. You must destroy cache keys when you are
    finished with them, i.e. after all reads and writes are completed.

    @param key to be destroyed.

 */
TSReturnCode TSCacheKeyDestroy(TSCacheKey key);

/* --------------------------------------------------------------------------
   cache url */
TSReturnCode TSCacheUrlSet(TSHttpTxn txnp, const char *url, int length);

TSReturnCode TSCacheKeyDataTypeSet(TSCacheKey key, TSCacheDataType type);

/* --------------------------------------------------------------------------
   Configuration */
unsigned int TSConfigSet(unsigned int id, void *data, TSConfigDestroyFunc funcp);
TSConfig     TSConfigGet(unsigned int id);
void         TSConfigRelease(unsigned int id, TSConfig configp);
void        *TSConfigDataGet(TSConfig configp);

TSReturnCode TSMgmtConfigFileAdd(const char *parent, const char *fileName);

/* --------------------------------------------------------------------------
   Management */
void         TSMgmtUpdateRegister(TSCont contp, const char *plugin_name, const char *plugin_file_name = nullptr);
TSReturnCode TSMgmtIntGet(const char *var_name, TSMgmtInt *result);
TSReturnCode TSMgmtCounterGet(const char *var_name, TSMgmtCounter *result);
TSReturnCode TSMgmtFloatGet(const char *var_name, TSMgmtFloat *result);
TSReturnCode TSMgmtStringGet(const char *var_name, TSMgmtString *result);
TSReturnCode TSMgmtSourceGet(const char *var_name, TSMgmtSource *source);
TSReturnCode TSMgmtConfigFileAdd(const char *parent, const char *fileName);
TSReturnCode TSMgmtDataTypeGet(const char *var_name, TSRecordDataType *result);

/* --------------------------------------------------------------------------
   TSHRTime, this is a candidate for deprecation in v10.0.0 */
TSHRTime TShrtime(void);

/* --------------------------------------------------------------------------
   Continuations */
TSCont                TSContCreate(TSEventFunc funcp, TSMutex mutexp);
void                  TSContDestroy(TSCont contp);
void                  TSContDataSet(TSCont contp, void *data);
void                 *TSContDataGet(TSCont contp);
TSAction              TSContScheduleOnPool(TSCont contp, TSHRTime timeout, TSThreadPool tp);
TSAction              TSContScheduleOnThread(TSCont contp, TSHRTime timeout, TSEventThread ethread);
std::vector<TSAction> TSContScheduleOnEntirePool(TSCont contp, TSHRTime timeout, TSThreadPool tp);
TSAction              TSContScheduleEveryOnPool(TSCont contp, TSHRTime every /* millisecs */, TSThreadPool tp);
TSAction              TSContScheduleEveryOnThread(TSCont contp, TSHRTime every /* millisecs */, TSEventThread ethread);
std::vector<TSAction> TSContScheduleEveryOnEntirePool(TSCont contp, TSHRTime every /* millisecs */, TSThreadPool tp);
TSReturnCode          TSContThreadAffinitySet(TSCont contp, TSEventThread ethread);
TSEventThread         TSContThreadAffinityGet(TSCont contp);
void                  TSContThreadAffinityClear(TSCont contp);
TSAction              TSHttpSchedule(TSCont contp, TSHttpTxn txnp, TSHRTime timeout);
int                   TSContCall(TSCont contp, TSEvent event, void *edata);
TSMutex               TSContMutexGet(TSCont contp);

/* --------------------------------------------------------------------------
   Plugin lifecycle  hooks */
void TSLifecycleHookAdd(TSLifecycleHookID id, TSCont contp);
/* --------------------------------------------------------------------------
   HTTP hooks */
void TSHttpHookAdd(TSHttpHookID id, TSCont contp);

/* --------------------------------------------------------------------------
   HTTP sessions */
void TSHttpSsnHookAdd(TSHttpSsn ssnp, TSHttpHookID id, TSCont contp);
void TSHttpSsnReenable(TSHttpSsn ssnp, TSEvent event);
int  TSHttpSsnTransactionCount(TSHttpSsn ssnp);
/* Get the TSVConn from a session. */
TSVConn TSHttpSsnClientVConnGet(TSHttpSsn ssnp);
TSVConn TSHttpSsnServerVConnGet(TSHttpSsn ssnp);
/* Get the TSVConn from a transaction. */
TSVConn TSHttpTxnServerVConnGet(TSHttpTxn txnp);

/* --------------------------------------------------------------------------
   SSL connections */
/* Re-enable an SSL connection from a hook.
   This must be called exactly once before the SSL connection will resume. */
void TSVConnReenable(TSVConn sslvcp);
/* Extended version that allows for passing a status event on reenabling
 */
void TSVConnReenableEx(TSVConn sslvcp, TSEvent event);
/*  Set the connection to go into blind tunnel mode */
TSReturnCode TSVConnTunnel(TSVConn sslp);
/*  Return the SSL object associated with the connection */
TSSslConnection TSVConnSslConnectionGet(TSVConn sslp);
/* Return the file descriptoer associated with the connection */
int TSVConnFdGet(TSVConn sslp);
/* Return the intermediate X509StoreCTX object that references the certificate being validated */
TSSslVerifyCTX TSVConnSslVerifyCTXGet(TSVConn sslp);
/*  Fetch a SSL context from the global lookup table */
TSSslContext TSSslContextFindByName(const char *name);
TSSslContext TSSslContextFindByAddr(struct sockaddr const *);
/* Fetch SSL client contexts from the global lookup table */
TSReturnCode TSSslClientContextsNamesGet(int n, const char **result, int *actual);
TSSslContext TSSslClientContextFindByName(const char *ca_paths, const char *ck_paths);

/* Update SSL certs in internal storage from given path */
TSReturnCode TSSslClientCertUpdate(const char *cert_path, const char *key_path);
TSReturnCode TSSslServerCertUpdate(const char *cert_path, const char *key_path);

/* Update the transient secret table for SSL_CTX loading */
TSReturnCode TSSslSecretSet(const char *secret_name, int secret_name_length, const char *secret_data, int secret_data_length);

/* Returns secret with given name (not null terminted).  If there is no secret with the given name, return value will
** be null and secret_data_lenght will be zero.  Calling code must free data buffer by calling TSfree(). */
char *TSSslSecretGet(const char *secret_name, int secret_name_length, int *secret_data_length);

TSReturnCode TSSslSecretUpdate(const char *secret_name, int secret_name_length);

/* Create a new SSL context based on the settings in records.yaml */
TSSslContext TSSslServerContextCreate(TSSslX509 cert, const char *certname, const char *rsp_file);
void         TSSslContextDestroy(TSSslContext ctx);
TSReturnCode TSSslTicketKeyUpdate(char *ticketData, int ticketDataLen);
TSAcceptor   TSAcceptorGet(TSVConn sslp);
TSAcceptor   TSAcceptorGetbyID(int ID);
int          TSAcceptorCount();
int          TSAcceptorIDGet(TSAcceptor acceptor);
TSReturnCode TSVConnProtocolDisable(TSVConn connp, const char *protocol_name);
TSReturnCode TSVConnProtocolEnable(TSVConn connp, const char *protocol_name);

/*  Returns 1 if the sslp argument refers to a SSL connection */
int TSVConnIsSsl(TSVConn sslp);
/* Returns 1 if a certificate was provided in the TLS handshake, 0 otherwise.
 */
int         TSVConnProvidedSslCert(TSVConn sslp);
const char *TSVConnSslSniGet(TSVConn sslp, int *length);

TSSslSession TSSslSessionGet(const TSSslSessionID *session_id);
int          TSSslSessionGetBuffer(const TSSslSessionID *session_id, char *buffer, int *len_ptr);
TSReturnCode TSSslSessionInsert(const TSSslSessionID *session_id, TSSslSession add_session, TSSslConnection ssl_conn);
TSReturnCode TSSslSessionRemove(const TSSslSessionID *session_id);

/* --------------------------------------------------------------------------
   HTTP transactions */
void      TSHttpTxnHookAdd(TSHttpTxn txnp, TSHttpHookID id, TSCont contp);
TSHttpSsn TSHttpTxnSsnGet(TSHttpTxn txnp);

/* Gets the client request header for a specified HTTP transaction. */
TSReturnCode TSHttpTxnClientReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
/* Gets the client response header for a specified HTTP transaction. */
TSReturnCode TSHttpTxnClientRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
/* Gets the server request header from a specified HTTP transaction. */
TSReturnCode TSHttpTxnServerReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
/* Gets the server response header from a specified HTTP transaction. */
TSReturnCode TSHttpTxnServerRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
/* Gets the cached request header for a specified HTTP transaction. */
TSReturnCode TSHttpTxnCachedReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
/* Gets the cached response header for a specified HTTP transaction. */
TSReturnCode TSHttpTxnCachedRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);

TSReturnCode TSHttpTxnPristineUrlGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *url_loc);

/**
 * @brief Gets  the number of transactions between the Traffic Server proxy and the origin server from a single session.
 *        Any value greater than zero indicates connection reuse.
 *
 * @param txnp The transaction
 * @return int The number of transactions between the Traffic Server proxy and the origin server from a single session
 */
int TSHttpTxnServerSsnTransactionCount(TSHttpTxn txnp);

/** Get the effective URL for the transaction.
    The effective URL is the URL taking in to account both the explicit
    URL in the request and the HOST field.

    A possibly non-null terminated string is returned.

    @note The returned string is allocated and must be freed by the caller
    after use with @c TSfree.
*/
char *TSHttpTxnEffectiveUrlStringGet(TSHttpTxn txnp, int *length /**< String length return, may be @c nullptr. */
);

/** Get the effective URL for in the header (if any), with the scheme and host normalized to lower case letter.
    The effective URL is the URL taking in to account both the explicit
    URL in the request and the HOST field.

    A possibly non-null terminated string is returned.

    @return TS_SUCCESS if successful, TS_ERROR if no URL in header or other error.
*/
TSReturnCode TSHttpHdrEffectiveUrlBufGet(TSMBuffer hdr_buf, TSMLoc hdr_loc, char *buf, int64_t size, int64_t *length);

void TSHttpTxnRespCacheableSet(TSHttpTxn txnp, int flag);
void TSHttpTxnReqCacheableSet(TSHttpTxn txnp, int flag);

/** Set flag indicating whether or not to cache the server response for
    given TSHttpTxn

    @note This should be done in the HTTP_READ_RESPONSE_HDR_HOOK or earlier.

    @note If TSHttpTxnRespCacheableSet() is not working the way you expect,
    this may be the function you should use instead.

    @param txnp The transaction whose server response you do not want to store.
    @param flag Set 0 to allow storing and 1 to disable storing.

    @return TS_SUCCESS.
*/
TSReturnCode TSHttpTxnServerRespNoStoreSet(TSHttpTxn txnp, int flag);

/** Get flag indicating whether or not to cache the server response for
    given TSHttpTxn
    @param txnp The transaction whose server response you do not want to store.

    @return TS_SUCCESS.
*/
bool         TSHttpTxnServerRespNoStoreGet(TSHttpTxn txnp);
TSReturnCode TSFetchPageRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
char        *TSFetchRespGet(TSHttpTxn txnp, int *length);
TSReturnCode TSHttpTxnCacheLookupStatusGet(TSHttpTxn txnp, int *lookup_status);

TSReturnCode TSHttpTxnTransformRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);

/** Set the @a port value for the inbound (user agent) connection in the transaction @a txnp.
    This is used primarily where the connection is synthetic and therefore does not have a port.
    @note @a port is in @b host @b order.
*/
void TSHttpTxnClientIncomingPortSet(TSHttpTxn txnp, int port);

/** Get client address for transaction @a txnp.
    Retrieves the socket address of the remote client that has
    connected to Traffic Server for transaction @a txnp. The
    return structure is the generic socket address storage in
    order to be address-family agnostic. The user of this function
    can then go on to do the appropriate thing with the type
    specified in the ss_family field of the structure whether
    that be for IPv4, IPv6, or any other address family.

    @return Client address for connection to client in transaction @a txnp.

 */
struct sockaddr const *TSHttpTxnClientAddrGet(TSHttpTxn txnp);
/** Get the incoming address.

    @note The pointer is valid only for the current callback. Clients
    that need to keep the value across callbacks must maintain their
    own storage.

    @return Local address of the client connection for transaction @a txnp.
*/
struct sockaddr const *TSHttpTxnIncomingAddrGet(TSHttpTxn txnp);
/** Get the outgoing address.

    @note The pointer is valid only for the current callback. Clients
    that need to keep the value across callbacks must maintain their
    own storage.

    @return Local address of the server connection for transaction @a txnp.
*/
struct sockaddr const *TSHttpTxnOutgoingAddrGet(TSHttpTxn txnp);
/** Get the origin server address.
 *
    @note The pointer is valid only for the current callback. Clients
    that need to keep the value across callbacks must maintain their
    own storage.

    @return The address of the origin server for transaction @a txnp.
*/
struct sockaddr const *TSHttpTxnServerAddrGet(TSHttpTxn txnp);
/** Set the origin server address.

    This must be invoked before the origin server address is looked up.
    If called no lookup is done, the address @a addr is used instead.

    @return @c TS_SUCCESS if the origin server address is set, @c TS_ERROR otherwise.
*/
TSReturnCode TSHttpTxnServerAddrSet(TSHttpTxn txnp, struct sockaddr const *addr /**< Address for origin server. */
);

/** Get the next hop address.
 *
    @note The pointer is valid only for the current callback. Clients
    that need to keep the value across callbacks must maintain their
    own storage.

    @return The address of the next hop for transaction @a txnp.
*/
struct sockaddr const *TSHttpTxnNextHopAddrGet(TSHttpTxn txnp);

/** Get the next hop name.
 *
    @note The pointer is valid only for the current callback. Clients
    that need to keep the value across callbacks must maintain their
    own storage.

    @return The name of the next hop for transaction @a txnp.
*/
const char *TSHttpTxnNextHopNameGet(TSHttpTxn txnp);

/** Get the next hop port.
 *
    Retrieves the next hop parent port.
                Returns -1 if not valid.

    @return The port of the next hop for transaction @a txnp.

 */
int TSHttpTxnNextHopPortGet(TSHttpTxn txnp);

TSReturnCode TSHttpTxnClientFdGet(TSHttpTxn txnp, int *fdp);
TSReturnCode TSHttpTxnOutgoingAddrSet(TSHttpTxn txnp, struct sockaddr const *addr);
TSReturnCode TSHttpTxnOutgoingTransparencySet(TSHttpTxn txnp, int flag);
TSReturnCode TSHttpTxnServerFdGet(TSHttpTxn txnp, int *fdp);

/* TS-1008: the above TXN calls for the Client conn should work with SSN */
struct sockaddr const *TSHttpSsnClientAddrGet(TSHttpSsn ssnp);
struct sockaddr const *TSHttpSsnIncomingAddrGet(TSHttpSsn ssnp);
TSReturnCode           TSHttpSsnClientFdGet(TSHttpSsn ssnp, int *fdp);
/* TS-1008 END */

/** Change packet firewall mark for the client side connection
 *
    @note The change takes effect immediately

    @return TS_SUCCESS if the client connection was modified
*/
TSReturnCode TSHttpTxnClientPacketMarkSet(TSHttpTxn txnp, int mark);

/** Change packet firewall mark for the server side connection
 *
    @note The change takes effect immediately, if no OS connection has been
    made, then this sets the mark that will be used IF an OS connection
    is established

    @return TS_SUCCESS if the (future?) server connection was modified
*/
TSReturnCode TSHttpTxnServerPacketMarkSet(TSHttpTxn txnp, int mark);

/** Change packet DSCP for the client side connection
 *
    @note The change takes effect immediately

    @return TS_SUCCESS if the client connection was modified
*/
TSReturnCode TSHttpTxnClientPacketDscpSet(TSHttpTxn txnp, int dscp);

/** Change packet DSCP for the server side connection
 *

    @note The change takes effect immediately, if no OS connection has been
    made, then this sets the mark that will be used IF an OS connection
    is established

    @return TS_SUCCESS if the (future?) server connection was modified
*/
TSReturnCode TSHttpTxnServerPacketDscpSet(TSHttpTxn txnp, int dscp);

/**
   Sets an error type body to a transaction. Note that both string arguments
   must be allocated with TSmalloc() or TSstrdup(). The mimetype argument is
   optional, if not provided it defaults to "text/html". Sending an empty
   string would prevent setting a content type header (but that is not advised).

   @param txnp HTTP transaction whose parent proxy to get.
   @param buf The body message (must be heap allocated).
   @param buflength Length of the body message.
   @param mimetype The MIME type to set the response to (can be null, but must
          be heap allocated if non-null).
*/
void TSHttpTxnErrorBodySet(TSHttpTxn txnp, char *buf, size_t buflength, char *mimetype);

/**
   Retrives the error body, if any, from a transaction. This would be a body as set
   via the API body.

   @param txnp HTTP transaction whose parent proxy to get.
   @param buflength Optional outpu pointer to the length of the body message.
   @param mimetype Optional output pointer to the MIME type of the response.
*/
char *TSHttpTxnErrorBodyGet(TSHttpTxn txnp, size_t *buflength, char **mimetype);

/**
    Retrieves the parent proxy hostname and port, if parent
    proxying is enabled. If parent proxying is not enabled,
    TSHttpTxnParentProxyGet() sets hostname to nullptr and port to -1.

    @param txnp HTTP transaction whose parent proxy to get.
    @param hostname of the parent proxy.
    @param port parent proxy's port.

 */
TSReturnCode TSHttpTxnParentProxyGet(TSHttpTxn txnp, const char **hostname, int *port);

/**
    Sets the parent proxy name and port. The string hostname is copied
    into the TSHttpTxn; you can modify or delete the string after
    calling TSHttpTxnParentProxySet().

    @param txnp HTTP transaction whose parent proxy to set.
    @param hostname parent proxy host name string.
    @param port parent proxy port to set.

 */
void TSHttpTxnParentProxySet(TSHttpTxn txnp, const char *hostname, int port);

TSReturnCode TSHttpTxnParentSelectionUrlGet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj);
TSReturnCode TSHttpTxnParentSelectionUrlSet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj);

void TSHttpTxnUntransformedRespCache(TSHttpTxn txnp, int on);
void TSHttpTxnTransformedRespCache(TSHttpTxn txnp, int on);

/**
    Notifies the HTTP transaction txnp that the plugin is
    finished processing the current hook. The plugin tells the
    transaction to either continue (TS_EVENT_HTTP_CONTINUE) or stop
    (TS_EVENT_HTTP_ERROR).

    You must always reenable the HTTP transaction after the processing
    of each transaction event. However, never reenable twice.
    Reenabling twice is a serious error.

    @param txnp transaction to be reenabled.
    @param event tells the transaction how to continue:
      - TS_EVENT_HTTP_CONTINUE, which means that the transaction
        should continue.
      - TS_EVENT_HTTP_ERROR which terminates the transaction
        and sends an error to the client if no response has already
        been sent.

 */
void         TSHttpTxnReenable(TSHttpTxn txnp, TSEvent event);
TSReturnCode TSHttpCacheReenable(TSCacheTxn txnp, const TSEvent event, const void *data, const uint64_t size);

/* The reserve API should only be use in TSAPI plugins, during plugin initialization!
   The lookup methods can be used anytime, but are best used during initialization as well,
   or at least "cache" the results for best performance. */
TSReturnCode TSUserArgIndexReserve(TSUserArgType type, const char *name, const char *description, int *arg_idx);
TSReturnCode TSUserArgIndexNameLookup(TSUserArgType type, const char *name, int *arg_idx, const char **description);
TSReturnCode TSUserArgIndexLookup(TSUserArgType type, int arg_idx, const char **name, const char **description);
void         TSUserArgSet(void *data, int arg_idx, void *arg);
void        *TSUserArgGet(void *data, int arg_idx);

void         TSHttpTxnStatusSet(TSHttpTxn txnp, TSHttpStatus status);
TSHttpStatus TSHttpTxnStatusGet(TSHttpTxn txnp);

void TSHttpTxnActiveTimeoutSet(TSHttpTxn txnp, int timeout);
void TSHttpTxnConnectTimeoutSet(TSHttpTxn txnp, int timeout);
void TSHttpTxnDNSTimeoutSet(TSHttpTxn txnp, int timeout);
void TSHttpTxnNoActivityTimeoutSet(TSHttpTxn txnp, int timeout);

TSServerState TSHttpTxnServerStateGet(TSHttpTxn txnp);

/* --------------------------------------------------------------------------
   Transaction specific debugging control  */

/**
       Set the transaction specific debugging flag for this transaction.
       When turned on, internal debug messages related to this transaction
       will be written even if the debug tag isn't on.

    @param txnp transaction to change.
    @param on set to 1 to turn on, 0 to turn off.
*/
void TSHttpTxnDebugSet(TSHttpTxn txnp, int on);
/**
       Returns the transaction specific debugging flag for this transaction.

    @param txnp transaction to check.
    @return 1 if enabled, 0 otherwise.
*/
int TSHttpTxnDebugGet(TSHttpTxn txnp);
/**
       Set the session specific debugging flag for this client session.
       When turned on, internal debug messages related to this session and all transactions
       in the session will be written even if the debug tag isn't on.

    @param ssnp Client session to change.
    @param on set to 1 to turn on, 0 to turn off.
*/
void TSHttpSsnDebugSet(TSHttpSsn ssnp, int on);
/**
       Returns the transaction specific debugging flag for this client session.

    @param txnp Client session to check.
    @return 1 if enabled, 0 otherwise.
*/
int TSHttpSsnDebugGet(TSHttpSsn ssnp, int *on);

/* --------------------------------------------------------------------------
   Intercepting Http Transactions */

/**
    Allows a plugin take over the servicing of the request as though
    it was the origin server. contp will be sent TS_EVENT_NET_ACCEPT.
    The edata passed with TS_NET_EVENT_ACCEPT is an TSVConn just as
    it would be for a normal accept. The plugin must act as if it is
    an http server and read the http request and body off the TSVConn
    and send an http response header and body.

    TSHttpTxnIntercept() must be called be called from only
    TS_HTTP_READ_REQUEST_HOOK. Using TSHttpTxnIntercept will
    bypass the Traffic Server cache. If response sent by the plugin
    should be cached, use TSHttpTxnServerIntercept() instead.
    TSHttpTxnIntercept() primary use is allow plugins to serve data
    about their functioning directly.

    TSHttpTxnIntercept() must only be called once per transaction.

    @param contp continuation called to handle the interception.
    @param txnp transaction to be intercepted.

 */
void TSHttpTxnIntercept(TSCont contp, TSHttpTxn txnp);

/**
    Allows a plugin take over the servicing of the request as though
    it was the origin server. In the event a request needs to be
    made to the server for transaction txnp, contp will be sent
    TS_EVENT_NET_ACCEPT. The edata passed with TS_NET_EVENT_ACCEPT
    is an TSVConn just as it would be for a normal accept. The plugin
    must act as if it is an http server and read the http request and
    body off the TSVConn and send an http response header and body.

    TSHttpTxnInterceptServer() must be not be called after
    the connection to the server has taken place. The last hook
    last hook in that TSHttpTxnIntercept() can be called from is
    TS_HTTP_READ_CACHE_HDR_HOOK. If a connection to the server is
    not necessary, contp is not called.

    The response from the plugin is cached subject to standard
    and configured http caching rules. Should the plugin wish the
    response not be cached, the plugin must use appropriate http
    response headers to prevent caching. The primary purpose of
    TSHttpTxnInterceptServer() is allow plugins to provide gateways
    to other protocols or to allow to plugin to it's own transport for
    the next hop to the server. TSHttpTxnInterceptServer() overrides
    parent cache configuration.

    TSHttpTxnInterceptServer() must only be called once per
    transaction.

    @param contp continuation called to handle the interception
    @param txnp transaction to be intercepted.

 */
void TSHttpTxnServerIntercept(TSCont contp, TSHttpTxn txnp);

/* --------------------------------------------------------------------------
   Initiate Http Connection */

/**
    Allows the plugin to initiate an http connection. The TSVConn the
    plugin receives as the result of successful operates identically to
    one created through TSNetConnect. Aside from allowing the plugin
    to set the client ip and port for logging, the functionality of
    TSHttpConnect() is identical to connecting to localhost on the
    proxy port with TSNetConnect(). TSHttpConnect() is more efficient
    than TSNetConnect() to localhost since it avoids the overhead of
    passing the data through the operating system.

    This returns a VConn that connected to the transaction.

    @param options a TSHttpConnectPluginOptions structure that specifies options.
 */
TSVConn TSHttpConnectPlugin(TSHttpConnectOptions *options);

/** Backwards compatible version.
    This function calls This provides a @a buffer_index of 8 and a @a buffer_water_mark of 0.

    @param addr Target address of the origin server.
    @param tag A logging tag that can be accessed via the pitag field. May be @c nullptr.
    @param id A logging id that can be access via the piid field.
 */
TSVConn TSHttpConnectWithPluginId(struct sockaddr const *addr, const char *tag, int64_t id);

/** Backwards compatible version.
    This provides a @a tag of "plugin" and an @a id of 0.
 */
TSVConn TSHttpConnect(struct sockaddr const *addr);

/**
   Get an instance of TSHttpConnectOptions with default values.
 */
TSHttpConnectOptions TSHttpConnectOptionsGet(TSConnectType connect_type);

/**
   Get the value of proxy.config.plugin.vc.default_buffer_index from the TSHttpTxn
 */
TSIOBufferSizeIndex TSPluginVCIOBufferIndexGet(TSHttpTxn txnp);

/**
   Get the value of proxy.config.plugin.vc.default_buffer_water_mark from the TSHttpTxn
 */
TSIOBufferWaterMark TSPluginVCIOBufferWaterMarkGet(TSHttpTxn txnp);

/* --------------------------------------------------------------------------
 Initiate Transparent Http Connection */
/**
    Allows the plugin to initiate a transparent http connection. This operates
    identically to TSHttpConnect except that it is treated as an intercepted
    transparent connection by the session and transaction state machines.

    @param client_addr the address that the resulting connection will be seen as
      coming from
    @param server_addr the address that the resulting connection will be seen as
      attempting to connect to when intercepted
    @param vc will be set to point to the new TSVConn on success.

 */
TSVConn TSHttpConnectTransparent(struct sockaddr const *client_addr, struct sockaddr const *server_addr);

TSFetchSM TSFetchUrl(const char *request, int request_len, struct sockaddr const *addr, TSCont contp,
                     TSFetchWakeUpOptions callback_options, TSFetchEvent event);
void      TSFetchPages(TSFetchUrlParams_t *params);

/**
 * Extended FetchSM's AIPs
 */

/*
 * Create FetchSM, this API will enable stream IO automatically.
 *
 * @param contp: continuation to be callbacked.
 * @param method: request method.
 * @param url: scheme://host[:port]/path.
 * @param version: client http version, eg: "HTTP/1.1".
 * @param client_addr: client addr sent to log.
 * @param flags: can be bitwise OR of several TSFetchFlags.
 *
 * return TSFetchSM which should be destroyed by TSFetchDestroy().
 */
TSFetchSM TSFetchCreate(TSCont contp, const char *method, const char *url, const char *version, struct sockaddr const *client_addr,
                        int flags);

/*
 * Set fetch flags to FetchSM Context
 *
 * @param fetch_sm: returned value of TSFetchCreate().
 * @param flags: can be bitwise OR of several TSFetchFlags.
 *
 * return void
 */
void TSFetchFlagSet(TSFetchSM fetch_sm, int flags);

/*
 * Create FetchSM, this API will enable stream IO automatically.
 *
 * @param fetch_sm: returned value of TSFetchCreate().
 * @param name: name of header.
 * @param name_len: len of name.
 * @param value: value of header.
 * @param name_len: len of value.
 *
 * return TSFetchSM which should be destroyed by TSFetchDestroy().
 */
void TSFetchHeaderAdd(TSFetchSM fetch_sm, const char *name, int name_len, const char *value, int value_len);

/*
 * Write data to FetchSM
 *
 * @param fetch_sm: returned value of TSFetchCreate().
 * @param data/len: data to be written to fetch sm.
 */
void TSFetchWriteData(TSFetchSM fetch_sm, const void *data, size_t len);

/*
 * Read up to *len* bytes from FetchSM into *buf*.
 *
 * @param fetch_sm: returned value of TSFetchCreate().
 * @param buf/len: buffer to contain data from fetch sm.
 */
ssize_t TSFetchReadData(TSFetchSM fetch_sm, void *buf, size_t len);

/*
 * Launch FetchSM to do http request, before calling this API,
 * you should append http request header into fetch sm through
 * TSFetchWriteData() API
 *
 * @param fetch_sm: comes from returned value of TSFetchCreate().
 */
void TSFetchLaunch(TSFetchSM fetch_sm);

/*
 * Destroy FetchSM
 *
 * @param fetch_sm: returned value of TSFetchCreate().
 */
void TSFetchDestroy(TSFetchSM fetch_sm);

/*
 * Set user-defined data in FetchSM
 */
void TSFetchUserDataSet(TSFetchSM fetch_sm, void *data);

/*
 * Get user-defined data in FetchSM
 */
void *TSFetchUserDataGet(TSFetchSM fetch_sm);

/*
 * Get client response hdr mbuffer
 */
TSMBuffer TSFetchRespHdrMBufGet(TSFetchSM fetch_sm);

/*
 * Get client response hdr mloc
 */
TSMLoc TSFetchRespHdrMLocGet(TSFetchSM fetch_sm);

/* Check if HTTP State machine is internal or not */
int TSHttpTxnIsInternal(TSHttpTxn txnp);
int TSHttpSsnIsInternal(TSHttpSsn ssnp);

/* --------------------------------------------------------------------------
   HTTP alternate selection */
TSReturnCode TSHttpAltInfoClientReqGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *offset);
TSReturnCode TSHttpAltInfoCachedReqGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *offset);
TSReturnCode TSHttpAltInfoCachedRespGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *offset);
void         TSHttpAltInfoQualitySet(TSHttpAltInfo infop, float quality);

/* --------------------------------------------------------------------------
   Actions */
void TSActionCancel(TSAction actionp);
int  TSActionDone(TSAction actionp);

/* --------------------------------------------------------------------------
   VConnections */
TSVIO TSVConnReadVIOGet(TSVConn connp);
TSVIO TSVConnWriteVIOGet(TSVConn connp);
int   TSVConnClosedGet(TSVConn connp);

TSVIO TSVConnRead(TSVConn connp, TSCont contp, TSIOBuffer bufp, int64_t nbytes);
TSVIO TSVConnWrite(TSVConn connp, TSCont contp, TSIOBufferReader readerp, int64_t nbytes);
void  TSVConnClose(TSVConn connp);
void  TSVConnAbort(TSVConn connp, int error);
void  TSVConnShutdown(TSVConn connp, int read, int write);

/* --------------------------------------------------------------------------
   Cache VConnections */
int64_t TSVConnCacheObjectSizeGet(TSVConn connp);

/* --------------------------------------------------------------------------
   Transformations */
TSVConn TSTransformCreate(TSEventFunc event_funcp, TSHttpTxn txnp);
TSVConn TSTransformOutputVConnGet(TSVConn connp);

/* --------------------------------------------------------------------------
   Net VConnections */
struct sockaddr const *TSNetVConnRemoteAddrGet(TSVConn vc);

/**
    Opens a network connection to the host specified by ip on the port
    specified by port. If the connection is successfully opened, contp
    is called back with the event TS_EVENT_NET_CONNECT and the new
    network vconnection will be passed in the event data parameter.
    If the connection is not successful, contp is called back with
    the event TS_EVENT_NET_CONNECT_FAILED.

    @return something allows you to check if the connection is complete,
      or cancel the attempt to connect.

 */
TSAction TSNetConnect(
  TSCont                 contp, /**< continuation that is called back when the attempted net connection either succeeds or fails. */
  struct sockaddr const *to     /**< Address to which to connect. */
);

/**
 * Retrieves the continuation associated with creating the TSVConn
 */
TSCont TSNetInvokingContGet(TSVConn conn);

/**
 * Retrieves the transaction associated with creating the TSVConn
 */
TSHttpTxn TSNetInvokingTxnGet(TSVConn conn);

TSAction TSNetAccept(TSCont contp, int port, int domain, int accept_threads);

/**
 Attempt to attach the contp continuation to sockets that have already been
 opened by the traffic Server and defined as belonging to plugins (based on
 records.yaml configuration). If a connection is successfully accepted,
 the TS_EVENT_NET_ACCEPT is delivered to the continuation. The event
 data will be a valid TSVConn bound to the accepted connection.
 In order to configure such a socket, add the "plugin" keyword to a port
 in proxy.config.http.server_ports like "8082:plugin"
 Transparency/IP settings can also be defined, but a port cannot have
 both the "ssl" or "plugin" keywords configured.

 Need to update records.yaml comments on proxy.config.http.server_ports
 when this option is promoted from experimental.
*/
TSReturnCode TSPluginDescriptorAccept(TSCont contp);

/**
  Listen on all SSL ports for connections for the specified protocol name.

  TSNetAcceptNamedProtocol registers the specified protocol for all
  statically configured TLS ports. When a client using the TLS Next Protocol
  Negotiation extension negotiates the requested protocol, TrafficServer will
  route the request to the given handler. Note that the protocol is not
  registered on ports opened by other plugins.

  The event and data provided to the handler are the same as for
  TSNetAccept(). If a connection is successfully accepted, the event code
  will be TS_EVENT_NET_ACCEPT and the event data will be a valid TSVConn
  bound to the accepted connection.

  Neither contp nor protocol are copied. They must remain valid for the
  lifetime of the plugin.

  TSNetAcceptNamedProtocol fails if the requested protocol cannot be
  registered on all of the configured TLS ports. If it fails, the protocol
  will not be registered on any ports (ie.. no partial failure).
*/
TSReturnCode TSNetAcceptNamedProtocol(TSCont contp, const char *protocol);

/**
  Create a new port from the string specification used by the
  proxy.config.http.server_ports configuration value.
 */
TSPortDescriptor TSPortDescriptorParse(const char *descriptor);

/**
   Start listening on the given port descriptor. If a connection is
   successfully accepted, the TS_EVENT_NET_ACCEPT is delivered to the
   continuation. The event data will be a valid TSVConn bound to the accepted
   connection.
 */
TSReturnCode TSPortDescriptorAccept(TSPortDescriptor, TSCont);

/* --------------------------------------------------------------------------
   DNS Lookups */
TSAction TSHostLookup(TSCont contp, const char *hostname, size_t namelen);
/** Retrieve an address from the host lookup.
 *
 * @param lookup_result Result handle passed to event callback.
 * @return A @c sockaddr with the address if successful, a @c nullptr if not.
 */
struct sockaddr const *TSHostLookupResultAddrGet(TSHostLookupResult lookup_result);

/* TODO: Eventually, we might want something like this as well, but it requires
   support for building the HostDBInfo struct:
   void TSHostLookupResultSet(TSHttpTxn txnp, TSHostLookupResult result);
*/

/* --------------------------------------------------------------------------
   Cache VConnections */
/**
    Asks the Traffic Server cache if the object corresponding to key
    exists in the cache and can be read. If the object can be read,
    the Traffic Server cache calls the continuation contp back with
    the event TS_EVENT_CACHE_OPEN_READ. In this case, the cache also
    passes contp a cache vconnection and contp can then initiate a
    read operation on that vconnection using TSVConnRead.

    If the object cannot be read, the cache calls contp back with
    the event TS_EVENT_CACHE_OPEN_READ_FAILED. The user (contp)
    has the option to cancel the action returned by TSCacheRead.
    Note that reentrant calls are possible, i.e. the cache can call
    back the user (contp) in the same call.

    @param contp continuation to be called back if a read operation
      is permissible.
    @param key cache key corresponding to the object to be read.
    @return something allowing the user to cancel or schedule the
      cache read.

 */
TSAction TSCacheRead(TSCont contp, TSCacheKey key);

/**
    Asks the Traffic Server cache if contp can start writing the
    object (corresponding to key) to the cache. If the object
    can be written, the cache calls contp back with the event
    TS_EVENT_CACHE_OPEN_WRITE. In this case, the cache also passes
    contp a cache vconnection and contp can then initiate a write
    operation on that vconnection using TSVConnWrite. The object
    is not committed to the cache until the vconnection is closed.
    When all data has been transferred, the user (contp) must do
    an TSVConnClose. In case of any errors, the user MUST do an
    TSVConnAbort(contp, 0).

    If the object cannot be written, the cache calls contp back with
    the event TS_EVENT_CACHE_OPEN_WRITE_FAILED. This can happen,
    for example, if there is another object with the same key being
    written to the cache. The user (contp) has the option to cancel
    the action returned by TSCacheWrite.

    Note that reentrant calls are possible, i.e. the cache can call
    back the user (contp) in the same call.

    @param contp continuation that the cache calls back (telling it
      whether the write operation can proceed or not).
    @param key cache key corresponding to the object to be cached.
    @return something allowing the user to cancel or schedule the
      cache write.

 */
TSAction TSCacheWrite(TSCont contp, TSCacheKey key);

/**
    Removes the object corresponding to key from the cache. If the
    object was removed successfully, the cache calls contp back
    with the event TS_EVENT_CACHE_REMOVE. If the object was not
    found in the cache, the cache calls contp back with the event
    TS_EVENT_CACHE_REMOVE_FAILED.

    In both of these callbacks, the user (contp) does not have to do
    anything. The user does not get any vconnection from the cache,
    since no data needs to be transferred. When the cache calls
    contp back with TS_EVENT_CACHE_REMOVE, the remove has already
    been committed.

    @param contp continuation that the cache calls back reporting the
      success or failure of the remove.
    @param key cache key corresponding to the object to be removed.
    @return something allowing the user to cancel or schedule the
      remove.

 */
TSAction     TSCacheRemove(TSCont contp, TSCacheKey key);
TSReturnCode TSCacheReady(int *is_ready);
TSAction     TSCacheScan(TSCont contp, TSCacheKey key, int KB_per_second);

/* Cache APIs that are not yet fully supported and/or frozen nor complete. */
TSReturnCode TSCacheBufferInfoGet(TSCacheTxn txnp, uint64_t *length, uint64_t *offset);

TSCacheHttpInfo TSCacheHttpInfoCreate();
void            TSCacheHttpInfoReqGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *obj);
void            TSCacheHttpInfoRespGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *obj);
void            TSCacheHttpInfoReqSet(TSCacheHttpInfo infop, TSMBuffer bufp, TSMLoc obj);
void            TSCacheHttpInfoRespSet(TSCacheHttpInfo infop, TSMBuffer bufp, TSMLoc obj);
void            TSCacheHttpInfoKeySet(TSCacheHttpInfo infop, TSCacheKey key);
void            TSCacheHttpInfoSizeSet(TSCacheHttpInfo infop, int64_t size);
int             TSCacheHttpInfoVector(TSCacheHttpInfo infop, void *data, int length);
int64_t         TSCacheHttpInfoSizeGet(TSCacheHttpInfo infop);

void TSVConnCacheHttpInfoSet(TSVConn connp, TSCacheHttpInfo infop);

TSCacheHttpInfo TSCacheHttpInfoCopy(TSCacheHttpInfo infop);
void            TSCacheHttpInfoReqGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *offset);
void            TSCacheHttpInfoRespGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *offset);
void            TSCacheHttpInfoDestroy(TSCacheHttpInfo infop);

time_t       TSCacheHttpInfoReqSentTimeGet(TSCacheHttpInfo infop);
time_t       TSCacheHttpInfoRespReceivedTimeGet(TSCacheHttpInfo infop);
TSReturnCode TSHttpTxnCachedRespTimeGet(TSHttpTxn txnp, time_t *resp_time);

/* --------------------------------------------------------------------------
   VIOs */
void             TSVIOReenable(TSVIO viop);
TSIOBuffer       TSVIOBufferGet(TSVIO viop);
TSIOBufferReader TSVIOReaderGet(TSVIO viop);
int64_t          TSVIONBytesGet(TSVIO viop);
void             TSVIONBytesSet(TSVIO viop, int64_t nbytes);
int64_t          TSVIONDoneGet(TSVIO viop);
void             TSVIONDoneSet(TSVIO viop, int64_t ndone);
int64_t          TSVIONTodoGet(TSVIO viop);
TSMutex          TSVIOMutexGet(TSVIO viop);
TSCont           TSVIOContGet(TSVIO viop);
TSVConn          TSVIOVConnGet(TSVIO viop);

/* --------------------------------------------------------------------------
   Buffers */
TSIOBuffer TSIOBufferCreate(void);

/**
    Creates a new TSIOBuffer of the specified size. With this function,
    you can create smaller buffers than the 32K buffer created by
    TSIOBufferCreate(). In some situations using smaller buffers can
    improve performance.

    @param index size of the new TSIOBuffer to be created.
    @param new TSIOBuffer of the specified size.

 */
TSIOBuffer TSIOBufferSizedCreate(TSIOBufferSizeIndex index);

/**
    The watermark of an TSIOBuffer is the minimum number of bytes
    of data that have to be in the buffer before calling back any
    continuation that has initiated a read operation on this buffer.
    TSIOBufferWaterMarkGet() will provide the size of the watermark,
    in bytes, for a specified TSIOBuffer.

    @param bufp buffer whose watermark the function gets.

 */
int64_t TSIOBufferWaterMarkGet(TSIOBuffer bufp);

/**
    The watermark of an TSIOBuffer is the minimum number of bytes
    of data that have to be in the buffer before calling back any
    continuation that has initiated a read operation on this buffer.
    As a writer feeds data into the TSIOBuffer, no readers are called
    back until the amount of data reaches the watermark. Setting
    a watermark can improve performance because it avoids frequent
    callbacks to read small amounts of data. TSIOBufferWaterMarkSet()
    assigns a watermark to a particular TSIOBuffer.

    @param bufp buffer whose water mark the function sets.
    @param water_mark watermark setting, as a number of bytes.

 */
void TSIOBufferWaterMarkSet(TSIOBuffer bufp, int64_t water_mark);

void            TSIOBufferDestroy(TSIOBuffer bufp);
TSIOBufferBlock TSIOBufferStart(TSIOBuffer bufp);
int64_t         TSIOBufferCopy(TSIOBuffer bufp, TSIOBufferReader readerp, int64_t length, int64_t offset);

/**
    Writes length bytes of data contained in the string buf to the
    TSIOBuffer bufp. Returns the number of bytes of data successfully
    written to the TSIOBuffer.

    @param bufp is the TSIOBuffer to write into.
    @param buf string to write into the TSIOBuffer.
    @param length of the string buf.
    @return length of data successfully copied into the buffer,
      in bytes.

 */
int64_t TSIOBufferWrite(TSIOBuffer bufp, const void *buf, int64_t length);
void    TSIOBufferProduce(TSIOBuffer bufp, int64_t nbytes);

TSIOBufferBlock TSIOBufferBlockNext(TSIOBufferBlock blockp);
const char     *TSIOBufferBlockReadStart(TSIOBufferBlock blockp, TSIOBufferReader readerp, int64_t *avail);
int64_t         TSIOBufferBlockReadAvail(TSIOBufferBlock blockp, TSIOBufferReader readerp);
char           *TSIOBufferBlockWriteStart(TSIOBufferBlock blockp, int64_t *avail);
int64_t         TSIOBufferBlockWriteAvail(TSIOBufferBlock blockp);

TSIOBufferReader TSIOBufferReaderAlloc(TSIOBuffer bufp);
TSIOBufferReader TSIOBufferReaderClone(TSIOBufferReader readerp);
void             TSIOBufferReaderFree(TSIOBufferReader readerp);
TSIOBufferBlock  TSIOBufferReaderStart(TSIOBufferReader readerp);
void             TSIOBufferReaderConsume(TSIOBufferReader readerp, int64_t nbytes);
int64_t          TSIOBufferReaderAvail(TSIOBufferReader readerp);
int64_t          TSIOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length);

struct sockaddr const *TSNetVConnLocalAddrGet(TSVConn vc);

/* --------------------------------------------------------------------------
   Stats and configs based on librecords raw stats (this is preferred API until we
   rewrite stats).

   This is available as of Apache TS v2.2.*/
enum TSStatPersistence {
  TS_STAT_PERSISTENT = 1,
  TS_STAT_NON_PERSISTENT,
};

enum TSStatSync {
  TS_STAT_SYNC_SUM = 0,
  TS_STAT_SYNC_COUNT,
  TS_STAT_SYNC_AVG,
  TS_STAT_SYNC_TIMEAVG,
};

/* APIs to create new records.yaml configurations */
TSReturnCode TSMgmtStringCreate(TSRecordType rec_type, const char *name, const TSMgmtString data_default,
                                TSRecordUpdateType update_type, TSRecordCheckType check_type, const char *check_regex,
                                TSRecordAccessType access_type);
TSReturnCode TSMgmtIntCreate(TSRecordType rec_type, const char *name, TSMgmtInt data_default, TSRecordUpdateType update_type,
                             TSRecordCheckType check_type, const char *check_regex, TSRecordAccessType access_type);

/*  Note that only TS_RECORDDATATYPE_INT is supported at this point. */
int TSStatCreate(const char *the_name, TSRecordDataType the_type, TSStatPersistence persist, TSStatSync sync);

void TSStatIntIncrement(int the_stat, TSMgmtInt amount);
void TSStatIntDecrement(int the_stat, TSMgmtInt amount);
/* Currently not supported. */
/* void TSStatFloatIncrement(int the_stat, float amount); */
/* void TSStatFloatDecrement(int the_stat, float amount); */

TSMgmtInt TSStatIntGet(int the_stat);
void      TSStatIntSet(int the_stat, TSMgmtInt value);
/* Currently not supported. */
/* TSReturnCode TSStatFloatGet(int the_stat, float* value); */
/* TSReturnCode TSStatFloatSet(int the_stat, float value); */

TSReturnCode TSStatFindName(const char *name, int *idp);

/**
   Records.yaml file handling API.

   If you need to parse a records.yaml file and need to handle each node separately then
   this API should be used, an example of this would be the conf_remap plugin.

   TSYAMLRecNodeHandler

   Callback function for the caller to deal with each parsed node. ``cfg`` holds
   the details of the parsed field. `data` can be used to pass information along.
*/
using TSYAMLRecNodeHandler = TSReturnCode (*)(const TSYAMLRecCfgFieldData *cfg, void *data);
/**
   Parse a YAML node following the record structure internals. On every scalar node
   the @a handler callback will be invoked with the appropriate parsed fields. @a data
   can be used to pass information along to every callback, this could be handy when
   you need to read/set data inside the @c TSYAMLRecNodeHandler to be read at a later stage.

   This will return TS_ERROR if there was an issue while parsing the file. Particular node errors
   should be handled by the @c TSYAMLRecNodeHandler implementation.
*/
TSReturnCode TSRecYAMLConfigParse(TSYaml node, TSYAMLRecNodeHandler handler, void *data);

/* --------------------------------------------------------------------------
   logging api */

/**
    The following enum values are flags, so they should be powers
    of two. With the exception of TS_LOG_MODE_INVALID_FLAG, they
    are all used to configure the creation of an TSTextLogObject
    through the mode argument to TSTextLogObjectCreate().
    TS_LOG_MODE_INVALID_FLAG is used internally to check the validity
    of this argument. Insert new flags before TS_LOG_MODE_INVALID_FLAG,
    and set TS_LOG_MODE_INVALID_FLAG to the largest power of two of
    the enum.

 */
enum {
  TS_LOG_MODE_ADD_TIMESTAMP = 1,
  TS_LOG_MODE_DO_NOT_RENAME = 2,
  TS_LOG_MODE_INVALID_FLAG  = 4,
};

/**
    This type represents a custom log file that you create with
    TSTextLogObjectCreate(). Your plugin writes entries into this
    log file using TSTextLogObjectWrite().

 */
using TSTextLogObject = struct tsapi_textlogobject *;

using TSRecordDumpCb = void (*)(TSRecordType rec_type, void *edata, int registered, const char *name, TSRecordDataType data_type,
                                TSRecordData *datum);

void TSRecordDump(int rec_type, TSRecordDumpCb callback, void *edata);

/**

    Creates a new custom log file that your plugin can write to. You
    can design the fields and inputs to the log file using the
    TSTextLogObjectWrite() function. The logs you create are treated
    like ordinary logs; they are rolled if log rolling is enabled.

    @param filename new log file being created. The new log file
      is created in the logs directory. You can specify a path to a
      subdirectory within the log directory, e.g. subdir/filename,
      but make sure you create the subdirectory first. If you do
      not specify a file name extension, the extension ".log" is
      automatically added.
    @param mode is one (or both) of the following:
      - TS_LOG_MODE_ADD_TIMESTAMP Whenever the plugin makes a log
        entry using TSTextLogObjectWrite (see below), it prepends
        the entry with a timestamp.
      - TS_LOG_MODE_DO_NOT_RENAME This means that if there is a
        filename conflict, Traffic Server should not attempt to rename
        the custom log. The consequence of a name conflict is that the
        custom log will simply not be created, e.g. suppose you call:
          @code
          log = TSTextLogObjectCreate("squid" , mode, nullptr, &error);
          @endcode
        If mode is TS_LOG_MODE_DO_NOT_RENAME, you will NOT get a new
        log (you'll get a null pointer) if squid.log already exists.
        If mode is not TS_LOG_MODE_DO_NOT_RENAME, Traffic Server
        tries to rename the log to a new name (it will try squid_1.log).
    @param new_log_obj new custom log file.
    @return error code:
      - TS_LOG_ERROR_NO_ERROR No error; the log object has been
        created successfully.
      - TS_LOG_ERROR_OBJECT_CREATION Log object not created. This
        error is rare and would most likely be caused by the system
        running out of memory.
      - TS_LOG_ERROR_FILENAME_CONFLICTS You get this error if mode =
        TS_LOG_MODE_DO_NOT_RENAME, and if there is a naming conflict.
        The log object is not created.
      - TS_LOG_ERROR_FILE_ACCESS Log object not created because of
        a file access problem (for example, no write permission to the
        logging directory, or a specified subdirectory for the log file
        does not exist).

 */
TSReturnCode TSTextLogObjectCreate(const char *filename, int mode, TSTextLogObject *new_log_obj);

/**
    Writes a printf-style formatted statement to an TSTextLogObject
    (a plugin custom log).

    @param the_object log object to write to. You must first create
      this object with TSTextLogObjectCreate().
    @param format printf-style formatted statement to be printed.
    @param ... parameters in the formatted statement. A newline is
      automatically added to the end.
    @return one of the following errors:
      - TS_LOG_ERROR_NO_ERROR Means that the write was successful.
      - TS_LOG_ERROR_LOG_SPACE_EXHAUSTED Means that Traffic Server
        ran out of disk space for logs. If you see this error you might
        want to roll logs more often.
      - TS_LOG_ERROR_INTERNAL_ERROR Indicates some internal problem
        with a log entry (such as an entry larger than the size of the
        log write buffer). This error is very unusual.

 */
TSReturnCode TSTextLogObjectWrite(TSTextLogObject the_object, const char *format, ...) TS_PRINTFLIKE(2, 3);

/**
    This immediately flushes the contents of the log write buffer for
    the_object to disk. Use this call only if you want to make sure that
    log entries are flushed immediately. This call has a performance
    cost. Traffic Server flushes the log buffer automatically about
    every 1 second.

    @param the_object custom log file whose write buffer is to be
      flushed.

 */
void TSTextLogObjectFlush(TSTextLogObject the_object);

/**
    Destroys a log object and releases the memory allocated to it.
    Use this call if you are done with the log.

    @param  the_object custom log to be destroyed.

 */
TSReturnCode TSTextLogObjectDestroy(TSTextLogObject the_object);

/**
    Set log header.

 */
void TSTextLogObjectHeaderSet(TSTextLogObject the_object, const char *header);

/**
    Enable/disable rolling.

    @param rolling_enabled a valid proxy.config.log.rolling_enabled value.

 */
TSReturnCode TSTextLogObjectRollingEnabledSet(TSTextLogObject the_object, int rolling_enabled);

/**
    Set the rolling interval.

 */
void TSTextLogObjectRollingIntervalSecSet(TSTextLogObject the_object, int rolling_interval_sec);

/**
    Set the rolling offset. rolling_offset_hr specifies the hour (between 0 and 23) when log rolling
    should take place.

 */
void TSTextLogObjectRollingOffsetHrSet(TSTextLogObject the_object, int rolling_offset_hr);

/**
    Set the rolling size. rolling_size_mb specifies the size in MB when log rolling
    should take place.

 */
void TSTextLogObjectRollingSizeMbSet(TSTextLogObject the_object, int rolling_size_mb);

/**
    Async disk IO read

    @return TS_SUCCESS or TS_ERROR.
 */
TSReturnCode TSAIORead(int fd, off_t offset, char *buf, size_t buffSize, TSCont contp);

/**
    Async disk IO buffer get

    @return char* to the buffer
 */
char *TSAIOBufGet(TSAIOCallback data);

/**
    Async disk IO get number of bytes

    @return the number of bytes
 */
int TSAIONBytesGet(TSAIOCallback data);

/**
    Async disk IO write

    @return TS_SUCCESS or TS_ERROR.
 */
TSReturnCode TSAIOWrite(int fd, off_t offset, char *buf, size_t bufSize, TSCont contp);

/**
    Async disk IO set number of threads

    @return TS_SUCCESS or TS_ERROR.
 */
TSReturnCode TSAIOThreadNumSet(int thread_num);

/**
    Check if transaction was aborted (due client/server errors etc.)
    Client_abort is set as True, in case the abort was caused by the Client.

    @return 1 if transaction was aborted
*/
TSReturnCode TSHttpTxnAborted(TSHttpTxn txnp, bool *client_abort);

TSVConn TSVConnCreate(TSEventFunc event_funcp, TSMutex mutexp);
TSVConn TSVConnFdCreate(int fd);

/* api functions to access stats */
/* ClientResp APIs exist as well and are exposed in PrivateFrozen  */
int     TSHttpTxnClientReqHdrBytesGet(TSHttpTxn txnp);
int64_t TSHttpTxnClientReqBodyBytesGet(TSHttpTxn txnp);
int     TSHttpTxnServerReqHdrBytesGet(TSHttpTxn txnp);
int64_t TSHttpTxnServerReqBodyBytesGet(TSHttpTxn txnp);
int     TSHttpTxnPushedRespHdrBytesGet(TSHttpTxn txnp);
int64_t TSHttpTxnPushedRespBodyBytesGet(TSHttpTxn txnp);
int     TSHttpTxnServerRespHdrBytesGet(TSHttpTxn txnp);
int64_t TSHttpTxnServerRespBodyBytesGet(TSHttpTxn txnp);
int     TSHttpTxnClientRespHdrBytesGet(TSHttpTxn txnp);
int64_t TSHttpTxnClientRespBodyBytesGet(TSHttpTxn txnp);
int     TSVConnIsSslReused(TSVConn sslp);

/****************************************************************************
 *  Allow to set the body of a POST request.
 ****************************************************************************/
void TSHttpTxnServerRequestBodySet(TSHttpTxn txnp, char *buf, int64_t buflength);

/**
   Return the current (if set) SSL Cipher. This is still owned by the
   core, and must not be free'd.

   @param sslp The connection pointer

   @return the SSL Cipher
*/
const char *TSVConnSslCipherGet(TSVConn sslp);

/**
   Return the current (if set) SSL Protocol. This is still owned by the
   core, and must not be free'd.

   @param sslp The connection pointer

   @return the SSL Protocol
*/
const char *TSVConnSslProtocolGet(TSVConn sslp);

/**
   Return the current (if set) SSL Curve. This is still owned by the
   core, and must not be free'd.

   @param txnp the transaction pointer

   @return the SSL Curve
*/
const char *TSVConnSslCurveGet(TSVConn sslp);

/* NetVC timeout APIs. */
void TSVConnInactivityTimeoutSet(TSVConn connp, TSHRTime timeout);
void TSVConnInactivityTimeoutCancel(TSVConn connp);
void TSVConnActiveTimeoutSet(TSVConn connp, TSHRTime timeout);
void TSVConnActiveTimeoutCancel(TSVConn connp);

/*
  ability to skip the remap phase of the State Machine
  this only really makes sense in TS_HTTP_READ_REQUEST_HDR_HOOK
*/
void TSSkipRemappingSet(TSHttpTxn txnp, int flag);

/*
  Set or get various overridable configurations, for a transaction. This should
  probably be done as early as possible, e.g. TS_HTTP_READ_REQUEST_HDR_HOOK.
*/
TSReturnCode TSHttpTxnConfigIntSet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtInt value);
TSReturnCode TSHttpTxnConfigIntGet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtInt *value);
TSReturnCode TSHttpTxnConfigFloatSet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtFloat value);
TSReturnCode TSHttpTxnConfigFloatGet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtFloat *value);
TSReturnCode TSHttpTxnConfigStringSet(TSHttpTxn txnp, TSOverridableConfigKey conf, const char *value, int length);
TSReturnCode TSHttpTxnConfigStringGet(TSHttpTxn txnp, TSOverridableConfigKey conf, const char **value, int *length);

TSReturnCode TSHttpTxnConfigFind(const char *name, int length, TSOverridableConfigKey *conf, TSRecordDataType *type);

/**
   This is a generalization of the old TSHttpTxnFollowRedirect(), but gives finer
   control over the behavior. Instead of using the Location: header for the new
   destination, this API takes the new URL as a parameter. Calling this API
   transfers the ownership of the URL from the plugin to the core, so you must
   make sure it is heap allocated, and that you do not free it.

   Calling this API implicitly also enables the "Follow Redirect" feature, so
   there is no need to set that overridable configuration as well.

   @param txnp the transaction pointer
   @param url  a heap allocated string with the URL
   @param url_len the length of the URL
*/
void TSHttpTxnRedirectUrlSet(TSHttpTxn txnp, const char *url, const int url_len);

/**
   Return the current (if set) redirection URL string. This is still owned by the
   core, and must not be free'd.

   @param txnp the transaction pointer
   @param url_len_ptr a pointer to where the URL length is to be stored

   @return the url string
*/
const char *TSHttpTxnRedirectUrlGet(TSHttpTxn txnp, int *url_len_ptr);

/**
   Return the number of redirection retries we have done. This starts off
   at zero, and can be used to select different URLs based on which attempt this
   is. This can be useful for example when providing a list of URLs to try, and
   do so in order until one succeeds.

   @param txnp the transaction pointer

   @return the redirect try count
*/
int TSHttpTxnRedirectRetries(TSHttpTxn txnp);

/* Get current HTTP connection stats */
int TSHttpCurrentClientConnectionsGet(void);
int TSHttpCurrentActiveClientConnectionsGet(void);
int TSHttpCurrentIdleClientConnectionsGet(void);
int TSHttpCurrentCacheConnectionsGet(void);
int TSHttpCurrentServerConnectionsGet(void);

/* =====  Http Transactions =====  */
TSReturnCode TSHttpTxnCachedRespModifiableGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
TSReturnCode TSHttpTxnCacheLookupStatusSet(TSHttpTxn txnp, int cachelookup);
TSReturnCode TSHttpTxnCacheLookupUrlGet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj);
TSReturnCode TSHttpTxnCacheLookupUrlSet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj);
TSReturnCode TSHttpTxnPrivateSessionSet(TSHttpTxn txnp, int private_session);
const char  *TSHttpTxnCacheDiskPathGet(TSHttpTxn txnp, int *length);
int          TSHttpTxnBackgroundFillStarted(TSHttpTxn txnp);
int          TSHttpTxnIsWebsocket(TSHttpTxn txnp);

/* Get the Txn's (HttpSM's) unique identifier, which is a sequence number since server start) */
uint64_t TSHttpTxnIdGet(TSHttpTxn txnp);

/* Get the Ssn's unique identifier */
int64_t TSHttpSsnIdGet(TSHttpSsn ssnp);

/* Expose internal Base64 Encoding / Decoding */
TSReturnCode TSBase64Decode(const char *str, size_t str_len, unsigned char *dst, size_t dst_size, size_t *length);
TSReturnCode TSBase64Encode(const char *str, size_t str_len, char *dst, size_t dst_size, size_t *length);

/* Get milestone timers, useful for measuring where we are spending time in the transaction processing */
/**
   Return the particular milestone timer for the transaction. If 0 is returned, it means
   the transaction has not yet reached that milestone. Asking for an "unknown" milestone is
   an error.

   @param txnp the transaction pointer
   @param milestone the requested milestone timer
   was created.
   @param time a pointer to a TSHRTime where we will store the timer

   @return @c TS_SUCCESS if the milestone is supported, TS_ERROR otherwise

*/
TSReturnCode TSHttpTxnMilestoneGet(TSHttpTxn txnp, TSMilestonesType milestone, TSHRTime *time);

/**
  Test whether a request / response header pair would be cacheable under the current
  configuration. This would typically be used in TS_HTTP_READ_RESPONSE_HDR_HOOK, when
  you have both the client request and server response ready.

  @param txnp the transaction pointer
  @param request the client request header. If null, use the transactions client request.
  @param response the server response header. If null, use the transactions origin response.

  @return 1 if the request / response is cacheable, 0 otherwise
*/
int TSHttpTxnIsCacheable(TSHttpTxn txnp, TSMBuffer request, TSMBuffer response);

/**
  Get the maximum age in seconds as indicated by the origin server.
  This would typically be used in TS_HTTP_READ_RESPONSE_HDR_HOOK, when you have
  the server response ready.

  @param txnp the transaction pointer
  @param response the server response header. If null, use the transactions origin response.

  @return the age in seconds if specified by Cache-Control, -1 otherwise
*/
int TSHttpTxnGetMaxAge(TSHttpTxn txnp, TSMBuffer response);

/**
   Return a string representation for a TSServerState value. This is useful for plugin debugging.

   @param state the value of this TSServerState

   @return the string representation of the state
*/
const char *TSHttpServerStateNameLookup(TSServerState state);

/**
   Return a string representation for a TSHttpHookID value. This is useful for plugin debugging.

   @param hook the value of this TSHttpHookID

   @return the string representation of the hook ID
*/
const char *TSHttpHookNameLookup(TSHttpHookID hook);

/**
   Return a string representation for a TSEvent value. This is useful for plugin debugging.

   @param event the value of this TSHttpHookID

   @return the string representation of the event
*/
const char *TSHttpEventNameLookup(TSEvent event);

/* APIs for dealing with UUIDs, either self made, or the system wide process UUID. See
   https://docs.trafficserver.apache.org/en/latest/developer-guide/api/functions/TSUuidCreate.en.html
*/
TSUuid        TSUuidCreate(void);
TSReturnCode  TSUuidInitialize(TSUuid uuid, TSUuidVersion v);
void          TSUuidDestroy(TSUuid uuid);
TSReturnCode  TSUuidCopy(TSUuid dest, const TSUuid src);
const char   *TSUuidStringGet(const TSUuid uuid);
TSUuidVersion TSUuidVersionGet(const TSUuid uuid);
TSReturnCode  TSUuidStringParse(TSUuid uuid, const char *uuid_str);
TSReturnCode  TSClientRequestUuidGet(TSHttpTxn txnp, char *uuid_str);

/* Get the process global UUID, resets on every startup */
TSUuid TSProcessUuidGet(void);

/**
   Returns the plugin_tag.
*/
const char *TSHttpTxnPluginTagGet(TSHttpTxn txnp);

/*
 * Return information about the client protocols.
 */
TSReturnCode TSHttpTxnClientProtocolStackGet(TSHttpTxn txnp, int count, const char **result, int *actual);
TSReturnCode TSHttpSsnClientProtocolStackGet(TSHttpSsn ssnp, int count, const char **result, int *actual);
const char  *TSHttpTxnClientProtocolStackContains(TSHttpTxn txnp, char const *tag);
const char  *TSHttpSsnClientProtocolStackContains(TSHttpSsn ssnp, char const *tag);
const char  *TSNormalizedProtocolTag(char const *tag);
const char  *TSRegisterProtocolTag(char const *tag);

/*
 * Return information about the server protocols.
 */
TSReturnCode TSHttpTxnServerProtocolStackGet(TSHttpTxn txnp, int count, const char **result, int *actual);
const char  *TSHttpTxnServerProtocolStackContains(TSHttpTxn txnp, char const *tag);

// If, for the given transaction, the URL has been remapped, this function puts the memory location of the "from" URL object in
// the variable pointed to by urlLocp, and returns TS_SUCCESS.  (The URL object will be within memory allocated to the
// transaction object.)  Otherwise, the function returns TS_ERROR.
//
TSReturnCode TSRemapFromUrlGet(TSHttpTxn txnp, TSMLoc *urlLocp);

// If, for the given transaction, the URL has been remapped, this function puts the memory location of the "to" URL object in the
// variable pointed to by urlLocp, and returns TS_SUCCESS.  (The URL object will be within memory allocated to the transaction
// object.)  Otherwise, the function returns TS_ERROR.
//
TSReturnCode TSRemapToUrlGet(TSHttpTxn txnp, TSMLoc *urlLocp);

// Get some plugin details from the TSRemapPluginInfo
void *TSRemapDLHandleGet(TSRemapPluginInfo plugin_info);

// Override response behavior, and hard-set the state machine for whether to succeed or fail, and how.
void TSHttpTxnResponseActionSet(TSHttpTxn txnp, TSResponseAction *action);

// Get the overridden response behavior set by previously called plugins.
void TSHttpTxnResponseActionGet(TSHttpTxn txnp, TSResponseAction *action);

/*
 * Get a TSIOBufferReader to read the buffered body. The return value needs to be freed.
 */
TSIOBufferReader TSHttpTxnPostBufferReaderGet(TSHttpTxn txnp);

/**
 * @brief Get the client error received from the transaction
 *
 * @param txnp The transaction where the error code is stored
 * @param error_class Either session/connection or stream/transaction error
 * @param error_code Error code received from the client
 */
void TSHttpTxnClientReceivedErrorGet(TSHttpTxn txnp, uint32_t *error_class, uint64_t *error_code);

/**
 * @brief Get the client error sent from the transaction
 *
 * @param txnp The transaction where the error code is stored
 * @param error_class Either session/connection or stream/transaction error
 * @param error_code Error code sent to the client
 */
void TSHttpTxnClientSentErrorGet(TSHttpTxn txnp, uint32_t *error_class, uint64_t *error_code);

/**
 * @brief Get the server error received from the transaction
 *
 * @param txnp The transaction where the error code is stored
 * @param error_class Either session/connection or stream/transaction error
 * @param error_code Error code sent from the server
 */
void TSHttpTxnServerReceivedErrorGet(TSHttpTxn txnp, uint32_t *error_class, uint64_t *error_code);

/**
 * @brief Get the server error sent from the transaction
 *
 * @param txnp The transaction where the error code is stored
 * @param error_class Either session/connection or stream/transaction error
 * @param error_code Error code sent to the server
 */
void TSHttpTxnServerSentErrorGet(TSHttpTxn txnp, uint32_t *error_class, uint64_t *error_code);

/**
 * Initiate an HTTP/2 Server Push preload request.
 * Use this api to register a URL that you want to preload with HTTP/2 Server Push.
 *
 * @param url the URL string to preload.
 * @param url_len the length of the URL string.
 */
TSReturnCode TSHttpTxnServerPush(TSHttpTxn txnp, const char *url, int url_len);

/** Retrieve the client side stream id for the stream of which the
 * provided transaction is a part.
 *
 * @param[in] txnp The Transaction for which the stream id should be retrieved.
 * @param[out] stream_id The stream id for this transaction.
 *
 * @return TS_ERROR if a stream id cannot be retrieved for the given
 * transaction given its protocol. For instance, if txnp is an HTTP/1.1
 * transaction, then a TS_ERROR will be returned because HTTP/1.1 does not
 * implement streams.
 */
TSReturnCode TSHttpTxnClientStreamIdGet(TSHttpTxn txnp, uint64_t *stream_id);

/** Retrieve the client side priority for the stream of which the
 * provided transaction is a part.
 *
 * @param[in] txnp The Transaction for which the stream id should be retrieved.
 * @param[out] priority The priority for the stream in this transaction.
 *
 * @return TS_ERROR if a priority cannot be retrieved for the given
 * transaction given its protocol. For instance, if txnp is an HTTP/1.1
 * transaction, then a TS_ERROR will be returned because HTTP/1.1 does not
 * implement stream priorities.
 */
TSReturnCode TSHttpTxnClientStreamPriorityGet(TSHttpTxn txnp, TSHttpPriority *priority);

/*
 * Returns TS_SUCCESS if hostname is this machine, as used for parent and remap self-detection.
 * Returns TS_ERROR if hostname is not this machine.
 */
TSReturnCode TSHostnameIsSelf(const char *hostname, size_t hostname_len);

/*
 * Gets the status of hostname in the outparam status, and the status reason in the outparam reason.
 * The reason is a logical-or combination of the reasons in TSHostStatusReason.
 * If either outparam is null, it will not be set and no error will be returned.
 * Returns TS_SUCCESS if the hostname was a parent and existed in the HostStatus, else TS_ERROR.
 */
TSReturnCode TSHostStatusGet(const char *hostname, const size_t hostname_len, TSHostStatus *status, unsigned int *reason);

/*
 * Sets the status of hostname in status, down_time, and reason.
 * The reason is a logical-or combination of the reasons in TSHostStatusReason.
 */
void TSHostStatusSet(const char *hostname, const size_t hostname_len, TSHostStatus status, const unsigned int down_time,
                     const unsigned int reason);

/*
 * Set or get various HTTP Transaction control settings.
 */
bool         TSHttpTxnCntlGet(TSHttpTxn txnp, TSHttpCntlType ctrl);
TSReturnCode TSHttpTxnCntlSet(TSHttpTxn txnp, TSHttpCntlType ctrl, bool data);

/**
 * JSONRPC callback signature for method calls.
 */
using TSRPCMethodCb = void (*)(const char *id, TSYaml params);
/**
 * JSONRPC callback signature for notification calls
 */
using TSRPCNotificationCb = void (*)(TSYaml params);

/**
 * @brief Method to perform a registration and validation when a plugin is expected to handle JSONRPC calls.
 *
 * @note YAMLCPP The JSONRPC library will only provide binary compatibility within the life-span of a major release. Plugins must
 * check-in if they intent to handle RPC commands, passing their yamlcpp library version this function will validate it against
 * the one used internally in TS.
 *
 * @param provider_name The name of the provider.
 * @param provider_len The length of the provider string.
 * @param yamlcpp_lib_version a string with the yamlcpp library version.
 * @param yamlcpp_lib_len The length of the yamlcpp_lib_len string.
 * @return A new TSRPCProviderHandle, nullptr if the yamlcpp_lib_version was not set, or the yamlcpp version does not match with
 * the one used internally in TS. The returned TSRPCProviderHandle will be set with the provider's name. The caller should pass
 * the returned TSRPCProviderHandle object to each subsequent TSRPCRegisterMethod/Notification* call.
 */
TSRPCProviderHandle TSRPCRegister(const char *provider_name, size_t provider_len, const char *yamlcpp_lib_version,
                                  size_t yamlcpp_lib_len);

/**
 * @brief Add new registered method handler to the JSON RPC engine.
 *
 * @param name Call name to be exposed by the RPC Engine, this should match the incoming request. i.e: If you register 'get_stats'
 *             then the incoming jsonrpc call should have this very same name in the 'method' field. .. {...'method':
 *             'get_stats'...} .
 * @param name_len The length of the name string.
 * @param callback  The function to be registered. See @c TSRPCMethodCb
 * @param info TSRPCProviderHandle pointer, this will be used to provide more context information about this call. This object
 * ideally should be the one returned by the TSRPCRegister API.
 * @param opt Pointer to @c TSRPCHandlerOptions object. This will be used to store specifics about a particular call, the rpc
 *            manager will use this object to perform certain actions. A copy of this object wil be stored by the rpc manager.
 *
 * @return TS_SUCCESS if the handler was successfully registered, TS_ERROR if the handler is already registered.
 */
TSReturnCode TSRPCRegisterMethodHandler(const char *name, size_t name_len, TSRPCMethodCb callback, TSRPCProviderHandle info,
                                        const TSRPCHandlerOptions *opt);

/**
 * @brief Add new registered notification handler to the JSON RPC engine.
 *
 * @param name Call name to be exposed by the RPC Engine, this should match the incoming request. i.e: If you register 'get_stats'
 *             then the incoming jsonrpc call should have this very same name in the 'method' field. .. {...'method':
 *             'get_stats'...} .
 * @param name_len The length of the name string.
 * @param callback  The function to be registered. See @c TSRPCNotificationCb
 * @param info TSRPCProviderHandle pointer, this will be used to provide more description for instance, when logging before or
 * after a call. This object ideally should be the one returned by the TSRPCRegister API.
 * @param opt Pointer to @c TSRPCHandlerOptions object. This will be used to store specifics about a particular call, the rpc
 *            manager will use this object to perform certain actions. A copy of this object wil be stored by the rpc manager.
 * @return TS_SUCCESS if the handler was successfully registered, TS_ERROR if the handler is already registered.
 */
TSReturnCode TSRPCRegisterNotificationHandler(const char *name, size_t name_len, TSRPCNotificationCb callback,
                                              TSRPCProviderHandle info, const TSRPCHandlerOptions *opt);

/**
 * @brief Function to notify the JSONRPC engine that the current handler is done working.
 *
 * This function must be used when implementing a 'method' rpc handler. Once the work is done and the response is ready to be sent
 * back to the client, this function should be called. Is expected to set the YAML node as response. If the response is empty a
 * 'success' message will be added to the client's response.
 *
 * @note This should not be used if you registered your handler as a notification: @c TSRPCNotificationCb
 * @param resp The YAML node that contains the call response.
 * @return TS_SUCCESS if no issues. TS_ERROR otherwise.
 */
TSReturnCode TSRPCHandlerDone(TSYaml resp);

/**
 * @brief Function to notify the JSONRPC engine that the current handler is done working and an error has arisen.
 *
 * @note This should not be used if you registered your handler as a notification: @c TSRPCNotificationCb
 * call.
 * @param code Error code.
 * @param descr A text with a description of the error.
 * @param descr_len The length of the description string.
 * @note The @c code and @c descr will be part of the @c 'data' field in the jsonrpc error response.
 * @return TS_SUCCESS if no issues. TS_ERROR otherwise.
 */
TSReturnCode TSRPCHandlerError(int code, const char *descr, size_t descr_len);

/** Do another cache lookup with a different cache key.
 *
 * @param txnp Transaction.
 * @param url URL to use for cache key.
 * @param length Length of the string in @a url
 *
 * @return @c TS_SUCCESS on success, @c TS_ERROR if the @a txnp is invalid or the @a url is
 * not a valid URL.
 *
 * If @a length is negative, @c strlen will be used to determine the length of @a url.
 *
 * @a url must be syntactically a URL, but otherwise it is just a string and does not need to
 * be retrievable.
 *
 * This can only be called in a @c TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK callback. To set the cache
 * key for the first lookup, use @c TSCacheUrlSet.
 *
 * @see TSCacheUrlSet
 */
TSReturnCode TSHttpTxnRedoCacheLookup(TSHttpTxn txnp, const char *url, int length);

/* IP addr parsing. This is a candidate for deprecation in v10.0.0, in favor of libswoc */
TSReturnCode TSIpStringToAddr(const char *str, size_t str_len, struct sockaddr *addr);

/**
 * Return information about the type of the transaction. Is it a tunnel transaction or fully parsed?
 * If tunneled is it due to parse failures and TR_PASS or is it due to an explicit configuration.
 *
 * @param[in] txnp The Transaction for which the type should be retrieved.
 *
 * @return enun value of type TSTxnType
 */
TSTxnType TSHttpTxnTypeGet(TSHttpTxn txnp);

/* Get Arbitrary Txn info such as cache lookup details etc as defined in TSHttpTxnInfoKey */
/**
   Return the particular txn info requested.

   @param txnp the transaction pointer
   @param key the requested txn info.
   @param TSMgmtInt a pointer to a integer where the return value is stored

   @return @c TS_SUCCESS if the requested info is supported, TS_ERROR otherwise

*/
TSReturnCode TSHttpTxnInfoIntGet(TSHttpTxn txnp, TSHttpTxnInfoKey key, TSMgmtInt *value);

/* Get Arbitrary Ssn info such as total transaction count etc as defined in TSHttpSsnInfoKey */
/**
   Return the particular ssn info requested.

   @param ssnp the transaction pointer
   @param key the requested ssn info.
   @param TSMgmtInt a pointer to a integer where the return value is stored

   @return @c TS_SUCCESS if the requested info is supported, TS_ERROR otherwise

*/
TSReturnCode TSHttpSsnInfoIntGet(TSHttpSsn ssnp, TSHttpSsnInfoKey key, TSMgmtInt *value, uint64_t sub_key = 0);

/****************************************************************************
 *  TSHttpTxnCacheLookupCountGet
 *  Return: TS_SUCCESS/TS_ERROR
 ****************************************************************************/
TSReturnCode TSHttpTxnCacheLookupCountGet(TSHttpTxn txnp, int *lookup_count);
TSReturnCode TSHttpTxnServerRespIgnore(TSHttpTxn txnp);
TSReturnCode TSHttpTxnShutDown(TSHttpTxn txnp, TSEvent event);
TSReturnCode TSHttpTxnCloseAfterResponse(TSHttpTxn txnp, int should_close);

int          TSHttpTxnClientReqIsServerStyle(TSHttpTxn txnp);
TSReturnCode TSHttpTxnUpdateCachedObject(TSHttpTxn txnp);

/**
  Opens a network connection to the host specified by the 'to' sockaddr
  spoofing the client addr to equal the 'from' sockaddr.
  If the connection is successfully opened, contp
  is called back with the event TS_EVENT_NET_CONNECT and the new
  network vconnection will be passed in the event data parameter.
  If the connection is not successful, contp is called back with
  the event TS_EVENT_NET_CONNECT_FAILED.

  Note: It is possible to receive TS_EVENT_NET_CONNECT
  even if the connection failed, because of the implementation of
  network sockets in the underlying operating system. There is an
  exception: if a plugin tries to open a connection to a port on
  its own host machine, then TS_EVENT_NET_CONNECT is sent only
  if the connection is successfully opened. In general, however,
  your plugin needs to look for an TS_EVENT_VCONN_WRITE_READY to
  be sure that the connection is successfully opened.

  @return TSAction which allows you to check if the connection is complete,
    or cancel the attempt to connect.

*/
TSAction TSNetConnectTransparent(
  TSCont                 contp, /**< continuation that is called back when the attempted net connection either succeeds or fails. */
  struct sockaddr const *from,  /**< Address to spoof as connection origin */
  struct sockaddr const *to     /**< Address to which to connect. */
);

/**
  Allocates contiguous, aligned, raw (no construction) memory for a given number number of instances of type T.

  @return Pointer to raw (in spite of pointer type) memory for first instance.
*/
template <typename T>
T *
TSRalloc(size_t count = 1 /**< Number of instances of T to allocate storage for. */
)
{
  return static_cast<std::remove_cv_t<T> *>(TSmalloc(count * sizeof(T)));
}

/**
   Return the particular PROXY protocol info requested.

   @param vconn the vconection pointer
   @param key the requested PROXY protocol info. One of TSVConnPPInfoKey or TLV type ID
   @param value a pointer to a const char pointer where the return value is stored
   @param length a pointer to a integer where the length of return value is stored

   @return @c TS_SUCCESS if the requested info is supported, TS_ERROR otherwise

*/
TSReturnCode TSVConnPPInfoGet(TSVConn vconn, uint16_t key, const char **value, int *length);

/**
   Return the particular PROXY protocol info requested.

   @param vconn the vconection pointer
   @param key the requested PROXY protocol info. One of TSVConnPPInfoKey or TLV type ID
   @param value a pointer to a integer where the return value is stored

   @return @c TS_SUCCESS if the requested info is supported, TS_ERROR otherwise

*/
TSReturnCode TSVConnPPInfoIntGet(TSVConn vconn, uint16_t key, TSMgmtInt *value);
