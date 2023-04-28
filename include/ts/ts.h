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

#include <ts/apidefs.h>
#include <ts/parentselectdefs.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* --------------------------------------------------------------------------
   Memory */
#define TSmalloc(s) _TSmalloc((s), TS_RES_MEM_PATH)
#define TSrealloc(p, s) _TSrealloc((p), (s), TS_RES_MEM_PATH)
#define TSstrdup(p) _TSstrdup((p), -1, TS_RES_MEM_PATH)
#define TSstrndup(p, n) _TSstrdup((p), (n), TS_RES_MEM_PATH)
#define TSstrlcpy(d, s, l) _TSstrlcpy((d), (s), (l))
#define TSstrlcat(d, s, l) _TSstrlcat((d), (s), (l))
#define TSfree(p) _TSfree(p)

tsapi void *_TSmalloc(size_t size, const char *path);
tsapi void *_TSrealloc(void *ptr, size_t size, const char *path);
tsapi char *_TSstrdup(const char *str, int64_t length, const char *path);
tsapi size_t _TSstrlcpy(char *dst, const char *str, size_t siz);
tsapi size_t _TSstrlcat(char *dst, const char *str, size_t siz);
tsapi void _TSfree(void *ptr);

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
tsapi TSReturnCode TSHandleMLocRelease(TSMBuffer bufp, TSMLoc parent, TSMLoc mloc);

/* --------------------------------------------------------------------------
   Install and plugin locations */
/**
    Gets the path of the directory in which Traffic Server is installed.
    Use this function to specify the location of files that the
    plugin uses.

    @return pointer to Traffic Server install directory.

 */
tsapi const char *TSInstallDirGet(void);

/**
    Gets the path of the directory of Traffic Server configuration.

    @return pointer to Traffic Server configuration directory.

 */
tsapi const char *TSConfigDirGet(void);

/**
    Gets the path of the directory of Traffic Server runtime.

    @return pointer to Traffic Server runtime directory.

 */
tsapi const char *TSRuntimeDirGet(void);

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
tsapi const char *TSPluginDirGet(void);

/* --------------------------------------------------------------------------
   Traffic Server Version */
/**
    Gets the version of Traffic Server currently running. Use this
    function to make sure that the plugin version and Traffic Server
    version are compatible. See the SDK sample code for usage.

    @return pointer to version of Traffic Server running the plugin.

 */
tsapi const char *TSTrafficServerVersionGet(void);

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
tsapi TSReturnCode TSPluginRegister(const TSPluginRegistrationInfo *plugin_info);

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
tsapi TSReturnCode TSPluginDSOReloadEnable(int enabled);

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
tsapi TSFile TSfopen(const char *filename, const char *mode);

/**
    Closes the file to which filep points and frees the data structures
    and buffers associated with it. If the file was opened for writing,
    any pending data is flushed.

    @param filep file to be closed.

 */
tsapi void TSfclose(TSFile filep);

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
tsapi ssize_t TSfread(TSFile filep, void *buf, size_t length);

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
tsapi ssize_t TSfwrite(TSFile filep, const void *buf, size_t length);

/**
    Flushes pending data that has been buffered up in memory from
    previous calls to TSfwrite().

    @param filep file to flush.

 */
tsapi void TSfflush(TSFile filep);

/**
    Reads a line from the file pointed to by filep into the buffer buf.
    Lines are terminated by a line feed character, '\n'. The line
    placed in the buffer includes the line feed character and is
    terminated with a NULL. If the line is longer than length bytes
    then only the first length-minus-1 bytes are placed in buf.

    @param filep file to read from.
    @param buf buffer to read into.
    @param length size of the buffer to read into.
    @return pointer to the string read into the buffer buf.

 */
tsapi char *TSfgets(TSFile filep, char *buf, size_t length);

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
tsapi void TSStatus(const char *fmt, ...) TS_PRINTFLIKE(1, 2);    // Log information
tsapi void TSNote(const char *fmt, ...) TS_PRINTFLIKE(1, 2);      // Log significant information
tsapi void TSWarning(const char *fmt, ...) TS_PRINTFLIKE(1, 2);   // Log concerning information
tsapi void TSError(const char *fmt, ...) TS_PRINTFLIKE(1, 2);     // Log operational failure, fail CI
tsapi void TSFatal(const char *fmt, ...) TS_PRINTFLIKE(1, 2);     // Log recoverable crash, fail CI, exit & restart
tsapi void TSAlert(const char *fmt, ...) TS_PRINTFLIKE(1, 2);     // Log recoverable crash, fail CI, exit & restart, Ops attention
tsapi void TSEmergency(const char *fmt, ...) TS_PRINTFLIKE(1, 2); // Log unrecoverable crash, fail CI, exit, Ops attention

/* --------------------------------------------------------------------------
   Assertions */
tsapi void _TSReleaseAssert(const char *txt, const char *f, int l) TS_NORETURN;
tsapi int _TSAssert(const char *txt, const char *f, int l);

#define TSReleaseAssert(EX) ((void)((EX) ? (void)0 : _TSReleaseAssert(#EX, __FILE__, __LINE__)))

#define TSAssert(EX) (void)((EX) || (_TSAssert(#EX, __FILE__, __LINE__)))

/* --------------------------------------------------------------------------
   Marshal buffers */
/**
    Creates a new marshal buffer and initializes the reference count
    to 1.

 */
tsapi TSMBuffer TSMBufferCreate(void);

/**
    Ignores the reference count and destroys the marshal buffer bufp.
    The internal data buffer associated with the marshal buffer is
    also destroyed if the marshal buffer allocated it.

    @param bufp marshal buffer to be destroyed.

 */
tsapi TSReturnCode TSMBufferDestroy(TSMBuffer bufp);

/* --------------------------------------------------------------------------
   URLs */
/**
    Creates a new URL within the marshal buffer bufp. Returns a
    location for the URL within the marshal buffer.

    @param bufp marshal buffer containing the new URL.
    @param locp pointer to a TSMLoc to store the MLoc into.

 */
tsapi TSReturnCode TSUrlCreate(TSMBuffer bufp, TSMLoc *locp);

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
tsapi TSReturnCode TSUrlClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_url, TSMLoc *locp);

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
tsapi TSReturnCode TSUrlCopy(TSMBuffer dest_bufp, TSMLoc dest_offset, TSMBuffer src_bufp, TSMLoc src_offset);

/**
    Formats a URL stored in an TSMBuffer into an TSIOBuffer.

    @param bufp marshal buffer contain the URL to be printed.
    @param offset location of the URL within bufp.
    @param iobufp destination TSIOBuffer for the URL.

 */
tsapi void TSUrlPrint(TSMBuffer bufp, TSMLoc offset, TSIOBuffer iobufp);

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
tsapi TSParseResult TSUrlParse(TSMBuffer bufp, TSMLoc offset, const char **start, const char *end);

/**
    Calculates the length of the URL located at url_loc within the
    marshal buffer bufp if it were returned as a string. This length
    is the same as the length returned by TSUrlStringGet().

    @param bufp marshal buffer containing the URL whose length you want.
    @param offset location of the URL within the marshal buffer bufp.
    @return string length of the URL.

 */
tsapi int TSUrlLengthGet(TSMBuffer bufp, TSMLoc offset);

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
tsapi char *TSUrlStringGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Retrieves the scheme portion of the URL located at url_loc within
    the marshal buffer bufp. TSUrlSchemeGet() places the length of
    the string in the length argument. If the length is NULL then no
    attempt is made to dereference it.

    @param bufp marshal buffer storing the URL.
    @param offset location of the URL within bufp.
    @param length length of the returned string.
    @return The scheme portion of the URL, as a string.

 */
tsapi const char *TSUrlRawSchemeGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Retrieves the scheme portion of the URL located at url_loc within
    the marshal buffer bufp. TSUrlSchemeGet() places the length of
    the string in the length argument. If the length is NULL then no
    attempt is made to dereference it.  If there is no explicit scheme,
    a scheme of http is returned if the URL type is HTTP, and a scheme
    of https is returned if the URL type is HTTPS.

    @param bufp marshal buffer storing the URL.
    @param offset location of the URL within bufp.
    @param length length of the returned string.
    @return The scheme portion of the URL, as a string.

 */
tsapi const char *TSUrlSchemeGet(TSMBuffer bufp, TSMLoc offset, int *length);

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
tsapi TSReturnCode TSUrlSchemeSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

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
tsapi const char *TSUrlUserGet(TSMBuffer bufp, TSMLoc offset, int *length);

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
tsapi TSReturnCode TSUrlUserSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

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
tsapi const char *TSUrlPasswordGet(TSMBuffer bufp, TSMLoc offset, int *length);

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
tsapi TSReturnCode TSUrlPasswordSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

/**
    Retrieves the host portion of the URL located at url_loc
    within bufp. Note: the returned string is not guaranteed to be
    null-terminated.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param length of the returned string.
    @return Host portion of the URL.

 */
tsapi const char *TSUrlHostGet(TSMBuffer bufp, TSMLoc offset, int *length);

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
tsapi TSReturnCode TSUrlHostSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

/**
    Returns the port portion of the URL located at url_loc if explicitly present,
    otherwise the canonical port for the URL.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @return port portion of the URL.

 */
tsapi int TSUrlPortGet(TSMBuffer bufp, TSMLoc offset);

/**
    Returns the port portion of the URL located at url_loc if explicitly present,
    otherwise 0.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @return port portion of the URL.

 */
tsapi int TSUrlRawPortGet(TSMBuffer bufp, TSMLoc offset);

/**
    Sets the port portion of the URL located at url_loc.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param port new port setting for the URL.

 */
tsapi TSReturnCode TSUrlPortSet(TSMBuffer bufp, TSMLoc offset, int port);

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
tsapi const char *TSUrlPathGet(TSMBuffer bufp, TSMLoc offset, int *length);

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
tsapi TSReturnCode TSUrlPathSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

