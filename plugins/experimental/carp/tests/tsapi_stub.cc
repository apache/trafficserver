/////////////////////////////////////////////////////////////////////////
// This file contains stub routines that need need to exist in order to
// do some level of unit testing.  We can't use the "real" INK routines
// since they are not in a library.  Since our plugin is a library that
// contains both low level IO routins as well as the plugin "glue" needed
// to make them work, we'll have refereces to some of these routines
// even if we want to only unit test our low level routines.
/////////////////////////////////////////////////////////////////////////

#include "tsapi_stub.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <pthread.h>

//#include <log4cpp/Category.hh>
#include <pcre.h>
#include <iostream>
#include <utility>
#include <assert.h>
#include <string.h>

using namespace std;

bool g_showDebug=true;

const void *TS_ERROR_PTR = "SOMETHING BAD HAPPENED IN THE STUB";
const TSMLoc TS_NULL_MLOC = NULL;

const char* TS_HTTP_METHOD_GET = "GET";
const char* TS_HTTP_METHOD_DELETE = "DELETE";
const char* TS_HTTP_METHOD_PURGE = "PURGE";
int TS_HTTP_LEN_DELETE = 6;
int TS_HTTP_LEN_PURGE = 5;

const char* TS_MIME_FIELD_HOST="host";
const char* TS_URL_SCHEME_HTTP="http";
const char* TS_URL_SCHEME_HTTPS="https";
int TS_URL_LEN_HTTP = 4;
int TS_URL_LEN_HTTPS = 5;

#define MAX_URL_SIZE 2096

TSReturnCode
TSUrlHttpParamsSet(TSMBuffer bufp, TSMLoc offset, const char* value, int length)
{
  return TS_SUCCESS;
}

TSAction 
TSContSchedule(TSCont contp, TSHRTime timeout, TSThreadPool tp) 
{
  return NULL;
}

const char*
TSUrlHttpParamsGet(TSMBuffer bufp, TSMLoc offset, int* length)
{
  UrlStruct * url(reinterpret_cast<UrlStruct *> (offset));

  *length = url->params.length();

  return url->params.c_str();
}

char*
TSUrlStringGet(TSMBuffer bufp, TSMLoc offset, int* length)
{
  UrlStruct * url(reinterpret_cast<UrlStruct *> (offset));
  url->url = url->scheme + "://" + url->host + "/" + url->path;
  if (url->query.length() > 0)
    url->url += "?" + url->query;

  *length = url->url.length();
  return (char*) url->url.c_str();
}

