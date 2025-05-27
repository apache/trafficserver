/** @file

  Implements the Traffic Server C API functions.

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

#include <atomic>
#include <tuple>
#include <unordered_map>
#include <string_view>
#include <string>

#include "iocore/net/NetVConnection.h"
#include "iocore/net/NetHandler.h"
#include "iocore/net/UDPNet.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_base64.h"
#include "tscore/Encoding.h"
#include "tscore/PluginUserArgs.h"
#include "tscore/Layout.h"
#include "tscore/Diags.h"
#include "tsutil/Metrics.h"

#include "api/InkAPIInternal.h"
#include "api/HttpAPIHooks.h"
#include "proxy/logging/Log.h"
#include "proxy/hdrs/URL.h"
#include "proxy/hdrs/MIME.h"
#include "proxy/hdrs/HTTP.h"
#include "proxy/ProxySession.h"
#include "proxy/http2/Http2ClientSession.h"
#include "proxy/PoolableSession.h"
#include "proxy/http/HttpSM.h"
#include "proxy/http/HttpConfig.h"
#include "proxy/PluginHttpConnect.h"
#include "../iocore/net/P_Net.h"
#include "../iocore/net/P_UnixNet.h"
#include "../iocore/net/P_SSLNetVConnection.h"
#include "../iocore/cache/P_CacheHttp.h"
#include "../iocore/cache/CacheVC.h"
#include "records/RecCore.h"
#include "../records/P_RecCore.h"
#include "../records/P_RecDefs.h"
#include "../iocore/net/P_SSLConfig.h"
#include "../iocore/net/P_SSLClientUtils.h"
#include "iocore/dns/DNSProcessor.h"
#include "iocore/net/ConnectionTracker.h"
#include "iocore/net/SSLAPIHooks.h"
#include "iocore/net/SSLDiags.h"
#include "iocore/net/TLSBasicSupport.h"
#include "iocore/eventsystem/ConfigProcessor.h"
#include "proxy/Plugin.h"
#include "proxy/logging/LogObject.h"
#include "proxy/logging/LogConfig.h"
#include "proxy/PluginVC.h"
#include "proxy/http/HttpSessionAccept.h"
#include "proxy/PluginVC.h"
#include "proxy/FetchSM.h"
#include "api/LifecycleAPIHooks.h"
#include "proxy/http/HttpDebugNames.h"
#include "iocore/aio/AIO.h"
#include "iocore/eventsystem/Tasks.h"

#include "../iocore/net/P_OCSPStapling.h"
#include "records/RecDefs.h"
#include "records/RecCore.h"
#include "records/RecYAMLDecoder.h"
#include "iocore/utils/Machine.h"
#include "proxy/http/HttpProxyServerMain.h"
#include "shared/overridable_txn_vars.h"
#include "mgmt/config/FileManager.h"

#include "mgmt/rpc/jsonrpc/JsonRPC.h"
#include <swoc/bwf_base.h>
#include "ts/ts.h"

/****************************************************************
 *  IMPORTANT - READ ME
 * Any plugin using the IO Core must enter
 *   with a held mutex.  SDK 1.0, 1.1 & 2.0 did not
 *   have this restriction so we need to add a mutex
 *   to Plugin's Continuation if it tries to use the IOCore
 * Not only does the plugin have to have a mutex
 *   before entering the IO Core.  The mutex needs to be held.
 *   We now take out the mutex on each call to ensure it is
 *   held for the entire duration of the IOCore call
 ***************************************************************/

// helper macro for setting HTTPHdr data
#define SET_HTTP_HDR(_HDR, _BUF_PTR, _OBJ_PTR)          \
  _HDR.m_heap = ((HdrHeapSDKHandle *)_BUF_PTR)->m_heap; \
  _HDR.m_http = (HTTPHdrImpl *)_OBJ_PTR;                \
  _HDR.m_mime = _HDR.m_http->m_fields_impl;

// This assert is for internal API use only.
#if TS_USE_FAST_SDK
#define sdk_assert(EX) (void)(EX)
#else
#define sdk_assert(EX) ((void)((EX) ? (void)0 : _TSReleaseAssert(#EX, __FILE__, __LINE__)))
#endif

namespace rpc
{
extern std::mutex              g_rpcHandlingMutex;
extern std::condition_variable g_rpcHandlingCompletion;
extern swoc::Rv<YAML::Node>    g_rpcHandlerResponseData;
extern bool                    g_rpcHandlerProcessingCompleted;
} // namespace rpc

static ts::Metrics &global_api_metrics = ts::Metrics::instance();

ConfigUpdateCbTable *global_config_cbs = nullptr;

// Fetchpages SM
extern ClassAllocator<FetchSM> FetchSMAllocator;

/* From proxy/http/HttpProxyServerMain.c: */
extern bool ssl_register_protocol(const char *, Continuation *);

extern SSLSessionCache *session_cache; // declared extern in P_SSLConfig.h

// External converters.
extern MgmtConverter const &HttpDownServerCacheTimeConv;

extern HttpSessionAccept                 *plugin_http_accept;
extern HttpSessionAccept                 *plugin_http_transparent_accept;
extern thread_local PluginThreadContext  *pluginThreadContext;
static ClassAllocator<APIHook>            apiHookAllocator("apiHookAllocator");
extern ClassAllocator<INKContInternal>    INKContAllocator;
extern ClassAllocator<INKVConnInternal>   INKVConnAllocator;
static ClassAllocator<MIMEFieldSDKHandle> mHandleAllocator("MIMEFieldSDKHandle");

// forward declarations
TSReturnCode sdk_sanity_check_null_ptr(void const *ptr);
TSReturnCode sdk_sanity_check_mbuffer(TSMBuffer bufp);

namespace
{

DbgCtl dbg_ctl_plugin{"plugin"};
DbgCtl dbg_ctl_parent_select{"parent_select"};
DbgCtl dbg_ctl_iocore_net{"iocore_net"};
DbgCtl dbg_ctl_cache_url{"cache_url"};
DbgCtl dbg_ctl_ssl{"ssl"};
DbgCtl dbg_ctl_ssl_cert_update{"ssl.cert_update"};
DbgCtl dbg_ctl_ssl_session_cache_insert{"ssl.session_cache.insert"};
DbgCtl dbg_ctl_rpc_api{"rpc.api"};

} // end anonymous namespace

/******************************************************/
/* Allocators for field handles and standalone fields */
/******************************************************/
static MIMEFieldSDKHandle *
sdk_alloc_field_handle(TSMBuffer /* bufp ATS_UNUSED */, MIMEHdrImpl *mh)
{
  MIMEFieldSDKHandle *handle = THREAD_ALLOC(mHandleAllocator, this_thread());

  // TODO: Should remove this when memory allocation can't fail.
  sdk_assert(sdk_sanity_check_null_ptr((void *)handle) == TS_SUCCESS);

  obj_init_header(handle, HdrHeapObjType::FIELD_SDK_HANDLE, sizeof(MIMEFieldSDKHandle), 0);
  handle->mh = mh;

  return handle;
}

static void
sdk_free_field_handle(TSMBuffer bufp, MIMEFieldSDKHandle *field_handle)
{
  if (sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS) {
    THREAD_FREE(field_handle, mHandleAllocator, this_thread());
  }
}

// Defines in InkAPIInternal.cc
extern char traffic_server_version[128];
extern int  ts_major_version;
extern int  ts_minor_version;
extern int  ts_patch_version;

/** Reservation for a user arg.
 */
struct UserArg {
  TSUserArgType type;
  std::string   name;        ///< Name of reserving plugin.
  std::string   description; ///< Description of use for this arg.
};

// Managing the user args tables, and the global storage (which is assumed to be the biggest, by far).
UserArg                                 UserArgTable[TS_USER_ARGS_COUNT][MAX_USER_ARGS[TS_USER_ARGS_GLB]];
static PluginUserArgs<TS_USER_ARGS_GLB> global_user_args;
std::atomic<int>                        UserArgIdx[TS_USER_ARGS_COUNT]; // Table of next reserved index.

////////////////////////////////////////////////////////////////////
//
// API error logging
//
////////////////////////////////////////////////////////////////////
void
TSStatus(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  StatusV(fmt, args);
  va_end(args);
}

void
TSNote(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  NoteV(fmt, args);
  va_end(args);
}

void
TSWarning(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  WarningV(fmt, args);
  va_end(args);
}

void
TSError(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  ErrorV(fmt, args);
  va_end(args);
}

void
TSFatal(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  FatalV(fmt, args);
  va_end(args);
}

void
TSAlert(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  AlertV(fmt, args);
  va_end(args);
}

void
TSEmergency(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  EmergencyV(fmt, args);
  va_end(args);
}

// Assert in debug AND optim
void
_TSReleaseAssert(const char *text, const char *file, int line)
{
  _ink_assert(text, file, line);
}

// Assert only in debug
int
#ifdef DEBUG
_TSAssert(const char *text, const char *file, int line)
{
  _ink_assert(text, file, line);
  return 0;
}
#else
_TSAssert(const char *, const char *, int)
{
  return 0;
}
#endif

////////////////////////////////////////////////////////////////////
//
// SDK Interoperability Support
//
// ----------------------------------------------------------------
//
// Standalone Fields (SDK Version-Interoperability Hack)
//
//
// A "standalone" field is an ugly hack for portability with old
// versions of the SDK that mirrored the old header system.  In
// the old system, you could create arbitrary tiny little field
// objects, distinct from MIME header objects, and link them
// together.  In the new header system, all fields are internal
// constituents of the MIME header.  To preserve the semantics of
// the old SDK, we need to maintain the concept of fields that
// are created outside of a MIME header.  Whenever a field is
// "attached" to a MIME header, it is copied into the MIME header
// field's slot, and the handle to the field is updated to refer
// to the new field.
//
// Hopefully, we can eliminate this old compatibility interface and
// migrate users to the newer semantics quickly.
//
// ----------------------------------------------------------------
//
// MIMEField SDK Handles (SDK Version-Interoperability Hack)
//
// MIMEField "handles" are used by the SDK as an indirect reference
// to the MIMEField.  Because versions 1 & 2 of the SDK allowed
// standalone fields that existed without associated MIME headers,
// and because the version 3 SDK requires an associated MIME header
// for all field mutation operations (for presence bits, etc.) we
// need a data structure that:
//
//   * identifies standalone fields and stores field name/value
//     information for fields that are not yet in a header
//   * redirects the field to a real header field when the field
//     is inserted into a header
//   * maintains the associated MIMEHdrImpl when returning field
//     slots from lookup and create functions
//
// If the MIMEHdrImpl pointer is null, then the handle points
// to a standalone field, otherwise the handle points to a field
// within the MIME header.
//
////////////////////////////////////////////////////////////////////

/*****************************************************************/
/* Handles to headers are impls, but need to handle MIME or HTTP */
/*****************************************************************/

inline MIMEHdrImpl *
_hdr_obj_to_mime_hdr_impl(HdrHeapObjImpl *obj)
{
  MIMEHdrImpl *impl;
  if (static_cast<HdrHeapObjType>(obj->m_type) == HdrHeapObjType::HTTP_HEADER) {
    impl = static_cast<HTTPHdrImpl *>(obj)->m_fields_impl;
  } else if (static_cast<HdrHeapObjType>(obj->m_type) == HdrHeapObjType::MIME_HEADER) {
    impl = static_cast<MIMEHdrImpl *>(obj);
  } else {
    ink_release_assert(!"mloc not a header type");
    impl = nullptr; /* gcc does not know about 'ink_release_assert' - make it happy */
  }
  return impl;
}

inline MIMEHdrImpl *
_hdr_mloc_to_mime_hdr_impl(TSMLoc mloc)
{
  return _hdr_obj_to_mime_hdr_impl(reinterpret_cast<HdrHeapObjImpl *>(mloc));
}

TSReturnCode
sdk_sanity_check_field_handle(TSMLoc field, TSMLoc parent_hdr = nullptr)
{
  if (field == TS_NULL_MLOC) {
    return TS_ERROR;
  }

  MIMEFieldSDKHandle *field_handle = reinterpret_cast<MIMEFieldSDKHandle *>(field);
  if (static_cast<HdrHeapObjType>(field_handle->m_type) != HdrHeapObjType::FIELD_SDK_HANDLE) {
    return TS_ERROR;
  }

  if (parent_hdr != nullptr) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(parent_hdr);
    if (field_handle->mh != mh) {
      return TS_ERROR;
    }
  }
  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_mbuffer(TSMBuffer bufp)
{
  HdrHeapSDKHandle *handle = reinterpret_cast<HdrHeapSDKHandle *>(bufp);
  if ((handle == nullptr) || (handle->m_heap == nullptr) || (handle->m_heap->m_magic != HdrBufMagic::ALIVE)) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_mime_hdr_handle(TSMLoc field)
{
  if (field == TS_NULL_MLOC) {
    return TS_ERROR;
  }

  MIMEFieldSDKHandle *field_handle = reinterpret_cast<MIMEFieldSDKHandle *>(field);
  if (static_cast<HdrHeapObjType>(field_handle->m_type) != HdrHeapObjType::MIME_HEADER) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_url_handle(TSMLoc field)
{
  if (field == TS_NULL_MLOC) {
    return TS_ERROR;
  }

  MIMEFieldSDKHandle *field_handle = reinterpret_cast<MIMEFieldSDKHandle *>(field);
  if (static_cast<HdrHeapObjType>(field_handle->m_type) != HdrHeapObjType::URL) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_http_hdr_handle(TSMLoc field)
{
  if (field == TS_NULL_MLOC) {
    return TS_ERROR;
  }

  HTTPHdrImpl *field_handle = reinterpret_cast<HTTPHdrImpl *>(field);
  if (static_cast<HdrHeapObjType>(field_handle->m_type) != HdrHeapObjType::HTTP_HEADER) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_continuation(TSCont cont)
{
  if ((cont == nullptr) || (reinterpret_cast<INKContInternal *>(cont)->m_free_magic == INKCONT_INTERN_MAGIC_DEAD)) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_fetch_sm(TSFetchSM fetch_sm)
{
  if (fetch_sm == nullptr) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_http_ssn(TSHttpSsn ssnp)
{
  if (ssnp == nullptr) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_txn(TSHttpTxn txnp)
{
  if ((txnp != nullptr) && ((reinterpret_cast<HttpSM *>(txnp))->magic == HttpSmMagic_t::ALIVE)) {
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

TSReturnCode
sdk_sanity_check_mime_parser(TSMimeParser parser)
{
  if (parser == nullptr) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_http_parser(TSHttpParser parser)
{
  if (parser == nullptr) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_alt_info(TSHttpAltInfo info)
{
  if (info == nullptr) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_hook_id(TSHttpHookID id)
{
  return HttpAPIHooks::is_valid(id) ? TS_SUCCESS : TS_ERROR;
}

TSReturnCode
sdk_sanity_check_lifecycle_hook_id(TSLifecycleHookID id)
{
  return LifecycleAPIHooks::is_valid(id) ? TS_SUCCESS : TS_ERROR;
}

TSReturnCode
sdk_sanity_check_ssl_hook_id(TSHttpHookID id)
{
  if (id < TS_SSL_FIRST_HOOK || id > TS_SSL_LAST_HOOK) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_null_ptr(void const *ptr)
{
  return ptr == nullptr ? TS_ERROR : TS_SUCCESS;
}

// Plugin metric IDs index the plugin RSB, so bounds check against that.
static TSReturnCode
sdk_sanity_check_stat_id(int id)
{
  return (global_api_metrics.valid(id) ? TS_SUCCESS : TS_ERROR);
}

static TSReturnCode
sdk_sanity_check_rpc_handler_options(const TSRPCHandlerOptions *opt)
{
  if (nullptr == opt) {
    return TS_ERROR;
  }

  if (opt->auth.restricted < 0 || opt->auth.restricted > 1) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

/**
  The function checks if the buffer is Modifiable and returns true if
  it is modifiable, else returns false.

*/
bool
isWriteable(TSMBuffer bufp)
{
  if (bufp != nullptr) {
    return (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap->m_writeable;
  }
  return false;
}

////////////////////////////////////////////////////////////////////
//
// API memory management
//
////////////////////////////////////////////////////////////////////

void *
_TSmalloc(size_t size, const char * /* path ATS_UNUSED */)
{
  return ats_malloc(size);
}

void *
_TSrealloc(void *ptr, size_t size, const char * /* path ATS_UNUSED */)
{
  return ats_realloc(ptr, size);
}

// length has to be int64_t and not size_t, since -1 means to call strlen() to get length
char *
_TSstrdup(const char *str, int64_t length, const char *path)
{
  return _xstrdup(str, length, path);
}

size_t
TSstrlcpy(char *dst, const char *str, size_t siz)
{
  return ink_strlcpy(dst, str, siz);
}

size_t
TSstrlcat(char *dst, const char *str, size_t siz)
{
  return ink_strlcat(dst, str, siz);
}

void
TSfree(void *ptr)
{
  ats_free(ptr);
}

////////////////////////////////////////////////////////////////////
//
// Encoding utility
//
////////////////////////////////////////////////////////////////////
TSReturnCode
TSBase64Decode(const char *str, size_t str_len, unsigned char *dst, size_t dst_size, size_t *length)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)str) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)dst) == TS_SUCCESS);

  return ats_base64_decode(str, str_len, dst, dst_size, length) ? TS_SUCCESS : TS_ERROR;
}

TSReturnCode
TSBase64Encode(const char *str, size_t str_len, char *dst, size_t dst_size, size_t *length)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)str) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)dst) == TS_SUCCESS);

  return ats_base64_encode(str, str_len, dst, dst_size, length) ? TS_SUCCESS : TS_ERROR;
}

////////////////////////////////////////////////////////////////////
//
// API utility routines
//
////////////////////////////////////////////////////////////////////

ink_hrtime
TShrtime()
{
  return ink_get_hrtime();
}

////////////////////////////////////////////////////////////////////
//
// API install and plugin locations
//
////////////////////////////////////////////////////////////////////

const char *
TSInstallDirGet()
{
  static std::string prefix = Layout::get()->prefix;
  return prefix.c_str();
}

const char *
TSConfigDirGet()
{
  static std::string sysconfdir = RecConfigReadConfigDir();
  return sysconfdir.c_str();
}

const char *
TSRuntimeDirGet()
{
  static std::string runtimedir = RecConfigReadRuntimeDir();
  return runtimedir.c_str();
}

const char *
TSTrafficServerVersionGet()
{
  return traffic_server_version;
}

int
TSTrafficServerVersionGetMajor()
{
  return ts_major_version;
}
int
TSTrafficServerVersionGetMinor()
{
  return ts_minor_version;
}
int
TSTrafficServerVersionGetPatch()
{
  return ts_patch_version;
}

const char *
TSPluginDirGet()
{
  static std::string path = RecConfigReadPluginDir();
  return path.c_str();
}

////////////////////////////////////////////////////////////////////
//
// Plugin registration
//
////////////////////////////////////////////////////////////////////

TSReturnCode
TSPluginRegister(const TSPluginRegistrationInfo *plugin_info)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)plugin_info) == TS_SUCCESS);

  if (!plugin_reg_current) {
    return TS_ERROR;
  }

  plugin_reg_current->plugin_registered = true;

  if (plugin_info->plugin_name) {
    plugin_reg_current->plugin_name = ats_strdup(plugin_info->plugin_name);
  }

  if (plugin_info->vendor_name) {
    plugin_reg_current->vendor_name = ats_strdup(plugin_info->vendor_name);
  }

  if (plugin_info->support_email) {
    plugin_reg_current->support_email = ats_strdup(plugin_info->support_email);
  }

  return TS_SUCCESS;
}

TSReturnCode
TSPluginDSOReloadEnable(int enabled)
{
  TSReturnCode ret = TS_SUCCESS;
  if (!plugin_reg_current) {
    return TS_ERROR;
  }

  if (!enabled) {
    if (!PluginDso::loadedPlugins()->addPluginPathToDsoOptOutTable(plugin_reg_current->plugin_path)) {
      ret = TS_ERROR;
    }
  }

  return ret;
}

////////////////////////////////////////////////////////////////////
//
// API file management
//
////////////////////////////////////////////////////////////////////

TSFile
TSfopen(const char *filename, const char *mode)
{
  FileImpl *file;

  file = new FileImpl;
  if (!file->fopen(filename, mode)) {
    delete file;
    return nullptr;
  }

  return reinterpret_cast<TSFile>(file);
}

void
TSfclose(TSFile filep)
{
  FileImpl *file = reinterpret_cast<FileImpl *>(filep);
  file->fclose();
  delete file;
}

ssize_t
TSfread(TSFile filep, void *buf, size_t length)
{
  FileImpl *file = reinterpret_cast<FileImpl *>(filep);
  return file->fread(buf, length);
}

ssize_t
TSfwrite(TSFile filep, const void *buf, size_t length)
{
  FileImpl *file = reinterpret_cast<FileImpl *>(filep);
  return file->fwrite(buf, length);
}

void
TSfflush(TSFile filep)
{
  FileImpl *file = reinterpret_cast<FileImpl *>(filep);
  file->fflush();
}

char *
TSfgets(TSFile filep, char *buf, size_t length)
{
  FileImpl *file = reinterpret_cast<FileImpl *>(filep);
  return file->fgets(buf, length);
}

////////////////////////////////////////////////////////////////////
//
// Header component object handles
//
////////////////////////////////////////////////////////////////////

TSReturnCode
TSHandleMLocRelease(TSMBuffer bufp, TSMLoc parent, TSMLoc mloc)
{
  MIMEFieldSDKHandle *field_handle;
  HdrHeapObjImpl     *obj = reinterpret_cast<HdrHeapObjImpl *>(mloc);

  if (mloc == TS_NULL_MLOC) {
    return TS_SUCCESS;
  }

  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);

  switch (static_cast<HdrHeapObjType>(obj->m_type)) {
  case HdrHeapObjType::URL:
  case HdrHeapObjType::HTTP_HEADER:
  case HdrHeapObjType::MIME_HEADER:
    return TS_SUCCESS;

  case HdrHeapObjType::FIELD_SDK_HANDLE:
    field_handle = static_cast<MIMEFieldSDKHandle *>(obj);
    if (sdk_sanity_check_field_handle(mloc, parent) != TS_SUCCESS) {
      return TS_ERROR;
    }

    sdk_free_field_handle(bufp, field_handle);
    return TS_SUCCESS;

  default:
    ink_release_assert(!"invalid mloc");
    return TS_ERROR;
  }
}

////////////////////////////////////////////////////////////////////
//
// HdrHeaps (previously known as "Marshal Buffers")
//
////////////////////////////////////////////////////////////////////

// TSMBuffer: pointers to HdrHeapSDKHandle objects

TSMBuffer
TSMBufferCreate()
{
  TSMBuffer         bufp;
  HdrHeapSDKHandle *new_heap = new HdrHeapSDKHandle;

  new_heap->m_heap = new_HdrHeap();
  bufp             = reinterpret_cast<TSMBuffer>(new_heap);
  // TODO: Should remove this when memory allocation is guaranteed to fail.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  return bufp;
}

TSReturnCode
TSMBufferDestroy(TSMBuffer bufp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  HdrHeapSDKHandle *sdk_heap = reinterpret_cast<HdrHeapSDKHandle *>(bufp);
  sdk_heap->m_heap->destroy();
  delete sdk_heap;
  return TS_SUCCESS;
}

////////////////////////////////////////////////////////////////////
//
// URLs
//
////////////////////////////////////////////////////////////////////

// TSMBuffer: pointers to HdrHeapSDKHandle objects
// TSMLoc:    pointers to URLImpl objects
TSReturnCode
TSUrlCreate(TSMBuffer bufp, TSMLoc *locp)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr(locp) == TS_SUCCESS);

  if (isWriteable(bufp)) {
    HdrHeap *heap = reinterpret_cast<HdrHeapSDKHandle *>(bufp)->m_heap;
    *locp         = reinterpret_cast<TSMLoc>(url_create(heap));
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

TSReturnCode
TSUrlClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_url, TSMLoc *locp)
{
  sdk_assert(sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(src_url) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr(locp) == TS_SUCCESS);

  if (!isWriteable(dest_bufp)) {
    return TS_ERROR;
  }

  HdrHeap *s_heap, *d_heap;
  URLImpl *s_url, *d_url;

  s_heap = reinterpret_cast<HdrHeapSDKHandle *>(src_bufp)->m_heap;
  d_heap = reinterpret_cast<HdrHeapSDKHandle *>(dest_bufp)->m_heap;
  s_url  = reinterpret_cast<URLImpl *>(src_url);

  d_url = url_copy(s_url, s_heap, d_heap, (s_heap != d_heap));
  *locp = reinterpret_cast<TSMLoc>(d_url);
  return TS_SUCCESS;
}

TSReturnCode
TSUrlCopy(TSMBuffer dest_bufp, TSMLoc dest_obj, TSMBuffer src_bufp, TSMLoc src_obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(src_obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(dest_obj) == TS_SUCCESS);

  if (!isWriteable(dest_bufp)) {
    return TS_ERROR;
  }

  HdrHeap *s_heap, *d_heap;
  URLImpl *s_url, *d_url;

  s_heap = reinterpret_cast<HdrHeapSDKHandle *>(src_bufp)->m_heap;
  d_heap = reinterpret_cast<HdrHeapSDKHandle *>(dest_bufp)->m_heap;
  s_url  = reinterpret_cast<URLImpl *>(src_obj);
  d_url  = reinterpret_cast<URLImpl *>(dest_obj);

  url_copy_onto(s_url, s_heap, d_url, d_heap, (s_heap != d_heap));
  return TS_SUCCESS;
}

void
TSUrlPrint(TSMBuffer bufp, TSMLoc obj, TSIOBuffer iobufp)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_iocore_structure(iobufp) == TS_SUCCESS);

  MIOBuffer     *b = reinterpret_cast<MIOBuffer *>(iobufp);
  IOBufferBlock *blk;
  int            bufindex;
  int            tmp, dumpoffset;
  int            done;
  URL            u;

  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);
  dumpoffset   = 0;

  do {
    blk = b->get_current_block();
    if (!blk || blk->write_avail() == 0) {
      b->add_block();
      blk = b->get_current_block();
    }

    bufindex = 0;
    tmp      = dumpoffset;

    done = u.print(blk->end(), blk->write_avail(), &bufindex, &tmp);

    dumpoffset += bufindex;
    b->fill(bufindex);
  } while (!done);
}

TSParseResult
TSUrlParse(TSMBuffer bufp, TSMLoc obj, const char **start, const char *end)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)start) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)*start) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)end) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_PARSE_ERROR;
  }

  URL u;
  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);
  url_clear(u.m_url_impl);
  return static_cast<TSParseResult>(u.parse(start, end));
}

int
TSUrlLengthGet(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  URLImpl *url_impl = reinterpret_cast<URLImpl *>(obj);
  return url_length_get(url_impl);
}

char *
TSUrlStringGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  // bufp is not actually used anymore, so it can be null.
  if (bufp) {
    sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  }
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)length) == TS_SUCCESS);

  URLImpl *url_impl = reinterpret_cast<URLImpl *>(obj);
  return url_string_get(url_impl, nullptr, length, nullptr);
}

using URLPartGetF = std::string_view (URL::*)() const noexcept;
using URLPartSetF = void (URL::*)(std::string_view);

static const std::string_view
URLPartGet(TSMBuffer bufp, TSMLoc obj, URLPartGetF url_f)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  URL u;

  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);

  return (u.*url_f)();
}

static TSReturnCode
URLPartSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length, URLPartSetF url_f)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  URL u;
  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);

  if (!value) {
    length = 0;
  } else if (length < 0) {
    length = strlen(value);
  }
  (u.*url_f)(std::string_view{value, static_cast<std::string_view::size_type>(length)});

  return TS_SUCCESS;
}

const char *
TSUrlRawSchemeGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  auto scheme{URLPartGet(bufp, obj, &URL::scheme_get)};
  if (length) {
    *length = static_cast<int>(scheme.length());
  }
  return scheme.data();
}

const char *
TSUrlSchemeGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  char const *data = TSUrlRawSchemeGet(bufp, obj, length);
  if (data && *length) {
    return data;
  }
  switch (reinterpret_cast<URLImpl *>(obj)->m_url_type) {
  case URLType::HTTP:
    data    = URL_SCHEME_HTTP.c_str();
    *length = static_cast<int>(URL_SCHEME_HTTP.length());
    break;
  case URLType::HTTPS:
    data    = URL_SCHEME_HTTPS.c_str();
    *length = static_cast<int>(URL_SCHEME_HTTPS.length());
    break;
  default:
    *length = 0;
    data    = nullptr;
  }
  return data;
}

TSReturnCode
TSUrlSchemeSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  return URLPartSet(bufp, obj, value, length, &URL::scheme_set);
}

/* Internet specific URLs */

const char *
TSUrlUserGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  auto user{URLPartGet(bufp, obj, &URL::user_get)};
  if (length) {
    *length = static_cast<int>(user.length());
  }
  return user.data();
}

TSReturnCode
TSUrlUserSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  return URLPartSet(bufp, obj, value, length, &URL::user_set);
}

const char *
TSUrlPasswordGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  auto password{URLPartGet(bufp, obj, &URL::password_get)};
  if (length) {
    *length = static_cast<int>(password.length());
  }
  return password.data();
}

TSReturnCode
TSUrlPasswordSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  return URLPartSet(bufp, obj, value, length, &URL::password_set);
}

const char *
TSUrlHostGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  auto host{URLPartGet(bufp, obj, &URL::host_get)};
  if (length) {
    *length = static_cast<int>(host.length());
  }
  return host.data();
}

TSReturnCode
TSUrlHostSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  return URLPartSet(bufp, obj, value, length, &URL::host_set);
}

int
TSUrlPortGet(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  URL u;
  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);

  return u.port_get();
}

int
TSUrlRawPortGet(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  URL u;
  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);

  return u.port_get_raw();
}

TSReturnCode
TSUrlPortSet(TSMBuffer bufp, TSMLoc obj, int port)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  if (!isWriteable(bufp) || (port < 0)) {
    return TS_ERROR;
  }

  URL u;

  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);
  u.port_set(port);
  return TS_SUCCESS;
}

/* FTP and HTTP specific URLs  */

const char *
TSUrlPathGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  auto path{URLPartGet(bufp, obj, &URL::path_get)};
  if (length) {
    *length = static_cast<int>(path.length());
  }
  return path.data();
}

TSReturnCode
TSUrlPathSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  return URLPartSet(bufp, obj, value, length, &URL::path_set);
}

/* FTP specific URLs */

int
TSUrlFtpTypeGet(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  URL u;
  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);
  return u.type_code_get();
}

TSReturnCode
TSUrlFtpTypeSet(TSMBuffer bufp, TSMLoc obj, int type)
{
  // The valid values are : 0, 65('A'), 97('a'),
  // 69('E'), 101('e'), 73 ('I') and 105('i').
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  if ((type == 0 || type == 'A' || type == 'E' || type == 'I' || type == 'a' || type == 'i' || type == 'e') && isWriteable(bufp)) {
    URL u;

    u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
    u.m_url_impl = reinterpret_cast<URLImpl *>(obj);
    u.type_code_set(type);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

/* HTTP specific URLs */

const char *
TSUrlHttpQueryGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  auto query{URLPartGet(bufp, obj, &URL::query_get)};
  if (length) {
    *length = static_cast<int>(query.length());
  }
  return query.data();
}

TSReturnCode
TSUrlHttpQuerySet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  return URLPartSet(bufp, obj, value, length, &URL::query_set);
}

const char *
TSUrlHttpFragmentGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  auto fragment{URLPartGet(bufp, obj, &URL::fragment_get)};
  if (length) {
    *length = static_cast<int>(fragment.length());
  }
  return fragment.data();
}

TSReturnCode
TSUrlHttpFragmentSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  return URLPartSet(bufp, obj, value, length, &URL::fragment_set);
}

// URL percent encoding
TSReturnCode
TSStringPercentEncode(const char *str, int str_len, char *dst, size_t dst_size, size_t *length, const unsigned char *map)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)str) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)dst) == TS_SUCCESS);

  int new_len; // Unfortunately, a lot of the core uses "int" for length's internally...

  if (str_len < 0) {
    str_len = strlen(str);
  }

  sdk_assert(str_len < static_cast<int>(dst_size));

  // TODO: Perhaps we should make escapify_url() deal with const properly...
  // You would think making escapify_url const correct for the source argument would be easy, but in the case where
  // No escaping is needed, the source argument is returned.  If there is a destination argument, the source is copied over
  // However, if there is no destination argument, none is allocated.  I don't understand the full possibility of calling cases.
  // It seems like we might want to review how this is being called and perhaps create a number of smaller accessor methods that
  // can be set up correctly.
  if (nullptr == Encoding::pure_escapify_url(nullptr, const_cast<char *>(str), str_len, &new_len, dst, dst_size, map)) {
    if (length) {
      *length = 0;
    }
    return TS_ERROR;
  }

  if (length) {
    *length = new_len;
  }

  return TS_SUCCESS;
}

TSReturnCode
TSStringPercentDecode(const char *str, size_t str_len, char *dst, size_t dst_size, size_t *length)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)str) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)dst) == TS_SUCCESS);

  if (0 == str_len) {
    str_len = strlen(str);
  }

  // return unescapifyStr(str);
  char       *buffer = dst;
  const char *src    = str;
  int         s      = 0; // State, which we don't really use

  // TODO: We should check for "failures" here?
  unescape_str(buffer, buffer + dst_size, src, src + str_len, s);

  size_t data_written   = std::min<size_t>(buffer - dst, dst_size - 1);
  *(dst + data_written) = '\0';

  if (length) {
    *length = (data_written);
  }

  return TS_SUCCESS;
}