/* --------------------------------------------------------------------------
   FTP specific URLs */
/**
    Retrieves the FTP type of the URL located at url_loc within bufp.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @return FTP type of the URL.

 */
tsapi int TSUrlFtpTypeGet(TSMBuffer bufp, TSMLoc offset);

/**
    Sets the FTP type portion of the URL located at url_loc within
    bufp to the value type.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL to modify.
    @param type new FTP type for the URL.

 */
tsapi TSReturnCode TSUrlFtpTypeSet(TSMBuffer bufp, TSMLoc offset, int type);

/* --------------------------------------------------------------------------
   HTTP specific URLs */
/**
    Retrieves the HTTP params portion of the URL located at url_loc
    within bufp. The length of the returned string is in the length
    argument. Note: the returned string is not guaranteed to be
    null-terminated.

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param length of the returned string.
    @return HTTP params portion of the URL.

 */
tsapi const char *TSUrlHttpParamsGet(TSMBuffer bufp, TSMLoc offset, int *length);

/**
    Sets the HTTP params portion of the URL located at url_loc within
    bufp to the string value. If length is -1 that TSUrlHttpParamsSet()
    assumes that value is null-terminated. Otherwise, the length of
    the string value is taken to be length. TSUrlHttpParamsSet()
    copies the string to within bufp, so you can modify or delete
    value after calling TSUrlHttpParamsSet().

    @param bufp marshal buffer containing the URL.
    @param offset location of the URL.
    @param value HTTP params string to set in the URL.
    @param length string length of the new HTTP params value.

 */
tsapi TSReturnCode TSUrlHttpParamsSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

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
tsapi const char *TSUrlHttpQueryGet(TSMBuffer bufp, TSMLoc offset, int *length);

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
tsapi TSReturnCode TSUrlHttpQuerySet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

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
tsapi const char *TSUrlHttpFragmentGet(TSMBuffer bufp, TSMLoc offset, int *length);

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
tsapi TSReturnCode TSUrlHttpFragmentSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);

/**
   Perform percent-encoding of the string in the buffer, storing the
   new string in the destination buffer. The length parameter will be
   set to the new (encoded) string length, or 0 if the encoding failed.

   @param str the string buffer to encode.
   @param str_len length of the string buffer.
   @param dst destination buffer.
   @param dst_size size of the destination buffer.
   @param length amount of data written to the destination buffer.
   @param map optional (can be NULL) map of characters to encode.

*/
tsapi TSReturnCode TSStringPercentEncode(const char *str, int str_len, char *dst, size_t dst_size, size_t *length,
                                         const unsigned char *map);

/**
   Similar to TSStringPercentEncode(), but works on a URL object.

   @param bufp marshal buffer containing the URL.
   @param offset location of the URL within bufp.
   @param dst destination buffer.
   @param dst_size size of the destination buffer.
   @param length amount of data written to the destination buffer.
   @param map optional (can be NULL) map of characters to encode.

*/
tsapi TSReturnCode TSUrlPercentEncode(TSMBuffer bufp, TSMLoc offset, char *dst, size_t dst_size, size_t *length,
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
tsapi TSReturnCode TSStringPercentDecode(const char *str, size_t str_len, char *dst, size_t dst_size, size_t *length);

/* --------------------------------------------------------------------------
   MIME headers */

/**
    Creates a MIME parser. The parser's data structure contains
    information about the header being parsed. A single MIME
    parser can be used multiple times, though not simultaneously.
    Before being used again, the parser must be cleared by calling
    TSMimeParserClear().

 */
tsapi TSMimeParser TSMimeParserCreate(void);

/**
    Clears the specified MIME parser so that it can be used again.

    @param parser to be cleared.

 */
tsapi void TSMimeParserClear(TSMimeParser parser);

/**
    Destroys the specified MIME parser and frees the associated memory.

    @param parser to destroy.
 */
tsapi void TSMimeParserDestroy(TSMimeParser parser);

/**
    Creates a new MIME header within bufp. Release with a call to
    TSHandleMLocRelease().

    @param bufp marshal buffer to contain the new MIME header.
    @param locp buffer pointer to contain the MLoc

 */
tsapi TSReturnCode TSMimeHdrCreate(TSMBuffer bufp, TSMLoc *locp);

/**
    Destroys the MIME header located at hdr_loc within bufp.

    @param bufp marshal buffer containing the MIME header to destroy.
    @param offset location of the MIME header.

 */
tsapi TSReturnCode TSMimeHdrDestroy(TSMBuffer bufp, TSMLoc offset);

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
tsapi TSReturnCode TSMimeHdrClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc *locp);

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
tsapi TSReturnCode TSMimeHdrCopy(TSMBuffer dest_bufp, TSMLoc dest_offset, TSMBuffer src_bufp, TSMLoc src_offset);

/**
    Formats the MIME header located at hdr_loc within bufp into the
    TSIOBuffer iobufp.

    @param bufp marshal buffer containing the header to be copied to
      an TSIOBuffer.
    @param offset
    @param iobufp target TSIOBuffer.

 */
tsapi void TSMimeHdrPrint(TSMBuffer bufp, TSMLoc offset, TSIOBuffer iobufp);

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
tsapi TSParseResult TSMimeHdrParse(TSMimeParser parser, TSMBuffer bufp, TSMLoc offset, const char **start, const char *end);

/**
    Calculates the length of the MIME header located at hdr_loc if it
    were returned as a string. This the length of the MIME header in
    its unparsed form.

    @param bufp marshal buffer containing the MIME header.
    @param offset location of the MIME header.
    @return string length of the MIME header located at hdr_loc.

 */
tsapi int TSMimeHdrLengthGet(TSMBuffer bufp, TSMLoc offset);

/**
    Removes and destroys all the MIME fields within the MIME header
    located at hdr_loc within the marshal buffer bufp.

    @param bufp marshal buffer containing the MIME header.
    @param offset location of the MIME header.

 */
tsapi TSReturnCode TSMimeHdrFieldsClear(TSMBuffer bufp, TSMLoc offset);

/**
    Returns a count of the number of MIME fields within the MIME header
    located at hdr_loc within the marshal buffer bufp.

    @param bufp marshal buffer containing the MIME header.
    @param offset location of the MIME header within bufp.
    @return number of MIME fields within the MIME header located
      at hdr_loc.

 */
tsapi int TSMimeHdrFieldsCount(TSMBuffer bufp, TSMLoc offset);

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
tsapi TSMLoc TSMimeHdrFieldGet(TSMBuffer bufp, TSMLoc hdr, int idx);

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
tsapi TSMLoc TSMimeHdrFieldFind(TSMBuffer bufp, TSMLoc hdr, const char *name, int length);

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
tsapi TSReturnCode TSMimeHdrFieldAppend(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);

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
tsapi TSReturnCode TSMimeHdrFieldRemove(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);

tsapi TSReturnCode TSMimeHdrFieldCreate(TSMBuffer bufp, TSMLoc hdr, TSMLoc *locp);

/****************************************************************************
 *  Create a new field and assign it a name all in one call
 ****************************************************************************/
tsapi TSReturnCode TSMimeHdrFieldCreateNamed(TSMBuffer bufp, TSMLoc mh_mloc, const char *name, int name_len, TSMLoc *locp);

/**
    Destroys the MIME field located at field within bufp. You must
    release the TSMLoc field with a call to TSHandleMLocRelease().

    @param bufp contains the MIME field to be destroyed.
    @param hdr location of the parent header containing the field
      to be destroyed. This could be the location of a MIME header or
      HTTP header.
    @param field location of the field to be destroyed.

 */
tsapi TSReturnCode TSMimeHdrFieldDestroy(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);

tsapi TSReturnCode TSMimeHdrFieldClone(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc src_field,
                                       TSMLoc *locp);
tsapi TSReturnCode TSMimeHdrFieldCopy(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMLoc dest_field, TSMBuffer src_bufp, TSMLoc src_hdr,
                                      TSMLoc src_field);
tsapi TSReturnCode TSMimeHdrFieldCopyValues(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMLoc dest_field, TSMBuffer src_bufp,
                                            TSMLoc src_hdr, TSMLoc src_field);
tsapi TSMLoc TSMimeHdrFieldNext(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);
tsapi TSMLoc TSMimeHdrFieldNextDup(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);
tsapi int TSMimeHdrFieldLengthGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);
tsapi const char *TSMimeHdrFieldNameGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int *length);
tsapi TSReturnCode TSMimeHdrFieldNameSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, const char *name, int length);

tsapi TSReturnCode TSMimeHdrFieldValuesClear(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);
tsapi int TSMimeHdrFieldValuesCount(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);

tsapi const char *TSMimeHdrFieldValueStringGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int *value_len_ptr);
tsapi int TSMimeHdrFieldValueIntGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx);
tsapi int64_t TSMimeHdrFieldValueInt64Get(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx);
tsapi unsigned int TSMimeHdrFieldValueUintGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx);
tsapi time_t TSMimeHdrFieldValueDateGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field);
tsapi TSReturnCode TSMimeHdrFieldValueStringSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value, int length);
tsapi TSReturnCode TSMimeHdrFieldValueIntSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int value);
tsapi TSReturnCode TSMimeHdrFieldValueInt64Set(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int64_t value);
tsapi TSReturnCode TSMimeHdrFieldValueUintSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, unsigned int value);
tsapi TSReturnCode TSMimeHdrFieldValueDateSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, time_t value);

tsapi TSReturnCode TSMimeHdrFieldValueAppend(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value, int length);
/* These Insert() APIs should be considered. Use the corresponding Set() API instead */
tsapi TSReturnCode TSMimeHdrFieldValueStringInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value,
                                                   int length);
tsapi TSReturnCode TSMimeHdrFieldValueIntInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int value);
tsapi TSReturnCode TSMimeHdrFieldValueUintInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, unsigned int value);
tsapi TSReturnCode TSMimeHdrFieldValueDateInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, time_t value);

