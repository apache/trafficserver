/** @file

This is an origin server / intercept plugin, which implements flexible health checks.

@section license

Copyright 2012 Go Daddy Operating Company, LLC   

Licensed under the Apache License, Version 2.0 (the "License");    
you may not use this file except in compliance with the License.    
You may obtain a copy of the License at        

http://www.apache.org/licenses/LICENSE-2.0    

Unless required by applicable law or agreed to in writing, software    
distributed under the License is distributed on an "AS IS" BASIS,    
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.    
See the License for the specific language governing permissions and    
limitations under the License. 
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <inttypes.h>

/* ToDo: Linux specific */
#include <sys/inotify.h>
#include <libgen.h>

#include "ts/ts.h"
#include "ink_defs.h"

static const char PLUGIN_NAME[] = "health_checks";

#define MAX_PATH_LEN  4096
#define MAX_FILENAME_LEN 2048
#define MAX_BODY_LEN 16384
#define FREELIST_TIMEOUT 300


/* Some atomic stuff, from ATS core */
typedef volatile void *vvoidp;

static inline
void *ink_atomic_swap_ptr(vvoidp mem, void *value)
{
  return __sync_lock_test_and_set((void**)mem, value);
}

/* Directories that we are watching for inotify IN_CREATE events. */
typedef struct HCDirEntry_t
{
  char dname[MAX_FILENAME_LEN];   /* Directory name */
  int wd;                         /* Watch descriptor */
  struct HCDirEntry_t *_next;     /* Linked list */
} HCDirEntry;

/* Information about a status file. This is never modified (only replaced, see HCFileInfo_t) */
typedef struct HCFileData_t
{
  time_t mtime;                   /* Last modified time of file */
  int exists;                     /* Does this file exist ? */
  char body[MAX_BODY_LEN];        /* Body from fname. NULL means file is missing */
  int b_len;                      /* Length of data */
  time_t remove;                  /* Used for deciding when the old object can be permanently removed */
  struct HCFileData_t *_next;     /* Only used when removing these bad boys */
} HCFileData;

/* The only thing that should change in this struct is data, atomically swapping ptrs */
typedef struct HCFileInfo_t
{
  char fname[MAX_FILENAME_LEN];   /* Filename */
  char *basename;                 /* The "basename" of the file */
  char path[MAX_PATH_LEN];        /* Path for this HC */
  int p_len;                      /* Length of path */
  const char *ok;                 /* Header for an OK result */
  int o_len;                      /* Length of OK header */
  const char *miss;               /* Header for miss results */
  int m_len;                      /* Length of miss header */
  HCFileData *data;               /* Holds the current data for this health check file */
  int wd;                         /* Watch descriptor */
  HCDirEntry *dir;                /* Reference to the directory this file resides in */
  struct HCFileInfo_t *_next;     /* Linked list */
} HCFileInfo;

/* Global configuration */
HCFileInfo *g_config;

/* State used for the intercept plugin. ToDo: Can this be improved ? */
typedef struct HCState_t
{
  TSVConn net_vc;
  TSVIO read_vio;
  TSVIO write_vio;

  TSIOBuffer req_buffer;
  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  int output_bytes;

  /* We actually need both here, so that our lock free switches works safely */
  HCFileInfo *info;
  HCFileData *data;
} HCState;


/* Read / check the status files */
static
void
reload_status_file(HCFileInfo *info, HCFileData *data)
{
  struct stat buf;

  data->exists = 0;
  if (!stat(info->fname, &buf)) {
    FILE *fd;

    if (NULL != (fd = fopen(info->fname, "r"))) {
      if ((data->b_len = fread(data->body, 1, MAX_BODY_LEN, fd)) > 0) {
        data->exists = 1;
        data->mtime = buf.st_mtime;
        data->remove = 0;
      }
      fclose(fd);
    }
  }
  if (!data->exists) {
    data->body[0] = '\0';
    data->b_len = 0;
    data->mtime = 0;
  }
}

/* Find a HCDirEntry from the linked list */
static HCDirEntry*
find_direntry(const char *dname, HCDirEntry *dir)
{
  while (dir) {
    if (!strncmp(dname, dir->dname, MAX_FILENAME_LEN))
      return dir;
    dir = dir->_next;
  }
  return NULL;
}