TSReturnCode
TSUrlPercentEncode(TSMBuffer bufp, TSMLoc obj, char *dst, size_t dst_size, size_t *length, const unsigned char *map)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  char        *url;
  int          url_len;
  TSReturnCode ret;
  URLImpl     *url_impl = reinterpret_cast<URLImpl *>(obj);

  // TODO: at some point, it might be nice to allow this to write to a pre-allocated buffer
  url = url_string_get(url_impl, nullptr, &url_len, nullptr);
  ret = TSStringPercentEncode(url, url_len, dst, dst_size, length, map);
  ats_free(url);

  return ret;
}

// pton
TSReturnCode
TSIpStringToAddr(const char *str, size_t str_len, sockaddr *addr)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)str) == TS_SUCCESS);

  if (0 != ats_ip_pton(std::string_view(str, str_len), addr)) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

////////////////////////////////////////////////////////////////////
//
// MIME Headers
//
////////////////////////////////////////////////////////////////////

/**************/
/* MimeParser */
/**************/

TSMimeParser
TSMimeParserCreate()
{
  TSMimeParser parser = reinterpret_cast<TSMimeParser>(ats_malloc(sizeof(MIMEParser)));

  mime_parser_init(reinterpret_cast<MIMEParser *>(parser));
  return parser;
}

void
TSMimeParserClear(TSMimeParser parser)
{
  sdk_assert(sdk_sanity_check_mime_parser(parser) == TS_SUCCESS);

  mime_parser_clear(reinterpret_cast<MIMEParser *>(parser));
}

void
TSMimeParserDestroy(TSMimeParser parser)
{
  sdk_assert(sdk_sanity_check_mime_parser(parser) == TS_SUCCESS);

  mime_parser_clear(reinterpret_cast<MIMEParser *>(parser));
  ats_free(parser);
}

/***********/
/* MimeHdr */
/***********/

// TSMBuffer: pointers to HdrHeapSDKHandle objects
// TSMLoc:    pointers to MIMEFieldSDKHandle objects

TSReturnCode
TSMimeHdrCreate(TSMBuffer bufp, TSMLoc *locp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, set *locp to TS_NULL_MLOC.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)locp) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  *locp = reinterpret_cast<TSMLoc>(mime_hdr_create((reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap));
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrDestroy(TSMBuffer bufp, TSMLoc obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS));

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);

  mime_hdr_destroy((reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap, mh);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc *locp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, set *locp to TS_NULL_MLOC.
  sdk_assert(sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mime_hdr_handle(src_hdr) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(src_hdr) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)locp) == TS_SUCCESS);

  if (!isWriteable(dest_bufp)) {
    return TS_ERROR;
  }

  HdrHeap     *s_heap, *d_heap;
  MIMEHdrImpl *s_mh, *d_mh;

  s_heap = (reinterpret_cast<HdrHeapSDKHandle *>(src_bufp))->m_heap;
  d_heap = (reinterpret_cast<HdrHeapSDKHandle *>(dest_bufp))->m_heap;
  s_mh   = _hdr_mloc_to_mime_hdr_impl(src_hdr);

  d_mh  = mime_hdr_clone(s_mh, s_heap, d_heap, (s_heap != d_heap));
  *locp = reinterpret_cast<TSMLoc>(d_mh);

  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrCopy(TSMBuffer dest_bufp, TSMLoc dest_obj, TSMBuffer src_bufp, TSMLoc src_obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(src_obj) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(src_obj) == TS_SUCCESS));
  sdk_assert((sdk_sanity_check_mime_hdr_handle(dest_obj) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(dest_obj) == TS_SUCCESS));

  if (!isWriteable(dest_bufp)) {
    return TS_ERROR;
  }

  HdrHeap     *s_heap, *d_heap;
  MIMEHdrImpl *s_mh, *d_mh;

  s_heap = (reinterpret_cast<HdrHeapSDKHandle *>(src_bufp))->m_heap;
  d_heap = (reinterpret_cast<HdrHeapSDKHandle *>(dest_bufp))->m_heap;
  s_mh   = _hdr_mloc_to_mime_hdr_impl(src_obj);
  d_mh   = _hdr_mloc_to_mime_hdr_impl(dest_obj);

  mime_hdr_fields_clear(d_heap, d_mh);
  mime_hdr_copy_onto(s_mh, s_heap, d_mh, d_heap, (s_heap != d_heap));
  return TS_SUCCESS;
}

void
TSMimeHdrPrint(TSMLoc obj, TSIOBuffer iobufp)
{
  sdk_assert((sdk_sanity_check_mime_hdr_handle(obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_iocore_structure(iobufp) == TS_SUCCESS);

  MIMEHdrImpl const *mh = _hdr_mloc_to_mime_hdr_impl(obj);
  MIOBuffer         *b  = reinterpret_cast<MIOBuffer *>(iobufp);
  IOBufferBlock     *blk;
  int                bufindex;
  int                tmp, dumpoffset = 0;
  int                done;

  do {
    blk = b->get_current_block();
    if (!blk || blk->write_avail() == 0) {
      b->add_block();
      blk = b->get_current_block();
    }

    bufindex = 0;
    tmp      = dumpoffset;
    done     = mime_hdr_print(mh, blk->end(), blk->write_avail(), &bufindex, &tmp);

    dumpoffset += bufindex;
    b->fill(bufindex);
  } while (!done);
}

TSParseResult
TSMimeHdrParse(TSMimeParser parser, TSMBuffer bufp, TSMLoc obj, const char **start, const char *end)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_null_ptr((void *)start) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)*start) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)end) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_PARSE_ERROR;
  }

  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);

  return static_cast<TSParseResult>(mime_parser_parse(reinterpret_cast<MIMEParser *>(parser),
                                                      (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap, mh, start, end, false,
                                                      false, false));
}

int
TSMimeHdrLengthGet(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS));

  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
  return mime_hdr_length_get(mh);
}

TSReturnCode
TSMimeHdrFieldsClear(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS));

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);

  mime_hdr_fields_clear((reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap, mh);
  return TS_SUCCESS;
}

int
TSMimeHdrFieldsCount(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(obj) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS));

  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
  return mime_hdr_fields_count(mh);
}

// The following three helper functions should not be used in plugins! Since they are not used
// by plugins, there's no need to validate the input.
static const char *
TSMimeFieldValueGet(TSMBuffer /* bufp ATS_UNUSED */, TSMLoc field_obj, int idx, int *value_len_ptr)
{
  MIMEFieldSDKHandle *handle = reinterpret_cast<MIMEFieldSDKHandle *>(field_obj);

  if (idx >= 0) {
    return mime_field_value_get_comma_val(handle->field_ptr, value_len_ptr, idx);
  } else {
    auto value{handle->field_ptr->value_get()};
    *value_len_ptr = static_cast<int>(value.length());
    return value.data();
  }
}

static void
TSMimeFieldValueSet(TSMBuffer bufp, TSMLoc field_obj, int idx, const char *value, int length)
{
  MIMEFieldSDKHandle *handle = reinterpret_cast<MIMEFieldSDKHandle *>(field_obj);
  HdrHeap            *heap   = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;

  if (length == -1) {
    length = strlen(value);
  }

  if (idx >= 0) {
    mime_field_value_set_comma_val(heap, handle->mh, handle->field_ptr, idx,
                                   std::string_view{value, static_cast<std::string_view::size_type>(length)});
  } else {
    mime_field_value_set(heap, handle->mh, handle->field_ptr,
                         std::string_view{value, static_cast<std::string_view::size_type>(length)}, true);
  }
}

static void
TSMimeFieldValueInsert(TSMBuffer bufp, TSMLoc field_obj, const char *value, int length, int idx)
{
  MIMEFieldSDKHandle *handle = reinterpret_cast<MIMEFieldSDKHandle *>(field_obj);
  HdrHeap            *heap   = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;

  if (length == -1) {
    length = strlen(value);
  }

  mime_field_value_insert_comma_val(heap, handle->mh, handle->field_ptr, idx,
                                    std::string_view{value, static_cast<std::string_view::size_type>(length)});
}

/****************/
/* MimeHdrField */
/****************/

// TSMBuffer: pointers to HdrHeapSDKHandle objects
// TSMLoc:    pointers to MIMEFieldSDKHandle objects

TSMLoc
TSMimeHdrFieldGet(TSMBuffer bufp, TSMLoc hdr_obj, int idx)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr_obj) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(hdr_obj) == TS_SUCCESS));
  sdk_assert(idx >= 0);

  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(hdr_obj);
  MIMEField   *f  = mime_hdr_field_get(mh, idx);

  if (f == nullptr) {
    return TS_NULL_MLOC;
  }

  MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, mh);

  h->field_ptr = f;
  return reinterpret_cast<TSMLoc>(h);
}

TSMLoc
TSMimeHdrFieldFind(TSMBuffer bufp, TSMLoc hdr_obj, const char *name, int length)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr_obj) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(hdr_obj) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_null_ptr((void *)name) == TS_SUCCESS);

  if (length == -1) {
    length = strlen(name);
  }

  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(hdr_obj);
  MIMEField   *f  = mime_hdr_field_find(mh, std::string_view{name, static_cast<std::string_view::size_type>(length)});

  if (f == nullptr) {
    return TS_NULL_MLOC;
  }

  MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, mh);

  h->field_ptr = f;
  return reinterpret_cast<TSMLoc>(h);
}

TSReturnCode
TSMimeHdrFieldAppend(TSMBuffer bufp, TSMLoc mh_mloc, TSMLoc field_mloc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(mh_mloc) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(mh_mloc) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field_mloc) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  MIMEField          *mh_field;
  MIMEHdrImpl        *mh           = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
  MIMEFieldSDKHandle *field_handle = reinterpret_cast<MIMEFieldSDKHandle *>(field_mloc);

  //////////////////////////////////////////////////////////////////////
  // The field passed in field_mloc might have been allocated from    //
  // inside a MIME header (the correct way), or it might have been    //
  // created in isolation as a "standalone field" (the old way).      //
  //                                                                  //
  // If it's a standalone field (the associated mime header is null), //
  // then we need to now allocate a real field inside the header,     //
  // copy over the data, and convert the standalone field into a      //
  // forwarding pointer to the real field, in case it's used again    //
  //////////////////////////////////////////////////////////////////////
  if (field_handle->mh == nullptr) {
    HdrHeap *heap = ((reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap);

    // allocate a new hdr field and copy any pre-set info
    mh_field = mime_field_create(heap, mh);

    // FIX: is it safe to copy everything over?
    memcpy(mh_field, field_handle->field_ptr, sizeof(MIMEField));

    // now set up the forwarding ptr from standalone field to hdr field
    field_handle->mh        = mh;
    field_handle->field_ptr = mh_field;
  }

  ink_assert(field_handle->mh == mh);
  ink_assert(field_handle->field_ptr->m_ptr_name);

  mime_hdr_field_attach(mh, field_handle->field_ptr, 1, nullptr);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldRemove(TSMBuffer bufp, TSMLoc mh_mloc, TSMLoc field_mloc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(mh_mloc) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(mh_mloc) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field_mloc, mh_mloc) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  MIMEFieldSDKHandle *field_handle = reinterpret_cast<MIMEFieldSDKHandle *>(field_mloc);

  if (field_handle->mh != nullptr) {
    MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
    ink_assert(mh == field_handle->mh);
    sdk_sanity_check_field_handle(field_mloc, mh_mloc);
    mime_hdr_field_detach(mh, field_handle->field_ptr, false); // only detach this dup
  }
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldDestroy(TSMBuffer bufp, TSMLoc mh_mloc, TSMLoc field_mloc)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(mh_mloc) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(mh_mloc) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field_mloc, mh_mloc) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  MIMEFieldSDKHandle *field_handle = reinterpret_cast<MIMEFieldSDKHandle *>(field_mloc);

  if (field_handle->mh == nullptr) { // NOT SUPPORTED!!
    ink_release_assert(!"Failed MH");
  } else {
    MIMEHdrImpl *mh   = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
    HdrHeap     *heap = ((reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap);

    ink_assert(mh == field_handle->mh);
    if (sdk_sanity_check_field_handle(field_mloc, mh_mloc) != TS_SUCCESS) {
      return TS_ERROR;
    }

    // detach and delete this field, but not all dups
    mime_hdr_field_delete(heap, mh, field_handle->field_ptr, false);
  }
  // for consistence, the handle will not be released here.
  // users will be required to do it.
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldCreate(TSMBuffer bufp, TSMLoc mh_mloc, TSMLoc *locp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, set *locp to TS_NULL_MLOC.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(mh_mloc) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(mh_mloc) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_null_ptr((void *)locp) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  MIMEHdrImpl        *mh   = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
  HdrHeap            *heap = ((reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap);
  MIMEFieldSDKHandle *h    = sdk_alloc_field_handle(bufp, mh);

  h->field_ptr = mime_field_create(heap, mh);
  *locp        = reinterpret_cast<TSMLoc>(h);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldCreateNamed(TSMBuffer bufp, TSMLoc mh_mloc, const char *name, int name_len, TSMLoc *locp)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(mh_mloc) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(mh_mloc) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_null_ptr((void *)name) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)locp) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  if (name_len == -1) {
    name_len = strlen(name);
  }

  MIMEHdrImpl        *mh   = _hdr_mloc_to_mime_hdr_impl(mh_mloc);
  HdrHeap            *heap = ((reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap);
  MIMEFieldSDKHandle *h    = sdk_alloc_field_handle(bufp, mh);
  h->field_ptr = mime_field_create_named(heap, mh, std::string_view{name, static_cast<std::string_view::size_type>(name_len)});
  *locp        = reinterpret_cast<TSMLoc>(h);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldCopy(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMLoc dest_field, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc src_field)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(src_hdr) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(src_hdr) == TS_SUCCESS));
  sdk_assert((sdk_sanity_check_mime_hdr_handle(dest_hdr) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(dest_hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(src_field, src_hdr) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_field_handle(dest_field, dest_hdr) == TS_SUCCESS);

  if (!isWriteable(dest_bufp)) {
    return TS_ERROR;
  }

  bool                dest_attached;
  MIMEFieldSDKHandle *s_handle = reinterpret_cast<MIMEFieldSDKHandle *>(src_field);
  MIMEFieldSDKHandle *d_handle = reinterpret_cast<MIMEFieldSDKHandle *>(dest_field);
  HdrHeap            *d_heap   = (reinterpret_cast<HdrHeapSDKHandle *>(dest_bufp))->m_heap;

  // FIX: This tortuous detach/change/attach algorithm is due to the
  //      fact that we can't change the name of an attached header (assertion)

  // TODO: This is never used ... is_live() has no side effects, so this should be ok
  // to not call, so commented out
  // src_attached = (s_handle->mh && s_handle->field_ptr->is_live());
  dest_attached = (d_handle->mh && d_handle->field_ptr->is_live());

  if (dest_attached) {
    mime_hdr_field_detach(d_handle->mh, d_handle->field_ptr, false);
  }

  mime_field_name_value_set(
    d_heap, d_handle->mh, d_handle->field_ptr, s_handle->field_ptr->m_wks_idx,
    std::string_view{s_handle->field_ptr->m_ptr_name, static_cast<std::string_view::size_type>(s_handle->field_ptr->m_len_name)},
    std::string_view{s_handle->field_ptr->m_ptr_value, static_cast<std::string_view::size_type>(s_handle->field_ptr->m_len_value)},
    0, 0, true);

  if (dest_attached) {
    mime_hdr_field_attach(d_handle->mh, d_handle->field_ptr, 1, nullptr);
  }
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldClone(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc src_field, TSMLoc *locp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, set *locp to TS_NULL_MLOC.
  sdk_assert(sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(dest_hdr) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(dest_hdr) == TS_SUCCESS));
  sdk_assert((sdk_sanity_check_mime_hdr_handle(src_hdr) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(src_hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(src_field, src_hdr) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)locp) == TS_SUCCESS);

  if (!isWriteable(dest_bufp)) {
    return TS_ERROR;
  }

  // This is sort of sub-optimal, since we'll check the args again. TODO.
  if (TSMimeHdrFieldCreate(dest_bufp, dest_hdr, locp) == TS_SUCCESS) {
    TSMimeHdrFieldCopy(dest_bufp, dest_hdr, *locp, src_bufp, src_hdr, src_field);
    return TS_SUCCESS;
  }
  // TSMimeHdrFieldCreate() failed for some reason.
  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldCopyValues(TSMBuffer dest_bufp, TSMLoc dest_hdr, TSMLoc dest_field, TSMBuffer src_bufp, TSMLoc src_hdr,
                         TSMLoc src_field)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(src_hdr) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(src_hdr) == TS_SUCCESS));
  sdk_assert((sdk_sanity_check_mime_hdr_handle(dest_hdr) == TS_SUCCESS) ||
             (sdk_sanity_check_http_hdr_handle(dest_hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(src_field, src_hdr) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_field_handle(dest_field, dest_hdr) == TS_SUCCESS);

  if (!isWriteable(dest_bufp)) {
    return TS_ERROR;
  }

  MIMEFieldSDKHandle *s_handle = reinterpret_cast<MIMEFieldSDKHandle *>(src_field);
  MIMEFieldSDKHandle *d_handle = reinterpret_cast<MIMEFieldSDKHandle *>(dest_field);
  HdrHeap            *d_heap   = (reinterpret_cast<HdrHeapSDKHandle *>(dest_bufp))->m_heap;
  MIMEField          *s_field, *d_field;

  s_field = s_handle->field_ptr;
  d_field = d_handle->field_ptr;
  mime_field_value_set(d_heap, d_handle->mh, d_field,
                       std::string_view{s_field->m_ptr_value, static_cast<std::string_view::size_type>(s_field->m_len_value)},
                       true);
  return TS_SUCCESS;
}

TSMLoc
TSMimeHdrFieldNext(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  if (auto handle = reinterpret_cast<MIMEFieldSDKHandle *>(field); handle->mh != nullptr) {
    if (auto spot = handle->mh->find(handle->field_ptr); spot != handle->mh->end()) {
      if (++spot != handle->mh->end()) {
        MIMEFieldSDKHandle *h = sdk_alloc_field_handle(bufp, handle->mh);
        h->field_ptr          = &*spot;
        return reinterpret_cast<TSMLoc>(h);
      }
    }
  }

  return TS_NULL_MLOC;
}

TSMLoc
TSMimeHdrFieldNextDup(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  MIMEHdrImpl        *mh           = _hdr_mloc_to_mime_hdr_impl(hdr);
  MIMEFieldSDKHandle *field_handle = reinterpret_cast<MIMEFieldSDKHandle *>(field);
  MIMEField          *next         = field_handle->field_ptr->m_next_dup;
  if (next == nullptr) {
    return TS_NULL_MLOC;
  }

  MIMEFieldSDKHandle *next_handle = sdk_alloc_field_handle(bufp, mh);
  next_handle->field_ptr          = next;
  return reinterpret_cast<TSMLoc>(next_handle);
}

int
TSMimeHdrFieldLengthGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  MIMEFieldSDKHandle *handle = reinterpret_cast<MIMEFieldSDKHandle *>(field);
  return mime_field_length_get(handle->field_ptr);
}

const char *
TSMimeHdrFieldNameGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int *length)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)length) == TS_SUCCESS);

  MIMEFieldSDKHandle *handle = reinterpret_cast<MIMEFieldSDKHandle *>(field);
  auto                name{handle->field_ptr->name_get()};
  *length = static_cast<int>(name.length());
  return name.data();
}

TSReturnCode
TSMimeHdrFieldNameSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, const char *name, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)name) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  if (length == -1) {
    length = strlen(name);
  }

  MIMEFieldSDKHandle *handle = reinterpret_cast<MIMEFieldSDKHandle *>(field);
  HdrHeap            *heap   = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;

  int attached = (handle->mh && handle->field_ptr->is_live());

  if (attached) {
    mime_hdr_field_detach(handle->mh, handle->field_ptr, false);
  }

  handle->field_ptr->name_set(heap, handle->mh, std::string_view{name, static_cast<std::string_view::size_type>(length)});

  if (attached) {
    mime_hdr_field_attach(handle->mh, handle->field_ptr, 1, nullptr);
  }
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldValuesClear(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  MIMEFieldSDKHandle *handle = reinterpret_cast<MIMEFieldSDKHandle *>(field);
  HdrHeap            *heap   = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;

  /**
   * Modified the string value passed from an empty string ("") to null.
   * An empty string is also considered to be a token. The correct value of
   * the field after this function should be null.
   */
  mime_field_value_set(heap, handle->mh, handle->field_ptr, std::string_view{nullptr, 0}, true);
  return TS_SUCCESS;
}

int
TSMimeHdrFieldValuesCount(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  MIMEFieldSDKHandle *handle = reinterpret_cast<MIMEFieldSDKHandle *>(field);
  return mime_field_value_get_comma_val_count(handle->field_ptr);
}

const char *
TSMimeHdrFieldValueStringGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int *value_len_ptr)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)value_len_ptr) == TS_SUCCESS);

  return TSMimeFieldValueGet(bufp, field, idx, value_len_ptr);
}

time_t
TSMimeHdrFieldValueDateGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  int         value_len;
  const char *value_str = TSMimeFieldValueGet(bufp, field, -1, &value_len);

  if (value_str == nullptr) {
    return static_cast<time_t>(0);
  }

  return mime_parse_date(value_str, value_str + value_len);
}

time_t
TSMimeParseDate(char const *const value_str, int const value_len)
{
  if (value_str == nullptr) {
    return static_cast<time_t>(0);
  }

  return mime_parse_date(value_str, value_str + value_len);
}

int
TSMimeHdrFieldValueIntGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  int         value_len;
  const char *value_str = TSMimeFieldValueGet(bufp, field, idx, &value_len);

  if (value_str == nullptr) {
    return 0;
  }

  return mime_parse_int(value_str, value_str + value_len);
}

int64_t
TSMimeHdrFieldValueInt64Get(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  int         value_len;
  const char *value_str = TSMimeFieldValueGet(bufp, field, idx, &value_len);

  if (value_str == nullptr) {
    return 0;
  }

  return mime_parse_int64(value_str, value_str + value_len);
}

unsigned int
TSMimeHdrFieldValueUintGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  int         value_len;
  const char *value_str = TSMimeFieldValueGet(bufp, field, idx, &value_len);

  if (value_str == nullptr) {
    return 0;
  }

  return mime_parse_uint(value_str, value_str + value_len);
}

TSReturnCode
TSMimeHdrFieldValueStringSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)value) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  if (length == -1) {
    length = strlen(value);
  }

  TSMimeFieldValueSet(bufp, field, idx, value, length);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldValueDateSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, time_t value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  char tmp[33];
  int  len = mime_format_date(tmp, value);

  // idx is ignored and we overwrite all existing values
  // TSMimeFieldValueSet(bufp, field_obj, idx, tmp, len);
  TSMimeFieldValueSet(bufp, field, -1, tmp, len);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeFormatDate(time_t const value_time, char *const value_str, int *const value_length)
{
  if (value_length == nullptr) {
    return TS_ERROR;
  }

  if (*value_length < 33) {
    return TS_ERROR;
  }

  *value_length = mime_format_date(value_str, value_time);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldValueIntSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  char tmp[16];
  int  len = mime_format_int(tmp, value, sizeof(tmp));

  TSMimeFieldValueSet(bufp, field, idx, tmp, len);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldValueInt64Set(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int64_t value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  char tmp[20];
  int  len = mime_format_int64(tmp, value, sizeof(tmp));

  TSMimeFieldValueSet(bufp, field, idx, tmp, len);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldValueUintSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, unsigned int value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  char tmp[16];
  int  len = mime_format_uint(tmp, value, sizeof(tmp));

  TSMimeFieldValueSet(bufp, field, idx, tmp, len);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldValueAppend(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)value) == TS_SUCCESS);
  sdk_assert(idx >= 0);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  MIMEFieldSDKHandle *handle = reinterpret_cast<MIMEFieldSDKHandle *>(field);
  HdrHeap            *heap   = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;

  if (length == -1) {
    length = strlen(value);
  }
  mime_field_value_extend_comma_val(heap, handle->mh, handle->field_ptr, idx,
                                    std::string_view{value, static_cast<std::string_view::size_type>(length)});
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldValueStringInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR, else return TS_SUCCESS.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)value) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  if (length == -1) {
    length = strlen(value);
  }
  TSMimeFieldValueInsert(bufp, field, value, length, idx);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldValueIntInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR, else return TS_SUCCESS.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  char tmp[16];
  int  len = mime_format_int(tmp, value, sizeof(tmp));

  TSMimeFieldValueInsert(bufp, field, tmp, len, idx);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldValueUintInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, unsigned int value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR, else return TS_SUCCESS.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  char tmp[16];
  int  len = mime_format_uint(tmp, value, sizeof(tmp));

  TSMimeFieldValueInsert(bufp, field, tmp, len, idx);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldValueDateInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, time_t value)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR, else return TS_SUCCESS
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  if (TSMimeHdrFieldValuesClear(bufp, hdr, field) == TS_ERROR) {
    return TS_ERROR;
  }

  char tmp[33];
  int  len = mime_format_date(tmp, value);
  // idx ignored, overwrite all existing values
  // (void)TSMimeFieldValueInsert(bufp, field_obj, tmp, len, idx);
  (void)TSMimeFieldValueSet(bufp, field, -1, tmp, len);
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldValueDelete(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert((sdk_sanity_check_mime_hdr_handle(hdr) == TS_SUCCESS) || (sdk_sanity_check_http_hdr_handle(hdr) == TS_SUCCESS));
  sdk_assert(sdk_sanity_check_field_handle(field, hdr) == TS_SUCCESS);
  sdk_assert(idx >= 0);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  MIMEFieldSDKHandle *handle = reinterpret_cast<MIMEFieldSDKHandle *>(field);
  HdrHeap            *heap   = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;

  mime_field_value_delete_comma_val(heap, handle->mh, handle->field_ptr, idx);
  return TS_SUCCESS;
}

const char *
TSMimeHdrStringToWKS(const char *str, int length)
{
  if (length < 0) {
    return hdrtoken_string_to_wks(str);
  } else {
    return hdrtoken_string_to_wks(str, length);
  }
}

/**************/
/* HttpParser */
/**************/
TSHttpParser
TSHttpParserCreate()
{
  TSHttpParser parser = reinterpret_cast<TSHttpParser>(ats_malloc(sizeof(HTTPParser)));
  http_parser_init(reinterpret_cast<HTTPParser *>(parser));

  return parser;
}

void
TSHttpParserClear(TSHttpParser parser)
{
  sdk_assert(sdk_sanity_check_http_parser(parser) == TS_SUCCESS);
  http_parser_clear(reinterpret_cast<HTTPParser *>(parser));
}

void
TSHttpParserDestroy(TSHttpParser parser)
{
  sdk_assert(sdk_sanity_check_http_parser(parser) == TS_SUCCESS);
  http_parser_clear(reinterpret_cast<HTTPParser *>(parser));
  ats_free(parser);
}

/***********/
/* HttpHdr */
/***********/

TSMLoc
TSHttpHdrCreate(TSMBuffer bufp)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);

  HTTPHdr h;
  h.m_heap = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  h.create(HTTPType::UNKNOWN);
  return reinterpret_cast<TSMLoc>(h.m_http);
}

void
TSHttpHdrDestroy(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);

  // No more objects counts in heap or deallocation
  //   so do nothing!

  // HDR FIX ME - Did this free the MBuffer in Pete's old system
}

TSReturnCode
TSHttpHdrClone(TSMBuffer dest_bufp, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc *locp)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If not allowed, set *locp to TS_NULL_MLOC.
  sdk_assert(sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(src_hdr) == TS_SUCCESS);

  if (!isWriteable(dest_bufp)) {
    return TS_ERROR;
  }

  HdrHeap     *s_heap, *d_heap;
  HTTPHdrImpl *s_hh, *d_hh;

  s_heap = (reinterpret_cast<HdrHeapSDKHandle *>(src_bufp))->m_heap;
  d_heap = (reinterpret_cast<HdrHeapSDKHandle *>(dest_bufp))->m_heap;
  s_hh   = reinterpret_cast<HTTPHdrImpl *>(src_hdr);

  if (static_cast<HdrHeapObjType>(s_hh->m_type) != HdrHeapObjType::HTTP_HEADER) {
    return TS_ERROR;
  }

  // TODO: This is never used
  // inherit_strs = (s_heap != d_heap ? true : false);
  d_hh  = http_hdr_clone(s_hh, s_heap, d_heap);
  *locp = reinterpret_cast<TSMLoc>(d_hh);

  return TS_SUCCESS;
}

TSReturnCode
TSHttpHdrCopy(TSMBuffer dest_bufp, TSMLoc dest_obj, TSMBuffer src_bufp, TSMLoc src_obj)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(src_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(dest_bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(dest_obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(src_obj) == TS_SUCCESS);

  if (!isWriteable(dest_bufp)) {
    return TS_ERROR;
  }

  bool         inherit_strs;
  HdrHeap     *s_heap, *d_heap;
  HTTPHdrImpl *s_hh, *d_hh;

  s_heap = (reinterpret_cast<HdrHeapSDKHandle *>(src_bufp))->m_heap;
  d_heap = (reinterpret_cast<HdrHeapSDKHandle *>(dest_bufp))->m_heap;
  s_hh   = reinterpret_cast<HTTPHdrImpl *>(src_obj);
  d_hh   = reinterpret_cast<HTTPHdrImpl *>(dest_obj);

  if ((static_cast<HdrHeapObjType>(s_hh->m_type) != HdrHeapObjType::HTTP_HEADER) ||
      (static_cast<HdrHeapObjType>(d_hh->m_type) != HdrHeapObjType::HTTP_HEADER)) {
    return TS_ERROR;
  }

  inherit_strs = (s_heap != d_heap ? true : false);
  TSHttpHdrTypeSet(dest_bufp, dest_obj, static_cast<TSHttpType>(s_hh->m_polarity));
  http_hdr_copy_onto(s_hh, s_heap, d_hh, d_heap, inherit_strs);
  return TS_SUCCESS;
}

void
TSHttpHdrPrint(TSMBuffer bufp, TSMLoc obj, TSIOBuffer iobufp)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_iocore_structure(iobufp) == TS_SUCCESS);

  MIOBuffer     *b = reinterpret_cast<MIOBuffer *>(iobufp);
  IOBufferBlock *blk;
  HTTPHdr        h;
  int            bufindex;
  int            tmp, dumpoffset;
  int            done;

  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(static_cast<HdrHeapObjType>(h.m_http->m_type) == HdrHeapObjType::HTTP_HEADER);

  dumpoffset = 0;
  do {
    blk = b->get_current_block();
    if (!blk || blk->write_avail() == 0) {
      b->add_block();
      blk = b->get_current_block();
    }

    bufindex = 0;
    tmp      = dumpoffset;

    done = h.print(blk->end(), blk->write_avail(), &bufindex, &tmp);

    dumpoffset += bufindex;
    b->fill(bufindex);
  } while (!done);
}

TSParseResult
TSHttpHdrParseReq(TSHttpParser parser, TSMBuffer bufp, TSMLoc obj, const char **start, const char *end)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)start) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)*start) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)end) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_PARSE_ERROR;
  }

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(static_cast<HdrHeapObjType>(h.m_http->m_type) == HdrHeapObjType::HTTP_HEADER);
  TSHttpHdrTypeSet(bufp, obj, TS_HTTP_TYPE_REQUEST);
  return static_cast<TSParseResult>(h.parse_req(reinterpret_cast<HTTPParser *>(parser), start, end, false));
}

TSParseResult
TSHttpHdrParseResp(TSHttpParser parser, TSMBuffer bufp, TSMLoc obj, const char **start, const char *end)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)start) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)*start) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)end) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_PARSE_ERROR;
  }

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(static_cast<HdrHeapObjType>(h.m_http->m_type) == HdrHeapObjType::HTTP_HEADER);
  TSHttpHdrTypeSet(bufp, obj, TS_HTTP_TYPE_RESPONSE);
  return static_cast<TSParseResult>(h.parse_resp(reinterpret_cast<HTTPParser *>(parser), start, end, false));
}

int
TSHttpHdrLengthGet(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(static_cast<HdrHeapObjType>(h.m_http->m_type) == HdrHeapObjType::HTTP_HEADER);
  return h.length_get();
}

TSHttpType
TSHttpHdrTypeGet(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);

  HTTPHdr h;
  SET_HTTP_HDR(h, bufp, obj);
  /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(h.m_http->m_type == HdrHeapObjType::HTTP_HEADER);
   */
  return static_cast<TSHttpType>(h.type_get());
}