TSReturnCode
TSMimeHdrCreate(TSMBuffer bufp, TSMLoc* locp)
{
  *locp = (TSMLoc) new Headers;
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrDestroy(TSMBuffer bufp, TSMLoc hdr)
{
  assert(hdr != NULL);
  Headers *header = (Headers*) hdr;

  delete header;
  return TS_SUCCESS;
}

TSReturnCode
TSUrlCreate(TSMBuffer bufp, TSMLoc* locp)
{
  *locp = (TSMLoc)new UrlStruct();
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldCreate(TSMBuffer bufp, TSMLoc hdr, TSMLoc* locp)
{
  *locp = NULL;
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldCreateNamed(TSMBuffer bufp, TSMLoc hdr, const char* name, int length, TSMLoc* locp)
{
  assert(hdr != NULL);
  Headers &header = *(Headers*) hdr;
//std::cerr << "TSMimeHdrFieldCreateNamed hdr=0x" << hex << (size_t)hdr << dec<< std::endl;

  header[std::string(name, length)] = "";

  Headers::const_iterator it = header.find(std::string(name, length));
  if (it != header.end()) {
    *locp = (TSMLoc)&(*it);
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldValueStringSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char* value, int length)
{
  assert(hdr != NULL);
  Headers &header = *(Headers*) hdr;
//std::cerr << "TSMimeHdrFieldValueStringSet hdr=0x" << hex << (size_t)hdr << dec<< std::endl;

  for (Headers::const_iterator it = header.begin(); it != header.end(); ++it) {
    std::cerr << "here " << std::endl;
    if (field == (TSMLoc)&*it) {
      header[it->first] = std::string(value, length);
      std::cerr << "second: " << it->second << std::endl;
      return TS_SUCCESS;
    }
  }

  return TS_ERROR;
}

const char*
TSMimeHdrFieldValueStringGet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int* value_len_ptr)
{
  assert(hdr != NULL);
  Headers &header = *(Headers*) hdr;
//std::cerr << "TSMimeHdrFieldValueStringGet hdr=0x" << hex << (size_t)hdr << dec<< std::endl;

  for (Headers::const_iterator it = header.begin(); it != header.end(); ++it) {
//    std::cerr << "here " << std::endl;
    if (field == (TSMLoc)&*it) {
      *value_len_ptr = it->second.length();
      std::cerr << "TSMimeHdrFieldValueStringGet " << it->first << "=" << it->second << std::endl;
      return it->second.c_str();
    }
  }

  return NULL;
}

TSMLoc
TSMimeHdrFieldFind(TSMBuffer bufp, TSMLoc hdr, const char* name, int length)
{
  assert(hdr != NULL);
  Headers &header = *(Headers*) hdr;
//  std::cerr << "TSMimeHdrFieldFind hdr=0x" << hex << (size_t)hdr << dec<< std::endl;

  std::cerr << "length: " << length << " size: " << header.size() << " " << std::string(name, length) << std::endl;
  /*  for (uint i = 0; i < header.size(); ++i) {
      if (header[i].first == std::string(name, length)) {
        return (void*) &(*(header[i]);
      }
    }
   */
  Headers::const_iterator it = header.find(std::string(name, length));
  if (it != header.end()) {
    return (TSMLoc)&(*it);
  }
  return NULL;
}


tsapi TSReturnCode
TSMimeHdrFieldValueStringInsert(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char* value, int length)
{
  assert(hdr != NULL);
  Headers &header = *(Headers*) hdr;
//std::cerr << "TSMimeHdrFieldValueStringInsert hdr=0x" << hex << (size_t)hdr << dec<< std::endl;
  for (Headers::const_iterator it = header.begin(); it != header.end(); ++it) {
    if (field == (TSMLoc)&*it) {
      header[it->first] = it->second + std::string(value, length);
      std::cerr << "TSMimeHdrFieldValueStringInsert header is now " << it->first << ": " << it->second << std::endl;
      return TS_SUCCESS;
    }
  }

  return TS_ERROR;

}

tsapi TSReturnCode
TSMimeHdrFieldRemove(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  assert(hdr != NULL);
  Headers &header = *(Headers*) hdr;
//std::cerr << "TSMimeHdrFieldRemove hdr=0x" << hex << (size_t)hdr << dec<< std::endl;

  for (Headers::iterator it = header.begin(); it != header.end(); ++it) {
    if (field == (TSMLoc)&*it) {
      std::cerr << "TSMimeHdrFieldRemove header  " << it->first << std::endl;
      header.erase(it);
      return TS_SUCCESS;
    }
  }

  return TS_ERROR;
}

tsapi TSReturnCode
TSMimeHdrFieldDestroy(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  return TS_SUCCESS;
}

TSMBuffer
TSMBufferCreate()
{
  TSMBuffer b = (tsapi_mbuffer*)malloc(1024);
  return b; // bogus implementation, but i think it works
}

TSReturnCode
TSMBufferDestroy(TSMBuffer bufp)
{
  if(bufp) free((void *)bufp);
  return (TS_SUCCESS); // ok for unit test to do noop
}

TSIOBufferReader
TSCacheBufferReaderGet(TSCacheTxn txnp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (NULL);
}

TSReturnCode
TSHandleMLocRelease(TSMBuffer bufp, TSMLoc parent, TSMLoc mloc)
{
  return (TS_SUCCESS); // ok for unit test to do noop
}

TSReturnCode
TSHandleStringRelease(TSMBuffer bufp, TSMLoc parent, const char *str)
{
  return TS_SUCCESS; // bogus implementation should be ok for unit tests
}

TSReturnCode
TSHttpCacheReenable(TSCacheTxn txnp, const TSEvent event, const void* data, const int size)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (TS_SUCCESS);
}

void
TSHttpTxnSetHttpRetStatus(TSHttpTxn txnp, TSHttpStatus http_retstatus)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
}

TSFile
TSfopen(const char *filename, const char *mode)
{
  FILE *f;
  
  f = fopen(filename,mode);
  return ((tsapi_file *)f);
}

void
TSfclose(TSFile filep)
{
  fclose((FILE *)filep);
}

char*
TSfgets(TSFile filep, char *buf, size_t length)
{
  char *r = fgets(buf,length,(FILE *)filep);
  return (r);
}

int
TSIsDebugTagSet(const char *t)
{
  //std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (1);
}

void EnableTSDebug(bool b)
{
  g_showDebug = b;
}

void
TSDebug(const char *tag, const char *format_str, ...)
{
  if (g_showDebug) {
    char buffer[4096];
    va_list ap;
    va_start(ap, format_str);
    vsnprintf(buffer, sizeof (buffer), format_str, ap);
    std::cerr << "[" << tag << "] (" << pthread_self() << ") " << buffer << std::endl;
    va_end(ap);
  }
}

const char*
TSIOBufferBlockReadStart(TSIOBufferBlock blockp, TSIOBufferReader readerp, int *avail)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (NULL);
}