tsapi TSReturnCode TSMimeHdrFieldValueDelete(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx);

/* --------------------------------------------------------------------------
   HTTP headers */
tsapi TSHttpParser TSHttpParserCreate(void);
tsapi void TSHttpParserClear(TSHttpParser parser);
tsapi void TSHttpParserDestroy(TSHttpParser parser);

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
tsapi TSParseResult TSHttpHdrParseReq(TSHttpParser parser, TSMBuffer bufp, TSMLoc offset, const char **start, const char *end);

tsapi TSParseResult TSHttpHdrParseResp(TSHttpParser parser, TSMBuffer bufp, TSMLoc offset, const char **start, const char *end);

tsapi TSMLoc TSHttpHdrCreate(TSMBuffer bufp);

/**
    Destroys the HTTP header located at hdr_loc within the marshal
    buffer bufp. Do not forget to release the handle hdr_loc with a
    call to TSHandleMLocRelease().

 */
tsapi void TSHttpHdrDestroy(TSMBuffer bufp, TSMLoc offset);

tsapi TSReturnCode TSHttpHdrClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc *locp);

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
tsapi TSReturnCode TSHttpHdrCopy(TSMBuffer dest_bufp, TSMLoc dest_offset, TSMBuffer src_bufp, TSMLoc src_offset);

tsapi void TSHttpHdrPrint(TSMBuffer bufp, TSMLoc offset, TSIOBuffer iobufp);

tsapi int TSHttpHdrLengthGet(TSMBuffer bufp, TSMLoc offset);

tsapi TSHttpType TSHttpHdrTypeGet(TSMBuffer bufp, TSMLoc offset);
tsapi TSReturnCode TSHttpHdrTypeSet(TSMBuffer bufp, TSMLoc offset, TSHttpType type);

tsapi int TSHttpHdrVersionGet(TSMBuffer bufp, TSMLoc offset);
tsapi TSReturnCode TSHttpHdrVersionSet(TSMBuffer bufp, TSMLoc offset, int ver);

tsapi const char *TSHttpHdrMethodGet(TSMBuffer bufp, TSMLoc offset, int *length);
tsapi TSReturnCode TSHttpHdrMethodSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);
tsapi const char *TSHttpHdrHostGet(TSMBuffer bufp, TSMLoc offset, int *length);
tsapi TSReturnCode TSHttpHdrUrlGet(TSMBuffer bufp, TSMLoc offset, TSMLoc *locp);
tsapi TSReturnCode TSHttpHdrUrlSet(TSMBuffer bufp, TSMLoc offset, TSMLoc url);

tsapi TSHttpStatus TSHttpHdrStatusGet(TSMBuffer bufp, TSMLoc offset);
tsapi TSReturnCode TSHttpHdrStatusSet(TSMBuffer bufp, TSMLoc offset, TSHttpStatus status);
tsapi const char *TSHttpHdrReasonGet(TSMBuffer bufp, TSMLoc offset, int *length);
tsapi TSReturnCode TSHttpHdrReasonSet(TSMBuffer bufp, TSMLoc offset, const char *value, int length);
tsapi const char *TSHttpHdrReasonLookup(TSHttpStatus status);

/* --------------------------------------------------------------------------
   Threads */
tsapi TSThread TSThreadCreate(TSThreadFunc func, void *data);
tsapi TSThread TSThreadInit(void);
tsapi void TSThreadDestroy(TSThread thread);
tsapi void TSThreadWait(TSThread thread);
tsapi TSThread TSThreadSelf(void);
tsapi TSEventThread TSEventThreadSelf(void);

/* --------------------------------------------------------------------------
   Mutexes */
tsapi TSMutex TSMutexCreate(void);
tsapi void TSMutexDestroy(TSMutex mutexp);
tsapi void TSMutexLock(TSMutex mutexp);
tsapi TSReturnCode TSMutexLockTry(TSMutex mutexp);

tsapi void TSMutexUnlock(TSMutex mutexp);

/* --------------------------------------------------------------------------
   cachekey */
/**
    Creates (allocates memory for) a new cache key.
 */
tsapi TSCacheKey TSCacheKeyCreate(void);

/**
    Generates a key for an object to be cached (written to the cache).

    @param key to be associated with the cached object. Before
      calling TSCacheKeySetDigest() you must create the key with
      TSCacheKeyCreate().
    @param input string that uniquely identifies the object. In most
      cases, it is the URL of the object.
    @param length of the string input.

 */
tsapi TSReturnCode TSCacheKeyDigestSet(TSCacheKey key, const char *input, int length);

tsapi TSReturnCode TSCacheKeyDigestFromUrlSet(TSCacheKey key, TSMLoc url);

/**
    Associates a host name to the cache key. Use this function if the
    cache has been partitioned by hostname. The hostname tells the
    cache which volume to use for the object.

    @param key of the cached object.
    @param hostname to associate with the cache key.
    @param host_len length of the string hostname.

 */
tsapi TSReturnCode TSCacheKeyHostNameSet(TSCacheKey key, const char *hostname, int host_len);

tsapi TSReturnCode TSCacheKeyPinnedSet(TSCacheKey key, time_t pin_in_cache);

/**
    Destroys a cache key. You must destroy cache keys when you are
    finished with them, i.e. after all reads and writes are completed.

    @param key to be destroyed.

 */
tsapi TSReturnCode TSCacheKeyDestroy(TSCacheKey key);

/* --------------------------------------------------------------------------
   cache url */
tsapi TSReturnCode TSCacheUrlSet(TSHttpTxn txnp, const char *url, int length);

/* --------------------------------------------------------------------------
   Configuration */
tsapi unsigned int TSConfigSet(unsigned int id, void *data, TSConfigDestroyFunc funcp);
tsapi TSConfig TSConfigGet(unsigned int id);
tsapi void TSConfigRelease(unsigned int id, TSConfig configp);
tsapi void *TSConfigDataGet(TSConfig configp);

/* --------------------------------------------------------------------------
   Management */
tsapi void TSMgmtUpdateRegister(TSCont contp, const char *plugin_name);
tsapi TSReturnCode TSMgmtIntGet(const char *var_name, TSMgmtInt *result);
tsapi TSReturnCode TSMgmtCounterGet(const char *var_name, TSMgmtCounter *result);
tsapi TSReturnCode TSMgmtFloatGet(const char *var_name, TSMgmtFloat *result);
tsapi TSReturnCode TSMgmtStringGet(const char *var_name, TSMgmtString *result);
tsapi TSReturnCode TSMgmtSourceGet(const char *var_name, TSMgmtSource *source);
tsapi TSReturnCode TSMgmtConfigFileAdd(const char *parent, const char *fileName);
tsapi TSReturnCode TSMgmtDataTypeGet(const char *var_name, TSRecordDataType *result);

/* --------------------------------------------------------------------------
   Continuations */
tsapi TSCont TSContCreate(TSEventFunc funcp, TSMutex mutexp);
tsapi void TSContDestroy(TSCont contp);
tsapi void TSContDataSet(TSCont contp, void *data);
tsapi void *TSContDataGet(TSCont contp);
tsapi TSAction TSContSchedule(TSCont contp, TSHRTime timeout);
tsapi TSAction TSContScheduleOnPool(TSCont contp, TSHRTime timeout, TSThreadPool tp);
tsapi TSAction TSContScheduleOnThread(TSCont contp, TSHRTime timeout, TSEventThread ethread);
tsapi TSAction TSContScheduleEvery(TSCont contp, TSHRTime every /* millisecs */);
tsapi TSAction TSContScheduleEveryOnPool(TSCont contp, TSHRTime every /* millisecs */, TSThreadPool tp);
tsapi TSAction TSContScheduleEveryOnThread(TSCont contp, TSHRTime every /* millisecs */, TSEventThread ethread);
tsapi TSReturnCode TSContThreadAffinitySet(TSCont contp, TSEventThread ethread);
tsapi TSEventThread TSContThreadAffinityGet(TSCont contp);
tsapi void TSContThreadAffinityClear(TSCont contp);
tsapi TSAction TSHttpSchedule(TSCont contp, TSHttpTxn txnp, TSHRTime timeout);
tsapi int TSContCall(TSCont contp, TSEvent event, void *edata);
tsapi TSMutex TSContMutexGet(TSCont contp);

/* --------------------------------------------------------------------------
   Plugin lifecycle  hooks */
tsapi void TSLifecycleHookAdd(TSLifecycleHookID id, TSCont contp);
/* --------------------------------------------------------------------------
   HTTP hooks */
tsapi void TSHttpHookAdd(TSHttpHookID id, TSCont contp);

/* --------------------------------------------------------------------------
   HTTP sessions */
tsapi void TSHttpSsnHookAdd(TSHttpSsn ssnp, TSHttpHookID id, TSCont contp);
tsapi void TSHttpSsnReenable(TSHttpSsn ssnp, TSEvent event);
tsapi int TSHttpSsnTransactionCount(TSHttpSsn ssnp);
/* Get the TSVConn from a session. */
tsapi TSVConn TSHttpSsnClientVConnGet(TSHttpSsn ssnp);
tsapi TSVConn TSHttpSsnServerVConnGet(TSHttpSsn ssnp);
/* Get the TSVConn from a transaction. */
tsapi TSVConn TSHttpTxnServerVConnGet(TSHttpTxn txnp);

/* --------------------------------------------------------------------------
   SSL connections */
/* Re-enable an SSL connection from a hook.
   This must be called exactly once before the SSL connection will resume. */
tsapi void TSVConnReenable(TSVConn sslvcp);
/* Extended version that allows for passing a status event on reenabling
 */