TSReturnCode
TSHttpHdrTypeSet(TSMBuffer bufp, TSMLoc obj, TSHttpType type)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);
  sdk_assert((type >= TS_HTTP_TYPE_UNKNOWN) && (type <= TS_HTTP_TYPE_RESPONSE));

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(static_cast<HdrHeapObjType>(h.m_http->m_type) == HdrHeapObjType::HTTP_HEADER);

  // FIX: why are we using an HTTPHdr here?  why can't we
  //      just manipulate the impls directly?

  // In Pete's MBuffer system you can change the type
  //   at will.  Not so anymore.  We need to try to
  //   fake the difference.  We not going to let
  //   people change the types of a header.  If they
  //   try, too bad.
  if (h.m_http->m_polarity == HTTPType::UNKNOWN) {
    if (type == static_cast<TSHttpType>(HTTPType::REQUEST)) {
      h.m_http->u.req.m_url_impl = url_create(h.m_heap);
      h.m_http->m_polarity       = static_cast<HTTPType>(type);
    } else if (type == static_cast<TSHttpType>(HTTPType::RESPONSE)) {
      h.m_http->m_polarity = static_cast<HTTPType>(type);
    }
  }
  return TS_SUCCESS;
}

int
TSHttpHdrVersionGet(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  HTTPVersion ver = h.version_get();
  return ver.get_flat_version();
}

TSReturnCode
TSHttpHdrVersionSet(TSMBuffer bufp, TSMLoc obj, int ver)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  HTTPHdr     h;
  HTTPVersion version{ver};

  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(static_cast<HdrHeapObjType>(h.m_http->m_type) == HdrHeapObjType::HTTP_HEADER);

  h.version_set(version);
  return TS_SUCCESS;
}

const char *
TSHttpHdrMethodGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)length) == TS_SUCCESS);

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  auto method{h.method_get()};
  *length = static_cast<int>(method.length());
  return method.data();
}

TSReturnCode
TSHttpHdrMethodSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)value) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  if (length < 0) {
    length = strlen(value);
  }

  h.method_set(std::string_view{value, static_cast<std::string_view::size_type>(length)});
  return TS_SUCCESS;
}

const char *
TSHttpHdrHostGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)length) == TS_SUCCESS);

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  auto host{h.host_get()};
  *length = static_cast<int>(host.length());
  return host.data();
}

TSReturnCode
TSHttpHdrUrlGet(TSMBuffer bufp, TSMLoc obj, TSMLoc *locp)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);

  HTTPHdrImpl *hh = reinterpret_cast<HTTPHdrImpl *>(obj);

  if (hh->m_polarity != HTTPType::REQUEST) {
    return TS_ERROR;
  }

  *locp = (reinterpret_cast<TSMLoc>(hh->u.req.m_url_impl));
  return TS_SUCCESS;
}

TSReturnCode
TSHttpHdrUrlSet(TSMBuffer bufp, TSMLoc obj, TSMLoc url)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(url) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  HdrHeap     *heap = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  HTTPHdrImpl *hh   = reinterpret_cast<HTTPHdrImpl *>(obj);

  if (static_cast<HdrHeapObjType>(hh->m_type) != HdrHeapObjType::HTTP_HEADER) {
    return TS_ERROR;
  }

  URLImpl *url_impl = reinterpret_cast<URLImpl *>(url);
  http_hdr_url_set(heap, hh, url_impl);
  return TS_SUCCESS;
}

TSHttpStatus
TSHttpHdrStatusGet(TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  return static_cast<TSHttpStatus>(h.status_get());
}

TSReturnCode
TSHttpHdrStatusSet(TSMBuffer bufp, TSMLoc obj, TSHttpStatus status)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  ink_assert(static_cast<HdrHeapObjType>(h.m_http->m_type) == HdrHeapObjType::HTTP_HEADER);
  h.status_set(static_cast<HTTPStatus>(status));
  return TS_SUCCESS;
}

const char *
TSHttpHdrReasonGet(TSMBuffer bufp, TSMLoc obj, int *length)
{
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)length) == TS_SUCCESS);

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  auto reason{h.reason_get()};
  *length = static_cast<int>(reason.length());
  return reason.data();
}

TSReturnCode
TSHttpHdrReasonSet(TSMBuffer bufp, TSMLoc obj, const char *value, int length)
{
  // Allow to modify the buffer only
  // if bufp is modifiable. If bufp is not modifiable return
  // TS_ERROR. If allowed, return TS_SUCCESS. Changed the
  // return value of function from void to TSReturnCode.
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(obj) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)value) == TS_SUCCESS);

  if (!isWriteable(bufp)) {
    return TS_ERROR;
  }

  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);
  /* Don't need the assert as the check is done in sdk_sanity_check_http_hdr_handle
     ink_assert(h.m_http->m_type == HdrHeapObjType::HTTP_HEADER);
  */

  if (length < 0) {
    length = strlen(value);
  }
  h.reason_set(std::string_view{value, static_cast<std::string_view::size_type>(length)});
  return TS_SUCCESS;
}

const char *
TSHttpHdrReasonLookup(TSHttpStatus status)
{
  return http_hdr_reason_lookup(static_cast<HTTPStatus>(status));
}

////////////////////////////////////////////////////////////////////
//
// Cache
//
////////////////////////////////////////////////////////////////////

inline TSReturnCode
sdk_sanity_check_cachekey(TSCacheKey key)
{
  if (nullptr == key) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSCacheKey
TSCacheKeyCreate()
{
  TSCacheKey key = reinterpret_cast<TSCacheKey>(new CacheInfo());

  // TODO: Probably remove this when we can be use "NEW" can't fail.
  sdk_assert(sdk_sanity_check_cachekey(key) == TS_SUCCESS);
  return key;
}

TSReturnCode
TSCacheKeyDigestSet(TSCacheKey key, const char *input, int length)
{
  sdk_assert(sdk_sanity_check_cachekey(key) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_iocore_structure((void *)input) == TS_SUCCESS);
  sdk_assert(length > 0);
  CacheInfo *ci = reinterpret_cast<CacheInfo *>(key);

  if (ci->magic != CACHE_INFO_MAGIC_ALIVE) {
    return TS_ERROR;
  }

  CryptoContext().hash_immediate(ci->cache_key, input, length);
  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyDigestFromUrlSet(TSCacheKey key, TSMLoc url)
{
  sdk_assert(sdk_sanity_check_cachekey(key) == TS_SUCCESS);

  if ((reinterpret_cast<CacheInfo *>(key))->magic != CACHE_INFO_MAGIC_ALIVE) {
    return TS_ERROR;
  }

  url_CryptoHash_get(reinterpret_cast<URLImpl *>(url), &(reinterpret_cast<CacheInfo *>(key))->cache_key);
  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyDataTypeSet(TSCacheKey key, TSCacheDataType type)
{
  sdk_assert(sdk_sanity_check_cachekey(key) == TS_SUCCESS);

  if ((reinterpret_cast<CacheInfo *>(key))->magic != CACHE_INFO_MAGIC_ALIVE) {
    return TS_ERROR;
  }

  switch (type) {
  case TS_CACHE_DATA_TYPE_NONE:
    (reinterpret_cast<CacheInfo *>(key))->frag_type = CACHE_FRAG_TYPE_NONE;
    break;
  case TS_CACHE_DATA_TYPE_OTHER: /* other maps to http */
  case TS_CACHE_DATA_TYPE_HTTP:
    (reinterpret_cast<CacheInfo *>(key))->frag_type = CACHE_FRAG_TYPE_HTTP;
    break;
  default:
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyHostNameSet(TSCacheKey key, const char *hostname, int host_len)
{
  sdk_assert(sdk_sanity_check_cachekey(key) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)hostname) == TS_SUCCESS);
  sdk_assert(host_len > 0);

  if ((reinterpret_cast<CacheInfo *>(key))->magic != CACHE_INFO_MAGIC_ALIVE) {
    return TS_ERROR;
  }

  CacheInfo *i = reinterpret_cast<CacheInfo *>(key);
  /* need to make a copy of the hostname. The caller
     might deallocate it anytime in the future */
  i->hostname = static_cast<char *>(ats_malloc(host_len));
  memcpy(i->hostname, hostname, host_len);
  i->len = host_len;
  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyPinnedSet(TSCacheKey key, time_t pin_in_cache)
{
  sdk_assert(sdk_sanity_check_cachekey(key) == TS_SUCCESS);

  if ((reinterpret_cast<CacheInfo *>(key))->magic != CACHE_INFO_MAGIC_ALIVE) {
    return TS_ERROR;
  }

  CacheInfo *i    = reinterpret_cast<CacheInfo *>(key);
  i->pin_in_cache = pin_in_cache;
  return TS_SUCCESS;
}

TSReturnCode
TSCacheKeyDestroy(TSCacheKey key)
{
  sdk_assert(sdk_sanity_check_cachekey(key) == TS_SUCCESS);

  if ((reinterpret_cast<CacheInfo *>(key))->magic != CACHE_INFO_MAGIC_ALIVE) {
    return TS_ERROR;
  }

  CacheInfo *i = reinterpret_cast<CacheInfo *>(key);

  ats_free(i->hostname);
  i->magic = CACHE_INFO_MAGIC_DEAD;
  delete i;
  return TS_SUCCESS;
}

TSCacheHttpInfo
TSCacheHttpInfoCopy(TSCacheHttpInfo infop)
{
  CacheHTTPInfo *new_info = new CacheHTTPInfo;

  new_info->copy(reinterpret_cast<CacheHTTPInfo *>(infop));
  return reinterpret_cast<TSCacheHttpInfo>(new_info);
}

void
TSCacheHttpInfoReqGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *obj)
{
  CacheHTTPInfo *info = reinterpret_cast<CacheHTTPInfo *>(infop);

  *(reinterpret_cast<HTTPHdr **>(bufp)) = info->request_get();
  *obj                                  = reinterpret_cast<TSMLoc>(info->request_get()->m_http);
  sdk_assert(sdk_sanity_check_mbuffer(*bufp) == TS_SUCCESS);
}

void
TSCacheHttpInfoRespGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *obj)
{
  CacheHTTPInfo *info = reinterpret_cast<CacheHTTPInfo *>(infop);

  *(reinterpret_cast<HTTPHdr **>(bufp)) = info->response_get();
  *obj                                  = reinterpret_cast<TSMLoc>(info->response_get()->m_http);
  sdk_assert(sdk_sanity_check_mbuffer(*bufp) == TS_SUCCESS);
}

time_t
TSCacheHttpInfoReqSentTimeGet(TSCacheHttpInfo infop)
{
  CacheHTTPInfo *info = reinterpret_cast<CacheHTTPInfo *>(infop);
  return info->request_sent_time_get();
}

time_t
TSCacheHttpInfoRespReceivedTimeGet(TSCacheHttpInfo infop)
{
  CacheHTTPInfo *info = reinterpret_cast<CacheHTTPInfo *>(infop);
  return info->response_received_time_get();
}

int64_t
TSCacheHttpInfoSizeGet(TSCacheHttpInfo infop)
{
  CacheHTTPInfo *info = reinterpret_cast<CacheHTTPInfo *>(infop);
  return info->object_size_get();
}

void
TSCacheHttpInfoReqSet(TSCacheHttpInfo infop, TSMBuffer bufp, TSMLoc obj)
{
  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);

  CacheHTTPInfo *info = reinterpret_cast<CacheHTTPInfo *>(infop);
  info->request_set(&h);
}

void
TSCacheHttpInfoRespSet(TSCacheHttpInfo infop, TSMBuffer bufp, TSMLoc obj)
{
  HTTPHdr h;

  SET_HTTP_HDR(h, bufp, obj);

  CacheHTTPInfo *info = reinterpret_cast<CacheHTTPInfo *>(infop);
  info->response_set(&h);
}

int
TSCacheHttpInfoVector(TSCacheHttpInfo infop, void *data, int length)
{
  CacheHTTPInfo      *info = reinterpret_cast<CacheHTTPInfo *>(infop);
  CacheHTTPInfoVector vector;

  vector.insert(info);

  int size = vector.marshal_length();

  if (size > length) {
    // error
    return 0;
  }

  return vector.marshal(static_cast<char *>(data), length);
}

void
TSCacheHttpInfoDestroy(TSCacheHttpInfo infop)
{
  (reinterpret_cast<CacheHTTPInfo *>(infop))->destroy();
}

TSCacheHttpInfo
TSCacheHttpInfoCreate()
{
  CacheHTTPInfo *info = new CacheHTTPInfo;
  info->create();

  return reinterpret_cast<TSCacheHttpInfo>(info);
}

////////////////////////////////////////////////////////////////////
//
// Configuration
//
////////////////////////////////////////////////////////////////////

unsigned int
TSConfigSet(unsigned int id, void *data, TSConfigDestroyFunc funcp)
{
  INKConfigImpl *config  = new INKConfigImpl;
  config->mdata          = data;
  config->m_destroy_func = funcp;
  return configProcessor.set(id, config);
}

TSConfig
TSConfigGet(unsigned int id)
{
  return reinterpret_cast<TSConfig>(configProcessor.get(id));
}

void
TSConfigRelease(unsigned int id, TSConfig configp)
{
  configProcessor.release(id, reinterpret_cast<ConfigInfo *>(configp));
}

void *
TSConfigDataGet(TSConfig configp)
{
  INKConfigImpl *config = reinterpret_cast<INKConfigImpl *>(configp);
  return config->mdata;
}

////////////////////////////////////////////////////////////////////
//
// Management
//
////////////////////////////////////////////////////////////////////

void
TSMgmtUpdateRegister(TSCont contp, const char *plugin_name, const char *plugin_file_name)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)plugin_name) == TS_SUCCESS);

  global_config_cbs->insert(reinterpret_cast<INKContInternal *>(contp), plugin_name, plugin_file_name);
}

TSReturnCode
TSMgmtIntGet(const char *var_name, TSMgmtInt *result)
{
  auto tmp{RecGetRecordInt(var_name)};

  // Try the old librecords first
  if (!tmp) {
    int id = global_api_metrics.lookup(var_name);

    if (id == ts::Metrics::NOT_FOUND) {
      return TS_ERROR;
    } else {
      *result = global_api_metrics[id].load();
    }
  } else {
    *result = tmp.value();
  }

  return TS_SUCCESS;
}

TSReturnCode
TSMgmtCounterGet(const char *var_name, TSMgmtCounter *result)
{
  auto tmp{RecGetRecordCounter(var_name)};

  // Try the old librecords first
  if (!tmp) {
    int id = global_api_metrics.lookup(var_name);

    if (id == ts::Metrics::NOT_FOUND) {
      return TS_ERROR;
    } else {
      *result = global_api_metrics[id].load();
    }
  } else {
    *result = tmp.value();
  }

  return TS_SUCCESS;
}

// ToDo: These don't have the new metrics, only librecords.
TSReturnCode
TSMgmtFloatGet(const char *var_name, TSMgmtFloat *result)
{
  auto tmp{RecGetRecordFloat(const_cast<char *>(var_name))};
  if (tmp) {
    *result = tmp.value();
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

TSReturnCode
TSMgmtStringGet(const char *var_name, TSMgmtString *result)
{
  auto tmp_str{RecGetRecordStringAlloc(const_cast<char *>(var_name))};
  auto tmp{ats_as_c_str(tmp_str)};

  if (tmp) {
    *result = ats_strdup(tmp);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMgmtSourceGet(const char *var_name, TSMgmtSource *source)
{
  return REC_ERR_OKAY == RecGetRecordSource(var_name, reinterpret_cast<RecSourceT *>(source)) ? TS_SUCCESS : TS_ERROR;
}

TSReturnCode
TSMgmtDataTypeGet(const char *var_name, TSRecordDataType *result)
{
  return REC_ERR_OKAY == RecGetRecordDataType(var_name, reinterpret_cast<RecDataT *>(result)) ? TS_SUCCESS : TS_ERROR;
}

////////////////////////////////////////////////////////////////////
//
// Continuations
//
////////////////////////////////////////////////////////////////////

TSCont
TSContCreate(TSEventFunc funcp, TSMutex mutexp)
{
  // mutexp can be null
  if (mutexp != nullptr) {
    sdk_assert(sdk_sanity_check_mutex(mutexp) == TS_SUCCESS);
  }

  if (pluginThreadContext) {
    pluginThreadContext->acquire();
  }

  INKContInternal *i = THREAD_ALLOC(INKContAllocator, this_thread());

  i->init(funcp, mutexp, pluginThreadContext);
  return reinterpret_cast<TSCont>(i);
}

void
TSContDestroy(TSCont contp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  if (i->m_context) {
    reinterpret_cast<PluginThreadContext *>(i->m_context)->release();
  }

  i->destroy();
}

void
TSContDataSet(TSCont contp, void *data)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  i->mdata = data;
}

void *
TSContDataGet(TSCont contp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  return i->mdata;
}

TSAction
TSContScheduleOnPool(TSCont contp, TSHRTime timeout, TSThreadPool tp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  /* ensure we are on a EThread */
  sdk_assert(sdk_sanity_check_null_ptr((void *)this_ethread()) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  if (ink_atomic_increment(static_cast<int *>(&i->m_event_count), 1) < 0) {
    ink_assert(!"not reached");
  }

  EventType etype;

  switch (tp) {
  case TS_THREAD_POOL_NET:
    etype = ET_NET;
    break;
  case TS_THREAD_POOL_TASK:
    etype = ET_TASK;
    break;
  case TS_THREAD_POOL_DNS:
    etype = ET_DNS;
    break;
  case TS_THREAD_POOL_UDP:
    etype = ET_UDP;
    break;
  default:
    etype = ET_TASK;
    break;
  }

  TSAction action;
  if (timeout == 0) {
    action = reinterpret_cast<TSAction>(eventProcessor.schedule_imm(i, etype));
  } else {
    action = reinterpret_cast<TSAction>(eventProcessor.schedule_in(i, HRTIME_MSECONDS(timeout), etype));
  }

  /* This is a hack. Should be handled in ink_types */
  action = reinterpret_cast<TSAction>(reinterpret_cast<uintptr_t>(action) | 0x1);
  return action;
}

TSAction
TSContScheduleOnThread(TSCont contp, TSHRTime timeout, TSEventThread ethread)
{
  ink_release_assert(ethread != nullptr);

  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  if (ink_atomic_increment(static_cast<int *>(&i->m_event_count), 1) < 0) {
    ink_assert(!"not reached");
  }

  EThread *eth = reinterpret_cast<EThread *>(ethread);
  if (i->getThreadAffinity() == nullptr) {
    i->setThreadAffinity(eth);
  }

  TSAction action;
  if (timeout == 0) {
    action = reinterpret_cast<TSAction>(eth->schedule_imm(i));
  } else {
    action = reinterpret_cast<TSAction>(eth->schedule_in(i, HRTIME_MSECONDS(timeout)));
  }

  /* This is a hack. Should be handled in ink_types */
  action = reinterpret_cast<TSAction>(reinterpret_cast<uintptr_t>(action) | 0x1);
  return action;
}

std::vector<TSAction>
TSContScheduleOnEntirePool(TSCont contp, TSHRTime timeout, TSThreadPool tp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  /* ensure we are on a EThread */
  sdk_assert(sdk_sanity_check_null_ptr((void *)this_ethread()) == TS_SUCCESS);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  // This is to allow the continuation to be scheduled on multiple threads
  sdk_assert(i->mutex == nullptr);

  EventType etype;

  switch (tp) {
  case TS_THREAD_POOL_NET:
    etype = ET_NET;
    break;
  case TS_THREAD_POOL_TASK:
    etype = ET_TASK;
    break;
  case TS_THREAD_POOL_DNS:
    etype = ET_DNS;
    break;
  case TS_THREAD_POOL_UDP:
    etype = ET_UDP;
    break;
  default:
    etype = ET_TASK;
    break;
  }

  if (ink_atomic_increment(static_cast<int *>(&i->m_event_count), eventProcessor.thread_group[etype]._count) < 0) {
    ink_assert(!"not reached");
  }

  return eventProcessor.schedule_entire(i, HRTIME_MSECONDS(timeout), 0, etype, timeout == 0 ? EVENT_IMMEDIATE : EVENT_INTERVAL);
}

TSAction
TSContScheduleEveryOnPool(TSCont contp, TSHRTime every, TSThreadPool tp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  /* ensure we are on a EThread */
  sdk_assert(sdk_sanity_check_null_ptr((void *)this_ethread()) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  if (ink_atomic_increment(static_cast<int *>(&i->m_event_count), 1) < 0) {
    ink_assert(!"not reached");
  }

  EventType etype;

  switch (tp) {
  case TS_THREAD_POOL_NET:
    etype = ET_NET;
    break;
  case TS_THREAD_POOL_TASK:
    etype = ET_TASK;
    break;
  case TS_THREAD_POOL_DNS:
    etype = ET_DNS;
    break;
  case TS_THREAD_POOL_UDP:
    etype = ET_UDP;
    break;
  default:
    etype = ET_TASK;
    break;
  }

  TSAction action = reinterpret_cast<TSAction>(eventProcessor.schedule_every(i, HRTIME_MSECONDS(every), etype));

  /* This is a hack. Should be handled in ink_types */
  action = reinterpret_cast<TSAction>(reinterpret_cast<uintptr_t>(action) | 0x1);
  return action;
}

TSAction
TSContScheduleEveryOnThread(TSCont contp, TSHRTime every, TSEventThread ethread)
{
  ink_release_assert(ethread != nullptr);

  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  if (ink_atomic_increment(static_cast<int *>(&i->m_event_count), 1) < 0) {
    ink_assert(!"not reached");
  }

  EThread *eth = reinterpret_cast<EThread *>(ethread);
  if (i->getThreadAffinity() == nullptr) {
    i->setThreadAffinity(eth);
  }

  TSAction action = reinterpret_cast<TSAction>(eth->schedule_every(i, HRTIME_MSECONDS(every)));

  /* This is a hack. Should be handled in ink_types */
  action = reinterpret_cast<TSAction>(reinterpret_cast<uintptr_t>(action) | 0x1);
  return action;
}

std::vector<TSAction>
TSContScheduleEveryOnEntirePool(TSCont contp, TSHRTime every, TSThreadPool tp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  /* ensure we are on a EThread */
  sdk_assert(sdk_sanity_check_null_ptr((void *)this_ethread()) == TS_SUCCESS);

  sdk_assert(every != 0);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  // This is to allow the continuation to be scheduled on multiple threads
  sdk_assert(i->mutex == nullptr);

  EventType etype;

  switch (tp) {
  case TS_THREAD_POOL_NET:
    etype = ET_NET;
    break;
  case TS_THREAD_POOL_TASK:
    etype = ET_TASK;
    break;
  case TS_THREAD_POOL_DNS:
    etype = ET_DNS;
    break;
  case TS_THREAD_POOL_UDP:
    etype = ET_UDP;
    break;
  default:
    etype = ET_TASK;
    break;
  }

  if (ink_atomic_increment(static_cast<int *>(&i->m_event_count), eventProcessor.thread_group[etype]._count) < 0) {
    ink_assert(!"not reached");
  }

  return eventProcessor.schedule_entire(i, 0, HRTIME_MSECONDS(every), etype, EVENT_INTERVAL);
}

TSReturnCode
TSContThreadAffinitySet(TSCont contp, TSEventThread ethread)
{
  ink_release_assert(ethread != nullptr);

  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  INKContInternal *i               = reinterpret_cast<INKContInternal *>(contp);
  EThread         *thread_affinity = reinterpret_cast<EThread *>(ethread);

  if (i->setThreadAffinity(thread_affinity)) {
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

TSEventThread
TSContThreadAffinityGet(TSCont contp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  return reinterpret_cast<TSEventThread>(i->getThreadAffinity());
}

void
TSContThreadAffinityClear(TSCont contp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  i->clearThreadAffinity();
}

TSAction
TSHttpSchedule(TSCont contp, TSHttpTxn txnp, TSHRTime timeout)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  if (ink_atomic_increment(&i->m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }

  TSAction      action;
  Continuation *cont = reinterpret_cast<Continuation *>(contp);
  HttpSM       *sm   = reinterpret_cast<HttpSM *>(txnp);

  sm->set_http_schedule(cont);

  if (timeout == 0) {
    action = reinterpret_cast<TSAction>(eventProcessor.schedule_imm(sm, ET_NET));
  } else {
    action = reinterpret_cast<TSAction>(eventProcessor.schedule_in(sm, HRTIME_MSECONDS(timeout), ET_NET));
  }

  action = reinterpret_cast<TSAction>(reinterpret_cast<uintptr_t>(action) | 0x1);
  return action;
}

int
TSContCall(TSCont contp, TSEvent event, void *edata)
{
  Continuation *c = reinterpret_cast<Continuation *>(contp);
  WEAK_MUTEX_TRY_LOCK(lock, c->mutex, this_ethread());
  if (!lock.is_locked()) {
    // If we cannot get the lock, the caller needs to restructure to handle rescheduling
    ink_release_assert(0);
  }
  return c->handleEvent(static_cast<int>(event), edata);
}

TSMutex
TSContMutexGet(TSCont contp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  Continuation *c = reinterpret_cast<Continuation *>(contp);
  return reinterpret_cast<TSMutex>(c->mutex.get());
}

/* HTTP hooks */

void
TSHttpHookAdd(TSHttpHookID id, TSCont contp)
{
  INKContInternal *icontp;
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_hook_id(id) == TS_SUCCESS);

  icontp = reinterpret_cast<INKContInternal *>(contp);

  TSSslHookInternalID internalId{id};
  if (internalId.is_in_bounds()) {
    SSLAPIHooks::instance()->append(internalId, icontp);
  } else { // Follow through the regular HTTP hook framework
    http_global_hooks->append(id, icontp);
  }
}

void
TSLifecycleHookAdd(TSLifecycleHookID id, TSCont contp)
{
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_lifecycle_hook_id(id) == TS_SUCCESS);

  g_lifecycle_hooks->append(id, reinterpret_cast<INKContInternal *>(contp));
}

/* HTTP sessions */
void
TSHttpSsnHookAdd(TSHttpSsn ssnp, TSHttpHookID id, TSCont contp)
{
  sdk_assert(sdk_sanity_check_http_ssn(ssnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_hook_id(id) == TS_SUCCESS);

  ProxySession *cs = reinterpret_cast<ProxySession *>(ssnp);
  cs->hook_add(id, reinterpret_cast<INKContInternal *>(contp));
}

int
TSHttpSsnTransactionCount(TSHttpSsn ssnp)
{
  sdk_assert(sdk_sanity_check_http_ssn(ssnp) == TS_SUCCESS);

  ProxySession *cs = reinterpret_cast<ProxySession *>(ssnp);
  return cs->get_transact_count();
}

TSVConn
TSHttpSsnClientVConnGet(TSHttpSsn ssnp)
{
  ProxySession *cs = reinterpret_cast<ProxySession *>(ssnp);
  return reinterpret_cast<TSVConn>(cs->get_netvc());
}

TSVConn
TSHttpSsnServerVConnGet(TSHttpSsn ssnp)
{
  PoolableSession *ss = reinterpret_cast<PoolableSession *>(ssnp);
  if (ss != nullptr) {
    return reinterpret_cast<TSVConn>(ss->get_netvc());
  }
  return nullptr;
}

TSVConn
TSHttpTxnServerVConnGet(TSHttpTxn txnp)
{
  TSVConn vconn = nullptr;
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  if (sm != nullptr) {
    ProxyTransaction *st = sm->get_server_txn();
    if (st != nullptr) {
      vconn = reinterpret_cast<TSVConn>(st->get_netvc());
    }
  }
  return vconn;
}

class TSHttpSsnCallback : public Continuation
{
public:
  TSHttpSsnCallback(ProxySession *cs, Ptr<ProxyMutex> m, TSEvent event) : Continuation(m), m_cs(cs), m_event(event)
  {
    SET_HANDLER(&TSHttpSsnCallback::event_handler);
  }

  int
  event_handler(int, void *)
  {
    // The current continuation is associated with the nethandler mutex.
    // We need to hold the nethandler mutex because the later Session logic may
    // activate the nethandler add_to_queue logic
    // Need to make sure we have the ProxySession mutex as well.
    EThread *eth = this_ethread();
    MUTEX_TRY_LOCK(trylock, m_cs->mutex, eth);
    if (!trylock.is_locked()) {
      eth->schedule_imm(this);
    } else {
      m_cs->handleEvent(static_cast<int>(m_event), nullptr);
      delete this;
    }
    return 0;
  }

private:
  ProxySession *m_cs;
  TSEvent       m_event;
};

void
TSHttpSsnReenable(TSHttpSsn ssnp, TSEvent event)
{
  sdk_assert(sdk_sanity_check_http_ssn(ssnp) == TS_SUCCESS);

  ProxySession *cs  = reinterpret_cast<ProxySession *>(ssnp);
  EThread      *eth = this_ethread();

  // If this function is being executed on a thread created by the API
  // which is DEDICATED, the continuation needs to be called back on a
  // REGULAR thread. Specially an ET_NET thread
  if (!eth->is_event_type(ET_NET)) {
    EThread *affinity_thread = cs->getThreadAffinity();
    if (affinity_thread && affinity_thread->is_event_type(ET_NET)) {
      NetHandler *nh = get_NetHandler(affinity_thread);
      affinity_thread->schedule_imm(new TSHttpSsnCallback(cs, nh->mutex, event), ET_NET);
    } else {
      eventProcessor.schedule_imm(new TSHttpSsnCallback(cs, cs->mutex, event), ET_NET);
    }
  } else {
    MUTEX_TRY_LOCK(trylock, cs->mutex, eth);
    if (!trylock.is_locked()) {
      EThread *affinity_thread = cs->getThreadAffinity();
      if (affinity_thread && affinity_thread->is_event_type(ET_NET)) {
        NetHandler *nh = get_NetHandler(affinity_thread);
        affinity_thread->schedule_imm(new TSHttpSsnCallback(cs, nh->mutex, event), ET_NET);
      } else {
        eventProcessor.schedule_imm(new TSHttpSsnCallback(cs, cs->mutex, event), ET_NET);
      }
    } else {
      cs->handleEvent(static_cast<int>(event), nullptr);
    }
  }
}

/* HTTP transactions */
void
TSHttpTxnHookAdd(TSHttpTxn txnp, TSHttpHookID id, TSCont contp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_hook_id(id) == TS_SUCCESS);

  HttpSM  *sm   = reinterpret_cast<HttpSM *>(txnp);
  APIHook *hook = sm->txn_hook_get(id);

  // Traverse list of hooks and add a particular hook only once
  while (hook != nullptr) {
    if (hook->m_cont == reinterpret_cast<INKContInternal *>(contp)) {
      return;
    }
    hook = hook->m_link.next;
  }
  sm->txn_hook_add(id, reinterpret_cast<INKContInternal *>(contp));
}

TSHttpSsn
TSHttpTxnSsnGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return reinterpret_cast<TSHttpSsn>(sm->get_ua_txn() ? reinterpret_cast<TSHttpSsn>(sm->get_ua_txn()->get_proxy_ssn()) : nullptr);
}

TSReturnCode
TSHttpTxnClientReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)obj) == TS_SUCCESS);

  HttpSM  *sm   = reinterpret_cast<HttpSM *>(txnp);
  HTTPHdr *hptr = &(sm->t_state.hdr_info.client_request);

  if (hptr->valid()) {
    *(reinterpret_cast<HTTPHdr **>(bufp)) = hptr;
    *obj                                  = reinterpret_cast<TSMLoc>(hptr->m_http);
    if (sdk_sanity_check_mbuffer(*bufp) == TS_SUCCESS) {
      hptr->mark_target_dirty();
      return TS_SUCCESS;
      ;
    }
  }
  return TS_ERROR;
}

// pristine url is the url before remap
TSReturnCode
TSHttpTxnPristineUrlGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *url_loc)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)url_loc) == TS_SUCCESS);

  HttpSM  *sm   = reinterpret_cast<HttpSM *>(txnp);
  HTTPHdr *hptr = &(sm->t_state.hdr_info.client_request);

  if (hptr->valid()) {
    *(reinterpret_cast<HTTPHdr **>(bufp)) = hptr;
    *url_loc                              = reinterpret_cast<TSMLoc>(sm->t_state.unmapped_url.m_url_impl);

    if (sdk_sanity_check_mbuffer(*bufp) == TS_SUCCESS) {
      if (*url_loc == nullptr) {
        *url_loc = reinterpret_cast<TSMLoc>(hptr->m_http->u.req.m_url_impl);
      }
      if (*url_loc) {
        return TS_SUCCESS;
      }
    }
  }
  return TS_ERROR;
}

int
TSHttpTxnServerSsnTransactionCount(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  // Any value greater than zero indicates connection reuse.
  return sm->server_transact_count;
}