int64_t
TSIOBufferBlockReadAvail(TSIOBufferBlock blockp, TSIOBufferReader readerp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (0);
}

TSReturnCode
TSPluginInfoRegister(TSPluginRegistrationInfo *plugin_info)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (TS_SUCCESS);
}


// TSReturnCode     TSCacheBufferInfoGet(TSCacheTxn txnp, TSU64 *length, TSU64 *offset) {
//   std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
//   return(TS_SUCCESS);
// }

void
TSError(const char *format_str, ...)
{
  char buffer[4096];
  va_list ap;
  va_start(ap, format_str);
  vsnprintf(buffer, sizeof (buffer), format_str, ap);
  std::cerr << buffer;
  std::cerr << "(" << pthread_self() << ") " << buffer;
  va_end(ap);
}
#define MAX_CONTINUATIONS 100
TSEventFunc g_Continuations[MAX_CONTINUATIONS];
void* g_ContinuationData[MAX_CONTINUATIONS];
bool g_ContinuationInit = false;

TSCont
TSContCreate(TSEventFunc funcp, TSMutex mutexp)
{
  if(!g_ContinuationInit) {
    for(size_t i=0;i<MAX_CONTINUATIONS;i++) {
      g_Continuations[i]=NULL;
      g_ContinuationData[i] = NULL;
    }
    g_ContinuationInit = true;
  }
  // find an empty slot (can't use location 0 since 0==NULL)
  for(size_t i=1;i<MAX_CONTINUATIONS;i++) {
    if(g_Continuations[i]==NULL) { // is empty?
      g_Continuations[i] = funcp;
      return (tsapi_cont*)i;
    }
  }

  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": INCREASE MAX_CONTINUATIONS, no space left for more" << endl;
  return (NULL);
}


void
TSContDestroy(TSCont contp)
{
  g_Continuations[(size_t)contp]=NULL;
}

void
TSContDataSet(TSCont contp, void *data)
{
   g_ContinuationData[(size_t)contp]=data;
}

void*
TSContDataGet(TSCont contp)
{
  return (g_ContinuationData[(size_t)contp]);
}

TSAction
TSContSchedule(TSCont contp, unsigned int timeout)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (NULL);
}
void
TSActionCancel(TSAction ) {
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
}

TSReturnCode
TSCacheKeyGet(TSCacheTxn txnp, void **key, int *length)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (TS_SUCCESS);
}

TSReturnCode
TSCacheBodyKeyGet(TSCacheTxn txnp, void **key, int *length)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (TS_SUCCESS);
}

/*TSReturnCode     TSCacheHookAdd (TSCacheHookID id, TSCont contp) {
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return(TS_SUCCESS);
}
 */
const char*
TSPluginDirGet(void)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (NULL);
}

TSMutex
TSMutexCreate(void)
{
  static bool bBeenSaid = false;
  if(!bBeenSaid)  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  bBeenSaid = true;
  return (NULL);
}