tsapi void TSVConnReenableEx(TSVConn sslvcp, TSEvent event);
/*  Set the connection to go into blind tunnel mode */
tsapi TSReturnCode TSVConnTunnel(TSVConn sslp);
/*  Return the SSL object associated with the connection */
tsapi TSSslConnection TSVConnSslConnectionGet(TSVConn sslp);
/* Return the intermediate X509StoreCTX object that references the certificate being validated */
tsapi TSSslVerifyCTX TSVConnSslVerifyCTXGet(TSVConn sslp);
/*  Fetch a SSL context from the global lookup table */
tsapi TSSslContext TSSslContextFindByName(const char *name);
tsapi TSSslContext TSSslContextFindByAddr(struct sockaddr const *);
/* Fetch SSL client contexts from the global lookup table */
tsapi TSReturnCode TSSslClientContextsNamesGet(int n, const char **result, int *actual);
tsapi TSSslContext TSSslClientContextFindByName(const char *ca_paths, const char *ck_paths);

/* Update SSL certs in internal storage from given path */
tsapi TSReturnCode TSSslClientCertUpdate(const char *cert_path, const char *key_path);
tsapi TSReturnCode TSSslServerCertUpdate(const char *cert_path, const char *key_path);

/* Create a new SSL context based on the settings in records.config */
tsapi TSSslContext TSSslServerContextCreate(TSSslX509 cert, const char *certname, const char *rsp_file);
tsapi void TSSslContextDestroy(TSSslContext ctx);
tsapi TSReturnCode TSSslTicketKeyUpdate(char *ticketData, int ticketDataLen);
TSAcceptor TSAcceptorGet(TSVConn sslp);
TSAcceptor TSAcceptorGetbyID(int ID);
int TSAcceptorCount();
int TSAcceptorIDGet(TSAcceptor acceptor);
TSReturnCode TSVConnProtocolDisable(TSVConn connp, const char *protocol_name);
TSReturnCode TSVConnProtocolEnable(TSVConn connp, const char *protocol_name);

/*  Returns 1 if the sslp argument refers to a SSL connection */
tsapi int TSVConnIsSsl(TSVConn sslp);
/* Returns 1 if a certificate was provided in the TLS handshake, 0 otherwise.
 */
tsapi int TSVConnProvidedSslCert(TSVConn sslp);
tsapi const char *TSVConnSslSniGet(TSVConn sslp, int *length);

tsapi TSSslSession TSSslSessionGet(const TSSslSessionID *session_id);
tsapi int TSSslSessionGetBuffer(const TSSslSessionID *session_id, char *buffer, int *len_ptr);
tsapi TSReturnCode TSSslSessionInsert(const TSSslSessionID *session_id, TSSslSession add_session, TSSslConnection ssl_conn);
tsapi TSReturnCode TSSslSessionRemove(const TSSslSessionID *session_id);

/* --------------------------------------------------------------------------
   HTTP transactions */
tsapi void TSHttpTxnHookAdd(TSHttpTxn txnp, TSHttpHookID id, TSCont contp);
tsapi TSHttpSsn TSHttpTxnSsnGet(TSHttpTxn txnp);

/* Gets the client request header for a specified HTTP transaction. */
tsapi TSReturnCode TSHttpTxnClientReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
/* Gets the client response header for a specified HTTP transaction. */
tsapi TSReturnCode TSHttpTxnClientRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
/* Gets the server request header from a specified HTTP transaction. */
tsapi TSReturnCode TSHttpTxnServerReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
/* Gets the server response header from a specified HTTP transaction. */
tsapi TSReturnCode TSHttpTxnServerRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
/* Gets the cached request header for a specified HTTP transaction. */
tsapi TSReturnCode TSHttpTxnCachedReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
/* Gets the cached response header for a specified HTTP transaction. */
tsapi TSReturnCode TSHttpTxnCachedRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);

tsapi TSReturnCode TSHttpTxnPristineUrlGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *url_loc);

/**
 * @brief Gets  the number of transactions between the Traffic Server proxy and the origin server from a single session.
 *        Any value greater than zero indicates connection reuse.
 *
 * @param txnp The transaction
 * @return int The number of transactions between the Traffic Server proxy and the origin server from a single session
 */
tsapi int TSHttpTxnServerSsnTransactionCount(TSHttpTxn txnp);

/** Get the effective URL for the transaction.
    The effective URL is the URL taking in to account both the explicit
    URL in the request and the HOST field.

    A possibly non-null terminated string is returned.

    @note The returned string is allocated and must be freed by the caller
    after use with @c TSfree.
*/
tsapi char *TSHttpTxnEffectiveUrlStringGet(TSHttpTxn txnp, int *length /**< String length return, may be @c NULL. */
);

/** Get the effective URL for in the header (if any), with the scheme and host normalized to lower case letter.
    The effective URL is the URL taking in to account both the explicit
    URL in the request and the HOST field.

    A possibly non-null terminated string is returned.

    @return TS_SUCCESS if successful, TS_ERROR if no URL in header or other error.
*/
tsapi TSReturnCode TSHttpHdrEffectiveUrlBufGet(TSMBuffer hdr_buf, TSMLoc hdr_loc, char *buf, int64_t size, int64_t *length);

tsapi void TSHttpTxnRespCacheableSet(TSHttpTxn txnp, int flag);
tsapi void TSHttpTxnReqCacheableSet(TSHttpTxn txnp, int flag);

/** Set flag indicating whether or not to cache the server response for
    given TSHttpTxn

    @note This should be done in the HTTP_READ_RESPONSE_HDR_HOOK or earlier.

    @note If TSHttpTxnRespCacheableSet() is not working the way you expect,
    this may be the function you should use instead.

    @param txnp The transaction whose server response you do not want to store.
    @param flag Set 0 to allow storing and 1 to disable storing.

    @return TS_SUCCESS.
*/
tsapi TSReturnCode TSHttpTxnServerRespNoStoreSet(TSHttpTxn txnp, int flag);

/** Get flag indicating whether or not to cache the server response for
    given TSHttpTxn
    @param txnp The transaction whose server response you do not want to store.

    @return TS_SUCCESS.
*/
tsapi bool TSHttpTxnServerRespNoStoreGet(TSHttpTxn txnp);
tsapi TSReturnCode TSFetchPageRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
tsapi char *TSFetchRespGet(TSHttpTxn txnp, int *length);
tsapi TSReturnCode TSHttpTxnCacheLookupStatusGet(TSHttpTxn txnp, int *lookup_status);

tsapi TSReturnCode TSHttpTxnTransformRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);

/** Set the @a port value for the inbound (user agent) connection in the transaction @a txnp.
    This is used primarily where the connection is synthetic and therefore does not have a port.
    @note @a port is in @b host @b order.
*/
tsapi void TSHttpTxnClientIncomingPortSet(TSHttpTxn txnp, int port);

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
tsapi struct sockaddr const *TSHttpTxnClientAddrGet(TSHttpTxn txnp);
/** Get the incoming address.

    @note The pointer is valid only for the current callback. Clients
    that need to keep the value across callbacks must maintain their
    own storage.

    @return Local address of the client connection for transaction @a txnp.
*/
tsapi struct sockaddr const *TSHttpTxnIncomingAddrGet(TSHttpTxn txnp);
/** Get the outgoing address.

    @note The pointer is valid only for the current callback. Clients
    that need to keep the value across callbacks must maintain their
    own storage.

    @return Local address of the server connection for transaction @a txnp.
*/
tsapi struct sockaddr const *TSHttpTxnOutgoingAddrGet(TSHttpTxn txnp);
/** Get the origin server address.
 *
    @note The pointer is valid only for the current callback. Clients
    that need to keep the value across callbacks must maintain their
    own storage.

    @return The address of the origin server for transaction @a txnp.
*/
tsapi struct sockaddr const *TSHttpTxnServerAddrGet(TSHttpTxn txnp);
/** Set the origin server address.

    This must be invoked before the origin server address is looked up.
    If called no lookup is done, the address @a addr is used instead.

    @return @c TS_SUCCESS if the origin server address is set, @c TS_ERROR otherwise.
*/
tsapi TSReturnCode TSHttpTxnServerAddrSet(TSHttpTxn txnp, struct sockaddr const *addr /**< Address for origin server. */
);

/** Get the next hop address.
 *
    @note The pointer is valid only for the current callback. Clients
    that need to keep the value across callbacks must maintain their
    own storage.

    @return The address of the next hop for transaction @a txnp.
*/
tsapi struct sockaddr const *TSHttpTxnNextHopAddrGet(TSHttpTxn txnp);

/** Get the next hop name.
 *
    @note The pointer is valid only for the current callback. Clients
    that need to keep the value across callbacks must maintain their
    own storage.

    @return The name of the next hop for transaction @a txnp.
*/
tsapi const char *TSHttpTxnNextHopNameGet(TSHttpTxn txnp);

tsapi TSReturnCode TSHttpTxnClientFdGet(TSHttpTxn txnp, int *fdp);
tsapi TSReturnCode TSHttpTxnOutgoingAddrSet(TSHttpTxn txnp, struct sockaddr const *addr);
tsapi TSReturnCode TSHttpTxnOutgoingTransparencySet(TSHttpTxn txnp, int flag);
tsapi TSReturnCode TSHttpTxnServerFdGet(TSHttpTxn txnp, int *fdp);

/* TS-1008: the above TXN calls for the Client conn should work with SSN */
tsapi struct sockaddr const *TSHttpSsnClientAddrGet(TSHttpSsn ssnp);
tsapi struct sockaddr const *TSHttpSsnIncomingAddrGet(TSHttpSsn ssnp);
tsapi TSReturnCode TSHttpSsnClientFdGet(TSHttpSsn ssnp, int *fdp);
/* TS-1008 END */

/** Change packet firewall mark for the client side connection
 *
    @note The change takes effect immediately

    @return TS_SUCCESS if the client connection was modified
*/
tsapi TSReturnCode TSHttpTxnClientPacketMarkSet(TSHttpTxn txnp, int mark);