// Shortcut to just get the URL.
// The caller is responsible to free memory that is allocated for the string
// that is returned.
char *
TSHttpTxnEffectiveUrlStringGet(TSHttpTxn txnp, int *length)
{
  sdk_assert(TS_SUCCESS == sdk_sanity_check_txn(txnp));
  sdk_assert(sdk_sanity_check_null_ptr((void *)length) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->t_state.hdr_info.client_request.url_string_get(nullptr, length);
}

TSReturnCode
TSHttpHdrEffectiveUrlBufGet(TSMBuffer hdr_buf, TSMLoc hdr_loc, char *buf, int64_t size, int64_t *length)
{
  sdk_assert(sdk_sanity_check_mbuffer(hdr_buf) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_http_hdr_handle(hdr_loc) == TS_SUCCESS);
  if (size) {
    sdk_assert(sdk_sanity_check_null_ptr(buf) == TS_SUCCESS);
  }
  sdk_assert(sdk_sanity_check_null_ptr(length) == TS_SUCCESS);

  auto buf_handle = reinterpret_cast<HTTPHdr *>(hdr_buf);
  auto hdr_handle = reinterpret_cast<HTTPHdrImpl *>(hdr_loc);

  if (hdr_handle->m_polarity != HTTPType::REQUEST) {
    Dbg(dbg_ctl_plugin, "Trying to get a URL from response header %p", hdr_loc);
    return TS_ERROR;
  }

  int url_length = buf_handle->url_printed_length(URLNormalize::LC_SCHEME_HOST | URLNormalize::IMPLIED_SCHEME);

  sdk_assert(url_length >= 0);

  *length = url_length;

  // If the user-provided buffer is too small to hold the URL string, do not put anything in it.  This is not considered
  // an error case.
  //
  if (url_length <= size) {
    int index  = 0;
    int offset = 0;

    buf_handle->url_print(buf, size, &index, &offset, URLNormalize::LC_SCHEME_HOST | URLNormalize::IMPLIED_SCHEME);
  }

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnClientRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)obj) == TS_SUCCESS);

  HttpSM  *sm   = reinterpret_cast<HttpSM *>(txnp);
  HTTPHdr *hptr = &(sm->t_state.hdr_info.client_response);

  if (hptr->valid()) {
    *(reinterpret_cast<HTTPHdr **>(bufp)) = hptr;
    *obj                                  = reinterpret_cast<TSMLoc>(hptr->m_http);
    sdk_assert(sdk_sanity_check_mbuffer(*bufp) == TS_SUCCESS);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSHttpTxnServerReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)obj) == TS_SUCCESS);

  HttpSM  *sm   = reinterpret_cast<HttpSM *>(txnp);
  HTTPHdr *hptr = &(sm->t_state.hdr_info.server_request);

  if (hptr->valid()) {
    *(reinterpret_cast<HTTPHdr **>(bufp)) = hptr;
    *obj                                  = reinterpret_cast<TSMLoc>(hptr->m_http);
    sdk_assert(sdk_sanity_check_mbuffer(*bufp) == TS_SUCCESS);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSHttpTxnServerRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)obj) == TS_SUCCESS);

  HttpSM  *sm   = reinterpret_cast<HttpSM *>(txnp);
  HTTPHdr *hptr = &(sm->t_state.hdr_info.server_response);

  if (hptr->valid()) {
    *(reinterpret_cast<HTTPHdr **>(bufp)) = hptr;
    *obj                                  = reinterpret_cast<TSMLoc>(hptr->m_http);
    sdk_assert(sdk_sanity_check_mbuffer(*bufp) == TS_SUCCESS);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSHttpTxnCachedReqGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)obj) == TS_SUCCESS);

  HttpSM   *sm         = reinterpret_cast<HttpSM *>(txnp);
  HTTPInfo *cached_obj = sm->t_state.cache_info.object_read;

  // The following check is need to prevent the HttpSM handle copy from going bad
  // Since the cache manages the header buffer, sm->t_state.cache_info.object_read
  // is the only way to tell if handle has gone bad.
  if ((!cached_obj) || (!cached_obj->valid())) {
    return TS_ERROR;
  }

  HTTPHdr *cached_hdr = sm->t_state.cache_info.object_read->request_get();

  if (!cached_hdr->valid()) {
    return TS_ERROR;
  }
  // We can't use the HdrHeapSDKHandle structure in the RamCache since multiple
  // threads can access. We need to create our own for the transaction and return that.
  HdrHeapSDKHandle **handle = &(sm->t_state.cache_req_hdr_heap_handle);

  if (*handle == nullptr) {
    *handle           = static_cast<HdrHeapSDKHandle *>(sm->t_state.arena.alloc(sizeof(HdrHeapSDKHandle)));
    (*handle)->m_heap = cached_hdr->m_heap;
  }

  *(reinterpret_cast<HdrHeapSDKHandle **>(bufp)) = *handle;
  *obj                                           = reinterpret_cast<TSMLoc>(cached_hdr->m_http);
  sdk_assert(sdk_sanity_check_mbuffer(*bufp) == TS_SUCCESS);

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnCachedRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)obj) == TS_SUCCESS);

  HttpSM   *sm         = reinterpret_cast<HttpSM *>(txnp);
  HTTPInfo *cached_obj = sm->t_state.cache_info.object_read;

  // The following check is need to prevent the HttpSM handle copy from going bad
  // Since the cache manages the header buffer, sm->t_state.cache_info.object_read
  // is the only way to tell if handle has gone bad.
  if ((!cached_obj) || (!cached_obj->valid())) {
    return TS_ERROR;
  }

  HTTPHdr *cached_hdr = sm->t_state.cache_info.object_read->response_get();

  if (!cached_hdr->valid()) {
    return TS_ERROR;
  }
  // We can't use the HdrHeapSDKHandle structure in the RamCache since multiple
  //  threads can access.  We need to create our own for the transaction and return that.
  HdrHeapSDKHandle **handle = &(sm->t_state.cache_resp_hdr_heap_handle);

  if (*handle == nullptr) {
    *handle = static_cast<HdrHeapSDKHandle *>(sm->t_state.arena.alloc(sizeof(HdrHeapSDKHandle)));
  }
  // Always reset the m_heap to make sure the heap is not stale
  (*handle)->m_heap = cached_hdr->m_heap;

  *(reinterpret_cast<HdrHeapSDKHandle **>(bufp)) = *handle;
  *obj                                           = reinterpret_cast<TSMLoc>(cached_hdr->m_http);
  sdk_assert(sdk_sanity_check_mbuffer(*bufp) == TS_SUCCESS);

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnCachedRespModifiableGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)obj) == TS_SUCCESS);

  HttpSM              *sm               = reinterpret_cast<HttpSM *>(txnp);
  HttpTransact::State *s                = &(sm->t_state);
  HTTPHdr             *c_resp           = nullptr;
  HTTPInfo            *cached_obj       = sm->t_state.cache_info.object_read;
  HTTPInfo            *cached_obj_store = &(sm->t_state.cache_info.object_store);

  if ((!cached_obj) || (!cached_obj->valid())) {
    return TS_ERROR;
  }

  if (!cached_obj_store->valid()) {
    cached_obj_store->create();
  }

  c_resp = cached_obj_store->response_get();
  if (!c_resp->valid()) {
    cached_obj_store->response_set(cached_obj->response_get());
  }
  c_resp                        = cached_obj_store->response_get();
  s->api_modifiable_cached_resp = true;

  ink_assert(c_resp != nullptr && c_resp->valid());
  *(reinterpret_cast<HTTPHdr **>(bufp)) = c_resp;
  *obj                                  = reinterpret_cast<TSMLoc>(c_resp->m_http);
  sdk_assert(sdk_sanity_check_mbuffer(*bufp) == TS_SUCCESS);

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnCacheLookupStatusGet(TSHttpTxn txnp, int *lookup_status)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)lookup_status) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  switch (sm->t_state.cache_lookup_result) {
  case HttpTransact::CacheLookupResult_t::MISS:
  case HttpTransact::CacheLookupResult_t::DOC_BUSY:
    *lookup_status = TS_CACHE_LOOKUP_MISS;
    break;
  case HttpTransact::CacheLookupResult_t::HIT_STALE:
    *lookup_status = TS_CACHE_LOOKUP_HIT_STALE;
    break;
  case HttpTransact::CacheLookupResult_t::HIT_WARNING:
  case HttpTransact::CacheLookupResult_t::HIT_FRESH:
    *lookup_status = TS_CACHE_LOOKUP_HIT_FRESH;
    break;
  case HttpTransact::CacheLookupResult_t::SKIPPED:
    *lookup_status = TS_CACHE_LOOKUP_SKIPPED;
    break;
  case HttpTransact::CacheLookupResult_t::NONE:
  default:
    return TS_ERROR;
  };
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnCacheLookupCountGet(TSHttpTxn txnp, int *lookup_count)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)lookup_count) == TS_SUCCESS);

  HttpSM *sm    = reinterpret_cast<HttpSM *>(txnp);
  *lookup_count = sm->t_state.cache_info.lookup_count;
  return TS_SUCCESS;
}

/* two hooks this function may gets called:
   TS_HTTP_READ_CACHE_HDR_HOOK   &
   TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK
 */
TSReturnCode
TSHttpTxnCacheLookupStatusSet(TSHttpTxn txnp, int cachelookup)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM                            *sm        = reinterpret_cast<HttpSM *>(txnp);
  HttpTransact::CacheLookupResult_t *sm_status = &(sm->t_state.cache_lookup_result);

  // converting from a miss to a hit is not allowed
  if (*sm_status == HttpTransact::CacheLookupResult_t::MISS && cachelookup != TS_CACHE_LOOKUP_MISS) {
    return TS_ERROR;
  }

  // here is to handle converting a hit to a miss
  if (cachelookup == TS_CACHE_LOOKUP_MISS && *sm_status != HttpTransact::CacheLookupResult_t::MISS) {
    sm->t_state.api_cleanup_cache_read = true;
    ink_assert(sm->t_state.transact_return_point != nullptr);
    sm->t_state.transact_return_point = HttpTransact::HandleCacheOpenRead;
  }

  switch (cachelookup) {
  case TS_CACHE_LOOKUP_MISS:
    *sm_status = HttpTransact::CacheLookupResult_t::MISS;
    break;
  case TS_CACHE_LOOKUP_HIT_STALE:
    *sm_status = HttpTransact::CacheLookupResult_t::HIT_STALE;
    break;
  case TS_CACHE_LOOKUP_HIT_FRESH:
    *sm_status = HttpTransact::CacheLookupResult_t::HIT_FRESH;
    break;
  default:
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnInfoIntGet(TSHttpTxn txnp, TSHttpTxnInfoKey key, TSMgmtInt *value)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)value) == TS_SUCCESS);

  HttpSM      *s    = reinterpret_cast<HttpSM *>(txnp);
  HttpCacheSM *c_sm = &(s->get_cache_sm());

  switch (key) {
  case TS_TXN_INFO_CACHE_HIT_RAM:
    *value = (static_cast<TSMgmtInt>(c_sm->is_ram_cache_hit()));
    break;
  case TS_TXN_INFO_CACHE_COMPRESSED_IN_RAM:
    *value = (static_cast<TSMgmtInt>(c_sm->is_compressed_in_ram()));
    break;
  case TS_TXN_INFO_CACHE_HIT_RWW:
    *value = (static_cast<TSMgmtInt>(c_sm->is_readwhilewrite_inprogress()));
    break;
  case TS_TXN_INFO_CACHE_OPEN_READ_TRIES:
    *value = (static_cast<TSMgmtInt>(c_sm->get_open_read_tries()));
    break;
  case TS_TXN_INFO_CACHE_OPEN_WRITE_TRIES:
    *value = (static_cast<TSMgmtInt>(c_sm->get_open_write_tries()));
    break;
  case TS_TXN_INFO_CACHE_VOLUME:
    *value = (static_cast<TSMgmtInt>(c_sm->get_volume_number()));
    break;
  default:
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
TSHttpSsnInfoIntGet(TSHttpSsn ssnp, TSHttpSsnInfoKey key, TSMgmtInt *value, uint64_t sub_key)
{
  sdk_assert(sdk_sanity_check_http_ssn(ssnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)value) == TS_SUCCESS);

  ProxySession *ssn = reinterpret_cast<ProxySession *>(ssnp);

  switch (key) {
  case TS_SSN_INFO_TRANSACTION_COUNT:
    *value = ssn->get_transact_count();
    break;
  case TS_SSN_INFO_RECEIVED_FRAME_COUNT:
    if (!ssn->is_protocol_framed()) {
      return TS_ERROR;
    }
    *value = ssn->get_received_frame_count(sub_key);
    break;
  default:
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

int
TSHttpTxnIsWebsocket(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->t_state.is_websocket;
}

const char *
TSHttpTxnCacheDiskPathGet(TSHttpTxn txnp, int *length)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM     *sm   = reinterpret_cast<HttpSM *>(txnp);
  char const *path = nullptr;

  if (HttpCacheSM *c_sm = &(sm->get_cache_sm()); c_sm) {
    path = c_sm->get_disk_path();
  }
  if (length) {
    *length = path ? strlen(path) : 0;
  }

  return path;
}

TSReturnCode
TSHttpTxnCacheLookupUrlGet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  URL     u, *l_url;

  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);
  if (!u.valid()) {
    return TS_ERROR;
  }

  l_url = sm->t_state.cache_info.lookup_url;
  if (l_url && l_url->valid()) {
    u.copy(l_url);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSHttpTxnCacheLookupUrlSet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  URL     u, *l_url;

  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);
  if (!u.valid()) {
    return TS_ERROR;
  }

  l_url = sm->t_state.cache_info.lookup_url;
  if (!l_url) {
    sm->t_state.cache_info.lookup_url_storage.create(nullptr);
    sm->t_state.cache_info.lookup_url = &(sm->t_state.cache_info.lookup_url_storage);
    l_url                             = sm->t_state.cache_info.lookup_url;
  }

  if (!l_url || !l_url->valid()) {
    return TS_ERROR;
  } else {
    l_url->copy(&u);
  }

  return TS_SUCCESS;
}

/**
 * timeout is in msec
 * overrides as proxy.config.http.transaction_active_timeout_out
 **/
void
TSHttpTxnActiveTimeoutSet(TSHttpTxn txnp, int timeout)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpTransact::State *s          = &((reinterpret_cast<HttpSM *>(txnp))->t_state);
  s->api_txn_active_timeout_value = timeout;
}

/**
 * timeout is in msec
 * overrides as proxy.config.http.connect_attempts_timeout
 **/
void
TSHttpTxnConnectTimeoutSet(TSHttpTxn txnp, int timeout)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpTransact::State *s           = &((reinterpret_cast<HttpSM *>(txnp))->t_state);
  s->api_txn_connect_timeout_value = timeout;
}

/**
 * timeout is in msec
 * overrides as proxy.config.dns.lookup_timeout
 **/
void
TSHttpTxnDNSTimeoutSet(TSHttpTxn txnp, int timeout)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpTransact::State *s = &((reinterpret_cast<HttpSM *>(txnp))->t_state);

  s->api_txn_dns_timeout_value = timeout;
}

/**
 * timeout is in msec
 * overrides as proxy.config.http.transaction_no_activity_timeout_out
 **/
void
TSHttpTxnNoActivityTimeoutSet(TSHttpTxn txnp, int timeout)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpTransact::State *s               = &((reinterpret_cast<HttpSM *>(txnp))->t_state);
  s->api_txn_no_activity_timeout_value = timeout;
}

TSReturnCode
TSHttpTxnServerRespNoStoreSet(TSHttpTxn txnp, int flag)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpTransact::State *s          = &((reinterpret_cast<HttpSM *>(txnp))->t_state);
  s->api_server_response_no_store = (flag != 0);

  return TS_SUCCESS;
}

bool
TSHttpTxnServerRespNoStoreGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpTransact::State *s = &((reinterpret_cast<HttpSM *>(txnp))->t_state);
  return s->api_server_response_no_store;
}

TSReturnCode
TSHttpTxnServerRespIgnore(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpTransact::State *s          = &((reinterpret_cast<HttpSM *>(txnp))->t_state);
  HTTPInfo            *cached_obj = s->cache_info.object_read;
  HTTPHdr             *cached_resp;

  if (cached_obj == nullptr || !cached_obj->valid()) {
    return TS_ERROR;
  }

  cached_resp = cached_obj->response_get();
  if (cached_resp == nullptr || !cached_resp->valid()) {
    return TS_ERROR;
  }

  s->api_server_response_ignore = true;

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnShutDown(TSHttpTxn txnp, TSEvent event)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  if (event == TS_EVENT_HTTP_TXN_CLOSE) {
    return TS_ERROR;
  }

  HttpTransact::State *s  = &((reinterpret_cast<HttpSM *>(txnp))->t_state);
  s->api_http_sm_shutdown = true;

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnAborted(TSHttpTxn txnp, bool *client_abort)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(client_abort != nullptr);

  *client_abort = false;
  HttpSM *sm    = reinterpret_cast<HttpSM *>(txnp);
  switch (sm->t_state.squid_codes.log_code) {
  case SquidLogCode::ERR_CLIENT_ABORT:
  case SquidLogCode::ERR_CLIENT_READ_ERROR:
  case SquidLogCode::TCP_SWAPFAIL:
    // check for client abort and cache read error
    *client_abort = true;
    return TS_SUCCESS;
  default:
    break;
  }

  if (sm->t_state.current.server && sm->t_state.current.server->abort == HttpTransact::ABORTED) {
    // check for the server abort
    return TS_SUCCESS;
  }
  // there can be the case of transformation error.
  return TS_ERROR;
}

void
TSHttpTxnReqCacheableSet(TSHttpTxn txnp, int flag)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm                    = reinterpret_cast<HttpSM *>(txnp);
  sm->t_state.api_req_cacheable = (flag != 0);
}

void
TSHttpTxnRespCacheableSet(TSHttpTxn txnp, int flag)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm                     = reinterpret_cast<HttpSM *>(txnp);
  sm->t_state.api_resp_cacheable = (flag != 0);
}

int
TSHttpTxnClientReqIsServerStyle(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return (sm->t_state.hdr_info.client_req_is_server_style ? 1 : 0);
}

TSReturnCode
TSHttpTxnUpdateCachedObject(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM              *sm               = reinterpret_cast<HttpSM *>(txnp);
  HttpTransact::State *s                = &(sm->t_state);
  HTTPInfo            *cached_obj_store = &(sm->t_state.cache_info.object_store);
  HTTPHdr             *client_request   = &(sm->t_state.hdr_info.client_request);

  if (!cached_obj_store->valid() || !cached_obj_store->response_get()) {
    return TS_ERROR;
  }

  if (!cached_obj_store->request_get() && !client_request->valid()) {
    return TS_ERROR;
  }

  if (s->cache_info.write_lock_state == HttpTransact::CacheWriteLock_t::READ_RETRY) {
    return TS_ERROR;
  }

  s->api_update_cached_object = HttpTransact::UpdateCachedObject_t::PREPARE;
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnTransformRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM  *sm   = reinterpret_cast<HttpSM *>(txnp);
  HTTPHdr *hptr = &(sm->t_state.hdr_info.transform_response);

  if (hptr->valid()) {
    *(reinterpret_cast<HTTPHdr **>(bufp)) = hptr;
    *obj                                  = reinterpret_cast<TSMLoc>(hptr->m_http);
    return sdk_sanity_check_mbuffer(*bufp);
  }

  return TS_ERROR;
}

sockaddr const *
TSHttpSsnClientAddrGet(TSHttpSsn ssnp)
{
  ProxySession *cs = reinterpret_cast<ProxySession *>(ssnp);

  if (cs == nullptr) {
    return nullptr;
  }
  return cs->get_remote_addr();
}
sockaddr const *
TSHttpTxnClientAddrGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  TSHttpSsn ssnp = TSHttpTxnSsnGet(txnp);
  return TSHttpSsnClientAddrGet(ssnp);
}

sockaddr const *
TSHttpSsnIncomingAddrGet(TSHttpSsn ssnp)
{
  ProxySession *cs = reinterpret_cast<ProxySession *>(ssnp);

  if (cs == nullptr) {
    return nullptr;
  }
  return cs->get_local_addr();
}
sockaddr const *
TSHttpTxnIncomingAddrGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  TSHttpSsn ssnp = TSHttpTxnSsnGet(txnp);
  return TSHttpSsnIncomingAddrGet(ssnp);
}

sockaddr const *
TSHttpTxnOutgoingAddrGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  const sockaddr   *retval = nullptr;
  NetVConnection   *vc     = nullptr;
  ProxyTransaction *ssn    = sm->get_server_txn();
  if (ssn == nullptr) {
    vc = sm->get_server_vc();
  } else {
    vc = ssn->get_netvc();
  }
  if (vc != nullptr) {
    retval = vc->get_local_addr();
  }
  return retval;
}

sockaddr const *
TSHttpTxnServerAddrGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return &sm->t_state.server_info.dst_addr.sa;
}

TSReturnCode
TSHttpTxnServerAddrSet(TSHttpTxn txnp, struct sockaddr const *addr)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  if (sm->t_state.dns_info.set_upstream_address(addr)) {
    sm->t_state.dns_info.os_addr_style = ResolveInfo::OS_Addr::USE_API;
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

void
TSHttpTxnClientIncomingPortSet(TSHttpTxn txnp, int port)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm                                            = reinterpret_cast<HttpSM *>(txnp);
  sm->t_state.client_info.dst_addr.network_order_port() = htons(port);
}

// [amc] This might use the port. The code path should do that but it
// hasn't been tested.
TSReturnCode
TSHttpTxnOutgoingAddrSet(TSHttpTxn txnp, const struct sockaddr *addr)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  sm->get_ua_txn()->upstream_outbound_options.outbound_port = ats_ip_port_host_order(addr);
  sm->get_ua_txn()->set_outbound_ip(swoc::IPAddr(addr));
  return TS_SUCCESS;
}

sockaddr const *
TSHttpTxnNextHopAddrGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  /**
   * Return zero if the server structure is not yet constructed.
   */
  if (sm->t_state.current.server == nullptr) {
    return nullptr;
  }

  return &sm->t_state.current.server->dst_addr.sa;
}

const char *
TSHttpTxnNextHopNameGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  /**
   * Return zero if the server structure is not yet constructed.
   */
  if (sm->t_state.current.server == nullptr) {
    return nullptr;
  }

  return sm->t_state.current.server->name;
}

int
TSHttpTxnNextHopPortGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  auto sm = reinterpret_cast<HttpSM const *>(txnp);
  /**
   * Return -1 if the server structure is not yet constructed.
   */
  if (nullptr == sm->t_state.current.server) {
    return -1;
  }
  return sm->t_state.current.server->dst_addr.host_order_port();
}

TSReturnCode
TSHttpTxnOutgoingTransparencySet(TSHttpTxn txnp, int flag)
{
  if (TS_SUCCESS != sdk_sanity_check_txn(txnp)) {
    return TS_ERROR;
  }

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  if (nullptr == sm || nullptr == sm->get_ua_txn()) {
    return TS_ERROR;
  }

  sm->get_ua_txn()->set_outbound_transparent(flag);
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnClientPacketMarkSet(TSHttpTxn txnp, int mark)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  if (nullptr == sm->get_ua_txn()) {
    return TS_ERROR;
  }

  NetVConnection *vc = sm->get_ua_txn()->get_netvc();
  if (nullptr == vc) {
    return TS_ERROR;
  }

  vc->options.packet_mark = static_cast<uint32_t>(mark);
  vc->apply_options();
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnServerPacketMarkSet(TSHttpTxn txnp, int mark)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  // change the mark on an active server session
  ProxyTransaction *ssn = sm->get_server_txn();
  if (nullptr != ssn) {
    NetVConnection *vc = ssn->get_netvc();
    if (vc != nullptr) {
      vc->options.packet_mark = static_cast<uint32_t>(mark);
      vc->apply_options();
    }
  }

  // update the transactions mark config for future connections
  TSHttpTxnConfigIntSet(txnp, TS_CONFIG_NET_SOCK_PACKET_MARK_OUT, mark);
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnClientPacketDscpSet(TSHttpTxn txnp, int dscp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  if (nullptr == sm->get_ua_txn()) {
    return TS_ERROR;
  }

  NetVConnection *vc = sm->get_ua_txn()->get_netvc();
  if (nullptr == vc) {
    return TS_ERROR;
  }

  vc->options.packet_tos = static_cast<uint32_t>(dscp) << 2;
  vc->apply_options();
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnServerPacketDscpSet(TSHttpTxn txnp, int dscp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  // change the tos on an active server session
  ProxyTransaction *ssn = sm->get_server_txn();
  if (nullptr != ssn) {
    NetVConnection *vc = ssn->get_netvc();
    if (vc != nullptr) {
      vc->options.packet_tos = static_cast<uint32_t>(dscp) << 2;
      vc->apply_options();
    }
  }

  // update the transactions mark config for future connections
  TSHttpTxnConfigIntSet(txnp, TS_CONFIG_NET_SOCK_PACKET_TOS_OUT, dscp << 2);
  return TS_SUCCESS;
}

// Set the body, or, if you provide a null buffer, clear the body message
void
TSHttpTxnErrorBodySet(TSHttpTxn txnp, char *buf, size_t buflength, char *mimetype)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM              *sm = reinterpret_cast<HttpSM *>(txnp);
  HttpTransact::State *s  = &(sm->t_state);

  // Cleanup anything already set.
  s->free_internal_msg_buffer();
  ats_free(s->internal_msg_buffer_type);

  s->internal_msg_buffer                     = buf;
  s->internal_msg_buffer_size                = buf ? buflength : 0;
  s->internal_msg_buffer_fast_allocator_size = -1;

  s->internal_msg_buffer_type = mimetype;
}

char *
TSHttpTxnErrorBodyGet(TSHttpTxn txnp, size_t *buflength, char **mimetype)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM              *sm = reinterpret_cast<HttpSM *>(txnp);
  HttpTransact::State *s  = &(sm->t_state);

  if (buflength) {
    *buflength = s->internal_msg_buffer_size;
  }

  if (mimetype) {
    *mimetype = s->internal_msg_buffer_type;
  }

  return s->internal_msg_buffer;
}

void
TSHttpTxnServerRequestBodySet(TSHttpTxn txnp, char *buf, int64_t buflength)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM              *sm = reinterpret_cast<HttpSM *>(txnp);
  HttpTransact::State *s  = &(sm->t_state);

  // Cleanup anything already set.
  s->free_internal_msg_buffer();

  if (buf) {
    s->api_server_request_body_set = true;
    s->internal_msg_buffer         = buf;
    s->internal_msg_buffer_size    = buflength;
  } else {
    s->api_server_request_body_set = false;
    s->internal_msg_buffer         = nullptr;
    s->internal_msg_buffer_size    = 0;
  }
  s->internal_msg_buffer_fast_allocator_size = -1;
}

TSReturnCode
TSHttpTxnParentProxyGet(TSHttpTxn txnp, const char **hostname, int *port)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  *hostname = sm->t_state.api_info.parent_proxy_name;
  *port     = sm->t_state.api_info.parent_proxy_port;

  return TS_SUCCESS;
}

void
TSHttpTxnParentProxySet(TSHttpTxn txnp, const char *hostname, int port)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)hostname) == TS_SUCCESS);
  sdk_assert(port > 0);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  sm->t_state.api_info.parent_proxy_name = sm->t_state.arena.str_store(hostname, strlen(hostname));
  sm->t_state.api_info.parent_proxy_port = port;
}

TSReturnCode
TSHttpTxnParentSelectionUrlGet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  URL     u, *l_url;

  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);
  if (!u.valid()) {
    return TS_ERROR;
  }

  l_url = sm->t_state.cache_info.parent_selection_url;
  if (l_url && l_url->valid()) {
    u.copy(l_url);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSHttpTxnParentSelectionUrlSet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc obj)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_mbuffer(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_url_handle(obj) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  URL     u, *l_url;

  u.m_heap     = (reinterpret_cast<HdrHeapSDKHandle *>(bufp))->m_heap;
  u.m_url_impl = reinterpret_cast<URLImpl *>(obj);
  if (!u.valid()) {
    return TS_ERROR;
  }

  l_url = sm->t_state.cache_info.parent_selection_url;
  if (!l_url) {
    sm->t_state.cache_info.parent_selection_url_storage.create(nullptr);
    sm->t_state.cache_info.parent_selection_url = &(sm->t_state.cache_info.parent_selection_url_storage);
    l_url                                       = sm->t_state.cache_info.parent_selection_url;
  }

  if (!l_url || !l_url->valid()) {
    return TS_ERROR;
  } else {
    l_url->copy(&u);
  }

  Dbg(dbg_ctl_parent_select, "TSHttpTxnParentSelectionUrlSet() parent_selection_url : addr = %p val = %p",
      &(sm->t_state.cache_info.parent_selection_url), sm->t_state.cache_info.parent_selection_url);

  return TS_SUCCESS;
}

void
TSHttpTxnUntransformedRespCache(TSHttpTxn txnp, int on)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm                               = reinterpret_cast<HttpSM *>(txnp);
  sm->t_state.api_info.cache_untransformed = (on ? true : false);
}

void
TSHttpTxnTransformedRespCache(TSHttpTxn txnp, int on)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm                             = reinterpret_cast<HttpSM *>(txnp);
  sm->t_state.api_info.cache_transformed = (on ? true : false);
}

class TSHttpSMCallback : public Continuation
{
public:
  TSHttpSMCallback(HttpSM *sm, TSEvent event) : Continuation(sm->mutex), m_sm(sm), m_event(event)
  {
    SET_HANDLER(&TSHttpSMCallback::event_handler);
  }

  int
  event_handler(int, void *)
  {
    m_sm->state_api_callback(static_cast<int>(m_event), nullptr);
    delete this;
    return 0;
  }

private:
  HttpSM *m_sm;
  TSEvent m_event;
};

//----------------------------------------------------------------------------
void
TSHttpTxnReenable(TSHttpTxn txnp, TSEvent event)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM  *sm  = reinterpret_cast<HttpSM *>(txnp);
  EThread *eth = this_ethread();

  // TS-2271: If this function is being executed on a thread which was not
  // created using the ATS EThread API, eth will be null, and the
  // continuation needs to be called back on a REGULAR thread.
  //
  // If we are not coming from the thread associated with the state machine,
  // reschedule.  Also reschedule if we cannot get the state machine lock.
  if (eth != nullptr && sm->getThreadAffinity() == eth) {
    MUTEX_TRY_LOCK(trylock, sm->mutex, eth);
    if (trylock.is_locked()) {
      ink_assert(eth->is_event_type(ET_NET));
      sm->state_api_callback(static_cast<int>(event), nullptr);
      return;
    }
  }
  // Couldn't call the handler directly, schedule to the original SM thread
  TSHttpSMCallback *cb = new TSHttpSMCallback(sm, event);
  cb->setThreadAffinity(sm->getThreadAffinity());
  eventProcessor.schedule_imm(cb, ET_NET);
}

TSReturnCode TSUserArgIndexNameLookup(TSUserArgType type, const char *name, int *arg_idx, const char **description);

TSReturnCode
TSUserArgIndexReserve(TSUserArgType type, const char *name, const char *description, int *ptr_idx)
{
  sdk_assert(sdk_sanity_check_null_ptr(ptr_idx) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr(name) == TS_SUCCESS);
  sdk_assert(0 <= type && type < TS_USER_ARGS_COUNT);

  int idx;

  /* Since this function is meant to be called during plugin initialization we could end up "leaking" indices during plugins
   * reload. Make sure we allocate 1 index per name, also current TSUserArgIndexNameLookup() implementation assumes 1-1
   * relationship as well. */
  const char *desc;

  if (TS_SUCCESS == TSUserArgIndexNameLookup(type, name, &idx, &desc)) {
    // Found existing index.

    // No need to add get_user_arg_offset(type) here since
    // TSUserArgIndexNameLookup already does so.
    *ptr_idx = idx;
    return TS_SUCCESS;
  }

  idx       = UserArgIdx[type]++;
  int limit = MAX_USER_ARGS[type];

  if (idx < limit) {
    UserArg &arg(UserArgTable[type][idx]);
    arg.name = name;
    if (description) {
      arg.description = description;
    }
    *ptr_idx = idx + get_user_arg_offset(type);

    return TS_SUCCESS;
  }
  return TS_ERROR;
}

TSReturnCode
TSUserArgIndexLookup(TSUserArgType type, int idx, const char **name, const char **description)
{
  sdk_assert(0 <= type && type < TS_USER_ARGS_COUNT);
  sdk_assert(SanityCheckUserIndex(type, idx));
  idx -= get_user_arg_offset(type);
  if (sdk_sanity_check_null_ptr(name) == TS_SUCCESS) {
    if (idx < UserArgIdx[type]) {
      UserArg &arg(UserArgTable[type][idx]);
      *name = arg.name.c_str();
      if (description) {
        *description = arg.description.c_str();
      }
      return TS_SUCCESS;
    }
  }
  return TS_ERROR;
}

// Not particularly efficient, but good enough for now.
TSReturnCode
TSUserArgIndexNameLookup(TSUserArgType type, const char *name, int *arg_idx, const char **description)
{
  sdk_assert(sdk_sanity_check_null_ptr(arg_idx) == TS_SUCCESS);
  sdk_assert(0 <= type && type < TS_USER_ARGS_COUNT);

  std::string_view n{name};

  for (UserArg *arg = UserArgTable[type], *limit = arg + UserArgIdx[type]; arg < limit; ++arg) {
    if (arg->name == n) {
      if (description) {
        *description = arg->description.c_str();
      }
      *arg_idx = arg - UserArgTable[type] + get_user_arg_offset(type);
      return TS_SUCCESS;
    }
  }
  return TS_ERROR;
}

// -------------
void
TSUserArgSet(void *data, int arg_idx, void *arg)
{
  if (nullptr != data) {
    PluginUserArgsMixin *user_args = dynamic_cast<PluginUserArgsMixin *>(static_cast<Continuation *>(data));
    sdk_assert(user_args);

    user_args->set_user_arg(arg_idx, arg);
  } else {
    global_user_args.set_user_arg(arg_idx, arg);
  }
}

void *
TSUserArgGet(void *data, int arg_idx)
{
  if (nullptr != data) {
    PluginUserArgsMixin *user_args = dynamic_cast<PluginUserArgsMixin *>(static_cast<Continuation *>(data));
    sdk_assert(user_args);

    return user_args->get_user_arg(arg_idx);
  } else {
    return global_user_args.get_user_arg(arg_idx);
  }
}

void
TSHttpTxnStatusSet(TSHttpTxn txnp, TSHttpStatus status)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm                   = reinterpret_cast<HttpSM *>(txnp);
  sm->t_state.http_return_code = static_cast<HTTPStatus>(status);
}

