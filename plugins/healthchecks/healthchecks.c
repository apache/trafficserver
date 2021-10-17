/** @file

This is an origin server / intercept plugin, which implements flexible health checks.

@section license

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
#include "tscore/ink_platform.h"
#include "tscore/ink_defs.h"

static const char PLUGIN_NAME[] = "healthchecks";
static const char SEPARATORS[]  = " \t\n";

#define MAX_PATH_LEN 4096
#define MAX_BODY_LEN 16384
#define FREELIST_TIMEOUT 300

static inline void *
ink_atomic_swap_ptr(void *mem, void *value)
{
  return __sync_lock_test_and_set((void **)mem, value);
}

/* Directories that we are watching for inotify IN_CREATE events. */
typedef struct HCDirEntry_t {
  char dname[MAX_PATH_LEN];   /* Directory name */
  int wd;                     /* Watch descriptor */
  struct HCDirEntry_t *_next; /* Linked list */
} HCDirEntry;

/* Information about a status file. This is never modified (only replaced, see HCFileInfo_t) */
typedef struct HCFileData_t {
  int exists;                 /* Does this file exist */
  char body[MAX_BODY_LEN];    /* Body from fname. NULL means file is missing */
  int b_len;                  /* Length of data */
  time_t remove;              /* Used for deciding when the old object can be permanently removed */
  struct HCFileData_t *_next; /* Only used when these guys end up on the freelist */
} HCFileData;

/* The only thing that should change in this struct is data, atomically swapping ptrs */
typedef struct HCFileInfo_t {
  char fname[MAX_PATH_LEN];   /* Filename */
  char *basename;             /* The "basename" of the file */
  char path[PATH_NAME_MAX];   /* URL path for this HC */
  int p_len;                  /* Length of path */
  const char *ok;             /* Header for an OK result */
  int o_len;                  /* Length of OK header */
  const char *miss;           /* Header for miss results */
  int m_len;                  /* Length of miss header */
  HCFileData *data;           /* Holds the current data for this health check file */
  int wd;                     /* Watch descriptor */
  HCDirEntry *dir;            /* Reference to the directory this file resides in */
  struct HCFileInfo_t *_next; /* Linked list */
} HCFileInfo;

/* Global configuration */
HCFileInfo *g_config;