void
TSMutexLock(TSMutex m)
{
  static bool bBeenSaid = false;
  if(!bBeenSaid)  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  bBeenSaid = true;
}

void
TSMutexUnlock(TSMutex m)
{
  static bool bBeenSaid = false;
  if(!bBeenSaid)  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  bBeenSaid = true;
}

tsapi void
TSIOBufferReaderConsume(TSIOBufferReader readerp, int64_t nbytes)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
}

TSIOBufferBlock
TSIOBufferReaderStart(TSIOBufferReader readerp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (NULL);
}

int64_t
TSIOBufferReaderAvail(TSIOBufferReader readerp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (0);
}

void*
_TSmalloc(size_t size, const char* path)
{
  return malloc(size);
}


//void*             _TSmalloc (unsigned int size, const char *path) {
//  return(malloc(size));
//}

void
_TSfree(void *ptr)
{
  free(ptr);
}

tsapi TSReturnCode
TSPluginRegister(TSSDKVersion sdk_version, TSPluginRegistrationInfo* plugin_info)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (TS_SUCCESS);
}

TSThread
TSThreadCreate(TSThreadFunc func, void *data)
{
  pthread_t tid;
  pthread_create(&tid, NULL, func, (void *)data);
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": TSThreadCreate spinning up new thread id=" << tid<< " data=0x"<<hex<<(size_t)data<<dec<<endl;
  return ((tsapi_thread*)tid);
}

TSThread
TSThreadSelf()
{
  return ((TSThread) pthread_self());
}

tsapi void
TSThreadDestroy(TSThread thread)
{
  pthread_cancel((pthread_t)thread);
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": TSThreadDestroy..called pthread_cancel for id=" << thread << endl;
  return;
}

TSReturnCode
TSUrlDestroy(TSMBuffer bufp, TSMLoc offset)
{
  delete reinterpret_cast<UrlStruct *> (offset);

  return TS_SUCCESS;
}

const char*
TSUrlHostGet(TSMBuffer bufp, TSMLoc offset, int *length)
{
  UrlStruct * url(reinterpret_cast<UrlStruct *> (offset));

  *length = url->host.length();

  return url->host.c_str();
}

const char*
TSUrlHttpQueryGet(TSMBuffer bufp, TSMLoc offset, int *length)
{
  UrlStruct * url(reinterpret_cast<UrlStruct *> (offset));

  *length = url->query.length();

  return url->query.c_str();
}

TSReturnCode
TSUrlPathSet(TSMBuffer bufp, TSMLoc obj, const char* value, int length)
{
  assert(obj != NULL);
  UrlStruct * url_obj(reinterpret_cast<UrlStruct *> (obj));

  url_obj->path.assign(value, length);
  return TS_SUCCESS;
}

TSParseResult
TSUrlParse(TSMBuffer bufp, TSMLoc obj, const char** start, const char* end)
{
  assert(obj != NULL);
  UrlStruct * url_obj(reinterpret_cast<UrlStruct *> (obj));

  if (!start || !*start || !end || !obj)
    return TS_PARSE_ERROR;

  url_obj->scheme.erase();
  url_obj->host.erase();
  url_obj->port = 80;
  url_obj->path.erase();
  url_obj->query.erase();
  url_obj->params.erase();


  std::string url(*start, end - *start);
  size_t ppos(0), tpos;
  size_t pos;
  pos = url.find("://");
  if (pos != std::string::npos) {
    url_obj->scheme = url.substr(ppos, pos - ppos);
    ppos = pos + 3;
  } else url_obj->scheme = "http";

  pos = url.find('/', ppos);
  if (pos == std::string::npos) pos = url.length();
  tpos = url.find(':', ppos);
  if (tpos < pos - 1) { // there's at least one character btw. : and (/ or end of string);
    std::string port = url.substr(tpos + 1, pos - tpos);
    url_obj->port = atoi(port.c_str()); // we'll assume it's a digit
  } else {
    tpos = pos;
  }
  
  if (pos < url.length()) {
    url_obj->host = url.substr(ppos, tpos - ppos);
    ppos = pos;
  } else {
    url_obj->host = url.substr(ppos);
    return TS_PARSE_DONE;
  }

  pos = url.find('?', ppos);
  if (pos == std::string::npos) pos = url.find(';', ppos);
  if (pos == std::string::npos) pos = url.length();
  if (ppos + 1 < pos) { // there's at least one character in the path
    url_obj->path = url.substr(ppos + 1, pos - ppos - 1); // skip the initial / to be consistent with what YTS does
  }
  ppos = pos; // INVARIANT: ppos is the location of the '?' or end of url

  if (ppos < url.length()) {
    if (url[ppos] == ';') {
      url_obj->params = url.substr(ppos + 1);
    } else {
      url_obj->query = url.substr(ppos + 1);
    }
  }
  return TS_PARSE_DONE;
}