/** Change packet firewall mark for the server side connection
 *
    @note The change takes effect immediately, if no OS connection has been
    made, then this sets the mark that will be used IF an OS connection
    is established

    @return TS_SUCCESS if the (future?) server connection was modified
*/
tsapi TSReturnCode TSHttpTxnServerPacketMarkSet(TSHttpTxn txnp, int mark);

/** Change packet TOS for the client side connection
 *
    @note The change takes effect immediately

    @note TOS is deprecated and replaced by DSCP, this is still used to
    set DSCP however the first 2 bits of this value will be ignored as
    they now belong to the ECN field.

    @return TS_SUCCESS if the client connection was modified
*/
tsapi TSReturnCode TSHttpTxnClientPacketTosSet(TSHttpTxn txnp, int tos);

/** Change packet TOS for the server side connection
 *

    @note The change takes effect immediately, if no OS connection has been
    made, then this sets the mark that will be used IF an OS connection
    is established

    @note TOS is deprecated and replaced by DSCP, this is still used to
    set DSCP however the first 2 bits of this value will be ignored as
    they now belong to the ECN field.

    @return TS_SUCCESS if the (future?) server connection was modified
*/
tsapi TSReturnCode TSHttpTxnServerPacketTosSet(TSHttpTxn txnp, int tos);

/** Change packet DSCP for the client side connection
 *
    @note The change takes effect immediately

    @return TS_SUCCESS if the client connection was modified
*/
tsapi TSReturnCode TSHttpTxnClientPacketDscpSet(TSHttpTxn txnp, int dscp);

/** Change packet DSCP for the server side connection
 *

    @note The change takes effect immediately, if no OS connection has been
    made, then this sets the mark that will be used IF an OS connection
    is established

    @return TS_SUCCESS if the (future?) server connection was modified
*/
tsapi TSReturnCode TSHttpTxnServerPacketDscpSet(TSHttpTxn txnp, int dscp);

/**
   Sets an error type body to a transaction. Note that both string arguments
   must be allocated with TSmalloc() or TSstrdup(). The mimetype argument is
   optional, if not provided it defaults to "text/html". Sending an empty
   string would prevent setting a content type header (but that is not advised).

   @param txnp HTTP transaction whose parent proxy to get.
   @param buf The body message (must be heap allocated).
   @param buflength Length of the body message.
   @param mimetype The MIME type to set the response to (can be NULL, but must
          be heap allocated if non-NULL).
*/
tsapi void TSHttpTxnErrorBodySet(TSHttpTxn txnp, char *buf, size_t buflength, char *mimetype);

/**
    Retrieves the parent proxy hostname and port, if parent
    proxying is enabled. If parent proxying is not enabled,
    TSHttpTxnParentProxyGet() sets hostname to NULL and port to -1.

    @param txnp HTTP transaction whose parent proxy to get.
    @param hostname of the parent proxy.
    @param port parent proxy's port.

 */
tsapi TSReturnCode TSHttpTxnParentProxyGet(TSHttpTxn txnp, const char **hostname, int *port);

/**
    Sets the parent proxy name and port. The string hostname is copied
    into the TSHttpTxn; you can modify or delete the string after
    calling TSHttpTxnParentProxySet().

    @param txnp HTTP transaction whose parent proxy to set.
    @param hostname parent proxy host name string.
    @param port parent proxy port to set.

 */
tsapi void TSHttpTxnParentProxySet(TSHttpTxn txnp, const char *hostname, int port);

tsapi TSReturnCode TSHttpTxnParentSelectionUrlGet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj);
tsapi TSReturnCode TSHttpTxnParentSelectionUrlSet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj);

tsapi void TSHttpTxnUntransformedRespCache(TSHttpTxn txnp, int on);
tsapi void TSHttpTxnTransformedRespCache(TSHttpTxn txnp, int on);

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
tsapi void TSHttpTxnReenable(TSHttpTxn txnp, TSEvent event);
tsapi TSReturnCode TSHttpCacheReenable(TSCacheTxn txnp, const TSEvent event, const void *data, const uint64_t size);

/* The reserve API should only be use in TSAPI plugins, during plugin initialization!
   The lookup methods can be used anytime, but are best used during initialization as well,
   or at least "cache" the results for best performance. */
tsapi TSReturnCode TSUserArgIndexReserve(TSUserArgType type, const char *name, const char *description, int *arg_idx);
tsapi TSReturnCode TSUserArgIndexNameLookup(TSUserArgType type, const char *name, int *arg_idx, const char **description);
tsapi TSReturnCode TSUserArgIndexLookup(TSUserArgType type, int arg_idx, const char **name, const char **description);
tsapi void TSUserArgSet(void *data, int arg_idx, void *arg);
tsapi void *TSUserArgGet(void *data, int arg_idx);

/* These are deprecated as of v9.0.0, and will be removed in v10.0.0 */
tsapi TS_DEPRECATED void TSHttpTxnArgSet(TSHttpTxn txnp, int arg_idx, void *arg);
tsapi TS_DEPRECATED void *TSHttpTxnArgGet(TSHttpTxn txnp, int arg_idx);
tsapi TS_DEPRECATED void TSHttpSsnArgSet(TSHttpSsn ssnp, int arg_idx, void *arg);
tsapi TS_DEPRECATED void *TSHttpSsnArgGet(TSHttpSsn ssnp, int arg_idx);
tsapi TS_DEPRECATED void TSVConnArgSet(TSVConn connp, int arg_idx, void *arg);
tsapi TS_DEPRECATED void *TSVConnArgGet(TSVConn connp, int arg_idx);

tsapi TS_DEPRECATED TSReturnCode TSHttpTxnArgIndexReserve(const char *name, const char *description, int *arg_idx);
tsapi TS_DEPRECATED TSReturnCode TSHttpTxnArgIndexNameLookup(const char *name, int *arg_idx, const char **description);
tsapi TS_DEPRECATED TSReturnCode TSHttpTxnArgIndexLookup(int arg_idx, const char **name, const char **description);
tsapi TS_DEPRECATED TSReturnCode TSHttpSsnArgIndexReserve(const char *name, const char *description, int *arg_idx);
tsapi TS_DEPRECATED TSReturnCode TSHttpSsnArgIndexNameLookup(const char *name, int *arg_idx, const char **description);
tsapi TS_DEPRECATED TSReturnCode TSHttpSsnArgIndexLookup(int arg_idx, const char **name, const char **description);
tsapi TS_DEPRECATED TSReturnCode TSVConnArgIndexReserve(const char *name, const char *description, int *arg_idx);
tsapi TS_DEPRECATED TSReturnCode TSVConnArgIndexNameLookup(const char *name, int *arg_idx, const char **description);
tsapi TS_DEPRECATED TSReturnCode TSVConnArgIndexLookup(int arg_idx, const char **name, const char **description);

tsapi void TSHttpTxnStatusSet(TSHttpTxn txnp, TSHttpStatus status);
tsapi TSHttpStatus TSHttpTxnStatusGet(TSHttpTxn txnp);

tsapi void TSHttpTxnActiveTimeoutSet(TSHttpTxn txnp, int timeout);
tsapi void TSHttpTxnConnectTimeoutSet(TSHttpTxn txnp, int timeout);
tsapi void TSHttpTxnDNSTimeoutSet(TSHttpTxn txnp, int timeout);
tsapi void TSHttpTxnNoActivityTimeoutSet(TSHttpTxn txnp, int timeout);

tsapi TSServerState TSHttpTxnServerStateGet(TSHttpTxn txnp);

/* --------------------------------------------------------------------------
   Transaction specific debugging control  */

/**
       Set the transaction specific debugging flag for this transaction.
       When turned on, internal debug messages related to this transaction
       will be written even if the debug tag isn't on.

    @param txnp transaction to change.
    @param on set to 1 to turn on, 0 to turn off.
*/
tsapi void TSHttpTxnDebugSet(TSHttpTxn txnp, int on);
/**
       Returns the transaction specific debugging flag for this transaction.

    @param txnp transaction to check.
    @return 1 if enabled, 0 otherwise.
*/
tsapi int TSHttpTxnDebugGet(TSHttpTxn txnp);
/**
       Set the session specific debugging flag for this client session.
       When turned on, internal debug messages related to this session and all transactions
       in the session will be written even if the debug tag isn't on.

    @param ssnp Client session to change.
    @param on set to 1 to turn on, 0 to turn off.
*/
tsapi void TSHttpSsnDebugSet(TSHttpSsn ssnp, int on);
/**
       Returns the transaction specific debugging flag for this client session.

    @param txnp Client session to check.
    @return 1 if enabled, 0 otherwise.
*/
tsapi int TSHttpSsnDebugGet(TSHttpSsn ssnp, int *on);

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
tsapi void TSHttpTxnIntercept(TSCont contp, TSHttpTxn txnp);

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
tsapi void TSHttpTxnServerIntercept(TSCont contp, TSHttpTxn txnp);

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
tsapi TSVConn TSHttpConnectPlugin(TSHttpConnectOptions *options);

/** Backwards compatible version.
    This function calls This provides a @a buffer_index of 8 and a @a buffer_water_mark of 0.

    @param addr Target address of the origin server.
    @param tag A logging tag that can be accessed via the pitag field. May be @c NULL.
    @param id A logging id that can be access via the piid field.
 */
tsapi TSVConn TSHttpConnectWithPluginId(struct sockaddr const *addr, const char *tag, int64_t id);

/** Backwards compatible version.
    This provides a @a tag of "plugin" and an @a id of 0.
 */
tsapi TSVConn TSHttpConnect(struct sockaddr const *addr);

/**
   Get an instance of TSHttpConnectOptions with default values.
 */
tsapi TSHttpConnectOptions TSHttpConnectOptionsGet(TSConnectType connect_type);

/**
   Get the value of proxy.config.plugin.vc.default_buffer_index from the TSHttpTxn
 */
tsapi TSIOBufferSizeIndex TSPluginVCIOBufferIndexGet(TSHttpTxn txnp);