/* State used for the intercept plugin. ToDo: Can this be improved ? */
typedef struct HCState_t {
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
static void
reload_status_file(HCFileInfo *info, HCFileData *data)
{
  FILE *fd;

  memset(data, 0, sizeof(HCFileData));
  if (NULL != (fd = fopen(info->fname, "r"))) {
    data->exists = 1;
    do {
      data->b_len = fread(data->body, 1, MAX_BODY_LEN, fd);
    } while (!feof(fd)); /*  Only save the last 16KB of the file ... */
    fclose(fd);
  }
}

/* Find a HCDirEntry from the linked list */
static HCDirEntry *
find_direntry(const char *dname, HCDirEntry *dir)
{
  while (dir) {
    if (!strncmp(dname, dir->dname, MAX_PATH_LEN)) {
      return dir;
    }
    dir = dir->_next;
  }
  return NULL;
}

/* Setup up watchers, directory as well as initial files */
static HCDirEntry *
setup_watchers(int fd)
{
  HCFileInfo *conf     = g_config;
  HCDirEntry *head_dir = NULL, *last_dir = NULL, *dir;
  char fname[MAX_PATH_LEN];

  while (conf) {
    conf->wd = inotify_add_watch(fd, conf->fname, IN_DELETE_SELF | IN_CLOSE_WRITE | IN_ATTRIB);
    TSDebug(PLUGIN_NAME, "Setting up a watcher for %s", conf->fname);
    strncpy(fname, conf->fname, MAX_PATH_LEN);
    char *dname = dirname(fname);
    /* Make sure to only watch each directory once */
    if (!(dir = find_direntry(dname, head_dir))) {
      TSDebug(PLUGIN_NAME, "Setting up a watcher for directory %s", dname);
      dir = TSmalloc(sizeof(HCDirEntry));
      memset(dir, 0, sizeof(HCDirEntry));
      strncpy(dir->dname, dname, MAX_PATH_LEN - 1);
      dir->wd = inotify_add_watch(fd, dname, IN_CREATE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB);
      if (!head_dir) {
        head_dir = dir;
      } else {
        last_dir->_next = dir;
      }
      last_dir = dir;
    }
    conf->dir = dir;
    conf      = conf->_next;
  }

  return head_dir;
}

/* Separate thread to monitor status files for reload */
#define INOTIFY_BUFLEN (1024 * sizeof(struct inotify_event))

static void *
hc_thread(void *data ATS_UNUSED)
{
  int fd              = inotify_init();
  HCFileData *fl_head = NULL;
  char buffer[INOTIFY_BUFLEN];
  struct timeval last_free, now;

  gettimeofday(&last_free, NULL);

  /* Setup watchers for the directories, these are a one time setup */
  setup_watchers(fd); // This is a leak, but since we enter an infinite loop this is ok?

  while (1) {
    HCFileData *fdata = fl_head, *fdata_prev = NULL;

    gettimeofday(&now, NULL);
    /* Read the inotify events, blocking until we get something */
    int len = read(fd, buffer, INOTIFY_BUFLEN);

    /* The fl_head is a linked list of previously released data entries. They
       are ordered "by time", so once we find one that is scheduled for deletion,
       we can also delete all entries after it in the linked list. */
    while (fdata) {
      if (now.tv_sec > fdata->remove) {
        /* Now drop off the "tail" from the freelist */
        if (fdata_prev) {
          fdata_prev->_next = NULL;
        } else {
          fl_head = NULL;
        }

        /* free() everything in the "tail" */
        do {
          HCFileData *next = fdata->_next;

          TSDebug(PLUGIN_NAME, "Cleaning up entry from freelist");
          TSfree(fdata);
          fdata = next;
        } while (fdata);
        break; /* Stop the loop, there's nothing else left to examine */
      }
      fdata_prev = fdata;
      fdata      = fdata->_next;
    }

    if (len >= 0) {
      int i = 0;

      while (i < len) {
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        HCFileInfo *finfo           = g_config;

        while (finfo && !((event->wd == finfo->wd) ||
                          ((event->wd == finfo->dir->wd) && !strncmp(event->name, finfo->basename, event->len)))) {
          finfo = finfo->_next;
        }
        if (finfo) {
          HCFileData *new_data = TSmalloc(sizeof(HCFileData));
          HCFileData *old_data;

          if (event->mask & (IN_CLOSE_WRITE | IN_ATTRIB)) {
            TSDebug(PLUGIN_NAME, "Modify file event (%d) on %s", event->mask, finfo->fname);
          } else if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
            TSDebug(PLUGIN_NAME, "Create file event (%d) on %s", event->mask, finfo->fname);
            finfo->wd = inotify_add_watch(fd, finfo->fname, IN_DELETE_SELF | IN_CLOSE_WRITE | IN_ATTRIB);
          } else if (event->mask & (IN_DELETE_SELF | IN_MOVED_FROM)) {
            TSDebug(PLUGIN_NAME, "Delete file event (%d) on %s", event->mask, finfo->fname);
            finfo->wd = inotify_rm_watch(fd, finfo->wd);
          }
          /* Load the new data and then swap this atomically */
          memset(new_data, 0, sizeof(HCFileData));
          reload_status_file(finfo, new_data);
          TSDebug(PLUGIN_NAME, "Reloaded %s, len == %d, exists == %d", finfo->fname, new_data->b_len, new_data->exists);
          old_data = ink_atomic_swap_ptr(&(finfo->data), new_data);

          /* Add the old data to the head of the freelist */
          old_data->remove = now.tv_sec + FREELIST_TIMEOUT;
          old_data->_next  = fl_head;
          fl_head          = old_data;
        }
        /* coverity[ -tainted_data_return] */
        i += sizeof(struct inotify_event) + event->len;
      }
    }
  }

  return NULL; /* Yeah, that never happens */
}