/* Setup up watchers, directory as well as initial files */
static HCDirEntry*
setup_watchers(int fd)
{
  HCFileInfo *conf = g_config;
  HCDirEntry *head_dir = NULL, *last_dir = NULL, *dir;
  char fname[MAX_FILENAME_LEN];
  char *dname;

  while (conf) {
    conf->wd = inotify_add_watch(fd, conf->fname, IN_DELETE_SELF|IN_CLOSE_WRITE);
    strncpy(fname, conf->fname, MAX_FILENAME_LEN - 1);
    dname = dirname(fname);
    if (!(dir = find_direntry(dname, head_dir))) {     /* Make sure to only watch each directory once */
      dir = TSmalloc(sizeof(HCDirEntry));
      memset(dir, 0, sizeof(HCDirEntry));
      strncpy(dir->dname, dname, MAX_FILENAME_LEN - 1);
      dir->wd = inotify_add_watch(fd, dname, IN_CREATE|IN_MOVED_FROM|IN_MOVED_TO);
      if (!head_dir) 
        head_dir = dir;
      else
        last_dir->_next = dir;
      last_dir = dir;
    }
    conf->dir = dir;
    conf = conf->_next;
  }

  return head_dir;
}


/* Separate thread to monitor status files for reload */
#define INOTIFY_BUFLEN (1024 * sizeof(struct inotify_event))

static void *
hc_thread(void *data ATS_UNUSED)
{
  int fd = inotify_init();
  HCDirEntry *dirs;
  int len;
  HCFileData *fl = NULL;
  char buffer[INOTIFY_BUFLEN];
  struct timeval last_free, now;

  gettimeofday(&last_free, NULL);

  /* Setup watchers for the directories, these are a one time setup */
  dirs = setup_watchers(fd);

  while (1) {
    int i = 0;

    /* First clean out anything old from the freelist */
    gettimeofday(&now, NULL);
    if ((now.tv_sec  - last_free.tv_sec) > FREELIST_TIMEOUT) {
      HCFileData *fdata = fl, *prev_fdata = fl;

      TSDebug(PLUGIN_NAME, "Checking the freelist");
      memcpy(&last_free, &now, sizeof(struct timeval));
      while(fdata) {
        if (fdata->remove > now.tv_sec) {
          if (prev_fdata)
            prev_fdata->_next = fdata->_next;
          fdata = fdata->_next;
          TSDebug(PLUGIN_NAME, "Cleaning up entry from frelist");
          TSfree(fdata);
        } else {
          prev_fdata = fdata;
          fdata = fdata->_next;
        }
      }
    }
    
    /* Read the inotify events, blocking! */
    len  = read(fd, buffer, INOTIFY_BUFLEN);
    if (len >= 0) {
      while (i < len) {
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        int wd = event->wd;
        HCFileInfo *finfo = g_config;

        while (finfo) {
          if ((wd == finfo->wd) || (wd == finfo->dir->wd && !strncmp(event->name, finfo->basename, event->len)))
            break;
          finfo = finfo->_next;
        }
        if (finfo) {
          HCFileData *new_data = TSmalloc(sizeof(HCFileData));

          if (event->mask & (IN_CLOSE_WRITE)) {
            TSDebug(PLUGIN_NAME, "Modify file event (%d) on %s", event->mask, finfo->fname);
          } else if (event->mask & (IN_CREATE|IN_MOVED_TO)) {
            TSDebug(PLUGIN_NAME, "Create file event (%d) on %s", event->mask, finfo->fname);
            finfo->wd = inotify_add_watch(fd, finfo->fname, IN_DELETE_SELF|IN_CLOSE_WRITE|IN_ATTRIB);
          } else if (event->mask & (IN_DELETE_SELF|IN_MOVED_FROM)) {
            TSDebug(PLUGIN_NAME, "Delete file event (%d) on %s", event->mask, finfo->fname);
            finfo->wd = inotify_rm_watch(fd, finfo->wd);
          }
          memset(new_data, 0, sizeof(HCFileData));
          reload_status_file(finfo, new_data);
          finfo->data->_next = fl;
          finfo->data->remove = now.tv_sec + FREELIST_TIMEOUT;
          fl = finfo->data;
          ink_atomic_swap_ptr(&(finfo->data), new_data);
        }
        i += sizeof(struct inotify_event) + event->len;
      }
    }
  }

  /* Cleanup, in case we later exit this thread ... */
  while (dirs) {
    HCDirEntry *d = dirs;

    dirs = dirs->_next;
    TSfree(d);
  }

  return NULL; /* Yeah, that never happens */
}

/* Config file parsing */
static const char HEADER_TEMPLATE[] = "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nCache-Control: no-cache\r\n";