TSHttpStatus
TSHttpTxnStatusGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return static_cast<TSHttpStatus>(sm->t_state.http_return_code);
}

TSReturnCode
TSHttpTxnCntlSet(TSHttpTxn txnp, TSHttpCntlType cntl, bool data)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  switch (cntl) {
  case TS_HTTP_CNTL_LOGGING_MODE:
    sm->t_state.api_info.logging_enabled = data;
    break;

  case TS_HTTP_CNTL_INTERCEPT_RETRY_MODE:
    sm->t_state.api_info.retry_intercept_failures = data;
    break;

  case TS_HTTP_CNTL_RESPONSE_CACHEABLE:
    sm->t_state.api_resp_cacheable = data;
    break;

  case TS_HTTP_CNTL_REQUEST_CACHEABLE:
    sm->t_state.api_req_cacheable = data;
    break;

  case TS_HTTP_CNTL_SERVER_NO_STORE:
    sm->t_state.api_server_response_no_store = data;
    break;

  case TS_HTTP_CNTL_TXN_DEBUG:
    sm->debug_on = data;
    break;

  case TS_HTTP_CNTL_SKIP_REMAPPING:
    sm->t_state.api_skip_all_remapping = data;
    break;

  default:
    return TS_ERROR;
    break;
  }

  return TS_SUCCESS;
}

bool
TSHttpTxnCntlGet(TSHttpTxn txnp, TSHttpCntlType ctrl)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  switch (ctrl) {
  case TS_HTTP_CNTL_LOGGING_MODE:
    return sm->t_state.api_info.logging_enabled;
    break;

  case TS_HTTP_CNTL_INTERCEPT_RETRY_MODE:
    return sm->t_state.api_info.retry_intercept_failures;
    break;

  case TS_HTTP_CNTL_RESPONSE_CACHEABLE:
    return sm->t_state.api_resp_cacheable;
    break;

  case TS_HTTP_CNTL_REQUEST_CACHEABLE:
    return sm->t_state.api_req_cacheable;
    break;

  case TS_HTTP_CNTL_SERVER_NO_STORE:
    return sm->t_state.api_server_response_no_store;
    break;

  case TS_HTTP_CNTL_TXN_DEBUG:
    return sm->debug_on;
    break;

  case TS_HTTP_CNTL_SKIP_REMAPPING:
    return sm->t_state.api_skip_all_remapping;
    break;

  default:
    break;
  }

  return false; // Unknown here, but oh well.
}

/* This is kinda horky, we have to use TSServerState instead of
   HttpTransact::ServerState_t, otherwise we have a prototype
   mismatch in the public ts/ts.h interfaces. */
TSServerState
TSHttpTxnServerStateGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpTransact::State *s = &((reinterpret_cast<HttpSM *>(txnp))->t_state);
  return static_cast<TSServerState>(s->current.state);
}

void
TSHttpTxnDebugSet(TSHttpTxn txnp, int on)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  (reinterpret_cast<HttpSM *>(txnp))->debug_on = on;
}

int
TSHttpTxnDebugGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  return (reinterpret_cast<HttpSM *>(txnp))->debug_on;
}

void
TSHttpSsnDebugSet(TSHttpSsn ssnp, int on)
{
  sdk_assert(sdk_sanity_check_http_ssn(ssnp) == TS_SUCCESS);
  (reinterpret_cast<ProxySession *>(ssnp))->set_debug(0 != on);
}

int
TSHttpSsnDebugGet(TSHttpSsn ssnp, int *on)
{
  sdk_assert(sdk_sanity_check_http_ssn(ssnp) == TS_SUCCESS);
  sdk_assert(on != nullptr);
  *on = (reinterpret_cast<ProxySession *>(ssnp))->debug();
  return TS_SUCCESS;
}

int
TSHttpTxnClientReqHdrBytesGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->client_request_hdr_bytes;
}

int64_t
TSHttpTxnClientReqBodyBytesGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->client_request_body_bytes;
}

int
TSHttpTxnServerReqHdrBytesGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->server_request_hdr_bytes;
}

int64_t
TSHttpTxnServerReqBodyBytesGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->server_request_body_bytes;
}

int
TSHttpTxnServerRespHdrBytesGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->server_response_hdr_bytes;
}

int64_t
TSHttpTxnServerRespBodyBytesGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->server_response_body_bytes;
}

int
TSHttpTxnClientRespHdrBytesGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->client_response_hdr_bytes;
}

int64_t
TSHttpTxnClientRespBodyBytesGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->client_response_body_bytes;
}

int
TSVConnIsSslReused(TSVConn sslp)
{
  NetVConnection    *vc     = reinterpret_cast<NetVConnection *>(sslp);
  SSLNetVConnection *ssl_vc = dynamic_cast<SSLNetVConnection *>(vc);

  return ssl_vc ? ssl_vc->getSSLSessionCacheHit() : 0;
}

const char *
TSVConnSslCipherGet(TSVConn sslp)
{
  NetVConnection  *vc    = reinterpret_cast<NetVConnection *>(sslp);
  TLSBasicSupport *tlsbs = vc->get_service<TLSBasicSupport>();

  return tlsbs ? tlsbs->get_tls_cipher_suite() : nullptr;
}

const char *
TSVConnSslProtocolGet(TSVConn sslp)
{
  NetVConnection  *vc    = reinterpret_cast<NetVConnection *>(sslp);
  TLSBasicSupport *tlsbs = vc->get_service<TLSBasicSupport>();

  return tlsbs ? tlsbs->get_tls_protocol_name() : nullptr;
}

const char *
TSVConnSslCurveGet(TSVConn sslp)
{
  NetVConnection  *vc    = reinterpret_cast<NetVConnection *>(sslp);
  TLSBasicSupport *tlsbs = vc->get_service<TLSBasicSupport>();

  return tlsbs ? tlsbs->get_tls_curve() : nullptr;
}

int
TSHttpTxnPushedRespHdrBytesGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->pushed_response_hdr_bytes;
}

int64_t
TSHttpTxnPushedRespBodyBytesGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->pushed_response_body_bytes;
}

// Get a particular milestone hrtime'r. Note that this can return 0, which means it has not
// been set yet.
TSReturnCode
TSHttpTxnMilestoneGet(TSHttpTxn txnp, TSMilestonesType milestone, ink_hrtime *time)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr(time) == TS_SUCCESS);
  HttpSM      *sm  = reinterpret_cast<HttpSM *>(txnp);
  TSReturnCode ret = TS_SUCCESS;

  if ((milestone < TS_MILESTONE_UA_BEGIN) || (milestone >= TS_MILESTONE_LAST_ENTRY)) {
    *time = -1;
    ret   = TS_ERROR;
  } else {
    *time = sm->milestones[milestone];
  }

  return ret;
}

TSReturnCode
TSHttpTxnCachedRespTimeGet(TSHttpTxn txnp, time_t *resp_time)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM   *sm         = reinterpret_cast<HttpSM *>(txnp);
  HTTPInfo *cached_obj = sm->t_state.cache_info.object_read;

  if (cached_obj == nullptr || !cached_obj->valid()) {
    return TS_ERROR;
  }

  *resp_time = cached_obj->response_received_time_get();
  return TS_SUCCESS;
}

int
TSHttpCurrentClientConnectionsGet()
{
  return Metrics::Gauge::load(http_rsb.current_client_connections);
}

int
TSHttpCurrentActiveClientConnectionsGet()
{
  return Metrics::Gauge::load(http_rsb.current_active_client_connections);
}

int
TSHttpCurrentIdleClientConnectionsGet()
{
  int64_t total  = Metrics::Gauge::load(http_rsb.current_client_connections);
  int64_t active = Metrics::Gauge::load(http_rsb.current_active_client_connections);

  if (total >= active) {
    return static_cast<int>(total - active);
  }

  return 0;
}

int
TSHttpCurrentCacheConnectionsGet()
{
  return Metrics::Gauge::load(http_rsb.current_cache_connections);
}

int
TSHttpCurrentServerConnectionsGet()
{
  return Metrics::Gauge::load(http_rsb.current_server_connections);
}

/* HTTP alternate selection */
TSReturnCode
TSHttpAltInfoClientReqGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_alt_info(infop) == TS_SUCCESS);

  HttpAltInfo *info = reinterpret_cast<HttpAltInfo *>(infop);

  *(reinterpret_cast<HTTPHdr **>(bufp)) = &info->m_client_req;
  *obj                                  = reinterpret_cast<TSMLoc>(info->m_client_req.m_http);

  return sdk_sanity_check_mbuffer(*bufp);
}

TSReturnCode
TSHttpAltInfoCachedReqGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_alt_info(infop) == TS_SUCCESS);

  HttpAltInfo *info = reinterpret_cast<HttpAltInfo *>(infop);

  *(reinterpret_cast<HTTPHdr **>(bufp)) = &info->m_cached_req;
  *obj                                  = reinterpret_cast<TSMLoc>(info->m_cached_req.m_http);

  return sdk_sanity_check_mbuffer(*bufp);
}

TSReturnCode
TSHttpAltInfoCachedRespGet(TSHttpAltInfo infop, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_alt_info(infop) == TS_SUCCESS);

  HttpAltInfo *info = reinterpret_cast<HttpAltInfo *>(infop);

  *(reinterpret_cast<HTTPHdr **>(bufp)) = &info->m_cached_resp;
  *obj                                  = reinterpret_cast<TSMLoc>(info->m_cached_resp.m_http);

  return sdk_sanity_check_mbuffer(*bufp);
}

void
TSHttpAltInfoQualitySet(TSHttpAltInfo infop, float quality)
{
  sdk_assert(sdk_sanity_check_alt_info(infop) == TS_SUCCESS);

  HttpAltInfo *info = reinterpret_cast<HttpAltInfo *>(infop);
  info->m_qvalue    = quality;
}

const char *
TSHttpTxnPluginTagGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->plugin_tag;
}

TSHttpConnectOptions
TSHttpConnectOptionsGet(TSConnectType connect_type)
{
  sdk_assert(connect_type > TS_CONNECT_UNDEFINED);
  sdk_assert(connect_type < TS_CONNECT_LAST_ENTRY);

  return TSHttpConnectOptions{.connect_type      = connect_type,
                              .addr              = nullptr,
                              .tag               = nullptr,
                              .id                = 0,
                              .buffer_index      = TS_IOBUFFER_SIZE_INDEX_32K,
                              .buffer_water_mark = TS_IOBUFFER_WATER_MARK_PLUGIN_VC_DEFAULT};
}

TSVConn
TSHttpConnectWithPluginId(sockaddr const *addr, const char *tag, int64_t id)
{
  TSHttpConnectOptions options = TSHttpConnectOptionsGet(TS_CONNECT_PLUGIN);
  options.addr                 = addr;
  options.tag                  = tag;
  options.id                   = id;

  return TSHttpConnectPlugin(&options);
}

TSVConn
TSHttpConnectPlugin(TSHttpConnectOptions *options)
{
  sdk_assert(options != nullptr);
  sdk_assert(options->connect_type == TS_CONNECT_PLUGIN);
  sdk_assert(options->addr);

  sdk_assert(ats_is_unix(options->addr) || ats_is_ip(options->addr));
  sdk_assert(ats_is_unix(options->addr) || ats_ip_port_cast(options->addr));
  return reinterpret_cast<TSVConn>(PluginHttpConnectInternal(options));
}

TSVConn
TSHttpConnect(sockaddr const *addr)
{
  return TSHttpConnectWithPluginId(addr, "plugin", 0);
}

TSVConn
TSHttpConnectTransparent(sockaddr const *client_addr, sockaddr const *server_addr)
{
  sdk_assert(ats_is_ip(client_addr));
  sdk_assert(ats_is_ip(server_addr));
  sdk_assert(!ats_is_ip_any(client_addr));
  sdk_assert(ats_ip_port_cast(client_addr));
  sdk_assert(!ats_is_ip_any(server_addr));
  sdk_assert(ats_ip_port_cast(server_addr));

  if (plugin_http_transparent_accept) {
    PluginVCCore *new_pvc = PluginVCCore::alloc(plugin_http_transparent_accept);

    // set active address expects host ordering and the above casts do not
    // swap when it is required
    new_pvc->set_active_addr(client_addr);
    new_pvc->set_passive_addr(server_addr);
    new_pvc->set_transparent(true, true);

    PluginVC *return_vc = new_pvc->connect();

    if (return_vc != nullptr) {
      PluginVC *other_side = return_vc->get_other_side();

      if (other_side != nullptr) {
        other_side->set_is_internal_request(true);
      }
    }

    return reinterpret_cast<TSVConn>(return_vc);
  }

  return nullptr;
}

/* Actions */
void
TSActionCancel(TSAction actionp)
{
  Action          *thisaction;
  INKContInternal *i;

  // Nothing to cancel
  if (actionp == nullptr) {
    return;
  }

  /* This is a hack. Should be handled in ink_types */
  if (reinterpret_cast<uintptr_t>(actionp) & 0x1) {
    thisaction = reinterpret_cast<Action *>(reinterpret_cast<uintptr_t>(actionp) - 1);
    if (thisaction) {
      i = static_cast<INKContInternal *>(thisaction->continuation);
      i->handle_event_count(EVENT_IMMEDIATE);
    } else { // The action pointer for an INKContInternal was effectively null, just go away
      return;
    }
  } else {
    thisaction = reinterpret_cast<Action *>(actionp);
  }

  thisaction->cancel();
}

// Currently no error handling necessary, actionp can be anything.
int
TSActionDone(TSAction actionp)
{
  return (reinterpret_cast<Action *>(actionp) == ACTION_RESULT_DONE) ? 1 : 0;
}

/* Connections */

TSVConn
TSVConnCreate(TSEventFunc event_funcp, TSMutex mutexp)
{
  if (mutexp == nullptr) {
    mutexp = reinterpret_cast<TSMutex>(new_ProxyMutex());
  }

  // TODO: probably don't need this if memory allocations fails properly
  sdk_assert(sdk_sanity_check_mutex(mutexp) == TS_SUCCESS);

  if (pluginThreadContext) {
    pluginThreadContext->acquire();
  }

  INKVConnInternal *i = THREAD_ALLOC(INKVConnAllocator, this_thread());

  sdk_assert(sdk_sanity_check_null_ptr((void *)i) == TS_SUCCESS);

  i->init(event_funcp, mutexp, pluginThreadContext);
  return reinterpret_cast<TSVConn>(i);
}

struct ActionSink : public Continuation {
  ActionSink() : Continuation(nullptr) { SET_HANDLER(&ActionSink::mainEvent); }
  int
  mainEvent(int event, void *edata)
  {
    // Just sink the event ...
    Dbg(dbg_ctl_iocore_net, "sinking event=%d (%s), edata=%p", event, HttpDebugNames::get_event_name(event), edata);
    return EVENT_CONT;
  }
};

static ActionSink a;

TSVConn
TSVConnFdCreate(int fd)
{
  UnixNetVConnection *vc;
  EThread            *t = this_ethread();

  if (unlikely(fd == NO_FD)) {
    return nullptr;
  }

  vc = static_cast<UnixNetVConnection *>(netProcessor.allocate_vc(t));
  if (vc == nullptr) {
    return nullptr;
  }

  // We need to set an Action to handle NET_EVENT_OPEN* events. Since we have a
  // socket already, we don't need to do anything in those events, so we can just
  // sink them. It's better to sink them here, than to make the NetVC code more
  // complex.
  vc->action_ = &a;

  vc->id          = net_next_connection_number();
  vc->submit_time = ink_get_hrtime();
  vc->mutex       = new_ProxyMutex();
  vc->set_is_transparent(false);
  vc->set_context(NetVConnectionContext_t::NET_VCONNECTION_OUT);

  // We should take the nh's lock and vc's lock before we get into the connectUp
  SCOPED_MUTEX_LOCK(lock, get_NetHandler(t)->mutex, t);
  SCOPED_MUTEX_LOCK(lock2, vc->mutex, t);

  if (vc->connectUp(t, fd) != CONNECT_SUCCESS) {
    return nullptr;
  }

  return reinterpret_cast<TSVConn>(vc);
}

TSVIO
TSVConnReadVIOGet(TSVConn connp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  VConnection *vc = reinterpret_cast<VConnection *>(connp);
  TSVIO        data;

  if (vc->get_data(TS_API_DATA_READ_VIO, &data)) {
    return data;
  }

  return nullptr;
}

TSVIO
TSVConnWriteVIOGet(TSVConn connp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  VConnection *vc = reinterpret_cast<VConnection *>(connp);
  TSVIO        data;

  if (vc->get_data(TS_API_DATA_WRITE_VIO, &data)) {
    return data;
  }

  return nullptr;
}

int
TSVConnClosedGet(TSVConn connp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  VConnection *vc   = reinterpret_cast<VConnection *>(connp);
  int          data = 0;
  bool         f    = vc->get_data(TS_API_DATA_CLOSED, &data);
  ink_assert(f); // This can fail in some cases, we need to track those down.
  return data;
}

TSVIO
TSVConnRead(TSVConn connp, TSCont contp, TSIOBuffer bufp, int64_t nbytes)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_iocore_structure(bufp) == TS_SUCCESS);
  sdk_assert(nbytes >= 0);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);
  VConnection *vc = reinterpret_cast<VConnection *>(connp);

  return reinterpret_cast<TSVIO>(
    vc->do_io_read(reinterpret_cast<INKContInternal *>(contp), nbytes, reinterpret_cast<MIOBuffer *>(bufp)));
}

TSVIO
TSVConnWrite(TSVConn connp, TSCont contp, TSIOBufferReader readerp, int64_t nbytes)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_iocore_structure(readerp) == TS_SUCCESS);
  sdk_assert(nbytes >= 0);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);
  VConnection *vc = reinterpret_cast<VConnection *>(connp);

  return reinterpret_cast<TSVIO>(
    vc->do_io_write(reinterpret_cast<INKContInternal *>(contp), nbytes, reinterpret_cast<IOBufferReader *>(readerp)));
}

void
TSVConnClose(TSVConn connp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  VConnection *vc = reinterpret_cast<VConnection *>(connp);
  vc->do_io_close();
}

void
TSVConnAbort(TSVConn connp, int error)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  VConnection *vc = reinterpret_cast<VConnection *>(connp);
  vc->do_io_close(error);
}

void
TSVConnShutdown(TSVConn connp, int read, int write)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  VConnection *vc = reinterpret_cast<VConnection *>(connp);

  if (read && write) {
    vc->do_io_shutdown(IO_SHUTDOWN_READWRITE);
  } else if (read) {
    vc->do_io_shutdown(IO_SHUTDOWN_READ);
  } else if (write) {
    vc->do_io_shutdown(IO_SHUTDOWN_WRITE);
  }
}

int64_t
TSVConnCacheObjectSizeGet(TSVConn connp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  CacheVC *vc = reinterpret_cast<CacheVC *>(connp);
  return vc->get_object_size();
}

void
TSVConnCacheHttpInfoSet(TSVConn connp, TSCacheHttpInfo infop)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  CacheVC *vc = reinterpret_cast<CacheVC *>(connp);
  if (static_cast<CacheOpType>(vc->op_type) == CacheOpType::Scan) {
    vc->set_http_info(reinterpret_cast<CacheHTTPInfo *>(infop));
  }
}

/* Transformations */

TSVConn
TSTransformCreate(TSEventFunc event_funcp, TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  return TSVConnCreate(event_funcp,
                       reinterpret_cast<TSMutex>(static_cast<Continuation *>(reinterpret_cast<HttpSM *>(txnp))->getMutex()));
}

TSVConn
TSTransformOutputVConnGet(TSVConn connp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  VConnection *vc = reinterpret_cast<VConnection *>(connp);
  TSVConn      data;

  vc->get_data(TS_API_DATA_OUTPUT_VC, &data); // This case can't fail.
  return data;
}

void
TSHttpTxnServerIntercept(TSCont contp, TSHttpTxn txnp)
{
  HttpSM *http_sm = reinterpret_cast<HttpSM *>(txnp);

  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);

  TSIOBufferSizeIndex buffer_index      = TSPluginVCIOBufferIndexGet(txnp);
  TSIOBufferWaterMark buffer_water_mark = TSPluginVCIOBufferWaterMarkGet(txnp);

  http_sm->plugin_tunnel_type = HttpPluginTunnel_t::AS_SERVER;
  http_sm->plugin_tunnel      = PluginVCCore::alloc(reinterpret_cast<INKContInternal *>(contp), buffer_index, buffer_water_mark);
}

void
TSHttpTxnIntercept(TSCont contp, TSHttpTxn txnp)
{
  HttpSM *http_sm = reinterpret_cast<HttpSM *>(txnp);

  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);

  TSIOBufferSizeIndex buffer_index      = TSPluginVCIOBufferIndexGet(txnp);
  TSIOBufferWaterMark buffer_water_mark = TSPluginVCIOBufferWaterMarkGet(txnp);

  http_sm->plugin_tunnel_type = HttpPluginTunnel_t::AS_INTERCEPT;
  http_sm->plugin_tunnel      = PluginVCCore::alloc(reinterpret_cast<INKContInternal *>(contp), buffer_index, buffer_water_mark);
}

TSIOBufferSizeIndex
TSPluginVCIOBufferIndexGet(TSHttpTxn txnp)
{
  TSMgmtInt index;

  if (TSHttpTxnConfigIntGet(txnp, TS_CONFIG_PLUGIN_VC_DEFAULT_BUFFER_INDEX, &index) == TS_SUCCESS &&
      index >= TS_IOBUFFER_SIZE_INDEX_128 && index <= MAX_BUFFER_SIZE_INDEX) {
    return static_cast<TSIOBufferSizeIndex>(index);
  }

  return TS_IOBUFFER_SIZE_INDEX_32K;
}

TSIOBufferWaterMark
TSPluginVCIOBufferWaterMarkGet(TSHttpTxn txnp)
{
  TSMgmtInt water_mark;

  if (TSHttpTxnConfigIntGet(txnp, TS_CONFIG_PLUGIN_VC_DEFAULT_BUFFER_WATER_MARK, &water_mark) == TS_SUCCESS &&
      water_mark > TS_IOBUFFER_WATER_MARK_UNDEFINED) {
    return static_cast<TSIOBufferWaterMark>(water_mark);
  }

  return TS_IOBUFFER_WATER_MARK_PLUGIN_VC_DEFAULT;
}

/* Net VConnections */
void
TSVConnInactivityTimeoutSet(TSVConn connp, TSHRTime timeout)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  NetVConnection *vc = reinterpret_cast<NetVConnection *>(connp);
  vc->set_inactivity_timeout(timeout);
}

void
TSVConnInactivityTimeoutCancel(TSVConn connp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  NetVConnection *vc = reinterpret_cast<NetVConnection *>(connp);
  vc->cancel_inactivity_timeout();
}

void
TSVConnActiveTimeoutSet(TSVConn connp, TSHRTime timeout)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  NetVConnection *vc = reinterpret_cast<NetVConnection *>(connp);
  vc->set_active_timeout(timeout);
}

void
TSVConnActiveTimeoutCancel(TSVConn connp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);

  NetVConnection *vc = reinterpret_cast<NetVConnection *>(connp);
  vc->cancel_active_timeout();
}

sockaddr const *
TSNetVConnLocalAddrGet(TSVConn connp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);
  NetVConnection *vc = reinterpret_cast<NetVConnection *>(connp);
  return vc->get_local_addr();
}

sockaddr const *
TSNetVConnRemoteAddrGet(TSVConn connp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(connp) == TS_SUCCESS);
  NetVConnection *vc = reinterpret_cast<NetVConnection *>(connp);
  return vc->get_remote_addr();
}

TSAction
TSNetConnect(TSCont contp, sockaddr const *addr)
{
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);
  sdk_assert(ats_is_ip(addr));

  HttpConfigParams *http_config_param = HttpConfig::acquire();
  NetVCOptions      opt;
  if (http_config_param) {
    opt.set_sock_param(http_config_param->oride.sock_recv_buffer_size_out, http_config_param->oride.sock_send_buffer_size_out,
                       http_config_param->oride.sock_option_flag_out, http_config_param->oride.sock_packet_mark_out,
                       http_config_param->oride.sock_packet_tos_out);
    HttpConfig::release(http_config_param);
  }

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  return reinterpret_cast<TSAction>(netProcessor.connect_re(reinterpret_cast<INKContInternal *>(contp), addr, opt));
}

TSAction
TSNetConnectTransparent(TSCont contp, sockaddr const *client_addr, sockaddr const *server_addr)
{
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);
  sdk_assert(ats_is_ip(server_addr));
  sdk_assert(ats_ip_are_compatible(client_addr, server_addr));

  NetVCOptions opt;
  opt.addr_binding = NetVCOptions::FOREIGN_ADDR;
  opt.local_ip.assign(client_addr);
  opt.local_port = ats_ip_port_host_order(client_addr);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  return reinterpret_cast<TSAction>(netProcessor.connect_re(reinterpret_cast<INKContInternal *>(contp), server_addr, opt));
}

TSCont
TSNetInvokingContGet(TSVConn conn)
{
  NetVConnection     *vc     = reinterpret_cast<NetVConnection *>(conn);
  UnixNetVConnection *net_vc = dynamic_cast<UnixNetVConnection *>(vc);
  TSCont              ret    = nullptr;
  if (net_vc) {
    const Action *action = net_vc->get_action();
    ret                  = reinterpret_cast<TSCont>(action->continuation);
  }
  return ret;
}

TSHttpTxn
TSNetInvokingTxnGet(TSVConn conn)
{
  TSCont    cont = TSNetInvokingContGet(conn);
  TSHttpTxn ret  = nullptr;
  if (cont) {
    Continuation *contobj = reinterpret_cast<Continuation *>(cont);
    HttpSM       *sm      = dynamic_cast<HttpSM *>(contobj);
    if (sm) {
      ret = reinterpret_cast<TSHttpTxn>(sm);
    }
  }
  return ret;
}

TSAction
TSNetAccept(TSCont contp, int port, int domain, int accept_threads)
{
  NetProcessor::AcceptOptions opt;

  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);
  sdk_assert(port > 0);
  sdk_assert(accept_threads >= -1);

  // TODO: Does this imply that only one "accept thread" could be
  // doing an accept at any time?
  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  opt = make_net_accept_options(nullptr, accept_threads);

  // If it's not IPv6, force to IPv4.
  opt.ip_family       = domain == AF_INET6 ? AF_INET6 : AF_INET;
  opt.local_port      = port;
  opt.frequent_accept = false;

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);
  return reinterpret_cast<TSAction>(netProcessor.accept(i, opt));
}

TSReturnCode
TSNetAcceptNamedProtocol(TSCont contp, const char *protocol)
{
  sdk_assert(protocol != nullptr);
  sdk_assert(contp != nullptr);
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);

  if (!ssl_register_protocol(protocol, reinterpret_cast<INKContInternal *>(contp))) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

/* DNS Lookups */
/// Context structure for the lookup callback to the plugin.
struct TSResolveInfo {
  IpEndpoint    addr;             ///< Lookup result.
  HostDBRecord *record = nullptr; ///< Record for the FQDN.
};

static int
TSHostLookupTrampoline(TSCont contp, TSEvent ev, void *data)
{
  auto c = reinterpret_cast<INKContInternal *>(contp);
  // Set up the local context.
  TSResolveInfo ri;
  ri.record = static_cast<HostDBRecord *>(data);
  if (ri.record) {
    ri.record->rr_info()[0].data.ip.toSockAddr(ri.addr);
  }
  auto *target = reinterpret_cast<INKContInternal *>(c->mdata);
  // Deliver the message.
  target->handleEvent(ev, &ri);
  // Cleanup.
  c->destroy();
  return TS_SUCCESS;
};

TSAction
TSHostLookup(TSCont contp, const char *hostname, size_t namelen)
{
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)hostname) == TS_SUCCESS);
  sdk_assert(namelen > 0);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  // There is no place to store the actual sockaddr to which a pointer should be returned.
  // therefore an intermediate continuation is created to intercept the reply from HostDB.
  // Its handler can create the required sockaddr context on the stack and then forward
  // the event to the plugin continuation. The sockaddr cannot be placed in the HostDB
  // record because that is a shared object.
  auto bouncer = INKContAllocator.alloc();
  bouncer->init(&TSHostLookupTrampoline, reinterpret_cast<TSMutex>(reinterpret_cast<INKContInternal *>(contp)->mutex.get()));
  bouncer->mdata = contp;
  return reinterpret_cast<TSAction>(hostDBProcessor.getbyname_re(bouncer, hostname, namelen));
}

sockaddr const *
TSHostLookupResultAddrGet(TSHostLookupResult lookup_result)
{
  sdk_assert(sdk_sanity_check_hostlookup_structure(lookup_result) == TS_SUCCESS);
  auto ri{reinterpret_cast<TSResolveInfo *>(lookup_result)};
  return ri->addr.isValid() ? &ri->addr.sa : nullptr;
}

/*
 * checks if the cache is ready
 */

/* Cache VConnections */
TSAction
TSCacheRead(TSCont contp, TSCacheKey key)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_cachekey(key) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  CacheInfo    *info = reinterpret_cast<CacheInfo *>(key);
  Continuation *i    = reinterpret_cast<INKContInternal *>(contp);

  return reinterpret_cast<TSAction>(cacheProcessor.open_read(
    i, &info->cache_key, info->frag_type, std::string_view{info->hostname, static_cast<std::string_view::size_type>(info->len)}));
}

TSAction
TSCacheWrite(TSCont contp, TSCacheKey key)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_cachekey(key) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  CacheInfo    *info = reinterpret_cast<CacheInfo *>(key);
  Continuation *i    = reinterpret_cast<INKContInternal *>(contp);

  return reinterpret_cast<TSAction>(
    cacheProcessor.open_write(i, &info->cache_key, info->frag_type, 0, false, info->pin_in_cache,
                              std::string_view{info->hostname, static_cast<std::string_view::size_type>(info->len)}));
}

TSAction
TSCacheRemove(TSCont contp, TSCacheKey key)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_cachekey(key) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  CacheInfo       *info = reinterpret_cast<CacheInfo *>(key);
  INKContInternal *i    = reinterpret_cast<INKContInternal *>(contp);

  return reinterpret_cast<TSAction>(cacheProcessor.remove(
    i, &info->cache_key, info->frag_type, std::string_view{info->hostname, static_cast<std::string_view::size_type>(info->len)}));
}

TSAction
TSCacheScan(TSCont contp, TSCacheKey key, int KB_per_second)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);
  // NOTE: key can be NULl here, so don't check for it.

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  INKContInternal *i = reinterpret_cast<INKContInternal *>(contp);

  if (key) {
    CacheInfo *info = reinterpret_cast<CacheInfo *>(key);
    return reinterpret_cast<TSAction>(
      cacheProcessor.scan(i, std::string_view{info->hostname, static_cast<std::string_view::size_type>(info->len)}, KB_per_second));
  }
  return reinterpret_cast<TSAction>(cacheProcessor.scan(i, std::string_view{}, KB_per_second));
}

/************************   REC Stats API    **************************/
int
TSStatCreate(const char *the_name, TSRecordDataType /* the_type ATS_UNUSED */, TSStatPersistence /* persist ATS_UNUSED */,
             TSStatSync /* sync ATS_UNUSED */)
{
  int id = Metrics::Gauge::create(the_name); // Gauges allows for all "int" operations

  if (id == ts::Metrics::NOT_FOUND) {
    return TS_ERROR;
  }

  return id;
}

void
TSStatIntIncrement(int id, TSMgmtInt amount)
{
  sdk_assert(sdk_sanity_check_stat_id(id) == TS_SUCCESS);
  global_api_metrics.increment(id, amount);
}

void
TSStatIntDecrement(int id, TSMgmtInt amount)
{
  sdk_assert(sdk_sanity_check_stat_id(id) == TS_SUCCESS);
  global_api_metrics.decrement(id, amount);
}

TSMgmtInt
TSStatIntGet(int id)
{
  sdk_assert(sdk_sanity_check_stat_id(id) == TS_SUCCESS);
  return global_api_metrics[id].load();
}

void
TSStatIntSet(int id, TSMgmtInt value)
{
  sdk_assert(sdk_sanity_check_stat_id(id) == TS_SUCCESS);
  global_api_metrics[id].store(value);
}

TSReturnCode
TSStatFindName(const char *name, int *idp)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)name) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)idp) == TS_SUCCESS);

  int id = global_api_metrics.lookup(name);

  if (id == ts::Metrics::NOT_FOUND) {
    return TS_ERROR;
  } else {
    *idp = id;
    return TS_SUCCESS;
  }
}

/**************************   Logging API   ****************************/

TSReturnCode
TSTextLogObjectCreate(const char *filename, int mode, TSTextLogObject *new_object)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)filename) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)new_object) == TS_SUCCESS);

  if (mode < 0 || mode >= TS_LOG_MODE_INVALID_FLAG) {
    *new_object = nullptr;
    return TS_ERROR;
  }

  TextLogObject *tlog = new TextLogObject(
    filename, Log::config->logfile_dir, static_cast<bool>(mode) & TS_LOG_MODE_ADD_TIMESTAMP, nullptr, Log::config->rolling_enabled,
    Log::config->preproc_threads, Log::config->rolling_interval_sec, Log::config->rolling_offset_hr, Log::config->rolling_size_mb,
    Log::config->rolling_max_count, Log::config->rolling_min_count, Log::config->rolling_allow_empty);
  if (tlog == nullptr) {
    *new_object = nullptr;
    return TS_ERROR;
  }

  int err = (mode & TS_LOG_MODE_DO_NOT_RENAME ? Log::config->log_object_manager.manage_api_object(tlog, 0) :
                                                Log::config->log_object_manager.manage_api_object(tlog));
  if (err != LogObjectManager::NO_FILENAME_CONFLICTS) {
    delete tlog;
    *new_object = nullptr;
    return TS_ERROR;
  }

  *new_object = reinterpret_cast<TSTextLogObject>(tlog);
  return TS_SUCCESS;
}