/**
   Get the value of proxy.config.plugin.vc.default_buffer_water_mark from the TSHttpTxn
 */
tsapi TSIOBufferWaterMark TSPluginVCIOBufferWaterMarkGet(TSHttpTxn txnp);

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
tsapi TSVConn TSHttpConnectTransparent(struct sockaddr const *client_addr, struct sockaddr const *server_addr);

tsapi TSFetchSM TSFetchUrl(const char *request, int request_len, struct sockaddr const *addr, TSCont contp,
                           TSFetchWakeUpOptions callback_options, TSFetchEvent event);
tsapi void TSFetchPages(TSFetchUrlParams_t *params);

/* Check if HTTP State machine is internal or not */
tsapi int TSHttpTxnIsInternal(TSHttpTxn txnp);
tsapi int TSHttpSsnIsInternal(TSHttpSsn ssnp);

/* --------------------------------------------------------------------------
   HTTP alternate selection */
tsapi TSReturnCode TSHttpAltInfoClientReqGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *offset);
tsapi TSReturnCode TSHttpAltInfoCachedReqGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *offset);
tsapi TSReturnCode TSHttpAltInfoCachedRespGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *offset);
tsapi void TSHttpAltInfoQualitySet(TSHttpAltInfo infop, float quality);

/* --------------------------------------------------------------------------
   Actions */
tsapi void TSActionCancel(TSAction actionp);
tsapi int TSActionDone(TSAction actionp);

/* --------------------------------------------------------------------------
   VConnections */
tsapi TSVIO TSVConnReadVIOGet(TSVConn connp);
tsapi TSVIO TSVConnWriteVIOGet(TSVConn connp);
tsapi int TSVConnClosedGet(TSVConn connp);

tsapi TSVIO TSVConnRead(TSVConn connp, TSCont contp, TSIOBuffer bufp, int64_t nbytes);
tsapi TSVIO TSVConnWrite(TSVConn connp, TSCont contp, TSIOBufferReader readerp, int64_t nbytes);
tsapi void TSVConnClose(TSVConn connp);
tsapi void TSVConnAbort(TSVConn connp, int error);
tsapi void TSVConnShutdown(TSVConn connp, int read, int write);

/* --------------------------------------------------------------------------
   Cache VConnections */
tsapi int64_t TSVConnCacheObjectSizeGet(TSVConn connp);

/* --------------------------------------------------------------------------
   Transformations */
tsapi TSVConn TSTransformCreate(TSEventFunc event_funcp, TSHttpTxn txnp);
tsapi TSVConn TSTransformOutputVConnGet(TSVConn connp);

/* --------------------------------------------------------------------------
   Net VConnections */
tsapi struct sockaddr const *TSNetVConnRemoteAddrGet(TSVConn vc);

/**
    Opens a network connection to the host specified by ip on the port
    specified by port. If the connection is successfully opened, contp
    is called back with the event TS_EVENT_NET_CONNECT and the new
    network vconnection will be passed in the event data parameter.
    If the connection is not successful, contp is called back with
    the event TS_EVENT_NET_CONNECT_FAILED.

    Note: on Solaris, it is possible to receive TS_EVENT_NET_CONNECT
    even if the connection failed, because of the implementation of
    network sockets in the underlying operating system. There is an
    exception: if a plugin tries to open a connection to a port on
    its own host machine, then TS_EVENT_NET_CONNECT is sent only
    if the connection is successfully opened. In general, however,
    your plugin needs to look for an TS_EVENT_VCONN_WRITE_READY to
    be sure that the connection is successfully opened.

    @return something allows you to check if the connection is complete,
      or cancel the attempt to connect.

 */
tsapi TSAction TSNetConnect(
  TSCont contp,             /**< continuation that is called back when the attempted net connection either succeeds or fails. */
  struct sockaddr const *to /**< Address to which to connect. */
);

/**
 * Retrieves the continuation associated with creating the TSVConn
 */
tsapi TSCont TSNetInvokingContGet(TSVConn conn);

/**
 * Retrieves the transaction associated with creating the TSVConn
 */
tsapi TSHttpTxn TSNetInvokingTxnGet(TSVConn conn);

tsapi TSAction TSNetAccept(TSCont contp, int port, int domain, int accept_threads);

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
tsapi TSReturnCode TSNetAcceptNamedProtocol(TSCont contp, const char *protocol);

/**
  Create a new port from the string specification used by the
  proxy.config.http.server_ports configuration value.
 */
tsapi TSPortDescriptor TSPortDescriptorParse(const char *descriptor);

/**
   Start listening on the given port descriptor. If a connection is
   successfully accepted, the TS_EVENT_NET_ACCEPT is delivered to the
   continuation. The event data will be a valid TSVConn bound to the accepted
   connection.
 */
tsapi TSReturnCode TSPortDescriptorAccept(TSPortDescriptor, TSCont);

/* --------------------------------------------------------------------------
   DNS Lookups */
tsapi TSAction TSHostLookup(TSCont contp, const char *hostname, size_t namelen);
tsapi struct sockaddr const *TSHostLookupResultAddrGet(TSHostLookupResult lookup_result);
/* TODO: Eventually, we might want something like this as well, but it requires
   support for building the HostDBInfo struct:
   tsapi void TSHostLookupResultSet(TSHttpTxn txnp, TSHostLookupResult result);
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
tsapi TSAction TSCacheRead(TSCont contp, TSCacheKey key);

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
tsapi TSAction TSCacheWrite(TSCont contp, TSCacheKey key);

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
tsapi TSAction TSCacheRemove(TSCont contp, TSCacheKey key);
tsapi TSReturnCode TSCacheReady(int *is_ready);
tsapi TSAction TSCacheScan(TSCont contp, TSCacheKey key, int KB_per_second);

/* --------------------------------------------------------------------------
   VIOs */
tsapi void TSVIOReenable(TSVIO viop);
tsapi TSIOBuffer TSVIOBufferGet(TSVIO viop);
tsapi TSIOBufferReader TSVIOReaderGet(TSVIO viop);
tsapi int64_t TSVIONBytesGet(TSVIO viop);
tsapi void TSVIONBytesSet(TSVIO viop, int64_t nbytes);
tsapi int64_t TSVIONDoneGet(TSVIO viop);
tsapi void TSVIONDoneSet(TSVIO viop, int64_t ndone);
tsapi int64_t TSVIONTodoGet(TSVIO viop);
tsapi TSMutex TSVIOMutexGet(TSVIO viop);
tsapi TSCont TSVIOContGet(TSVIO viop);
tsapi TSVConn TSVIOVConnGet(TSVIO viop);

/* --------------------------------------------------------------------------
   Buffers */
tsapi TSIOBuffer TSIOBufferCreate(void);

/**
    Creates a new TSIOBuffer of the specified size. With this function,
    you can create smaller buffers than the 32K buffer created by
    TSIOBufferCreate(). In some situations using smaller buffers can
    improve performance.

    @param index size of the new TSIOBuffer to be created.
    @param new TSIOBuffer of the specified size.

 */
tsapi TSIOBuffer TSIOBufferSizedCreate(TSIOBufferSizeIndex index);

/**
    The watermark of an TSIOBuffer is the minimum number of bytes
    of data that have to be in the buffer before calling back any
    continuation that has initiated a read operation on this buffer.
    TSIOBufferWaterMarkGet() will provide the size of the watermark,
    in bytes, for a specified TSIOBuffer.

    @param bufp buffer whose watermark the function gets.

 */
tsapi int64_t TSIOBufferWaterMarkGet(TSIOBuffer bufp);

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
tsapi void TSIOBufferWaterMarkSet(TSIOBuffer bufp, int64_t water_mark);

tsapi void TSIOBufferDestroy(TSIOBuffer bufp);
tsapi TSIOBufferBlock TSIOBufferStart(TSIOBuffer bufp);
tsapi int64_t TSIOBufferCopy(TSIOBuffer bufp, TSIOBufferReader readerp, int64_t length, int64_t offset);

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
tsapi int64_t TSIOBufferWrite(TSIOBuffer bufp, const void *buf, int64_t length);
tsapi void TSIOBufferProduce(TSIOBuffer bufp, int64_t nbytes);

tsapi TSIOBufferBlock TSIOBufferBlockNext(TSIOBufferBlock blockp);
tsapi const char *TSIOBufferBlockReadStart(TSIOBufferBlock blockp, TSIOBufferReader readerp, int64_t *avail);
tsapi int64_t TSIOBufferBlockReadAvail(TSIOBufferBlock blockp, TSIOBufferReader readerp);
tsapi char *TSIOBufferBlockWriteStart(TSIOBufferBlock blockp, int64_t *avail);
tsapi int64_t TSIOBufferBlockWriteAvail(TSIOBufferBlock blockp);

tsapi TSIOBufferReader TSIOBufferReaderAlloc(TSIOBuffer bufp);
tsapi TSIOBufferReader TSIOBufferReaderClone(TSIOBufferReader readerp);
tsapi void TSIOBufferReaderFree(TSIOBufferReader readerp);
tsapi TSIOBufferBlock TSIOBufferReaderStart(TSIOBufferReader readerp);
tsapi void TSIOBufferReaderConsume(TSIOBufferReader readerp, int64_t nbytes);
tsapi int64_t TSIOBufferReaderAvail(TSIOBufferReader readerp);
tsapi int64_t TSIOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length);

tsapi struct sockaddr const *TSNetVConnLocalAddrGet(TSVConn vc);

/* --------------------------------------------------------------------------
   Stats and configs based on librecords raw stats (this is preferred API until we
   rewrite stats). This system has a limitation of up to 1,500 stats max, controlled
   via proxy.config.stat_api.max_stats_allowed (default is 512).

   This is available as of Apache TS v2.2.*/
typedef enum {
  TS_STAT_PERSISTENT = 1,
  TS_STAT_NON_PERSISTENT,
} TSStatPersistence;