static char *
gen_header(char *status_str, char *mime, int *header_len)
{
  TSHttpStatus status;
  char * buf = NULL;

  status = atoi(status_str);
  if (status > 0 && status < 999) {
    const char* status_reason;
    int len = sizeof(HEADER_TEMPLATE) + 3 + 1;

    status_reason = TSHttpHdrReasonLookup(status);
    len += strlen(status_reason);
    len += strlen(mime);
    buf = TSmalloc(len);
    *header_len = snprintf(buf, len, HEADER_TEMPLATE, status, status_reason, mime);
  } else {
    *header_len = 0;
  }

  return buf;
}

static HCFileInfo*
parse_configs(const char* fname)
{
  FILE *fd;
  char buf[64*1024]; /* Way huge, but wth */
  HCFileInfo *head_finfo = NULL, *finfo = NULL, *prev_finfo = NULL;

  if (NULL == (fd = fopen(fname, "r")))
    return NULL;

  while (!feof(fd)) {
    char *str, *save;
    int state = 0;
    char *ok=NULL, *miss=NULL, *mime=NULL;

    prev_finfo = finfo;
    finfo = TSmalloc(sizeof(HCFileInfo));
    memset(finfo, 0, sizeof(HCFileInfo));

    if (NULL == head_finfo)
      head_finfo = finfo;
    if (prev_finfo)
      prev_finfo->_next = finfo;

    fread(buf, sizeof(buf), 1, fd);
    str = strtok_r(buf, "\t", &save);
    while (NULL != str) {
      if (strlen(str) > 0) {
        switch (state) {
        case 0:
          if ('/' == *str)
            ++str;
          strncpy(finfo->path, str, MAX_PATH_LEN - 1);
          finfo->p_len = strlen(finfo->path);
          break;
        case 1:
          strncpy(finfo->fname, str, MAX_FILENAME_LEN - 1);
          finfo->basename = strrchr(finfo->fname, '/');
          if (finfo->basename)
            ++(finfo->basename);
          break;
        case 2:
          mime = str;
          break;
        case 3:
          ok = str;
          break;
        case 4:
          miss = str;
          break;
        }
        ++state;
      }
      str = strtok_r(NULL, "\t", &save);
    }

    finfo->ok = gen_header(ok, mime, &finfo->o_len);
    finfo->miss = gen_header(miss, mime, &finfo->m_len);
    finfo->data = TSmalloc(sizeof(HCFileData));
    memset(finfo->data, 0, sizeof(HCFileData));
    reload_status_file(finfo, finfo->data);
  }
  fclose(fd);

  return head_finfo;
}


/* Cleanup after intercept has completed */
static void
cleanup(TSCont contp, HCState *my_state)
{
  if (my_state->req_buffer) {
    TSIOBufferDestroy(my_state->req_buffer);
    my_state->req_buffer = NULL;
  }

  if (my_state->resp_buffer) {
    TSIOBufferDestroy(my_state->resp_buffer);
    my_state->resp_buffer = NULL;
  }

  TSVConnClose(my_state->net_vc);
  TSfree(my_state);
  TSContDestroy(contp);
}

/* Add data to the output */
inline static int
add_data_to_resp(const char *buf, int len, HCState *my_state)
{
  TSIOBufferWrite(my_state->resp_buffer, buf, len);
  return len;
}

/* Process a read event from the SM */
static void
hc_process_read(TSCont contp, TSEvent event, HCState *my_state)
{
  if (event == TS_EVENT_VCONN_READ_READY) {
    if (my_state->data->exists) {
      TSDebug(PLUGIN_NAME, "Setting OK response header");
      my_state->output_bytes = add_data_to_resp(my_state->info->ok, my_state->info->o_len, my_state);
    } else {
      TSDebug(PLUGIN_NAME, "Setting MISS response header");
      my_state->output_bytes = add_data_to_resp(my_state->info->miss, my_state->info->m_len, my_state);
    }
    TSVConnShutdown(my_state->net_vc, 1, 0);
    my_state->write_vio = TSVConnWrite(my_state->net_vc, contp, my_state->resp_reader, INT64_MAX);
  } else if (event == TS_EVENT_ERROR) {
    TSError("hc_process_read: Received TS_EVENT_ERROR\n");
  } else if (event == TS_EVENT_VCONN_EOS) {
    /* client may end the connection, simply return */
    return;
  } else if (event == TS_EVENT_NET_ACCEPT_FAILED) {
    TSError("hc_process_read: Received TS_EVENT_NET_ACCEPT_FAILED\n");
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }
}