TSReturnCode
TSTextLogObjectWrite(TSTextLogObject the_object, const char *format, ...)
{
  sdk_assert(sdk_sanity_check_iocore_structure(the_object) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)format) == TS_SUCCESS);

  TSReturnCode retVal = TS_SUCCESS;

  va_list ap;
  va_start(ap, format);
  switch ((reinterpret_cast<TextLogObject *>(the_object))->va_write(format, ap)) {
  case (Log::LOG_OK):
  case (Log::SKIP):
  case (Log::AGGR):
    break;
  case (Log::FULL):
    retVal = TS_ERROR;
    break;
  case (Log::FAIL):
    retVal = TS_ERROR;
    break;
  default:
    ink_assert(!"invalid return code");
  }
  va_end(ap);

  return retVal;
}

void
TSTextLogObjectFlush(TSTextLogObject the_object)
{
  sdk_assert(sdk_sanity_check_iocore_structure(the_object) == TS_SUCCESS);

  (reinterpret_cast<TextLogObject *>(the_object))->force_new_buffer();
}

TSReturnCode
TSTextLogObjectDestroy(TSTextLogObject the_object)
{
  sdk_assert(sdk_sanity_check_iocore_structure(the_object) == TS_SUCCESS);

  if (Log::config->log_object_manager.unmanage_api_object(reinterpret_cast<TextLogObject *>(the_object))) {
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

void
TSTextLogObjectHeaderSet(TSTextLogObject the_object, const char *header)
{
  sdk_assert(sdk_sanity_check_iocore_structure(the_object) == TS_SUCCESS);

  (reinterpret_cast<TextLogObject *>(the_object))->set_log_file_header(header);
}

TSReturnCode
TSTextLogObjectRollingEnabledSet(TSTextLogObject the_object, int rolling_enabled)
{
  sdk_assert(sdk_sanity_check_iocore_structure(the_object) == TS_SUCCESS);

  if (LogRollingEnabledIsValid(rolling_enabled)) {
    (reinterpret_cast<TextLogObject *>(the_object))->set_rolling_enabled(static_cast<Log::RollingEnabledValues>(rolling_enabled));
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

void
TSTextLogObjectRollingIntervalSecSet(TSTextLogObject the_object, int rolling_interval_sec)
{
  sdk_assert(sdk_sanity_check_iocore_structure(the_object) == TS_SUCCESS);

  (reinterpret_cast<TextLogObject *>(the_object))->set_rolling_interval_sec(rolling_interval_sec);
}

void
TSTextLogObjectRollingOffsetHrSet(TSTextLogObject the_object, int rolling_offset_hr)
{
  sdk_assert(sdk_sanity_check_iocore_structure(the_object) == TS_SUCCESS);

  (reinterpret_cast<TextLogObject *>(the_object))->set_rolling_offset_hr(rolling_offset_hr);
}

void
TSTextLogObjectRollingSizeMbSet(TSTextLogObject the_object, int rolling_size_mb)
{
  sdk_assert(sdk_sanity_check_iocore_structure(the_object) == TS_SUCCESS);

  (reinterpret_cast<TextLogObject *>(the_object))->set_rolling_size_mb(rolling_size_mb);
}

TSReturnCode
TSHttpSsnClientFdGet(TSHttpSsn ssnp, int *fdp)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)fdp) == TS_SUCCESS);

  VConnection  *basecs = reinterpret_cast<VConnection *>(ssnp);
  ProxySession *cs     = dynamic_cast<ProxySession *>(basecs);

  if (cs == nullptr) {
    return TS_ERROR;
  }

  NetVConnection *vc = cs->get_netvc();
  if (vc == nullptr) {
    return TS_ERROR;
  }

  *fdp = vc->get_socket();
  return TS_SUCCESS;
}
TSReturnCode
TSHttpTxnClientFdGet(TSHttpTxn txnp, int *fdp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)fdp) == TS_SUCCESS);

  TSHttpSsn ssnp = TSHttpTxnSsnGet(txnp);
  return TSHttpSsnClientFdGet(ssnp, fdp);
}

TSReturnCode
TSHttpTxnServerFdGet(TSHttpTxn txnp, int *fdp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)fdp) == TS_SUCCESS);

  HttpSM *sm               = reinterpret_cast<HttpSM *>(txnp);
  *fdp                     = -1;
  TSReturnCode      retval = TS_ERROR;
  ProxyTransaction *ss     = sm->get_server_txn();
  if (ss != nullptr) {
    NetVConnection *vc = ss->get_netvc();
    if (vc != nullptr) {
      *fdp   = vc->get_socket();
      retval = TS_SUCCESS;
    }
  }
  return retval;
}

void
load_config_file_callback(const char *parent_file, const char *remap_file)
{
  FileManager::instance().configFileChild(parent_file, remap_file);
}

/* Config file name setting */
TSReturnCode
TSMgmtConfigFileAdd(const char *parent, const char *fileName)
{
  load_config_file_callback(parent, fileName);
  return TS_SUCCESS;
}

TSReturnCode
TSCacheUrlSet(TSHttpTxn txnp, const char *url, int length)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  Dbg(dbg_ctl_cache_url, "[TSCacheUrlSet]");

  if (sm->t_state.cache_info.lookup_url == nullptr) {
    Dbg(dbg_ctl_cache_url, "[TSCacheUrlSet] changing the cache url to: %s", url);

    if (length == -1) {
      length = strlen(url);
    }

    sm->t_state.cache_info.lookup_url_storage.create(nullptr);
    sm->t_state.cache_info.lookup_url = &(sm->t_state.cache_info.lookup_url_storage);
    sm->t_state.cache_info.lookup_url->parse(url, length);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

void
TSCacheHttpInfoKeySet(TSCacheHttpInfo infop, TSCacheKey keyp)
{
  // TODO: Check input ?
  CacheHTTPInfo *info = reinterpret_cast<CacheHTTPInfo *>(infop);
  CryptoHash    *key  = reinterpret_cast<CryptoHash *>(keyp);

  info->object_key_set(*key);
}

void
TSCacheHttpInfoSizeSet(TSCacheHttpInfo infop, int64_t size)
{
  // TODO: Check input ?
  CacheHTTPInfo *info = reinterpret_cast<CacheHTTPInfo *>(infop);

  info->object_size_set(size);
}

// this function should be called at TS_EVENT_HTTP_READ_RESPONSE_HDR
void
TSHttpTxnRedirectUrlSet(TSHttpTxn txnp, const char *url, const int url_len)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)url) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  ats_free(sm->redirect_url);
  sm->redirect_url     = nullptr;
  sm->redirect_url_len = 0;

  sm->redirect_url       = const_cast<char *>(url);
  sm->redirect_url_len   = url_len;
  sm->enable_redirection = true;
  sm->redirection_tries  = 0;

  // Make sure we allow for at least one redirection.
  if (sm->t_state.txn_conf->number_of_redirections <= 0) {
    sm->t_state.setup_per_txn_configs();
    sm->t_state.my_txn_conf().number_of_redirections = 1;
  }
}

const char *
TSHttpTxnRedirectUrlGet(TSHttpTxn txnp, int *url_len_ptr)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  *url_len_ptr = sm->redirect_url_len;
  return sm->redirect_url;
}

int
TSHttpTxnRedirectRetries(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  return sm->redirection_tries;
}

char *
TSFetchRespGet(TSHttpTxn txnp, int *length)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)length) == TS_SUCCESS);
  FetchSM *fetch_sm = reinterpret_cast<FetchSM *>(txnp);
  return fetch_sm->resp_get(length);
}

TSReturnCode
TSFetchPageRespGet(TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *obj)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)obj) == TS_SUCCESS);

  HTTPHdr *hptr = reinterpret_cast<HTTPHdr *>(txnp);

  if (hptr->valid()) {
    *(reinterpret_cast<HTTPHdr **>(bufp)) = hptr;
    *obj                                  = reinterpret_cast<TSMLoc>(hptr->m_http);
    return sdk_sanity_check_mbuffer(*bufp);
  }

  return TS_ERROR;
}

void
TSFetchPages(TSFetchUrlParams_t *params)
{
  TSFetchUrlParams_t *myparams = params;

  while (myparams != nullptr) {
    FetchSM  *fetch_sm = FetchSMAllocator.alloc();
    sockaddr *addr     = ats_ip_sa_cast(&myparams->ip);

    fetch_sm->init(reinterpret_cast<Continuation *>(myparams->contp), myparams->options, myparams->events, myparams->request,
                   myparams->request_len, addr);
    fetch_sm->httpConnect();
    myparams = myparams->next;
  }
}

TSFetchSM
TSFetchUrl(const char *headers, int request_len, sockaddr const *ip, TSCont contp, TSFetchWakeUpOptions callback_options,
           TSFetchEvent events)
{
  if (callback_options != NO_CALLBACK) {
    sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);
  }

  FetchSM *fetch_sm = FetchSMAllocator.alloc();

  fetch_sm->init(reinterpret_cast<Continuation *>(contp), callback_options, events, headers, request_len, ip);
  fetch_sm->httpConnect();

  return reinterpret_cast<TSFetchSM>(fetch_sm);
}

void
TSFetchFlagSet(TSFetchSM fetch_sm, int flags)
{
  sdk_assert(sdk_sanity_check_fetch_sm(fetch_sm) == TS_SUCCESS);
  (reinterpret_cast<FetchSM *>(fetch_sm))->set_fetch_flags(flags);
}

TSFetchSM
TSFetchCreate(TSCont contp, const char *method, const char *url, const char *version, struct sockaddr const *client_addr, int flags)
{
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);
  sdk_assert(ats_is_ip(client_addr));

  FetchSM *fetch_sm = FetchSMAllocator.alloc();

  fetch_sm->ext_init(reinterpret_cast<Continuation *>(contp), method, url, version, client_addr, flags);

  return reinterpret_cast<TSFetchSM>(fetch_sm);
}

void
TSFetchHeaderAdd(TSFetchSM fetch_sm, const char *name, int name_len, const char *value, int value_len)
{
  sdk_assert(sdk_sanity_check_fetch_sm(fetch_sm) == TS_SUCCESS);

  (reinterpret_cast<FetchSM *>(fetch_sm))->ext_add_header(name, name_len, value, value_len);
}

void
TSFetchWriteData(TSFetchSM fetch_sm, const void *data, size_t len)
{
  sdk_assert(sdk_sanity_check_fetch_sm(fetch_sm) == TS_SUCCESS);

  (reinterpret_cast<FetchSM *>(fetch_sm))->ext_write_data(data, len);
}

ssize_t
TSFetchReadData(TSFetchSM fetch_sm, void *buf, size_t len)
{
  sdk_assert(sdk_sanity_check_fetch_sm(fetch_sm) == TS_SUCCESS);

  return (reinterpret_cast<FetchSM *>(fetch_sm))->ext_read_data(static_cast<char *>(buf), len);
}

void
TSFetchLaunch(TSFetchSM fetch_sm)
{
  sdk_assert(sdk_sanity_check_fetch_sm(fetch_sm) == TS_SUCCESS);

  (reinterpret_cast<FetchSM *>(fetch_sm))->ext_launch();
}

void
TSFetchDestroy(TSFetchSM fetch_sm)
{
  sdk_assert(sdk_sanity_check_fetch_sm(fetch_sm) == TS_SUCCESS);

  (reinterpret_cast<FetchSM *>(fetch_sm))->ext_destroy();
}

void
TSFetchUserDataSet(TSFetchSM fetch_sm, void *data)
{
  sdk_assert(sdk_sanity_check_fetch_sm(fetch_sm) == TS_SUCCESS);

  (reinterpret_cast<FetchSM *>(fetch_sm))->ext_set_user_data(data);
}

void *
TSFetchUserDataGet(TSFetchSM fetch_sm)
{
  sdk_assert(sdk_sanity_check_fetch_sm(fetch_sm) == TS_SUCCESS);

  return (reinterpret_cast<FetchSM *>(fetch_sm))->ext_get_user_data();
}

TSMBuffer
TSFetchRespHdrMBufGet(TSFetchSM fetch_sm)
{
  sdk_assert(sdk_sanity_check_fetch_sm(fetch_sm) == TS_SUCCESS);

  return (reinterpret_cast<FetchSM *>(fetch_sm))->resp_hdr_bufp();
}

TSMLoc
TSFetchRespHdrMLocGet(TSFetchSM fetch_sm)
{
  sdk_assert(sdk_sanity_check_fetch_sm(fetch_sm) == TS_SUCCESS);

  return (reinterpret_cast<FetchSM *>(fetch_sm))->resp_hdr_mloc();
}

int
TSHttpSsnIsInternal(TSHttpSsn ssnp)
{
  ProxySession *cs = reinterpret_cast<ProxySession *>(ssnp);

  if (!cs) {
    return 0;
  }

  NetVConnection *vc = cs->get_netvc();
  if (!vc) {
    return 0;
  }

  return vc->get_is_internal_request() ? 1 : 0;
}

int
TSHttpTxnIsInternal(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  return TSHttpSsnIsInternal(TSHttpTxnSsnGet(txnp));
}

static void
txn_error_get(TSHttpTxn txnp, bool client, bool sent, uint32_t &error_class, uint64_t &error_code)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM                             *sm                    = reinterpret_cast<HttpSM *>(txnp);
  HttpTransact::ConnectionAttributes *connection_attributes = nullptr;

  if (client == true) {
    // client
    connection_attributes = &sm->t_state.client_info;
  } else {
    // server
    connection_attributes = &sm->t_state.server_info;
  }

  if (sent == true) {
    // sent
    error_code  = connection_attributes->tx_error_code.code;
    error_class = static_cast<uint32_t>(connection_attributes->tx_error_code.cls);
  } else {
    // received
    error_code  = connection_attributes->rx_error_code.code;
    error_class = static_cast<uint32_t>(connection_attributes->rx_error_code.cls);
  }
}

void
TSHttpTxnClientReceivedErrorGet(TSHttpTxn txnp, uint32_t *error_class, uint64_t *error_code)
{
  txn_error_get(txnp, true, false, *error_class, *error_code);
}

void
TSHttpTxnClientSentErrorGet(TSHttpTxn txnp, uint32_t *error_class, uint64_t *error_code)
{
  txn_error_get(txnp, true, true, *error_class, *error_code);
}

void
TSHttpTxnServerReceivedErrorGet(TSHttpTxn txnp, uint32_t *error_class, uint64_t *error_code)
{
  txn_error_get(txnp, false, false, *error_class, *error_code);
}

void
TSHttpTxnServerSentErrorGet(TSHttpTxn txnp, uint32_t *error_class, uint64_t *error_code)
{
  txn_error_get(txnp, false, true, *error_class, *error_code);
}

TSReturnCode
TSHttpTxnServerPush(TSHttpTxn txnp, const char *url, int url_len)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(url != nullptr);

  if (url_len < 0) {
    url_len = strlen(url);
  }

  URL url_obj;
  url_obj.create(nullptr);
  if (url_obj.parse(url, url_len) == ParseResult::ERROR) {
    url_obj.destroy();
    return TS_ERROR;
  }

  HttpSM      *sm     = reinterpret_cast<HttpSM *>(txnp);
  Http2Stream *stream = dynamic_cast<Http2Stream *>(sm->get_ua_txn());
  if (stream == nullptr) {
    url_obj.destroy();
    return TS_ERROR;
  }

  Http2ClientSession *ua_session = static_cast<Http2ClientSession *>(stream->get_proxy_ssn());
  SCOPED_MUTEX_LOCK(lock, ua_session->mutex, this_ethread());
  if (ua_session->connection_state.is_state_closed() || ua_session->is_url_pushed(url, url_len)) {
    url_obj.destroy();
    return TS_ERROR;
  }

  HTTPHdr *hptr = &(sm->t_state.hdr_info.client_request);
  TSMLoc   obj  = reinterpret_cast<TSMLoc>(hptr->m_http);

  MIMEHdrImpl *mh = _hdr_mloc_to_mime_hdr_impl(obj);
  MIMEField   *f  = mime_hdr_field_find(mh, static_cast<std::string_view>(MIME_FIELD_ACCEPT_ENCODING));
  if (!stream->push_promise(url_obj, f)) {
    url_obj.destroy();
    return TS_ERROR;
  }

  ua_session->add_url_to_pushed_table(url, url_len);

  url_obj.destroy();
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnClientStreamIdGet(TSHttpTxn txnp, uint64_t *stream_id)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(stream_id != nullptr);

  auto *sm     = reinterpret_cast<HttpSM *>(txnp);
  auto *stream = dynamic_cast<Http2Stream *>(sm->get_ua_txn());
  if (stream == nullptr) {
    return TS_ERROR;
  }
  *stream_id = stream->get_id();
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnClientStreamPriorityGet(TSHttpTxn txnp, TSHttpPriority *priority)
{
  static_assert(sizeof(TSHttpPriority) >= sizeof(TSHttp2Priority),
                "TSHttpPriorityType is incorrectly smaller than TSHttp2Priority.");
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(priority != nullptr);

  auto *sm     = reinterpret_cast<HttpSM *>(txnp);
  auto *stream = dynamic_cast<Http2Stream *>(sm->get_ua_txn());
  if (stream == nullptr) {
    return TS_ERROR;
  }

  auto *priority_out              = reinterpret_cast<TSHttp2Priority *>(priority);
  priority_out->priority_type     = HTTP_PRIORITY_TYPE_HTTP_2;
  priority_out->stream_dependency = stream->get_transaction_priority_dependence();
  priority_out->weight            = stream->get_transaction_priority_weight();

  return TS_SUCCESS;
}