/* Config file parsing */
static const char HEADER_TEMPLATE[] = "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nCache-Control: no-cache\r\n";

static char *
gen_header(char *status_str, char *mime, int *header_len)
{
  TSHttpStatus status;
  char *buf = NULL;

  status = atoi(status_str);
  if (status > TS_HTTP_STATUS_NONE && status < (TSHttpStatus)999) {
    const char *status_reason;
    int len = sizeof(HEADER_TEMPLATE) + 3 + 1;

    status_reason = TSHttpHdrReasonLookup(status);
    len += strlen(status_reason);
    len += strlen(mime);
    buf         = TSmalloc(len);
    *header_len = snprintf(buf, len, HEADER_TEMPLATE, status, status_reason, mime);
  } else {
    *header_len = 0;
  }

  return buf;
}

static HCFileInfo *
parse_configs(const char *fname)
{
  FILE *fd;
  char buf[2 * 1024];
  HCFileInfo *head_finfo = NULL, *finfo = NULL, *prev_finfo = NULL;

  if (!fname) {
    return NULL;
  }

  if ('/' == *fname) {
    fd = fopen(fname, "r");
  } else {
    char filename[PATH_MAX + 1];

    snprintf(filename, sizeof(filename), "%s/%s", TSConfigDirGet(), fname);
    fd = fopen(filename, "r");
  }

  if (NULL == fd) {
    TSError("%s: Could not open config file", PLUGIN_NAME);
    return NULL;
  }

  while (!feof(fd)) {
    char *str, *save;
    char *ok = NULL, *miss = NULL, *mime = NULL;

    finfo = TSmalloc(sizeof(HCFileInfo));
    memset(finfo, 0, sizeof(HCFileInfo));

    if (fgets(buf, sizeof(buf) - 1, fd)) {
      str       = strtok_r(buf, SEPARATORS, &save);
      int state = 0;
      while (NULL != str) {
        if (strlen(str) > 0) {
          switch (state) {
          case 0:
            if ('/' == *str) {
              ++str;
            }
            strncpy(finfo->path, str, PATH_NAME_MAX - 1);
            finfo->p_len = strlen(finfo->path);
            break;
          case 1:
            strncpy(finfo->fname, str, MAX_PATH_LEN - 1);
            finfo->basename = strrchr(finfo->fname, '/');
            if (finfo->basename) {
              ++(finfo->basename);
            }
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
        str = strtok_r(NULL, SEPARATORS, &save);
      }

      /* Fill in the info if everything was ok */
      if (state > 4) {
        TSDebug(PLUGIN_NAME, "Parsed: %s %s %s %s %s", finfo->path, finfo->fname, mime, ok, miss);
        finfo->ok   = gen_header(ok, mime, &finfo->o_len);
        finfo->miss = gen_header(miss, mime, &finfo->m_len);
        finfo->data = TSmalloc(sizeof(HCFileData));
        memset(finfo->data, 0, sizeof(HCFileData));
        reload_status_file(finfo, finfo->data);

        /* Add it the linked list */
        TSDebug(PLUGIN_NAME, "Adding path=%s to linked list", finfo->path);
        if (NULL == head_finfo) {
          head_finfo = finfo;
        } else {
          prev_finfo->_next = finfo;
        }
        prev_finfo = finfo;
      } else {
        TSfree(finfo);
      }
    }
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
    TSError("[healthchecks] hc_process_read: Received TS_EVENT_ERROR");
  } else if (event == TS_EVENT_VCONN_EOS) {
    /* client may end the connection, simply return */
    return;
  } else if (event == TS_EVENT_NET_ACCEPT_FAILED) {
    TSError("[healthchecks] hc_process_read: Received TS_EVENT_NET_ACCEPT_FAILED");
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

    len = snprintf(buf, sizeof(buf), "Content-Length: %d\r\n\r\n", my_state->data->b_len);
    my_state->output_bytes += add_data_to_resp(buf, len, my_state);
    if (my_state->data->b_len > 0) {
      my_state->output_bytes += add_data_to_resp(my_state->data->body, my_state->data->b_len, my_state);
    } else {
      my_state->output_bytes += add_data_to_resp("\r\n", 2, my_state);
    }
    TSVIONBytesSet(my_state->write_vio, my_state->output_bytes);
    TSVIOReenable(my_state->write_vio);
  } else if (event == TS_EVENT_VCONN_WRITE_COMPLETE) {
    cleanup(contp, my_state);
  } else if (event == TS_EVENT_ERROR) {
    TSError("[healthchecks] hc_process_write: Received TS_EVENT_ERROR");
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }
}

/* Process the accept event from the SM */
static void
hc_process_accept(TSCont contp, HCState *my_state)
{
  my_state->req_buffer  = TSIOBufferCreate();
  my_state->resp_buffer = TSIOBufferCreate();
  my_state->resp_reader = TSIOBufferReaderAlloc(my_state->resp_buffer);
  my_state->read_vio    = TSVConnRead(my_state->net_vc, contp, my_state->req_buffer, INT64_MAX);
}

/* Implement the server intercept */
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
  TSHttpTxn txnp   = (TSHttpTxn)edata;
  HCFileInfo *info = g_config;

  if ((TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &reqp, &hdr_loc)) && (TS_SUCCESS == TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc))) {
    int path_len     = 0;
    const char *path = TSUrlPathGet(reqp, url_loc, &path_len);

    /* Short circuit the / path, common case, and we won't allow healthchecks on / */
    if (!path || !path_len) {
      goto cleanup;
    }

    while (info) {
      if (info->p_len == path_len && !memcmp(info->path, path, path_len)) {
        TSDebug(PLUGIN_NAME, "Found match for /%.*s", path_len, path);
        break;
      }
      info = info->_next;
    }

    if (!info) {
      goto cleanup;
    }

    TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_SKIP_REMAPPING, true); /* not strictly necessary, but speed is everything these days */

    /* This is us -- register our intercept */
    icontp   = TSContCreate(hc_intercept, TSMutexCreate());
    my_state = (HCState *)TSmalloc(sizeof(*my_state));
    memset(my_state, 0, sizeof(*my_state));
    my_state->info = info;
    my_state->data = info->data;
    TSContDataSet(icontp, my_state);
    TSHttpTxnIntercept(icontp, txnp);
  }

cleanup:
  if (url_loc) {
    TSHandleMLocRelease(reqp, hdr_loc, url_loc);
  }
  if (hdr_loc) {
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

/* Initialize the plugin / global continuation hook */
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  if (2 != argc) {
    TSError("[healthchecks] Must specify a configuration file");
    return;
  }

  info.plugin_name   = "health_checks";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[healthchecks] Plugin registration failed");
    return;
  }

  /* This will update the global configuration file, and is not reloaded at run time */
  /* ToDo: Support reloading with traffic_ctl config reload ? */
  if (NULL == (g_config = parse_configs(argv[1]))) {
    TSError("[healthchecks] Unable to read / parse %s config file", argv[1]);
    return;
  }

  /* Setup the background thread */
  if (!TSThreadCreate(hc_thread, NULL)) {
    TSError("[healthchecks] Failure in thread creation");
    return;
  }

  /* Create a continuation with a mutex as there is a shared global structure
     containing the headers to add */
  TSDebug(PLUGIN_NAME, "Started %s plugin", PLUGIN_NAME);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(health_check_origin, NULL));
}