const char*
TSUrlPathGet(TSMBuffer bufp, TSMLoc offset, int *length)
{
  UrlStruct * url(reinterpret_cast<UrlStruct *> (offset));

  *length = url->path.length();

  return url->path.c_str();
}

int
TSUrlPortGet(TSMBuffer bufp, TSMLoc offset)
{
  UrlStruct * url(reinterpret_cast<UrlStruct *> (offset));

  return url->port;
}

const char*
TSUrlSchemeGet(TSMBuffer bufp, TSMLoc offset, int *length)
{
  UrlStruct * url(reinterpret_cast<UrlStruct *> (offset));

  *length = url->scheme.length();

  return url->scheme.c_str();
}

TSReturnCode
TSUrlSchemeSet(TSMBuffer bufp, TSMLoc offset, const char* value, int length)
{
  UrlStruct * url(reinterpret_cast<UrlStruct *> (offset));

  if(length != -1)
    url->scheme = string(value,length);
  else
    url->scheme = value;

  return TS_SUCCESS;
}

tsapi TSReturnCode
TSMimeHdrFieldAppend(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)
{
  assert(hdr != NULL);
  assert(field != NULL);
  return TS_SUCCESS;
}

tsapi TSReturnCode
TSUrlHostSet(TSMBuffer bufp, TSMLoc offset, const char* value, int length)
{
  assert(offset != NULL);
  UrlStruct * url(reinterpret_cast<UrlStruct *> (offset));
  url->host.assign(value, length);
  return TS_SUCCESS;
}

tsapi TSReturnCode
TSHttpTxnServerRespNoStoreSet(TSHttpTxn txnp, int flag)
{
  return TS_SUCCESS;
}

tsapi const char*
TSInstallDirGet(void)
{
  static const char installDir[] = "/home/y";
  return installDir;
}


tsapi const char*
TSConfigDirGet(void)
{
  static const char configDir[] = "/home/y/conf/trafficserver";
  return configDir;
}

tsapi int
_TSAssert(const char* txt, const char* f, int l)
{
  std::cerr << "*** ASSERT FAILED *** (" << txt << ") @ " << f << ":" << l << std::endl;
  return 0; // what is this supposed to return?
}

tsapi TSReturnCode
TSUrlPortSet(TSMBuffer bufp, TSMLoc offset, int port)
{
  assert(offset != NULL);
  UrlStruct * url(reinterpret_cast<UrlStruct *> (offset));
  url->port = port;
  return TS_SUCCESS;
}


tsapi TSIOBufferReader
TSIOBufferReaderAlloc(TSIOBuffer bufp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return (NULL);
}

tsapi TSParseResult
TSHttpHdrParseReq(TSHttpParser parser, TSMBuffer bufp, TSMLoc offset, const char** start, const char* end)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return TS_PARSE_OK;
}

tsapi TSParseResult
TSHttpHdrParseResp(TSHttpParser parser, TSMBuffer bufp, TSMLoc offset, const char** start, const char* end)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return TS_PARSE_OK;
}