TSReturnCode
TSAIORead(int fd, off_t offset, char *buf, size_t buffSize, TSCont contp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  Continuation *pCont = reinterpret_cast<Continuation *>(contp);
  AIOCallback  *pAIO  = new_AIOCallback();

  if (pAIO == nullptr) {
    return TS_ERROR;
  }

  pAIO->aiocb.aio_fildes = fd;
  pAIO->aiocb.aio_offset = offset;
  pAIO->aiocb.aio_nbytes = buffSize;

  pAIO->aiocb.aio_buf = buf;
  pAIO->action        = pCont;
  pAIO->thread        = pCont->mutex->thread_holding;

  if (ink_aio_read(pAIO, 1) == 1) {
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

char *
TSAIOBufGet(TSAIOCallback data)
{
  AIOCallback *pAIO = reinterpret_cast<AIOCallback *>(data);
  return static_cast<char *>(pAIO->aiocb.aio_buf);
}

int
TSAIONBytesGet(TSAIOCallback data)
{
  AIOCallback *pAIO = reinterpret_cast<AIOCallback *>(data);
  return static_cast<int>(pAIO->aio_result);
}

TSReturnCode
TSAIOWrite(int fd, off_t offset, char *buf, const size_t bufSize, TSCont contp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(contp) == TS_SUCCESS);

  Continuation *pCont = reinterpret_cast<Continuation *>(contp);
  AIOCallback  *pAIO  = new_AIOCallback();

  // TODO: Might be able to remove this when allocations can never fail.
  sdk_assert(sdk_sanity_check_null_ptr((void *)pAIO) == TS_SUCCESS);

  pAIO->aiocb.aio_fildes = fd;
  pAIO->aiocb.aio_offset = offset;
  pAIO->aiocb.aio_buf    = buf;
  pAIO->aiocb.aio_nbytes = bufSize;
  pAIO->action           = pCont;
  pAIO->thread           = pCont->mutex->thread_holding;

  if (ink_aio_write(pAIO, 1) == 1) {
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSAIOThreadNumSet(int thread_num)
{
  if (ink_aio_thread_num_set(thread_num)) {
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

void
TSRecordDump(int rec_type, TSRecordDumpCb callback, void *edata)
{
  RecDumpRecords(static_cast<RecT>(rec_type), reinterpret_cast<RecDumpEntryCb>(callback), edata);
}

/* ability to skip the remap phase of the State Machine
   this only really makes sense in TS_HTTP_READ_REQUEST_HDR_HOOK
*/
void
TSSkipRemappingSet(TSHttpTxn txnp, int flag)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM *sm                         = reinterpret_cast<HttpSM *>(txnp);
  sm->t_state.api_skip_all_remapping = (flag != 0);
}

/* These are the default converter function sets for management data types. If those are used the
 * proper converters can be determined here. For other types the converters must be explicitly
 * specified.
 *
 * The purpose of these are to allow configuration elements to not be management types but more
 * natural types (e.g., an enumeration can be the actual enumeration, not an @c MgmtInt that needs
 * frequent casting). In effect the converter does the casting for the plugin API, isolating that
 * to this API handling, with the rest of the code base using the natural types.
 */

/// Unhandled API conversions.
/// Because the code around the specially handled types still uses this in the default case,
/// it must compile for those cases. To indicate unhandled, return @c nullptr for @a conv.
/// @internal This should be a temporary state, eventually the other cases should be handled
/// via specializations here.
/// @internal C++ note - THIS MUST BE FIRST IN THE DECLARATIONS or it might be falsely used.
template <typename T>
inline void *
_memberp_to_generic(T *ptr, MgmtConverter const *&conv)
{
  conv = nullptr;
  return ptr;
}

/// API conversion for @c MgmtInt, identify conversion as integer.
inline void *
_memberp_to_generic(MgmtInt *ptr, MgmtConverter const *&conv)
{
  static const MgmtConverter converter([](const void *data) -> MgmtInt { return *static_cast<const MgmtInt *>(data); },
                                       [](void *data, MgmtInt i) -> void { *static_cast<MgmtInt *>(data) = i; });

  conv = &converter;
  return ptr;
}

/// API conversion for @c MgmtByte, handles integer / byte size differences.
inline void *
_memberp_to_generic(MgmtByte *ptr, MgmtConverter const *&conv)
{
  static const MgmtConverter converter{[](const void *data) -> MgmtInt { return *static_cast<const MgmtByte *>(data); },
                                       [](void *data, MgmtInt i) -> void { *static_cast<MgmtByte *>(data) = i; }};

  conv = &converter;
  return ptr;
}

/// API conversion for @c MgmtFloat, identity conversion as float.
inline void *
_memberp_to_generic(MgmtFloat *ptr, MgmtConverter const *&conv)
{
  static const MgmtConverter converter{[](const void *data) -> MgmtFloat { return *static_cast<const MgmtFloat *>(data); },
                                       [](void *data, MgmtFloat f) -> void { *static_cast<MgmtFloat *>(data) = f; }};

  conv = &converter;
  return ptr;
}

/// API conversion for arbitrary enum.
/// Handle casting to and from the enum type @a E.
template <typename E>
inline auto
_memberp_to_generic(MgmtFloat *ptr, MgmtConverter const *&conv) -> typename std::enable_if<std::is_enum<E>::value, void *>::type
{
  static const MgmtConverter converter{
    [](const void *data) -> MgmtInt { return static_cast<MgmtInt>(*static_cast<const E *>(data)); },
    [](void *data, MgmtInt i) -> void { *static_cast<E *>(data) = static_cast<E>(i); }};

  conv = &converter;
  return ptr;
}

// Little helper function to find the struct member
static void *
_conf_to_memberp(TSOverridableConfigKey conf, OverridableHttpConfigParams *overridableHttpConfig, MgmtConverter const *&conv)
{
  void *ret = nullptr;
  conv      = nullptr;

  switch (conf) {
  case TS_CONFIG_URL_REMAP_PRISTINE_HOST_HDR:
    ret = _memberp_to_generic(&overridableHttpConfig->maintain_pristine_host_hdr, conv);
    break;
  case TS_CONFIG_HTTP_CHUNKING_ENABLED:
    ret = _memberp_to_generic(&overridableHttpConfig->chunking_enabled, conv);
    break;
  case TS_CONFIG_HTTP_NEGATIVE_CACHING_ENABLED:
    ret = _memberp_to_generic(&overridableHttpConfig->negative_caching_enabled, conv);
    break;
  case TS_CONFIG_HTTP_NEGATIVE_CACHING_LIFETIME:
    ret = _memberp_to_generic(&overridableHttpConfig->negative_caching_lifetime, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_WHEN_TO_REVALIDATE:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_when_to_revalidate, conv);
    break;
  case TS_CONFIG_HTTP_KEEP_ALIVE_ENABLED_IN:
    ret = _memberp_to_generic(&overridableHttpConfig->keep_alive_enabled_in, conv);
    break;
  case TS_CONFIG_HTTP_KEEP_ALIVE_ENABLED_OUT:
    ret = _memberp_to_generic(&overridableHttpConfig->keep_alive_enabled_out, conv);
    break;
  case TS_CONFIG_HTTP_KEEP_ALIVE_POST_OUT:
    ret = _memberp_to_generic(&overridableHttpConfig->keep_alive_post_out, conv);
    break;
  case TS_CONFIG_HTTP_SERVER_SESSION_SHARING_MATCH:
    ret = _memberp_to_generic(&overridableHttpConfig->server_session_sharing_match, conv);
    break;
  case TS_CONFIG_NET_SOCK_RECV_BUFFER_SIZE_OUT:
    ret = _memberp_to_generic(&overridableHttpConfig->sock_recv_buffer_size_out, conv);
    break;
  case TS_CONFIG_NET_SOCK_SEND_BUFFER_SIZE_OUT:
    ret = _memberp_to_generic(&overridableHttpConfig->sock_send_buffer_size_out, conv);
    break;
  case TS_CONFIG_NET_SOCK_OPTION_FLAG_OUT:
    ret = _memberp_to_generic(&overridableHttpConfig->sock_option_flag_out, conv);
    break;
  case TS_CONFIG_HTTP_FORWARD_PROXY_AUTH_TO_PARENT:
    ret = _memberp_to_generic(&overridableHttpConfig->fwd_proxy_auth_to_parent, conv);
    break;
  case TS_CONFIG_HTTP_ANONYMIZE_REMOVE_FROM:
    ret = _memberp_to_generic(&overridableHttpConfig->anonymize_remove_from, conv);
    break;
  case TS_CONFIG_HTTP_ANONYMIZE_REMOVE_REFERER:
    ret = _memberp_to_generic(&overridableHttpConfig->anonymize_remove_referer, conv);
    break;
  case TS_CONFIG_HTTP_ANONYMIZE_REMOVE_USER_AGENT:
    ret = _memberp_to_generic(&overridableHttpConfig->anonymize_remove_user_agent, conv);
    break;
  case TS_CONFIG_HTTP_ANONYMIZE_REMOVE_COOKIE:
    ret = _memberp_to_generic(&overridableHttpConfig->anonymize_remove_cookie, conv);
    break;
  case TS_CONFIG_HTTP_ANONYMIZE_REMOVE_CLIENT_IP:
    ret = _memberp_to_generic(&overridableHttpConfig->anonymize_remove_client_ip, conv);
    break;
  case TS_CONFIG_HTTP_ANONYMIZE_INSERT_CLIENT_IP:
    ret = _memberp_to_generic(&overridableHttpConfig->anonymize_insert_client_ip, conv);
    break;
  case TS_CONFIG_HTTP_RESPONSE_SERVER_ENABLED:
    ret = _memberp_to_generic(&overridableHttpConfig->proxy_response_server_enabled, conv);
    break;
  case TS_CONFIG_HTTP_INSERT_SQUID_X_FORWARDED_FOR:
    ret = _memberp_to_generic(&overridableHttpConfig->insert_squid_x_forwarded_for, conv);
    break;
  case TS_CONFIG_HTTP_INSERT_FORWARDED:
    ret = _memberp_to_generic(&overridableHttpConfig->insert_forwarded, conv);
    break;
  case TS_CONFIG_HTTP_PROXY_PROTOCOL_OUT:
    ret = _memberp_to_generic(&overridableHttpConfig->proxy_protocol_out, conv);
    break;
  case TS_CONFIG_HTTP_SEND_HTTP11_REQUESTS:
    ret = _memberp_to_generic(&overridableHttpConfig->send_http11_requests, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_HTTP:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_http, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_IGNORE_CLIENT_NO_CACHE:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_ignore_client_no_cache, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_IGNORE_CLIENT_CC_MAX_AGE:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_ignore_client_cc_max_age, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_IMS_ON_CLIENT_NO_CACHE:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_ims_on_client_no_cache, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_IGNORE_SERVER_NO_CACHE:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_ignore_server_no_cache, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_CACHE_RESPONSES_TO_COOKIES:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_responses_to_cookies, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_IGNORE_AUTHENTICATION:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_ignore_auth, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_IGNORE_QUERY:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_ignore_query, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_REQUIRED_HEADERS:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_required_headers, conv);
    break;
  case TS_CONFIG_HTTP_INSERT_REQUEST_VIA_STR:
    ret = _memberp_to_generic(&overridableHttpConfig->insert_request_via_string, conv);
    break;
  case TS_CONFIG_HTTP_INSERT_RESPONSE_VIA_STR:
    ret = _memberp_to_generic(&overridableHttpConfig->insert_response_via_string, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_HEURISTIC_MIN_LIFETIME:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_heuristic_min_lifetime, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_HEURISTIC_MAX_LIFETIME:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_heuristic_max_lifetime, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_GUARANTEED_MIN_LIFETIME:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_guaranteed_min_lifetime, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_GUARANTEED_MAX_LIFETIME:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_guaranteed_max_lifetime, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_MAX_STALE_AGE:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_max_stale_age, conv);
    break;
  case TS_CONFIG_HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_IN:
    ret = _memberp_to_generic(&overridableHttpConfig->keep_alive_no_activity_timeout_in, conv);
    break;
  case TS_CONFIG_HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_OUT:
    ret = _memberp_to_generic(&overridableHttpConfig->keep_alive_no_activity_timeout_out, conv);
    break;
  case TS_CONFIG_HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_IN:
    ret = _memberp_to_generic(&overridableHttpConfig->transaction_no_activity_timeout_in, conv);
    break;
  case TS_CONFIG_HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_OUT:
    ret = _memberp_to_generic(&overridableHttpConfig->transaction_no_activity_timeout_out, conv);
    break;
  case TS_CONFIG_HTTP_TRANSACTION_ACTIVE_TIMEOUT_OUT:
    ret = _memberp_to_generic(&overridableHttpConfig->transaction_active_timeout_out, conv);
    break;
  case TS_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES:
    ret = _memberp_to_generic(&overridableHttpConfig->connect_attempts_max_retries, conv);
    break;
  case TS_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DOWN_SERVER:
    ret = _memberp_to_generic(&overridableHttpConfig->connect_attempts_max_retries_down_server, conv);
    break;
  case TS_CONFIG_HTTP_CONNECT_DOWN_POLICY:
    ret = _memberp_to_generic(&overridableHttpConfig->connect_down_policy, conv);
    break;
  case TS_CONFIG_HTTP_CONNECT_ATTEMPTS_RR_RETRIES:
    ret = _memberp_to_generic(&overridableHttpConfig->connect_attempts_rr_retries, conv);
    break;
  case TS_CONFIG_HTTP_CONNECT_ATTEMPTS_TIMEOUT:
    ret = _memberp_to_generic(&overridableHttpConfig->connect_attempts_timeout, conv);
    break;
  case TS_CONFIG_HTTP_DOWN_SERVER_CACHE_TIME:
    conv = &HttpDownServerCacheTimeConv;
    ret  = &overridableHttpConfig->down_server_timeout;
    break;
  case TS_CONFIG_HTTP_DOC_IN_CACHE_SKIP_DNS:
    ret = _memberp_to_generic(&overridableHttpConfig->doc_in_cache_skip_dns, conv);
    break;
  case TS_CONFIG_HTTP_BACKGROUND_FILL_ACTIVE_TIMEOUT:
    ret = _memberp_to_generic(&overridableHttpConfig->background_fill_active_timeout, conv);
    break;
  case TS_CONFIG_HTTP_RESPONSE_SERVER_STR:
    ret = _memberp_to_generic(&overridableHttpConfig->proxy_response_server_string, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_HEURISTIC_LM_FACTOR:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_heuristic_lm_factor, conv);
    break;
  case TS_CONFIG_HTTP_BACKGROUND_FILL_COMPLETED_THRESHOLD:
    ret = _memberp_to_generic(&overridableHttpConfig->background_fill_threshold, conv);
    break;
  case TS_CONFIG_NET_SOCK_PACKET_MARK_OUT:
    ret = _memberp_to_generic(&overridableHttpConfig->sock_packet_mark_out, conv);
    break;
  case TS_CONFIG_NET_SOCK_PACKET_TOS_OUT:
    ret = _memberp_to_generic(&overridableHttpConfig->sock_packet_tos_out, conv);
    break;
  case TS_CONFIG_HTTP_INSERT_AGE_IN_RESPONSE:
    ret = _memberp_to_generic(&overridableHttpConfig->insert_age_in_response, conv);
    break;
  case TS_CONFIG_HTTP_CHUNKING_SIZE:
    ret = _memberp_to_generic(&overridableHttpConfig->http_chunking_size, conv);
    break;
  case TS_CONFIG_HTTP_DROP_CHUNKED_TRAILERS:
    ret = _memberp_to_generic(&overridableHttpConfig->http_drop_chunked_trailers, conv);
    break;
  case TS_CONFIG_HTTP_STRICT_CHUNK_PARSING:
    ret = _memberp_to_generic(&overridableHttpConfig->http_strict_chunk_parsing, conv);
    break;
  case TS_CONFIG_HTTP_FLOW_CONTROL_ENABLED:
    ret = _memberp_to_generic(&overridableHttpConfig->flow_control_enabled, conv);
    break;
  case TS_CONFIG_HTTP_FLOW_CONTROL_LOW_WATER_MARK:
    ret = _memberp_to_generic(&overridableHttpConfig->flow_low_water_mark, conv);
    break;
  case TS_CONFIG_HTTP_FLOW_CONTROL_HIGH_WATER_MARK:
    ret = _memberp_to_generic(&overridableHttpConfig->flow_high_water_mark, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_RANGE_LOOKUP:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_range_lookup, conv);
    break;
  case TS_CONFIG_HTTP_NORMALIZE_AE:
    ret = _memberp_to_generic(&overridableHttpConfig->normalize_ae, conv);
    break;
  case TS_CONFIG_HTTP_DEFAULT_BUFFER_SIZE:
    ret = _memberp_to_generic(&overridableHttpConfig->default_buffer_size_index, conv);
    break;
  case TS_CONFIG_HTTP_DEFAULT_BUFFER_WATER_MARK:
    ret = _memberp_to_generic(&overridableHttpConfig->default_buffer_water_mark, conv);
    break;
  case TS_CONFIG_HTTP_REQUEST_HEADER_MAX_SIZE:
    ret = _memberp_to_generic(&overridableHttpConfig->request_hdr_max_size, conv);
    break;
  case TS_CONFIG_HTTP_RESPONSE_HEADER_MAX_SIZE:
    ret = _memberp_to_generic(&overridableHttpConfig->response_hdr_max_size, conv);
    break;
  case TS_CONFIG_HTTP_NEGATIVE_REVALIDATING_ENABLED:
    ret = _memberp_to_generic(&overridableHttpConfig->negative_revalidating_enabled, conv);
    break;
  case TS_CONFIG_HTTP_NEGATIVE_REVALIDATING_LIFETIME:
    ret = _memberp_to_generic(&overridableHttpConfig->negative_revalidating_lifetime, conv);
    break;
  case TS_CONFIG_SSL_HSTS_MAX_AGE:
    ret = _memberp_to_generic(&overridableHttpConfig->proxy_response_hsts_max_age, conv);
    break;
  case TS_CONFIG_SSL_HSTS_INCLUDE_SUBDOMAINS:
    ret = _memberp_to_generic(&overridableHttpConfig->proxy_response_hsts_include_subdomains, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_OPEN_READ_RETRY_TIME:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_open_read_retry_time, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_MAX_OPEN_READ_RETRIES:
    ret = _memberp_to_generic(&overridableHttpConfig->max_cache_open_read_retries, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_RANGE_WRITE:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_range_write, conv);
    break;
  case TS_CONFIG_HTTP_POST_CHECK_CONTENT_LENGTH_ENABLED:
    ret = _memberp_to_generic(&overridableHttpConfig->post_check_content_length_enabled, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_POST_METHOD:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_post_method, conv);
    break;
  case TS_CONFIG_HTTP_REQUEST_BUFFER_ENABLED:
    ret = _memberp_to_generic(&overridableHttpConfig->request_buffer_enabled, conv);
    break;
  case TS_CONFIG_HTTP_GLOBAL_USER_AGENT_HEADER:
    ret = _memberp_to_generic(&overridableHttpConfig->global_user_agent_header, conv);
    break;
  case TS_CONFIG_HTTP_AUTH_SERVER_SESSION_PRIVATE:
    ret = _memberp_to_generic(&overridableHttpConfig->auth_server_session_private, conv);
    break;
  case TS_CONFIG_HTTP_SLOW_LOG_THRESHOLD:
    ret = _memberp_to_generic(&overridableHttpConfig->slow_log_threshold, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_GENERATION:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_generation_number, conv);
    break;
  case TS_CONFIG_BODY_FACTORY_TEMPLATE_BASE:
    ret = _memberp_to_generic(&overridableHttpConfig->body_factory_template_base, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_OPEN_WRITE_FAIL_ACTION:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_open_write_fail_action, conv);
    break;
  case TS_CONFIG_HTTP_NUMBER_OF_REDIRECTIONS:
    ret = _memberp_to_generic(&overridableHttpConfig->number_of_redirections, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_MAX_OPEN_WRITE_RETRIES:
    ret = _memberp_to_generic(&overridableHttpConfig->max_cache_open_write_retries, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_MAX_OPEN_WRITE_RETRY_TIMEOUT:
    ret = _memberp_to_generic(&overridableHttpConfig->max_cache_open_write_retry_timeout, conv);
    break;
  case TS_CONFIG_HTTP_REDIRECT_USE_ORIG_CACHE_KEY:
    ret = _memberp_to_generic(&overridableHttpConfig->redirect_use_orig_cache_key, conv);
    break;
  case TS_CONFIG_HTTP_ATTACH_SERVER_SESSION_TO_CLIENT:
    ret = _memberp_to_generic(&overridableHttpConfig->attach_server_session_to_client, conv);
    break;
  case TS_CONFIG_HTTP_MAX_PROXY_CYCLES:
    ret = _memberp_to_generic(&overridableHttpConfig->max_proxy_cycles, conv);
    break;
  case TS_CONFIG_WEBSOCKET_NO_ACTIVITY_TIMEOUT:
    ret = _memberp_to_generic(&overridableHttpConfig->websocket_inactive_timeout, conv);
    break;
  case TS_CONFIG_WEBSOCKET_ACTIVE_TIMEOUT:
    ret = _memberp_to_generic(&overridableHttpConfig->websocket_active_timeout, conv);
    break;
  case TS_CONFIG_HTTP_UNCACHEABLE_REQUESTS_BYPASS_PARENT:
    ret = _memberp_to_generic(&overridableHttpConfig->uncacheable_requests_bypass_parent, conv);
    break;
  case TS_CONFIG_HTTP_PARENT_PROXY_TOTAL_CONNECT_ATTEMPTS:
    ret = _memberp_to_generic(&overridableHttpConfig->parent_connect_attempts, conv);
    break;
  case TS_CONFIG_HTTP_TRANSACTION_ACTIVE_TIMEOUT_IN:
    ret = _memberp_to_generic(&overridableHttpConfig->transaction_active_timeout_in, conv);
    break;
  case TS_CONFIG_SRV_ENABLED:
    ret = _memberp_to_generic(&overridableHttpConfig->srv_enabled, conv);
    break;
  case TS_CONFIG_HTTP_FORWARD_CONNECT_METHOD:
    ret = _memberp_to_generic(&overridableHttpConfig->forward_connect_method, conv);
    break;
  case TS_CONFIG_SSL_CLIENT_VERIFY_SERVER_POLICY:
  case TS_CONFIG_SSL_CLIENT_VERIFY_SERVER_PROPERTIES:
  case TS_CONFIG_SSL_CLIENT_SNI_POLICY:
  case TS_CONFIG_SSL_CLIENT_CERT_FILENAME:
  case TS_CONFIG_SSL_CERT_FILEPATH:
  case TS_CONFIG_SSL_CLIENT_PRIVATE_KEY_FILENAME:
  case TS_CONFIG_SSL_CLIENT_CA_CERT_FILENAME:
  case TS_CONFIG_SSL_CLIENT_ALPN_PROTOCOLS:
    // String, must be handled elsewhere
    break;
  case TS_CONFIG_PARENT_FAILURES_UPDATE_HOSTDB:
    ret = _memberp_to_generic(&overridableHttpConfig->parent_failures_update_hostdb, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_IGNORE_ACCEPT_MISMATCH:
    ret = _memberp_to_generic(&overridableHttpConfig->ignore_accept_mismatch, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_IGNORE_ACCEPT_LANGUAGE_MISMATCH:
    ret = _memberp_to_generic(&overridableHttpConfig->ignore_accept_language_mismatch, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_IGNORE_ACCEPT_ENCODING_MISMATCH:
    ret = _memberp_to_generic(&overridableHttpConfig->ignore_accept_encoding_mismatch, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_IGNORE_ACCEPT_CHARSET_MISMATCH:
    ret = _memberp_to_generic(&overridableHttpConfig->ignore_accept_charset_mismatch, conv);
    break;
  case TS_CONFIG_HTTP_PARENT_PROXY_FAIL_THRESHOLD:
    ret = _memberp_to_generic(&overridableHttpConfig->parent_fail_threshold, conv);
    break;
  case TS_CONFIG_HTTP_PARENT_PROXY_RETRY_TIME:
    ret = _memberp_to_generic(&overridableHttpConfig->parent_retry_time, conv);
    break;
  case TS_CONFIG_HTTP_PER_PARENT_CONNECT_ATTEMPTS:
    ret = _memberp_to_generic(&overridableHttpConfig->per_parent_connect_attempts, conv);
    break;
  case TS_CONFIG_HTTP_ALLOW_MULTI_RANGE:
    ret = _memberp_to_generic(&overridableHttpConfig->allow_multi_range, conv);
    break;
  case TS_CONFIG_HTTP_ALLOW_HALF_OPEN:
    ret = _memberp_to_generic(&overridableHttpConfig->allow_half_open, conv);
    break;
  case TS_CONFIG_HTTP_PER_SERVER_CONNECTION_MAX:
    ret  = &overridableHttpConfig->connection_tracker_config.server_max;
    conv = &ConnectionTracker::MAX_SERVER_CONV;
    break;
  case TS_CONFIG_HTTP_SERVER_MIN_KEEP_ALIVE_CONNS:
    ret  = &overridableHttpConfig->connection_tracker_config.server_min;
    conv = &ConnectionTracker::MIN_SERVER_CONV;
    break;
  case TS_CONFIG_HTTP_PER_SERVER_CONNECTION_MATCH:
    ret  = &overridableHttpConfig->connection_tracker_config.server_match;
    conv = &ConnectionTracker::SERVER_MATCH_CONV;
    break;
  case TS_CONFIG_HTTP_HOST_RESOLUTION_PREFERENCE:
    ret  = &overridableHttpConfig->host_res_data;
    conv = &HttpTransact::HOST_RES_CONV;
    break;
  case TS_CONFIG_HTTP_NO_DNS_JUST_FORWARD_TO_PARENT:
    ret = _memberp_to_generic(&overridableHttpConfig->no_dns_forward_to_parent, conv);
    break;
  case TS_CONFIG_PLUGIN_VC_DEFAULT_BUFFER_INDEX:
    ret = _memberp_to_generic(&overridableHttpConfig->plugin_vc_default_buffer_index, conv);
    break;
  case TS_CONFIG_PLUGIN_VC_DEFAULT_BUFFER_WATER_MARK:
    ret = _memberp_to_generic(&overridableHttpConfig->plugin_vc_default_buffer_water_mark, conv);
    break;
  case TS_CONFIG_NET_SOCK_NOTSENT_LOWAT:
    ret = _memberp_to_generic(&overridableHttpConfig->sock_packet_notsent_lowat, conv);
    break;
  case TS_CONFIG_BODY_FACTORY_RESPONSE_SUPPRESSION_MODE:
    ret = _memberp_to_generic(&overridableHttpConfig->response_suppression_mode, conv);
    break;
  case TS_CONFIG_HTTP_ENABLE_PARENT_TIMEOUT_MARKDOWNS:
    ret = _memberp_to_generic(&overridableHttpConfig->enable_parent_timeout_markdowns, conv);
    break;
  case TS_CONFIG_HTTP_DISABLE_PARENT_MARKDOWNS:
    ret = _memberp_to_generic(&overridableHttpConfig->disable_parent_markdowns, conv);
    break;
  case TS_CONFIG_NET_DEFAULT_INACTIVITY_TIMEOUT:
    ret = _memberp_to_generic(&overridableHttpConfig->default_inactivity_timeout, conv);
    break;
  case TS_CONFIG_HTTP_CACHE_CACHE_URLS_THAT_LOOK_DYNAMIC:
    ret = _memberp_to_generic(&overridableHttpConfig->cache_urls_that_look_dynamic, conv);
    break;

  // This helps avoiding compiler warnings, yet detect unhandled enum members.
  case TS_CONFIG_NULL:
  case TS_CONFIG_LAST_ENTRY:
    break;
  }

  return ret;
}

// 2nd little helper function to find the struct member for getting.
static const void *
_conf_to_memberp(TSOverridableConfigKey conf, const OverridableHttpConfigParams *overridableHttpConfig, MgmtConverter const *&conv)
{
  return _conf_to_memberp(conf, const_cast<OverridableHttpConfigParams *>(overridableHttpConfig), conv);
}

/* APIs to manipulate the overridable configuration options.
 */
TSReturnCode
TSHttpTxnConfigIntSet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtInt value)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM              *s = reinterpret_cast<HttpSM *>(txnp);
  MgmtConverter const *conv;

  s->t_state.setup_per_txn_configs();

  void *dest = _conf_to_memberp(conf, &(s->t_state.my_txn_conf()), conv);

  if (!dest || !conv->store_int) {
    return TS_ERROR;
  }

  conv->store_int(dest, value);

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnConfigIntGet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtInt *value)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)value) == TS_SUCCESS);

  HttpSM              *s = reinterpret_cast<HttpSM *>(txnp);
  MgmtConverter const *conv;
  const void          *src = _conf_to_memberp(conf, s->t_state.txn_conf, conv);

  if (!src || !conv->load_int) {
    return TS_ERROR;
  }

  *value = conv->load_int(src);

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnConfigFloatSet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtFloat value)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM              *s = reinterpret_cast<HttpSM *>(txnp);
  MgmtConverter const *conv;

  s->t_state.setup_per_txn_configs();

  void *dest = _conf_to_memberp(conf, &(s->t_state.my_txn_conf()), conv);

  if (!dest || !conv->store_float) {
    return TS_ERROR;
  }

  conv->store_float(dest, value);

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnConfigFloatGet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtFloat *value)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr(static_cast<void *>(value)) == TS_SUCCESS);

  MgmtConverter const *conv;
  const void          *src = _conf_to_memberp(conf, reinterpret_cast<HttpSM *>(txnp)->t_state.txn_conf, conv);

  if (!src || !conv->load_float) {
    return TS_ERROR;
  }
  *value = conv->load_float(src);

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnConfigStringSet(TSHttpTxn txnp, TSOverridableConfigKey conf, const char *value, int length)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  if (length == -1) {
    length = strlen(value);
  }

  HttpSM *s = reinterpret_cast<HttpSM *>(txnp);

  s->t_state.setup_per_txn_configs();

  switch (conf) {
  case TS_CONFIG_HTTP_RESPONSE_SERVER_STR:
    if (value && length > 0) {
      s->t_state.my_txn_conf().proxy_response_server_string     = const_cast<char *>(value); // The "core" likes non-const char*
      s->t_state.my_txn_conf().proxy_response_server_string_len = length;
    } else {
      s->t_state.my_txn_conf().proxy_response_server_string     = nullptr;
      s->t_state.my_txn_conf().proxy_response_server_string_len = 0;
    }
    break;
  case TS_CONFIG_HTTP_GLOBAL_USER_AGENT_HEADER:
    if (value && length > 0) {
      s->t_state.my_txn_conf().global_user_agent_header      = const_cast<char *>(value); // The "core" likes non-const char*
      s->t_state.my_txn_conf().global_user_agent_header_size = length;
    } else {
      s->t_state.my_txn_conf().global_user_agent_header      = nullptr;
      s->t_state.my_txn_conf().global_user_agent_header_size = 0;
    }
    break;
  case TS_CONFIG_BODY_FACTORY_TEMPLATE_BASE:
    if (value && length > 0) {
      s->t_state.my_txn_conf().body_factory_template_base     = const_cast<char *>(value);
      s->t_state.my_txn_conf().body_factory_template_base_len = length;
    } else {
      s->t_state.my_txn_conf().body_factory_template_base     = nullptr;
      s->t_state.my_txn_conf().body_factory_template_base_len = 0;
    }
    break;
  case TS_CONFIG_HTTP_INSERT_FORWARDED:
    if (value && length > 0) {
      swoc::LocalBufferWriter<1024> error;
      HttpForwarded::OptionBitSet   bs = HttpForwarded::optStrToBitset(std::string_view(value, length), error);
      if (!error.size()) {
        s->t_state.my_txn_conf().insert_forwarded = bs;
      } else {
        Error("HTTP %.*s", static_cast<int>(error.size()), error.data());
      }
    }
    break;
  case TS_CONFIG_HTTP_SERVER_SESSION_SHARING_MATCH:
    if (value && length > 0) {
      HttpConfig::load_server_session_sharing_match(value, s->t_state.my_txn_conf().server_session_sharing_match);
      s->t_state.my_txn_conf().server_session_sharing_match_str = const_cast<char *>(value);
    }
    break;
  case TS_CONFIG_SSL_CLIENT_VERIFY_SERVER_POLICY:
    if (value && length > 0) {
      s->t_state.my_txn_conf().ssl_client_verify_server_policy = const_cast<char *>(value);
    }
    break;
  case TS_CONFIG_SSL_CLIENT_VERIFY_SERVER_PROPERTIES:
    if (value && length > 0) {
      s->t_state.my_txn_conf().ssl_client_verify_server_properties = const_cast<char *>(value);
    }
    break;
  case TS_CONFIG_SSL_CLIENT_SNI_POLICY:
    if (value && length > 0) {
      s->t_state.my_txn_conf().ssl_client_sni_policy = const_cast<char *>(value);
    }
    break;
  case TS_CONFIG_SSL_CLIENT_CERT_FILENAME:
    if (value && length > 0) {
      s->t_state.my_txn_conf().ssl_client_cert_filename = const_cast<char *>(value);
    }
    break;
  case TS_CONFIG_SSL_CLIENT_PRIVATE_KEY_FILENAME:
    if (value && length > 0) {
      s->t_state.my_txn_conf().ssl_client_private_key_filename = const_cast<char *>(value);
    }
    break;
  case TS_CONFIG_SSL_CLIENT_CA_CERT_FILENAME:
    if (value && length > 0) {
      s->t_state.my_txn_conf().ssl_client_ca_cert_filename = const_cast<char *>(value);
    }
    break;
  case TS_CONFIG_SSL_CLIENT_ALPN_PROTOCOLS:
    if (value && length > 0) {
      s->t_state.my_txn_conf().ssl_client_alpn_protocols = const_cast<char *>(value);
    }
    break;
  case TS_CONFIG_SSL_CERT_FILEPATH:
    /* noop */
    break;
  case TS_CONFIG_HTTP_HOST_RESOLUTION_PREFERENCE:
    if (value && length > 0) {
      s->t_state.my_txn_conf().host_res_data.conf_value = const_cast<char *>(value);
    }
    [[fallthrough]];
  default: {
    if (value && length > 0) {
      MgmtConverter const *conv;
      void                *dest = _conf_to_memberp(conf, &(s->t_state.my_txn_conf()), conv);
      if (dest != nullptr && conv != nullptr && conv->store_string) {
        conv->store_string(dest, std::string_view(value, length));
      } else {
        return TS_ERROR;
      }
    }
    break;
  }
  }

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnConfigStringGet(TSHttpTxn txnp, TSOverridableConfigKey conf, const char **value, int *length)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void **)value) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)length) == TS_SUCCESS);

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  switch (conf) {
  case TS_CONFIG_HTTP_RESPONSE_SERVER_STR:
    *value  = sm->t_state.txn_conf->proxy_response_server_string;
    *length = sm->t_state.txn_conf->proxy_response_server_string_len;
    break;
  case TS_CONFIG_HTTP_GLOBAL_USER_AGENT_HEADER:
    *value  = sm->t_state.txn_conf->global_user_agent_header;
    *length = sm->t_state.txn_conf->global_user_agent_header_size;
    break;
  case TS_CONFIG_BODY_FACTORY_TEMPLATE_BASE:
    *value  = sm->t_state.txn_conf->body_factory_template_base;
    *length = sm->t_state.txn_conf->body_factory_template_base_len;
    break;
  case TS_CONFIG_HTTP_SERVER_SESSION_SHARING_MATCH:
    *value  = sm->t_state.txn_conf->server_session_sharing_match_str;
    *length = *value ? strlen(*value) : 0;
    break;
  default: {
    MgmtConverter const *conv;
    const void          *src = _conf_to_memberp(conf, sm->t_state.txn_conf, conv);
    if (src != nullptr && conv != nullptr && conv->load_string) {
      auto sv = conv->load_string(src);
      *value  = sv.data();
      *length = sv.size();
    } else {
      return TS_ERROR;
    }
    break;
  }
  }

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnConfigFind(const char *name, int length, TSOverridableConfigKey *conf, TSRecordDataType *type)
{
  sdk_assert(sdk_sanity_check_null_ptr(name) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr(conf) == TS_SUCCESS);

  std::string_view name_sv(name, length < 0 ? strlen(name) : length);
  if (auto config = ts::Overridable_Txn_Vars.find(name_sv); config != ts::Overridable_Txn_Vars.end()) {
    std::tie(*conf, *type) = config->second;
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSHttpTxnPrivateSessionSet(TSHttpTxn txnp, int private_session)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return TS_ERROR;
  }
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  if (sm->set_server_session_private(private_session)) {
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

// APIs to register new Mgmt (records) entries.
TSReturnCode
TSMgmtStringCreate(TSRecordType rec_type, const char *name, const TSMgmtString data_default, TSRecordUpdateType update_type,
                   TSRecordCheckType check_type, const char *check_regex, TSRecordAccessType access_type)
{
  if (check_regex == nullptr && check_type != TS_RECORDCHECK_NULL) {
    return TS_ERROR;
  }
  if (REC_ERR_OKAY != RecRegisterConfigString(static_cast<enum RecT>(rec_type), name, data_default,
                                              static_cast<enum RecUpdateT>(update_type), static_cast<enum RecCheckT>(check_type),
                                              check_regex, REC_SOURCE_PLUGIN, static_cast<enum RecAccessT>(access_type))) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
TSMgmtIntCreate(TSRecordType rec_type, const char *name, TSMgmtInt data_default, TSRecordUpdateType update_type,
                TSRecordCheckType check_type, const char *check_regex, TSRecordAccessType access_type)
{
  if (check_regex == nullptr && check_type != TS_RECORDCHECK_NULL) {
    return TS_ERROR;
  }
  if (REC_ERR_OKAY != RecRegisterConfigInt(static_cast<enum RecT>(rec_type), name, static_cast<RecInt>(data_default),
                                           static_cast<enum RecUpdateT>(update_type), static_cast<enum RecCheckT>(check_type),
                                           check_regex, REC_SOURCE_PLUGIN, static_cast<enum RecAccessT>(access_type))) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnCloseAfterResponse(TSHttpTxn txnp, int should_close)
{
  if (sdk_sanity_check_txn(txnp) != TS_SUCCESS) {
    return TS_ERROR;
  }

  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  if (should_close) {
    sm->t_state.client_info.keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
    if (sm->get_ua_txn()) {
      sm->set_ua_half_close_flag();
    }
  }
  // Don't change if PIPELINE is set...
  else if (sm->t_state.client_info.keep_alive == HTTPKeepAlive::NO_KEEPALIVE) {
    sm->t_state.client_info.keep_alive = HTTPKeepAlive::KEEPALIVE;
  }

  return TS_SUCCESS;
}

// Parse a port descriptor for the proxy.config.http.server_ports descriptor format.
TSPortDescriptor
TSPortDescriptorParse(const char *descriptor)
{
  HttpProxyPort *port = new HttpProxyPort();

  if (descriptor && port->processOptions(descriptor)) {
    return reinterpret_cast<TSPortDescriptor>(port);
  }

  delete port;
  return nullptr;
}

TSReturnCode
TSPortDescriptorAccept(TSPortDescriptor descp, TSCont contp)
{
  Action                     *action = nullptr;
  HttpProxyPort              *port   = reinterpret_cast<HttpProxyPort *>(descp);
  NetProcessor::AcceptOptions net(make_net_accept_options(port, -1 /* nthreads */));

  if (port->isSSL()) {
    action = sslNetProcessor.main_accept(reinterpret_cast<INKContInternal *>(contp), port->m_fd, net);
  } else {
    action = netProcessor.main_accept(reinterpret_cast<INKContInternal *>(contp), port->m_fd, net);
  }

  return action ? TS_SUCCESS : TS_ERROR;
}

TSReturnCode
TSPluginDescriptorAccept(TSCont contp)
{
  Action *action = nullptr;

  HttpProxyPort::Group &proxy_ports = HttpProxyPort::global();
  for (auto &port : proxy_ports) {
    if (port.isPlugin()) {
      NetProcessor::AcceptOptions net(make_net_accept_options(&port, -1 /* nthreads */));
      action = netProcessor.main_accept(reinterpret_cast<INKContInternal *>(contp), port.m_fd, net);
    }
  }
  return action ? TS_SUCCESS : TS_ERROR;
}

int
TSHttpTxnBackgroundFillStarted(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM *s = reinterpret_cast<HttpSM *>(txnp);

  return (s->background_fill == BackgroundFill_t::STARTED);
}

int
TSHttpTxnIsCacheable(TSHttpTxn txnp, TSMBuffer request, TSMBuffer response)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM  *sm = reinterpret_cast<HttpSM *>(txnp);
  HTTPHdr *req, *resp;

  // We allow for either request or response to be empty (or both), in
  // which case we default to the transactions request or response.
  if (request) {
    sdk_assert(sdk_sanity_check_mbuffer(request) == TS_SUCCESS);
    req = reinterpret_cast<HTTPHdr *>(request);
  } else {
    req = &(sm->t_state.hdr_info.client_request);
  }
  if (response) {
    sdk_assert(sdk_sanity_check_mbuffer(response) == TS_SUCCESS);
    resp = reinterpret_cast<HTTPHdr *>(response);
  } else {
    resp = &(sm->t_state.hdr_info.server_response);
  }

  // Make sure these are valid response / requests, then verify if it's cacheable.
  return (req->valid() && resp->valid() && HttpTransact::is_response_cacheable(&(sm->t_state), req, resp)) ? 1 : 0;
}

int
TSHttpTxnGetMaxAge(TSHttpTxn txnp, TSMBuffer response)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM  *sm = reinterpret_cast<HttpSM *>(txnp);
  HTTPHdr *resp;

  if (response) {
    // Make sure the response we got as a parameter is valid
    sdk_assert(sdk_sanity_check_mbuffer(response) == TS_SUCCESS);
    resp = reinterpret_cast<HTTPHdr *>(response);
  } else {
    // Use the transactions origin response if the user passed null
    resp = &(sm->t_state.hdr_info.server_response);
  }

  if (!resp || !resp->valid()) {
    return -1;
  }

  // We have a valid response, return max_age
  return HttpTransact::get_max_age(resp);
}

// Lookup various debug names for common HTTP types.
const char *
TSHttpServerStateNameLookup(TSServerState state)
{
  return HttpDebugNames::get_server_state_name(static_cast<HttpTransact::ServerState_t>(state));
}

const char *
TSHttpHookNameLookup(TSHttpHookID hook)
{
  return HttpDebugNames::get_api_hook_name(static_cast<TSHttpHookID>(hook));
}

const char *
TSHttpEventNameLookup(TSEvent event)
{
  return HttpDebugNames::get_event_name(static_cast<int>(event));
}

/// Re-enable NetVC that has TLSEventSupport.
class TSSslCallback : public Continuation
{
public:
  TSSslCallback(TLSEventSupport *tes, TSEvent event) : Continuation(tes->getMutexForTLSEvents().get()), m_tes(tes), m_event(event)
  {
    SET_HANDLER(&TSSslCallback::event_handler);
  }
  int
  event_handler(int /* event ATS_UNUSED */, void *)
  {
    m_tes->reenable(m_event);
    delete this;
    return 0;
  }

private:
  TLSEventSupport *m_tes;
  TSEvent          m_event;
};

/// SSL Hooks
TSReturnCode
TSVConnTunnel(TSVConn sslp)
{
  NetVConnection    *vc     = reinterpret_cast<NetVConnection *>(sslp);
  SSLNetVConnection *ssl_vc = dynamic_cast<SSLNetVConnection *>(vc);
  TSReturnCode       zret   = TS_SUCCESS;
  if (nullptr != ssl_vc) {
    ssl_vc->hookOpRequested = SslVConnOp::SSL_HOOK_OP_TUNNEL;
  } else {
    zret = TS_ERROR;
  }
  return zret;
}

TSSslConnection
TSVConnSslConnectionGet(TSVConn sslp)
{
  TSSslConnection ssl   = nullptr;
  NetVConnection *netvc = reinterpret_cast<NetVConnection *>(sslp);
  if (auto tbs = netvc->get_service<TLSBasicSupport>(); tbs) {
    ssl = reinterpret_cast<TSSslConnection>(tbs->get_tls_handle());
  }
  return ssl;
}

int
TSVConnFdGet(TSVConn vconnp)
{
  sdk_assert(sdk_sanity_check_null_ptr(vconnp) == TS_SUCCESS);
  NetVConnection *vc = reinterpret_cast<NetVConnection *>(vconnp);
  return vc->get_socket();
}

const char *
TSVConnSslSniGet(TSVConn sslp, int *length)
{
  if (sslp == nullptr) {
    if (length) {
      *length = 0;
    }
    return nullptr;
  }

  char const     *server_name = nullptr;
  NetVConnection *vc          = reinterpret_cast<NetVConnection *>(sslp);
  if (auto snis = vc->get_service<TLSSNISupport>(); snis) {
    server_name = snis->get_sni_server_name();
    if (length) {
      *length = server_name ? strlen(server_name) : 0;
    }
  }

  return server_name;
}

TSSslVerifyCTX
TSVConnSslVerifyCTXGet(TSVConn sslp)
{
  NetVConnection  *vc    = reinterpret_cast<NetVConnection *>(sslp);
  TLSBasicSupport *tlsbs = vc->get_service<TLSBasicSupport>();
  if (tlsbs != nullptr) {
    return reinterpret_cast<TSSslVerifyCTX>(tlsbs->get_tls_cert_to_verify());
  }
  return nullptr;
}

TSSslContext
TSSslContextFindByName(const char *name)
{
  if (nullptr == name || 0 == strlen(name)) {
    // an empty name is an invalid input
    return nullptr;
  }
  TSSslContext   ret    = nullptr;
  SSLCertLookup *lookup = SSLCertificateConfig::acquire();
  if (lookup != nullptr) {
    SSLCertContext *cc = lookup->find(name);
    if (cc) {
      shared_SSL_CTX ctx = cc->getCtx();
      if (ctx) {
        ret = reinterpret_cast<TSSslContext>(ctx.get());
      }
    }
    SSLCertificateConfig::release(lookup);
  }
  return ret;
}
TSSslContext
TSSslContextFindByAddr(struct sockaddr const *addr)
{
  TSSslContext   ret    = nullptr;
  SSLCertLookup *lookup = SSLCertificateConfig::acquire();
  if (lookup != nullptr) {
    IpEndpoint ip;
    ip.assign(addr);
    SSLCertContext *cc = lookup->find(ip);
    if (cc) {
      shared_SSL_CTX ctx = cc->getCtx();
      if (ctx) {
        ret = reinterpret_cast<TSSslContext>(ctx.get());
      }
    }
    SSLCertificateConfig::release(lookup);
  }
  return ret;
}

/**
 * This function sets the secret cache value for a given secret name.  This allows
 * plugins to load cert/key PEM information on for use by the TLS core
 */
TSReturnCode
TSSslSecretSet(const char *secret_name, int secret_name_length, const char *secret_data, int secret_data_len)
{
  TSReturnCode      retval = TS_SUCCESS;
  std::string const secret_name_str{secret_name, static_cast<unsigned>(secret_name_length)};
  SSLConfigParams  *load_params = SSLConfig::load_acquire();
  SSLConfigParams  *params      = SSLConfig::acquire();
  if (load_params != nullptr) { // Update the current data structure
    Dbg(dbg_ctl_ssl_cert_update, "Setting secrets in SSLConfig load for: %.*s", secret_name_length, secret_name);
    load_params->secrets.setSecret(secret_name_str, std::string_view(secret_data, secret_data_len));
    load_params->updateCTX(secret_name_str);
    SSLConfig::load_release(load_params);
  }
  if (params != nullptr) {
    Dbg(dbg_ctl_ssl_cert_update, "Setting secrets in SSLConfig for: %.*s", secret_name_length, secret_name);
    params->secrets.setSecret(secret_name_str, std::string_view(secret_data, secret_data_len));
    params->updateCTX(secret_name_str);
    SSLConfig::release(params);
  }
  return retval;
}

TSReturnCode
TSSslSecretUpdate(const char *secret_name, int secret_name_length)
{
  TSReturnCode     retval = TS_SUCCESS;
  SSLConfigParams *params = SSLConfig::acquire();
  if (params != nullptr) {
    params->updateCTX(std::string(secret_name, secret_name_length));
  }
  SSLConfig::release(params);
  return retval;
}

char *
TSSslSecretGet(const char *secret_name, int secret_name_length, int *secret_data_length)
{
  sdk_assert(secret_name != nullptr);
  sdk_assert(secret_data_length != nullptr);

  bool             loading = true;
  SSLConfigParams *params  = SSLConfig::load_acquire();
  if (params == nullptr) {
    params  = SSLConfig::acquire();
    loading = false;
  }
  std::string const secret_data = params->secrets.getSecret(std::string(secret_name, secret_name_length));
  char             *data{nullptr};
  if (secret_data.empty()) {
    *secret_data_length = 0;

  } else {
    data = static_cast<char *>(ats_malloc(secret_data.size()));
    memcpy(data, secret_data.data(), secret_data.size());
    *secret_data_length = secret_data.size();
  }
  if (loading) {
    SSLConfig::load_release(params);
  } else {
    SSLConfig::release(params);
  }
  return data;
}

/**
 * This function retrieves an array of lookup keys for client contexts loaded in
 * traffic server. Given a 2-level mapping for client contexts, every 2 lookup keys
 * can be used to locate and identify 1 context.
 * @param n Allocated size for result array.
 * @param result Const char pointer arrays to be filled with lookup keys.
 * @param actual Total number of lookup keys.
 */
TSReturnCode
TSSslClientContextsNamesGet(int n, const char **result, int *actual)
{
  sdk_assert(n == 0 || result != nullptr);
  int              idx = 0, count = 0;
  SSLConfigParams *params = SSLConfig::acquire();

  if (params) {
    auto &ctx_map_lock = params->ctxMapLock;
    auto &ca_map       = params->top_level_ctx_map;
    auto  mem          = static_cast<std::string_view *>(alloca(sizeof(std::string_view) * n));
    ink_mutex_acquire(&ctx_map_lock);
    for (auto &ca_pair : ca_map) {
      // Populate mem array with 2 strings each time
      for (auto &ctx_pair : ca_pair.second) {
        if (idx + 1 < n) {
          mem[idx++] = ca_pair.first;
          mem[idx++] = ctx_pair.first;
        }
        count += 2;
      }
    }
    ink_mutex_release(&ctx_map_lock);
    for (int i = 0; i < idx; i++) {
      result[i] = mem[i].data();
    }
  }
  if (actual) {
    *actual = count;
  }
  SSLConfig::release(params);
  return TS_SUCCESS;
}

/**
 * This function returns the client context corresponding to the lookup keys provided.
 * User should call TSSslClientContextsGet() first to determine which lookup keys are
 * present before querying for them. User will need to release the context returned
 * from this function.
 * Returns valid TSSslContext on success and nullptr on failure.
 * @param first_key Key string for the top level.
 * @param second_key Key string for the second level.
 */
TSSslContext
TSSslClientContextFindByName(const char *ca_paths, const char *ck_paths)
{
  if (!ca_paths || !ck_paths || ca_paths[0] == '\0' || ck_paths[0] == '\0') {
    return nullptr;
  }
  SSLConfigParams *params = SSLConfig::acquire();
  TSSslContext     retval = nullptr;
  if (params) {
    ink_mutex_acquire(&params->ctxMapLock);
    auto ca_iter = params->top_level_ctx_map.find(ca_paths);
    if (ca_iter != params->top_level_ctx_map.end()) {
      auto ctx_iter = ca_iter->second.find(ck_paths);
      if (ctx_iter != ca_iter->second.end()) {
        SSL_CTX_up_ref(ctx_iter->second.get());
        retval = reinterpret_cast<TSSslContext>(ctx_iter->second.get());
      }
    }
    ink_mutex_release(&params->ctxMapLock);
  }
  SSLConfig::release(params);
  return retval;
}

TSSslContext
TSSslServerContextCreate(TSSslX509 cert, const char *certname, const char *rsp_file)
{
  TSSslContext     ret    = nullptr;
  SSLConfigParams *config = SSLConfig::acquire();
  if (config != nullptr) {
    ret = reinterpret_cast<TSSslContext>(SSLCreateServerContext(config, nullptr));
    if (ret && SSLConfigParams::ssl_ocsp_enabled && cert && certname) {
      if (SSL_CTX_set_tlsext_status_cb(reinterpret_cast<SSL_CTX *>(ret), ssl_callback_ocsp_stapling)) {
        if (!ssl_stapling_init_cert(reinterpret_cast<SSL_CTX *>(ret), reinterpret_cast<X509 *>(cert), certname, rsp_file)) {
          Warning("failed to configure SSL_CTX for OCSP Stapling info for certificate at %s", certname);
        }
      }
    }
    SSLConfig::release(config);
  }
  return ret;
}

void
TSSslContextDestroy(TSSslContext ctx)
{
  SSLReleaseContext(reinterpret_cast<SSL_CTX *>(ctx));
}

TSReturnCode
TSSslClientCertUpdate(const char *cert_path, const char *key_path)
{
  if (nullptr == cert_path) {
    return TS_ERROR;
  }

  std::string      key;
  shared_SSL_CTX   client_ctx = nullptr;
  SSLConfigParams *params     = SSLConfig::acquire();

  // Generate second level key for client context lookup
  swoc::bwprint(key, "{}:{}", cert_path, key_path);
  Dbg(dbg_ctl_ssl_cert_update, "TSSslClientCertUpdate(): Use %.*s as key for lookup", static_cast<int>(key.size()), key.data());

  if (nullptr != params) {
    // Try to update client contexts maps
    auto       &ca_paths_map = params->top_level_ctx_map;
    auto       &map_lock     = params->ctxMapLock;
    std::string ca_paths_key;
    // First try to locate the client context and its CA path (by top level)
    ink_mutex_acquire(&map_lock);
    for (auto &ca_paths_pair : ca_paths_map) {
      auto &ctx_map = ca_paths_pair.second;
      auto  iter    = ctx_map.find(key);
      if (iter != ctx_map.end() && iter->second != nullptr) {
        ca_paths_key = ca_paths_pair.first;
        break;
      }
    }
    ink_mutex_release(&map_lock);

    // Only update on existing
    if (ca_paths_key.empty()) {
      return TS_ERROR;
    }

    // Extract CA related paths
    size_t      sep            = ca_paths_key.find(':');
    std::string ca_bundle_file = ca_paths_key.substr(0, sep);
    std::string ca_bundle_path = ca_paths_key.substr(sep + 1);

    // Build new client context
    client_ctx =
      shared_SSL_CTX(SSLCreateClientContext(params, ca_bundle_path.empty() ? nullptr : ca_bundle_path.c_str(),
                                            ca_bundle_file.empty() ? nullptr : ca_bundle_file.c_str(), cert_path, key_path),
                     SSL_CTX_free);

    // Successfully generates a client context, update in the map
    ink_mutex_acquire(&map_lock);
    auto iter = ca_paths_map.find(ca_paths_key);
    if (iter != ca_paths_map.end() && iter->second.count(key)) {
      iter->second[key] = client_ctx;
    } else {
      client_ctx = nullptr;
    }
    ink_mutex_release(&map_lock);
  }

  return client_ctx ? TS_SUCCESS : TS_ERROR;
}

TSReturnCode
TSSslServerCertUpdate(const char *cert_path, const char *key_path)
{
  if (nullptr == cert_path) {
    return TS_ERROR;
  }

  if (!key_path || key_path[0] == '\0') {
    key_path = cert_path;
  }

  SSLCertContext       *cc       = nullptr;
  shared_SSL_CTX        test_ctx = nullptr;
  std::shared_ptr<X509> cert     = nullptr;

  SSLConfig::scoped_config            config;
  SSLCertificateConfig::scoped_config lookup;

  if (lookup && config) {
    // Read cert from path to extract lookup key (common name)
    scoped_BIO bio(BIO_new_file(cert_path, "r"));
    if (bio) {
      cert = std::shared_ptr<X509>(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
    }
    if (!bio || !cert) {
      SSLError("Failed to load certificate/key from %s", cert_path);
      return TS_ERROR;
    }

    // Extract common name
    int              pos              = X509_NAME_get_index_by_NID(X509_get_subject_name(cert.get()), NID_commonName, -1);
    X509_NAME_ENTRY *common_name      = X509_NAME_get_entry(X509_get_subject_name(cert.get()), pos);
    ASN1_STRING     *common_name_asn1 = X509_NAME_ENTRY_get_data(common_name);
    char *common_name_str = reinterpret_cast<char *>(const_cast<unsigned char *>(ASN1_STRING_get0_data(common_name_asn1)));
    if (ASN1_STRING_length(common_name_asn1) != static_cast<int>(strlen(common_name_str))) {
      // Embedded null char
      return TS_ERROR;
    }
    Dbg(dbg_ctl_ssl_cert_update, "Updating from %s with common name %s", cert_path, common_name_str);

    // Update context to use cert
    cc = lookup->find(common_name_str);
    if (cc && cc->getCtx()) {
      test_ctx = shared_SSL_CTX(SSLCreateServerContext(config, cc->userconfig.get(), cert_path, key_path), SSLReleaseContext);
      if (!test_ctx) {
        return TS_ERROR;
      }
      // Atomic Swap
      cc->setCtx(test_ctx);
      return TS_SUCCESS;
    }
  }

  return TS_ERROR;
}

TSReturnCode
TSSslTicketKeyUpdate(char *ticketData, int ticketDataLen)
{
  return SSLTicketKeyConfig::reconfigure_data(ticketData, ticketDataLen) ? TS_SUCCESS : TS_ERROR;
}

TSReturnCode
TSVConnProtocolEnable(TSVConn connp, const char *protocol_name)
{
  TSReturnCode retval       = TS_ERROR;
  int          protocol_idx = globalSessionProtocolNameRegistry.toIndexConst(std::string_view{protocol_name});
  auto         net_vc       = reinterpret_cast<UnixNetVConnection *>(connp);
  if (auto alpn = net_vc->get_service<ALPNSupport>(); alpn) {
    alpn->enableProtocol(protocol_idx);
    retval = TS_SUCCESS;
  }
  return retval;
}

TSReturnCode
TSVConnProtocolDisable(TSVConn connp, const char *protocol_name)
{
  TSReturnCode retval       = TS_ERROR;
  int          protocol_idx = globalSessionProtocolNameRegistry.toIndexConst(std::string_view{protocol_name});
  auto         net_vc       = reinterpret_cast<UnixNetVConnection *>(connp);
  if (auto alpn = net_vc->get_service<ALPNSupport>(); alpn) {
    alpn->disableProtocol(protocol_idx);
    retval = TS_SUCCESS;
  }
  return retval;
}

TSAcceptor
TSAcceptorGet(TSVConn sslp)
{
  NetVConnection    *vc     = reinterpret_cast<NetVConnection *>(sslp);
  SSLNetVConnection *ssl_vc = dynamic_cast<SSLNetVConnection *>(vc);
  return ssl_vc ? reinterpret_cast<TSAcceptor>(ssl_vc->accept_object) : nullptr;
}

TSAcceptor
TSAcceptorGetbyID(int ID)
{
  SCOPED_MUTEX_LOCK(lock, naVecMutex, this_ethread());
  auto ret = naVec.at(ID);
  Dbg(dbg_ctl_ssl, "getNetAccept in INK API.cc %p", ret);
  return reinterpret_cast<TSAcceptor>(ret);
}

int
TSAcceptorIDGet(TSAcceptor acceptor)
{
  NetAccept *na = reinterpret_cast<NetAccept *>(acceptor);
  return na ? na->id : -1;
}

int
TSAcceptorCount()
{
  SCOPED_MUTEX_LOCK(lock, naVecMutex, this_ethread());
  return naVec.size();
}

int
TSVConnIsSsl(TSVConn sslp)
{
  NetVConnection    *vc     = reinterpret_cast<NetVConnection *>(sslp);
  SSLNetVConnection *ssl_vc = dynamic_cast<SSLNetVConnection *>(vc);
  return ssl_vc != nullptr;
}

int
TSVConnProvidedSslCert(TSVConn sslp)
{
  NetVConnection *vc = reinterpret_cast<NetVConnection *>(sslp);
  return vc->provided_cert();
}

void
TSVConnReenable(TSVConn vconn)
{
  TSVConnReenableEx(vconn, TS_EVENT_CONTINUE);
}

void
TSVConnReenableEx(TSVConn vconn, TSEvent event)
{
  NetVConnection *vc = reinterpret_cast<NetVConnection *>(vconn);

  if (auto tes = vc->get_service<TLSEventSupport>(); tes) {
    EThread *eth = this_ethread();

    // We use the mutex of VC's NetHandler so we can put the VC into ready_list by reenable()
    Ptr<ProxyMutex> m = tes->getMutexForTLSEvents();
    MUTEX_TRY_LOCK(trylock, m, eth);
    if (trylock.is_locked()) {
      tes->reenable(event);
    } else {
      // We schedule the reenable to the home thread of ssl_vc.
      tes->getThreadForTLSEvents()->schedule_imm(new TSSslCallback(tes, event));
    }
  }
}

TSReturnCode
TSVConnPPInfoGet(TSVConn vconn, uint16_t key, const char **value, int *length)
{
  NetVConnection *vc = reinterpret_cast<NetVConnection *>(vconn);

  if (key < 0x100) {
    auto &tlv = vc->get_proxy_protocol_info().tlv;
    if (auto ite = tlv.find(key); ite != tlv.end()) {
      *value  = ite->second.data();
      *length = ite->second.length();
    } else {
      return TS_ERROR;
    }
  } else {
    switch (key) {
    case TS_PP_INFO_SRC_ADDR:
      *value = reinterpret_cast<const char *>(vc->get_proxy_protocol_src_addr());
      if (*value == nullptr) {
        return TS_ERROR;
      }
      *length = ats_ip_size(reinterpret_cast<const sockaddr *>(*value));
      break;
    case TS_PP_INFO_DST_ADDR:
      *value = reinterpret_cast<const char *>(vc->get_proxy_protocol_dst_addr());
      if (*value == nullptr) {
        return TS_ERROR;
      }
      *length = ats_ip_size(reinterpret_cast<const sockaddr *>(*value));
      break;
    default:
      return TS_ERROR;
    }
  }

  return TS_SUCCESS;
}

TSReturnCode
TSVConnPPInfoIntGet(TSVConn vconn, uint16_t key, TSMgmtInt *value)
{
  NetVConnection *vc = reinterpret_cast<NetVConnection *>(vconn);

  if (key < 0x100) {
    // Unknown type value cannot be returned as an integer
    return TS_ERROR;
  } else {
    switch (key) {
    case TS_PP_INFO_VERSION:
      *value = static_cast<TSMgmtInt>(vc->get_proxy_protocol_version());
      break;
    case TS_PP_INFO_SRC_PORT:
      *value = static_cast<TSMgmtInt>(vc->get_proxy_protocol_src_port());
      break;
    case TS_PP_INFO_DST_PORT:
      *value = static_cast<TSMgmtInt>(vc->get_proxy_protocol_dst_port());
      break;
    case TS_PP_INFO_PROTOCOL:
      *value = static_cast<TSMgmtInt>(vc->get_proxy_protocol_info().ip_family);
      break;
    case TS_PP_INFO_SOCK_TYPE:
      *value = static_cast<TSMgmtInt>(vc->get_proxy_protocol_info().type);
      break;
    default:
      return TS_ERROR;
    }
  }

  return TS_SUCCESS;
}

TSSslSession
TSSslSessionGet(const TSSslSessionID *session_id)
{
  SSL_SESSION *session = nullptr;
  if (session_id && session_cache) {
    session_cache->getSession(reinterpret_cast<const SSLSessionID &>(*session_id), &session, nullptr);
  }
  return reinterpret_cast<TSSslSession>(session);
}

int
TSSslSessionGetBuffer(const TSSslSessionID *session_id, char *buffer, int *len_ptr)
{
  int true_len = 0;
  // Don't get if there is no session id or the cache is not yet set up
  if (session_id && session_cache && len_ptr) {
    true_len = session_cache->getSessionBuffer(reinterpret_cast<const SSLSessionID &>(*session_id), buffer, *len_ptr);
  }
  return true_len;
}

TSReturnCode
TSSslSessionInsert(const TSSslSessionID *session_id, TSSslSession add_session, TSSslConnection ssl_conn)
{
  // Don't insert if there is no session id or the cache is not yet set up
  if (session_id && session_cache) {
    if (dbg_ctl_ssl_session_cache_insert.on()) {
      const SSLSessionID *sid = reinterpret_cast<const SSLSessionID *>(session_id);
      char                buf[sid->len * 2 + 1];
      sid->toString(buf, sizeof(buf));
      DbgPrint(dbg_ctl_ssl_session_cache_insert, "TSSslSessionInsert: Inserting session '%s' ", buf);
    }
    SSL_SESSION *session = reinterpret_cast<SSL_SESSION *>(add_session);
    SSL         *ssl     = reinterpret_cast<SSL *>(ssl_conn);
    session_cache->insertSession(reinterpret_cast<const SSLSessionID &>(*session_id), session, ssl);
    // insertSession returns void, assume all went well
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSSslSessionRemove(const TSSslSessionID *session_id)
{
  // Don't remove if there is no session id or the cache is not yet set up
  if (session_id && session_cache) {
    session_cache->removeSession(reinterpret_cast<const SSLSessionID &>(*session_id));
    // removeSession returns void, assume all went well
    return TS_SUCCESS;
  } else {
    return TS_ERROR;
  }
}

// APIs for managing and using UUIDs.
TSUuid
TSUuidCreate()
{
  ATSUuid *uuid = new ATSUuid();
  return reinterpret_cast<TSUuid>(uuid);
}

void
TSUuidDestroy(TSUuid uuid)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)uuid) == TS_SUCCESS);
  delete reinterpret_cast<ATSUuid *>(uuid);
}

TSReturnCode
TSUuidCopy(TSUuid dest, const TSUuid src)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)dest) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)src) == TS_SUCCESS);
  ATSUuid *d = reinterpret_cast<ATSUuid *>(dest);
  ATSUuid *s = reinterpret_cast<ATSUuid *>(src);

  if (s->valid()) {
    *d = *s;
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSUuidInitialize(TSUuid uuid, TSUuidVersion v)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)uuid) == TS_SUCCESS);
  ATSUuid *u = reinterpret_cast<ATSUuid *>(uuid);

  u->initialize(v);
  return u->valid() ? TS_SUCCESS : TS_ERROR;
}