typedef enum {
  TS_STAT_SYNC_SUM = 0,
  TS_STAT_SYNC_COUNT,
  TS_STAT_SYNC_AVG,
  TS_STAT_SYNC_TIMEAVG,
} TSStatSync;

/* APIs to create new records.config configurations */
tsapi TSReturnCode TSMgmtStringCreate(TSRecordType rec_type, const char *name, const TSMgmtString data_default,
                                      TSRecordUpdateType update_type, TSRecordCheckType check_type, const char *check_regex,
                                      TSRecordAccessType access_type);
tsapi TSReturnCode TSMgmtIntCreate(TSRecordType rec_type, const char *name, TSMgmtInt data_default, TSRecordUpdateType update_type,
                                   TSRecordCheckType check_type, const char *check_regex, TSRecordAccessType access_type);

/*  Note that only TS_RECORDDATATYPE_INT is supported at this point. */
tsapi int TSStatCreate(const char *the_name, TSRecordDataType the_type, TSStatPersistence persist, TSStatSync sync);

tsapi void TSStatIntIncrement(int the_stat, TSMgmtInt amount);
tsapi void TSStatIntDecrement(int the_stat, TSMgmtInt amount);
/* Currently not supported. */
/* tsapi void TSStatFloatIncrement(int the_stat, float amount); */
/* tsapi void TSStatFloatDecrement(int the_stat, float amount); */

tsapi TSMgmtInt TSStatIntGet(int the_stat);
tsapi void TSStatIntSet(int the_stat, TSMgmtInt value);
/* Currently not supported. */
/* tsapi TSReturnCode TSStatFloatGet(int the_stat, float* value); */
/* tsapi TSReturnCode TSStatFloatSet(int the_stat, float value); */

tsapi TSReturnCode TSStatFindName(const char *name, int *idp);

/* --------------------------------------------------------------------------
   tracing api */

tsapi int TSIsDebugTagSet(const char *t);
tsapi void TSDebug(const char *tag, const char *format_str, ...) TS_PRINTFLIKE(2, 3);
/**
    Output a debug line even if the debug tag is turned off, as long as
    debugging is enabled. Could be used as follows:
    @code
    TSDebugSpecific(TSHttpTxnDebugGet(txn), "plugin_tag" , "Hello World from transaction %p", txn);
    @endcode
    will be printed if the plugin_tag is enabled or the transaction specific
    debugging is turned on for txn.

    @param debug_flag boolean flag.
    @param tag Debug tag for the line.
    @param format Format string.
    @param ... Format arguments.
 */
tsapi void TSDebugSpecific(int debug_flag, const char *tag, const char *format_str, ...) TS_PRINTFLIKE(3, 4);
extern int diags_on_for_plugins;
#define TSDEBUG             \
  if (diags_on_for_plugins) \
  TSDebug

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
typedef struct tsapi_textlogobject *TSTextLogObject;

typedef void (*TSRecordDumpCb)(TSRecordType rec_type, void *edata, int registered, const char *name, TSRecordDataType data_type,
                               TSRecordData *datum);

tsapi void TSRecordDump(int rec_type, TSRecordDumpCb callback, void *edata);

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
          log = TSTextLogObjectCreate("squid" , mode, NULL, &error);
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
tsapi TSReturnCode TSTextLogObjectCreate(const char *filename, int mode, TSTextLogObject *new_log_obj);

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
tsapi TSReturnCode TSTextLogObjectWrite(TSTextLogObject the_object, const char *format, ...) TS_PRINTFLIKE(2, 3);

/**
    This immediately flushes the contents of the log write buffer for
    the_object to disk. Use this call only if you want to make sure that
    log entries are flushed immediately. This call has a performance
    cost. Traffic Server flushes the log buffer automatically about
    every 1 second.

    @param the_object custom log file whose write buffer is to be
      flushed.

 */
tsapi void TSTextLogObjectFlush(TSTextLogObject the_object);

/**
    Destroys a log object and releases the memory allocated to it.
    Use this call if you are done with the log.

    @param  the_object custom log to be destroyed.

 */
tsapi TSReturnCode TSTextLogObjectDestroy(TSTextLogObject the_object);

/**
    Set log header.

 */
tsapi void TSTextLogObjectHeaderSet(TSTextLogObject the_object, const char *header);

/**
    Enable/disable rolling.

    @param rolling_enabled a valid proxy.config.log.rolling_enabled value.

 */
tsapi TSReturnCode TSTextLogObjectRollingEnabledSet(TSTextLogObject the_object, int rolling_enabled);

/**
    Set the rolling interval.

 */
tsapi void TSTextLogObjectRollingIntervalSecSet(TSTextLogObject the_object, int rolling_interval_sec);

/**
    Set the rolling offset. rolling_offset_hr specifies the hour (between 0 and 23) when log rolling
    should take place.

 */
tsapi void TSTextLogObjectRollingOffsetHrSet(TSTextLogObject the_object, int rolling_offset_hr);

/**
    Set the rolling size. rolling_size_mb specifies the size in MB when log rolling
    should take place.

 */
tsapi void TSTextLogObjectRollingSizeMbSet(TSTextLogObject the_object, int rolling_size_mb);

/**
    Async disk IO read

    @return TS_SUCCESS or TS_ERROR.
 */
tsapi TSReturnCode TSAIORead(int fd, off_t offset, char *buf, size_t buffSize, TSCont contp);

/**
    Async disk IO buffer get

    @return char* to the buffer
 */
tsapi char *TSAIOBufGet(TSAIOCallback data);

/**
    Async disk IO get number of bytes

    @return the number of bytes
 */
tsapi int TSAIONBytesGet(TSAIOCallback data);

/**
    Async disk IO write

    @return TS_SUCCESS or TS_ERROR.
 */
tsapi TSReturnCode TSAIOWrite(int fd, off_t offset, char *buf, size_t bufSize, TSCont contp);

/**
    Async disk IO set number of threads

    @return TS_SUCCESS or TS_ERROR.
 */
tsapi TSReturnCode TSAIOThreadNumSet(int thread_num);

/**
    Check if transaction was aborted (due client/server errors etc.)

    @return 1 if transaction was aborted
*/
tsapi TSReturnCode TSHttpTxnAborted(TSHttpTxn txnp);

tsapi TSVConn TSVConnCreate(TSEventFunc event_funcp, TSMutex mutexp);
tsapi TSVConn TSVConnFdCreate(int fd);

/* api functions to access stats */
/* ClientResp APIs exist as well and are exposed in PrivateFrozen  */
tsapi int TSHttpTxnClientReqHdrBytesGet(TSHttpTxn txnp);
tsapi int64_t TSHttpTxnClientReqBodyBytesGet(TSHttpTxn txnp);
tsapi int TSHttpTxnServerReqHdrBytesGet(TSHttpTxn txnp);
tsapi int64_t TSHttpTxnServerReqBodyBytesGet(TSHttpTxn txnp);
tsapi int TSHttpTxnPushedRespHdrBytesGet(TSHttpTxn txnp);
tsapi int64_t TSHttpTxnPushedRespBodyBytesGet(TSHttpTxn txnp);
tsapi int TSHttpTxnServerRespHdrBytesGet(TSHttpTxn txnp);
tsapi int64_t TSHttpTxnServerRespBodyBytesGet(TSHttpTxn txnp);
tsapi int TSHttpTxnClientRespHdrBytesGet(TSHttpTxn txnp);
tsapi int64_t TSHttpTxnClientRespBodyBytesGet(TSHttpTxn txnp);
tsapi int TSVConnIsSslReused(TSVConn sslp);

/**
   Return the current (if set) SSL Cipher. This is still owned by the
   core, and must not be free'd.

   @param sslp The connection pointer

   @return the SSL Cipher
*/
tsapi const char *TSVConnSslCipherGet(TSVConn sslp);

/**
   Return the current (if set) SSL Protocol. This is still owned by the
   core, and must not be free'd.

   @param sslp The connection pointer

   @return the SSL Protocol
*/
tsapi const char *TSVConnSslProtocolGet(TSVConn sslp);

/**
   Return the current (if set) SSL Curve. This is still owned by the
   core, and must not be free'd.

   @param txnp the transaction pointer

   @return the SSL Curve
*/
tsapi const char *TSVConnSslCurveGet(TSVConn sslp);

/* NetVC timeout APIs. */
tsapi void TSVConnInactivityTimeoutSet(TSVConn connp, TSHRTime timeout);
tsapi void TSVConnInactivityTimeoutCancel(TSVConn connp);
tsapi void TSVConnActiveTimeoutSet(TSVConn connp, TSHRTime timeout);
tsapi void TSVConnActiveTimeoutCancel(TSVConn connp);

/*
  ability to skip the remap phase of the State Machine
  this only really makes sense in TS_HTTP_READ_REQUEST_HDR_HOOK
*/
tsapi void TSSkipRemappingSet(TSHttpTxn txnp, int flag);

/*
  Set or get various overridable configurations, for a transaction. This should
  probably be done as early as possible, e.g. TS_HTTP_READ_REQUEST_HDR_HOOK.
*/
tsapi TSReturnCode TSHttpTxnConfigIntSet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtInt value);
tsapi TSReturnCode TSHttpTxnConfigIntGet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtInt *value);
tsapi TSReturnCode TSHttpTxnConfigFloatSet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtFloat value);
tsapi TSReturnCode TSHttpTxnConfigFloatGet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtFloat *value);
tsapi TSReturnCode TSHttpTxnConfigStringSet(TSHttpTxn txnp, TSOverridableConfigKey conf, const char *value, int length);
tsapi TSReturnCode TSHttpTxnConfigStringGet(TSHttpTxn txnp, TSOverridableConfigKey conf, const char **value, int *length);

tsapi TSReturnCode TSHttpTxnConfigFind(const char *name, int length, TSOverridableConfigKey *conf, TSRecordDataType *type);

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
tsapi void TSHttpTxnRedirectUrlSet(TSHttpTxn txnp, const char *url, const int url_len);