tsapi TSMLoc
TSHttpHdrCreate(TSMBuffer bufp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi void
TSHttpHdrPrint(TSMBuffer bufp, TSMLoc offset, TSIOBuffer iobufp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi void
TSVIOReenable(TSVIO viop)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi TSIOBuffer
TSVIOBufferGet(TSVIO viop)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi TSIOBufferReader
TSVIOReaderGet(TSVIO viop)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi int64_t
TSVIONBytesGet(TSVIO viop)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return 0;
}

tsapi void
TSVIONBytesSet(TSVIO viop, int64_t nbytes)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi int64_t
TSVIONDoneGet(TSVIO viop)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return 0;
}

tsapi void
TSVIONDoneSet(TSVIO viop, int64_t ndone)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi int64_t
TSVIONTodoGet(TSVIO viop)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return 0;
}

tsapi TSMutex
TSVIOMutexGet(TSVIO viop)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi TSCont
TSVIOContGet(TSVIO viop)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi TSVConn
TSVIOVConnGet(TSVIO viop)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi TSHttpStatus
TSHttpHdrStatusGet(TSMBuffer bufp, TSMLoc offset)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return TS_HTTP_STATUS_OK;
}

tsapi TSVIO
TSVConnRead(TSVConn connp, TSCont contp, TSIOBuffer bufp, int64_t nbytes)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi TSVIO
TSVConnWrite(TSVConn connp, TSCont contp, TSIOBufferReader readerp, int64_t nbytes)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi void
TSVConnClose(TSVConn connp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi void
TSVConnAbort(TSVConn connp, int error)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi void
TSVConnShutdown(TSVConn connp, int read, int write)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi TSAction
TSNetConnect(TSCont contp, /**< continuation that is called back when the attempted net connection either succeeds or fails. */
             struct sockaddr const* to /**< Address to which to connect. */
             )
{
  TSVConn vconn;
  void *edata = (void *)(&vconn); 

  g_Continuations[(size_t)contp](contp, TS_EVENT_NET_CONNECT, edata);
  g_Continuations[(size_t)contp](contp, TS_EVENT_VCONN_READ_READY, edata);
  g_Continuations[(size_t)contp](contp, TS_EVENT_VCONN_READ_COMPLETE, edata);
    
  return NULL;
}

tsapi TSIOBuffer
TSIOBufferCreate(void)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi void
TSIOBufferDestroy(TSIOBuffer bufp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi TSIOBufferBlock
TSIOBufferBlockNext(TSIOBufferBlock blockp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi const char*
TSIOBufferBlockReadStart(TSIOBufferBlock blockp, TSIOBufferReader readerp, int64_t* avail)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi int64_t
TSIOBufferWrite(TSIOBuffer bufp, const void* buf, int64_t length)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi void
TSIOBufferReaderFree(TSIOBufferReader readerp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi TSHttpParser
TSHttpParserCreate(void)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return NULL;
}

tsapi void
TSHttpParserClear(TSHttpParser parser)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi void
TSHttpParserDestroy(TSHttpParser parser)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi void
TSHttpTxnReenable(TSHttpTxn txnp, TSEvent event)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi void
TSSkipRemappingSet(TSHttpTxn txnp, int flag)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi TSReturnCode
TSHttpTxnClientReqGet(TSHttpTxn txnp, TSMBuffer* bufp, TSMLoc* offset)
{
  *bufp = (TSMBuffer)&(((TxnStruct*) txnp)->clientRequest);
  *offset = (TSMLoc)  &((((TxnStruct*) txnp)->clientRequest).clientReqHeaders);
  
  return TS_SUCCESS;
}

tsapi TSReturnCode
TSHttpHdrUrlGet(TSMBuffer bufp, TSMLoc offset, TSMLoc* locp)
{
  *locp = (TSMLoc)&(((TSMBufferStruct *)bufp)->url);
  return TS_SUCCESS;
}

tsapi TSReturnCode
TSHttpHdrUrlSet(TSMBuffer bufp, TSMLoc offset, TSMLoc locp) {
  return TS_SUCCESS;
}

tsapi const char*
TSHttpHdrMethodGet(TSMBuffer bufp, TSMLoc offset, int* length)
{
  *length = (((TSMBufferStruct *)bufp)->method.length());
  std::cerr << "TSHttpHdrMethodGet returning method " << ((TSMBufferStruct *)bufp)->method << endl;
  return (((TSMBufferStruct *)bufp)->method.c_str());
}

tsapi void
TSHttpHookAdd(TSHttpHookID id, TSCont contp)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return;
}

tsapi TSReturnCode
TSHttpTxnPristineUrlGet(TSHttpTxn txnp, TSMBuffer* bufp, TSMLoc* url_loc)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return TS_SUCCESS;
}

tsapi TSReturnCode
TSHttpTxnServerAddrSet(TSHttpTxn txnp, struct sockaddr const* addr)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return TS_SUCCESS;
}