TSUuid
TSProcessUuidGet()
{
  Machine *machine = Machine::instance();
  return reinterpret_cast<TSUuid>(&machine->uuid);
}

const char *
TSUuidStringGet(const TSUuid uuid)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)uuid) == TS_SUCCESS);
  ATSUuid *u = reinterpret_cast<ATSUuid *>(uuid);

  if (u->valid()) {
    return u->getString();
  }

  return nullptr;
}

TSReturnCode
TSClientRequestUuidGet(TSHttpTxn txnp, char *uuid_str)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)uuid_str) == TS_SUCCESS);

  HttpSM     *sm      = reinterpret_cast<HttpSM *>(txnp);
  const char *machine = const_cast<char *>(Machine::instance()->uuid.getString());
  int         len;

  len = snprintf(uuid_str, TS_CRUUID_STRING_LEN + 1, "%s-%" PRId64 "", machine, sm->sm_id);
  if (len > TS_CRUUID_STRING_LEN) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
TSUuidStringParse(TSUuid uuid, const char *str)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)uuid) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)str) == TS_SUCCESS);
  ATSUuid *u = reinterpret_cast<ATSUuid *>(uuid);

  if (u->parseString(str)) {
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSUuidVersion
TSUuidVersionGet(TSUuid uuid)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)uuid) == TS_SUCCESS);
  ATSUuid *u = reinterpret_cast<ATSUuid *>(uuid);

  return u->version();
}

// Expose the HttpSM's sequence number (ID)
uint64_t
TSHttpTxnIdGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  return static_cast<uint64_t>(sm->sm_id);
}

// Returns unique client session identifier
int64_t
TSHttpSsnIdGet(TSHttpSsn ssnp)
{
  sdk_assert(sdk_sanity_check_http_ssn(ssnp) == TS_SUCCESS);
  ProxySession const *cs = reinterpret_cast<ProxySession *>(ssnp);
  return cs->connection_id();
}

// Return information about the protocols used by the client
TSReturnCode
TSHttpTxnClientProtocolStackGet(TSHttpTxn txnp, int count, const char **result, int *actual)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(count == 0 || result != nullptr);
  HttpSM *sm        = reinterpret_cast<HttpSM *>(txnp);
  int     new_count = 0;
  if (sm && count > 0) {
    auto mem  = static_cast<std::string_view *>(alloca(sizeof(std::string_view) * count));
    new_count = sm->populate_client_protocol(mem, count);
    for (int i = 0; i < new_count; ++i) {
      result[i] = mem[i].data();
    }
  }
  if (actual) {
    *actual = new_count;
  }
  return TS_SUCCESS;
}

TSReturnCode
TSHttpSsnClientProtocolStackGet(TSHttpSsn ssnp, int count, const char **result, int *actual)
{
  sdk_assert(sdk_sanity_check_http_ssn(ssnp) == TS_SUCCESS);
  sdk_assert(count == 0 || result != nullptr);
  auto const *cs        = reinterpret_cast<ProxySession *>(ssnp);
  int         new_count = 0;
  if (cs && count > 0) {
    auto mem  = static_cast<std::string_view *>(alloca(sizeof(std::string_view) * count));
    new_count = cs->populate_protocol(mem, count);
    for (int i = 0; i < new_count; ++i) {
      result[i] = mem[i].data();
    }
  }
  if (actual) {
    *actual = new_count;
  }
  return TS_SUCCESS;
}

// Return information about the protocols used by the server
TSReturnCode
TSHttpTxnServerProtocolStackGet(TSHttpTxn txnp, int count, const char **result, int *actual)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(count == 0 || result != nullptr);
  HttpSM *sm        = reinterpret_cast<HttpSM *>(txnp);
  int     new_count = 0;
  if (sm && count > 0) {
    auto mem  = static_cast<std::string_view *>(alloca(sizeof(std::string_view) * count));
    new_count = sm->populate_server_protocol(mem, count);
    for (int i = 0; i < new_count; ++i) {
      result[i] = mem[i].data();
    }
  }
  if (actual) {
    *actual = new_count;
  }
  return TS_SUCCESS;
}

const char *
TSNormalizedProtocolTag(const char *tag)
{
  return RecNormalizeProtoTag(tag);
}

const char *
TSHttpTxnClientProtocolStackContains(TSHttpTxn txnp, const char *tag)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->client_protocol_contains(std::string_view{tag});
}

const char *
TSHttpSsnClientProtocolStackContains(TSHttpSsn ssnp, const char *tag)
{
  sdk_assert(sdk_sanity_check_http_ssn(ssnp) == TS_SUCCESS);
  ProxySession *cs = reinterpret_cast<ProxySession *>(ssnp);
  return cs->protocol_contains(std::string_view{tag});
}

const char *
TSHttpTxnServerProtocolStackContains(TSHttpTxn txnp, const char *tag)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return sm->server_protocol_contains(std::string_view{tag});
}

const char *
TSRegisterProtocolTag(const char * /* tag ATS_UNUSED */)
{
  return nullptr;
}

TSReturnCode
TSHttpTxnRedoCacheLookup(TSHttpTxn txnp, const char *url, int length)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);

  HttpSM              *sm = reinterpret_cast<HttpSM *>(txnp);
  HttpTransact::State *s  = &(sm->t_state);
  sdk_assert(s->next_action == HttpTransact::StateMachineAction_t::CACHE_LOOKUP);

  // Because of where this is in the state machine, the storage for the cache_info URL must
  // have already been initialized and @a lookup_url must be valid.
  auto result = s->cache_info.lookup_url->parse(url, length < 0 ? strlen(url) : length);
  if (ParseResult::DONE == result) {
    s->transact_return_point = nullptr;
    sm->rewind_state_machine();
    return TS_SUCCESS;
  }
  return TS_ERROR;
}

namespace
{
// Function that contains the common logic for TSRemapFrom/ToUrlGet().
//
TSReturnCode
remapUrlGet(TSHttpTxn txnp, TSMLoc *urlLocp, URL *(UrlMappingContainer::*mfp)() const)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr(urlLocp) == TS_SUCCESS);
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);

  URL *url = (sm->t_state.url_map.*mfp)();
  if (url == nullptr) {
    return TS_ERROR;
  }

  auto urlImpl = url->m_url_impl;
  if (urlImpl == nullptr) {
    return TS_ERROR;
  }

  *urlLocp = reinterpret_cast<TSMLoc>(urlImpl);

  return TS_SUCCESS;
}

} // end anonymous namespace

TSReturnCode
TSRemapFromUrlGet(TSHttpTxn txnp, TSMLoc *urlLocp)
{
  return remapUrlGet(txnp, urlLocp, &UrlMappingContainer::getFromURL);
}

TSReturnCode
TSRemapToUrlGet(TSHttpTxn txnp, TSMLoc *urlLocp)
{
  return remapUrlGet(txnp, urlLocp, &UrlMappingContainer::getToURL);
}

void *
TSRemapDLHandleGet(TSRemapPluginInfo plugin_info)
{
  sdk_assert(sdk_sanity_check_null_ptr(plugin_info) == TS_SUCCESS);
  RemapPluginInfo *info = reinterpret_cast<RemapPluginInfo *>(plugin_info);

  return info->dlh();
}

TSReturnCode
TSHostnameIsSelf(const char *hostname, size_t hostname_len)
{
  return Machine::instance()->is_self(std::string_view{hostname, hostname_len}) ? TS_SUCCESS : TS_ERROR;
}

TSReturnCode
TSHostStatusGet(const char *hostname, const size_t hostname_len, TSHostStatus *status, unsigned int *reason)
{
  HostStatRec *hst = HostStatus::instance().getHostStatus(std::string_view(hostname, hostname_len));
  if (hst == nullptr) {
    return TS_ERROR;
  }
  if (status != nullptr) {
    *status = hst->status;
  }
  if (reason != nullptr) {
    *reason = hst->reasons;
  }
  return TS_SUCCESS;
}

void
TSHostStatusSet(const char *hostname, const size_t hostname_len, TSHostStatus status, const unsigned int down_time,
                const unsigned int reason)
{
  HostStatus::instance().setHostStatus(std::string_view(hostname, hostname_len), status, down_time, reason);
}

// TSHttpTxnResponseActionSet takes a ResponseAction and sets it as the behavior for finding the next parent.
// Be aware ATS will never change this outside a plugin. Therefore, plugins which set the ResponseAction
// to retry must also un-set it after the subsequent success or failure, or ATS will retry forever!
//
// The passed *action must not be null, and is copied and may be destroyed after this call returns.
// Callers must maintain owernship of action.hostname, and its lifetime must exceed the transaction.
void
TSHttpTxnResponseActionSet(TSHttpTxn txnp, TSResponseAction *action)
{
  HttpSM              *sm    = reinterpret_cast<HttpSM *>(txnp);
  HttpTransact::State *s     = &(sm->t_state);
  s->response_action.handled = true;
  s->response_action.action  = *action;
}

// Get the ResponseAction set by a plugin.
//
// The action is an out-param and must point to a valid location.
// The returned action.hostname must not be modified, and is owned by some plugin if not null.
//
// The action members will always be zero, if no plugin has called TSHttpTxnResponseActionSet.
//
void
TSHttpTxnResponseActionGet(TSHttpTxn txnp, TSResponseAction *action)
{
  HttpSM              *sm = reinterpret_cast<HttpSM *>(txnp);
  HttpTransact::State *s  = &(sm->t_state);
  if (!s->response_action.handled) {
    memset(action, 0, sizeof(TSResponseAction)); // because {0} gives a C++ warning. Ugh.
  } else {
    *action = s->response_action.action;
  }
}

TSIOBufferReader
TSHttpTxnPostBufferReaderGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM *sm = reinterpret_cast<HttpSM *>(txnp);
  return reinterpret_cast<TSIOBufferReader>(sm->get_postbuf_clone_reader());
}

TSRPCProviderHandle
TSRPCRegister(const char *provider_name, size_t provider_len, const char *yaml_version, size_t yamlcpp_lib_len)
{
  sdk_assert(sdk_sanity_check_null_ptr(yaml_version) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr(provider_name) == TS_SUCCESS);

  // We want to make sure that plugins are using the same yaml library version as we use internally. Plugins have to cast the
  // TSYaml to the YAML::Node, in order for them to make sure the version compatibility they need to register here and make sure
  // the version is the same.
  if (std::string_view{yaml_version, yamlcpp_lib_len} != YAMLCPP_LIB_VERSION) {
    Dbg(dbg_ctl_rpc_api, "[%.*s] YAML version check failed. Passed='%.*s', expected='%s'", static_cast<int>(provider_len),
        provider_name, static_cast<int>(yamlcpp_lib_len), yaml_version, YAMLCPP_LIB_VERSION);

    return nullptr;
  }

  ::rpc::RPCRegistryInfo *info = new ::rpc::RPCRegistryInfo();
  info->provider               = {provider_name, provider_len};

  return reinterpret_cast<TSRPCProviderHandle>(info);
}

TSReturnCode
TSRPCRegisterMethodHandler(const char *name, size_t name_len, TSRPCMethodCb callback, TSRPCProviderHandle info,
                           const TSRPCHandlerOptions *opt)
{
  sdk_assert(sdk_sanity_check_rpc_handler_options(opt) == TS_SUCCESS);

  if (!::rpc::add_method_handler_from_plugin(
        {name, name_len},
        [callback](std::string_view const &id, const YAML::Node &params) -> void {
          std::string msgId{id.data(), id.size()};
          callback(msgId.c_str(), reinterpret_cast<TSYaml>(const_cast<YAML::Node *>(&params)));
        },
        reinterpret_cast<const ::rpc::RPCRegistryInfo *>(info), *opt)) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

TSReturnCode
TSRPCRegisterNotificationHandler(const char *name, size_t name_len, TSRPCNotificationCb callback, TSRPCProviderHandle info,
                                 const TSRPCHandlerOptions *opt)
{
  sdk_assert(sdk_sanity_check_rpc_handler_options(opt) == TS_SUCCESS);

  if (!::rpc::add_notification_handler(
        {name, name_len},
        [callback](const YAML::Node &params) -> void { callback(reinterpret_cast<TSYaml>(const_cast<YAML::Node *>(&params))); },
        reinterpret_cast<const ::rpc::RPCRegistryInfo *>(info), *opt)) {
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

TSReturnCode
TSRPCHandlerDone(TSYaml resp)
{
  Dbg(dbg_ctl_rpc_api, ">> Handler seems to be done");
  std::lock_guard<std::mutex> lock(::rpc::g_rpcHandlingMutex);
  auto                        data       = *reinterpret_cast<YAML::Node *>(resp);
  ::rpc::g_rpcHandlerResponseData        = data;
  ::rpc::g_rpcHandlerProcessingCompleted = true;
  ::rpc::g_rpcHandlingCompletion.notify_one();
  Dbg(dbg_ctl_rpc_api, ">> all set.");
  return TS_SUCCESS;
}

TSReturnCode
TSRPCHandlerError(int ec, const char *descr, size_t descr_len)
{
  Dbg(dbg_ctl_rpc_api, ">> Handler seems to be done with an error");
  std::lock_guard<std::mutex> lock(rpc::g_rpcHandlingMutex);
  ::rpc::g_rpcHandlerResponseData        = swoc::Errata(ts::make_errno_code(ec), "{}", swoc::TextView{descr, descr_len});
  ::rpc::g_rpcHandlerProcessingCompleted = true;
  ::rpc::g_rpcHandlingCompletion.notify_one();
  Dbg(dbg_ctl_rpc_api, ">> error  flagged.");
  return TS_SUCCESS;
}

TSReturnCode
TSRecYAMLConfigParse(TSYaml node, TSYAMLRecNodeHandler handler, void *data)
{
  swoc::Errata err;
  try {
    err = ParseRecordsFromYAML(
      *reinterpret_cast<YAML::Node *>(node),
      [handler, data](const CfgNode &field, swoc::Errata &) -> void {
        // Errors from the handler should be reported and handled by the handler.
        // RecYAMLConfigFileParse will report any YAML parsing error.
        TSYAMLRecCfgFieldData cfg;
        auto const           &field_str = field.node.as<std::string>();
        cfg.field_name                  = field_str.c_str();
        cfg.record_name                 = field.get_record_name().data();
        cfg.value_node                  = reinterpret_cast<TSYaml>(const_cast<YAML::Node *>(&field.value_node));
        handler(&cfg, data);
      },
      true /* lock */);
  } catch (std::exception const &ex) {
    err.note(ERRATA_ERROR, "RecYAMLConfigParse error cought: {}", ex.what());
  }
  // Drop API logs in case of an error.
  if (!err.empty()) {
    std::string buf;
    Dbg(dbg_ctl_plugin, "%s", swoc::bwprint(buf, "{}", err).c_str());
  }

  return err.empty() ? TS_SUCCESS : TS_ERROR;
}

TSTxnType
TSHttpTxnTypeGet(TSHttpTxn txnp)
{
  sdk_assert(sdk_sanity_check_txn(txnp) == TS_SUCCESS);
  HttpSM   *sm     = reinterpret_cast<HttpSM *>(txnp);
  TSTxnType retval = TS_TXN_TYPE_UNKNOWN;
  if (sm != nullptr) {
    if (sm->t_state.transparent_passthrough) {
      retval = TS_TXN_TYPE_TR_PASS_TUNNEL;
    } else if (sm->t_state.client_info.port_attribute == HttpProxyPort::TRANSPORT_BLIND_TUNNEL) {
      retval = TS_TXN_TYPE_EXPLICIT_TUNNEL;
    } else {
      retval = TS_TXN_TYPE_HTTP;
    }
  }
  return retval;
}