/* Process a write event from the SM */
static void
hc_process_write(TSCont contp, TSEvent event, HCState *my_state)
{
  if (event == TS_EVENT_VCONN_WRITE_READY) {
    char buf[48];
    int len;

    len = snprintf(buf, sizeof(buf)-1, "Content-Length: %d\r\n\r\n", my_state->data->b_len);
    my_state->output_bytes += add_data_to_resp(buf, len, my_state);
    if (my_state->data->b_len > 0)
      my_state->output_bytes += add_data_to_resp(my_state->data->body, my_state->data->b_len, my_state);
    else
      my_state->output_bytes += add_data_to_resp("\r\n", 2, my_state);
    TSVIONBytesSet(my_state->write_vio, my_state->output_bytes);
    TSVIOReenable(my_state->write_vio);
  } else if (TS_EVENT_VCONN_WRITE_COMPLETE) {
    cleanup(contp, my_state);
  } else if (event == TS_EVENT_ERROR) {
    TSError("hc_process_write: Received TS_EVENT_ERROR\n");
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }
}

/* Process the accept event from the SM */
static void
hc_process_accept(TSCont contp, HCState *my_state)
{
  my_state->req_buffer = TSIOBufferCreate();
  my_state->resp_buffer = TSIOBufferCreate();
  my_state->resp_reader = TSIOBufferReaderAlloc(my_state->resp_buffer);
  my_state->read_vio = TSVConnRead(my_state->net_vc, contp, my_state->req_buffer, INT64_MAX);
}

/* Imlement the server intercept */
static int
hc_intercept(TSCont contp, TSEvent event, void *edata)
{
  HCState *my_state = TSContDataGet(contp);

  if (event == TS_EVENT_NET_ACCEPT) {
    my_state->net_vc = (TSVConn)edata;
    hc_process_accept(contp, my_state);
  } else if (edata == my_state->read_vio) { /* All read events */
    hc_process_read(contp, event, my_state);
  } else if (edata == my_state->write_vio) { /* All write events */
    hc_process_write(contp, event, my_state);
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }

  return 0;
}

/* Read-request header continuation, used to kick off the server intercept if necessary */
static int
health_check_origin(TSCont contp ATS_UNUSED, TSEvent event ATS_UNUSED, void *edata)
{
  TSMBuffer reqp;
  TSMLoc hdr_loc = NULL, url_loc = NULL;
  TSCont icontp;
  HCState *my_state;
  TSHttpTxn txnp = (TSHttpTxn) edata;
  HCFileInfo *info = g_config;

  if ((TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &reqp, &hdr_loc)) &&
      (TS_SUCCESS == TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc))) {
    int path_len = 0;
    const char* path = TSUrlPathGet(reqp, url_loc, &path_len);

    while (info) {
      if (info->p_len == path_len && !memcmp(info->path, path, path_len))
        break;
      info = info->_next;
    }

    if (!info)
      goto cleanup;

    TSSkipRemappingSet(txnp, 1); /* not strictly necessary, but speed is everything these days */

    /* This is us -- register our intercept */
    icontp = TSContCreate(hc_intercept, TSMutexCreate());
    my_state = (HCState *)TSmalloc(sizeof(*my_state));
    memset(my_state, 0, sizeof(*my_state));
    my_state->info = info;
    my_state->data = info->data;
    TSContDataSet(icontp, my_state);
    TSHttpTxnIntercept(icontp, txnp);
  }

 cleanup:
  if (url_loc)
    TSHandleMLocRelease(reqp, hdr_loc, url_loc);
  if (hdr_loc)
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

/* Check the TS server version, make sure we're supporting it */
inline int
check_ts_version()
{
  const char *ts_version = TSTrafficServerVersionGet();

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3)
      return 0;

    /* Need at least TS 3.0 */
    if (major_ts_version >= 3)
      return 1;
  }

  return 0;
}

/* Initialize the plugin / global continuation hook */
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  if (2 != argc) {
    TSError("Must specify a configuration file.\n");
    return;
  }

  info.plugin_name = "health_checks";
  info.vendor_name = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(TS_SDK_VERSION_3_0, &info)) {
    TSError("Plugin registration failed. \n");
    return;
  }

  if (!check_ts_version()) {
    TSError("Plugin requires Traffic Server 3.0 or later\n");
    return;
  }

  /* This will update the global configuration file, and is not reloaded at run time */
  /* ToDo: Support reloading with traffic_line -x  ? */
  if (NULL == (g_config = parse_configs(argv[1]))) {
    TSError("Unable to read / parse %s config file", argv[1]);
    return;
  }

  /* Setup the background thread */
  if (!TSThreadCreate(hc_thread, NULL)) {
    TSError("Failure in thread creation");
    return;
  }

  /* Create a continuation with a mutex as there is a shared global structure
     containing the headers to add */
  TSDebug(PLUGIN_NAME, "Started %s plugin", PLUGIN_NAME);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(health_check_origin, NULL));
}