tsapi struct sockaddr const* 
TSHttpTxnServerAddrGet(TSHttpTxn txnp)
{
  static sockaddr sa;
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return &sa;
}

tsapi struct sockaddr const* 
TSHttpTxnClientAddrGet(TSHttpTxn txnp)
{
  return &(((TxnStruct*) txnp)->incomingClientAddr);
}

tsapi char*
_TSstrdup(const char* str, int64_t length, const char* path)
{
  if (length == -1) {
    length = strlen(str);
  }
  char *t = new char(length);
  memmove(t, str, length);
  return t;
}

tsapi void
TSHttpTxnErrorBodySet(TSHttpTxn txnp, char* buf, size_t buflength, char* mimetype)
{
  if(txnp) {
  ((TxnStruct*) txnp)->clientResponse.body = string(buf,buflength);
  cerr << "TSHttpTxnErrorBodySet body=" << ((TxnStruct*) txnp)->clientResponse.body << endl;
  } else {
    cerr << "TSHttpTxnErrorBodySet txnp == NULL" << endl;
    abort();
  }
  
}

static int txnArgCount = 0;
static void *txnArgs[100];

tsapi void
TSHttpTxnArgSet(TSHttpTxn txnp, int arg_idx, void* arg)
{
  txnArgs[arg_idx] = arg;
}

tsapi void*
TSHttpTxnArgGet(TSHttpTxn txnp, int arg_idx)
{
  return txnArgs[arg_idx];
}

tsapi TSReturnCode
TSHttpArgIndexReserve(const char* name, const char* description, int* arg_idx)
{
  *arg_idx = txnArgCount++;
  return TS_SUCCESS;
}

tsapi TSServerState
TSHttpTxnServerStateGet(TSHttpTxn txnp)
{
  /* possible return values
   *   { TS_SRVSTATE_STATE_UNDEFINED = 0,
  TS_SRVSTATE_ACTIVE_TIMEOUT,
  TS_SRVSTATE_BAD_INCOMING_RESPONSE,
  TS_SRVSTATE_CONNECTION_ALIVE,
  TS_SRVSTATE_CONNECTION_CLOSED,
  TS_SRVSTATE_CONNECTION_ERROR,
  TS_SRVSTATE_INACTIVE_TIMEOUT,
  TS_SRVSTATE_OPEN_RAW_ERROR,
  TS_SRVSTATE_PARSE_ERROR,
  TS_SRVSTATE_TRANSACTION_COMPLETE,
  TS_SRVSTATE_CONGEST_CONTROL_CONGESTED_ON_F,
  TS_SRVSTATE_CONGEST_CONTROL_CONGESTED_ON_M
   * */
  return TS_SRVSTATE_CONNECTION_ALIVE;
}

tsapi struct sockaddr const*
TSHttpTxnIncomingAddrGet(TSHttpTxn txnp)
{
  return &(((TxnStruct*) txnp)->incomingClientAddr);
}

tsapi TSReturnCode
TSHttpTxnConfigIntSet(TSHttpTxn txnp, TSOverridableConfigKey conf, TSMgmtInt value)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return TS_SUCCESS;
}

tsapi TSReturnCode
TSTextLogObjectCreate(const char* filename, int mode, TSTextLogObject * new_log_obj)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return TS_SUCCESS;
}

tsapi TSReturnCode
TSTextLogObjectWrite(TSTextLogObject the_object, const char *format, ...)
{
  std::cerr << __FILE__ << ":" << __FUNCTION__ << ": Unsupported!!!" << endl;
  return TS_SUCCESS;
}