/**
   Return the current (if set) redirection URL string. This is still owned by the
   core, and must not be free'd.

   @param txnp the transaction pointer
   @param url_len_ptr a pointer to where the URL length is to be stored

   @return the url string
*/
tsapi const char *TSHttpTxnRedirectUrlGet(TSHttpTxn txnp, int *url_len_ptr);

/**
   Return the number of redirection retries we have done. This starts off
   at zero, and can be used to select different URLs based on which attempt this
   is. This can be useful for example when providing a list of URLs to try, and
   do so in order until one succeeds.

   @param txnp the transaction pointer

   @return the redirect try count
*/
tsapi int TSHttpTxnRedirectRetries(TSHttpTxn txnp);

/* Get current HTTP connection stats */
tsapi int TSHttpCurrentClientConnectionsGet(void);
tsapi int TSHttpCurrentActiveClientConnectionsGet(void);
tsapi int TSHttpCurrentIdleClientConnectionsGet(void);
tsapi int TSHttpCurrentCacheConnectionsGet(void);
tsapi int TSHttpCurrentServerConnectionsGet(void);

/* =====  Http Transactions =====  */
tsapi TSReturnCode TSHttpTxnCachedRespModifiableGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
tsapi TSReturnCode TSHttpTxnCacheLookupStatusSet(TSHttpTxn txnp, int cachelookup);
tsapi TSReturnCode TSHttpTxnCacheLookupUrlGet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj);
tsapi TSReturnCode TSHttpTxnCacheLookupUrlSet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj);
tsapi TSReturnCode TSHttpTxnPrivateSessionSet(TSHttpTxn txnp, int private_session);
tsapi const char *TSHttpTxnCacheDiskPathGet(TSHttpTxn txnp, int *length);
tsapi int TSHttpTxnBackgroundFillStarted(TSHttpTxn txnp);
tsapi int TSHttpTxnIsWebsocket(TSHttpTxn txnp);

/* Get the Txn's (HttpSM's) unique identifier, which is a sequence number since server start) */
tsapi uint64_t TSHttpTxnIdGet(TSHttpTxn txnp);

/* Get the Ssn's unique identifier */
tsapi int64_t TSHttpSsnIdGet(TSHttpSsn ssnp);

/* Expose internal Base64 Encoding / Decoding */
tsapi TSReturnCode TSBase64Decode(const char *str, size_t str_len, unsigned char *dst, size_t dst_size, size_t *length);
tsapi TSReturnCode TSBase64Encode(const char *str, size_t str_len, char *dst, size_t dst_size, size_t *length);

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
tsapi TSReturnCode TSHttpTxnMilestoneGet(TSHttpTxn txnp, TSMilestonesType milestone, TSHRTime *time);

/**
  Test whether a request / response header pair would be cacheable under the current
  configuration. This would typically be used in TS_HTTP_READ_RESPONSE_HDR_HOOK, when
  you have both the client request and server response ready.

  @param txnp the transaction pointer
  @param request the client request header. If NULL, use the transactions client request.
  @param response the server response header. If NULL, use the transactions origin response.

  @return 1 if the request / response is cacheable, 0 otherwise
*/
tsapi int TSHttpTxnIsCacheable(TSHttpTxn txnp, TSMBuffer request, TSMBuffer response);

/**
  Get the maximum age in seconds as indicated by the origin server.
  This would typically be used in TS_HTTP_READ_RESPONSE_HDR_HOOK, when you have
  the server response ready.

  @param txnp the transaction pointer
  @param response the server response header. If NULL, use the transactions origin response.

  @return the age in seconds if specified by Cache-Control, -1 otherwise
*/
tsapi int TSHttpTxnGetMaxAge(TSHttpTxn txnp, TSMBuffer response);

/**
   Return a string representation for a TSServerState value. This is useful for plugin debugging.

   @param state the value of this TSServerState

   @return the string representation of the state
*/
tsapi const char *TSHttpServerStateNameLookup(TSServerState state);

/**
   Return a string representation for a TSHttpHookID value. This is useful for plugin debugging.

   @param hook the value of this TSHttpHookID

   @return the string representation of the hook ID
*/
tsapi const char *TSHttpHookNameLookup(TSHttpHookID hook);

/**
   Return a string representation for a TSEvent value. This is useful for plugin debugging.

   @param event the value of this TSHttpHookID

   @return the string representation of the event
*/
tsapi const char *TSHttpEventNameLookup(TSEvent event);

/* APIs for dealing with UUIDs, either self made, or the system wide process UUID. See
   https://docs.trafficserver.apache.org/en/latest/developer-guide/api/functions/TSUuidCreate.en.html
*/
tsapi TSUuid TSUuidCreate(void);
tsapi TSReturnCode TSUuidInitialize(TSUuid uuid, TSUuidVersion v);
tsapi void TSUuidDestroy(TSUuid uuid);
tsapi TSReturnCode TSUuidCopy(TSUuid dest, const TSUuid src);
tsapi const char *TSUuidStringGet(const TSUuid uuid);
tsapi TSUuidVersion TSUuidVersionGet(const TSUuid uuid);
tsapi TSReturnCode TSUuidStringParse(TSUuid uuid, const char *uuid_str);
tsapi TSReturnCode TSClientRequestUuidGet(TSHttpTxn txnp, char *uuid_str);

/* Get the process global UUID, resets on every startup */
tsapi TSUuid TSProcessUuidGet(void);

/**
   Returns the plugin_tag.
*/
tsapi const char *TSHttpTxnPluginTagGet(TSHttpTxn txnp);

/*
 * Return information about the client protocols.
 */
tsapi TSReturnCode TSHttpTxnClientProtocolStackGet(TSHttpTxn txnp, int count, const char **result, int *actual);
tsapi TSReturnCode TSHttpSsnClientProtocolStackGet(TSHttpSsn ssnp, int count, const char **result, int *actual);
tsapi const char *TSHttpTxnClientProtocolStackContains(TSHttpTxn txnp, char const *tag);
tsapi const char *TSHttpSsnClientProtocolStackContains(TSHttpSsn ssnp, char const *tag);
tsapi const char *TSNormalizedProtocolTag(char const *tag);
tsapi const char *TSRegisterProtocolTag(char const *tag);

/*
 * Return information about the server protocols.
 */
tsapi TSReturnCode TSHttpTxnServerProtocolStackGet(TSHttpTxn txnp, int count, const char **result, int *actual);
tsapi const char *TSHttpTxnServerProtocolStackContains(TSHttpTxn txnp, char const *tag);

// If, for the given transaction, the URL has been remapped, this function puts the memory location of the "from" URL object in
// the variable pointed to by urlLocp, and returns TS_SUCCESS.  (The URL object will be within memory allocated to the
// transaction object.)  Otherwise, the function returns TS_ERROR.
//
tsapi TSReturnCode TSRemapFromUrlGet(TSHttpTxn txnp, TSMLoc *urlLocp);

// If, for the given transaction, the URL has been remapped, this function puts the memory location of the "to" URL object in the
// variable pointed to by urlLocp, and returns TS_SUCCESS.  (The URL object will be within memory allocated to the transaction
// object.)  Otherwise, the function returns TS_ERROR.
//
tsapi TSReturnCode TSRemapToUrlGet(TSHttpTxn txnp, TSMLoc *urlLocp);

// Override response behavior, and hard-set the state machine for whether to succeed or fail, and how.
tsapi void TSHttpTxnResponseActionSet(TSHttpTxn txnp, TSResponseAction *action);

// Get the overridden response behavior set by previously called plugins.
tsapi void TSHttpTxnResponseActionGet(TSHttpTxn txnp, TSResponseAction *action);

/*
 * Get a TSIOBufferReader to read the buffered body. The return value needs to be freed.
 */
tsapi TSIOBufferReader TSHttpTxnPostBufferReaderGet(TSHttpTxn txnp);

/**
 * Initiate an HTTP/2 Server Push preload request.
 * Use this api to register a URL that you want to preload with HTTP/2 Server Push.
 *
 * @param url the URL string to preload.
 * @param url_len the length of the URL string.
 */
tsapi TSReturnCode TSHttpTxnServerPush(TSHttpTxn txnp, const char *url, int url_len);

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
tsapi TSReturnCode TSHttpTxnClientStreamIdGet(TSHttpTxn txnp, uint64_t *stream_id);

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
tsapi TSReturnCode TSHttpTxnClientStreamPriorityGet(TSHttpTxn txnp, TSHttpPriority *priority);

/*
 * Returns TS_SUCCESS if hostname is this machine, as used for parent and remap self-detection.
 * Returns TS_ERROR if hostname is not this machine.
 */
tsapi TSReturnCode TSHostnameIsSelf(const char *hostname, size_t hostname_len);

/*
 * Gets the status of hostname in the outparam status, and the status reason in the outparam reason.
 * The reason is a logical-or combination of the reasons in TSHostStatusReason.
 * If either outparam is null, it will not be set and no error will be returned.
 * Returns TS_SUCCESS if the hostname was a parent and existed in the HostStatus, else TS_ERROR.
 */
tsapi TSReturnCode TSHostStatusGet(const char *hostname, const size_t hostname_len, TSHostStatus *status, unsigned int *reason);

/*
 * Sets the status of hostname in status, down_time, and reason.
 * The reason is a logical-or combination of the reasons in TSHostStatusReason.
 */
tsapi void TSHostStatusSet(const char *hostname, const size_t hostname_len, TSHostStatus status, const unsigned int down_time,
                           const unsigned int reason);

/*
 * Set or get various HTTP Transaction control settings.
 */
tsapi bool TSHttpTxnCntlGet(TSHttpTxn txnp, TSHttpCntlType ctrl);
tsapi TSReturnCode TSHttpTxnCntlSet(TSHttpTxn txnp, TSHttpCntlType ctrl, bool data);

#ifdef __cplusplus
}
#endif /* __cplusplus */
