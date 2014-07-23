/** @file

  A brief file description

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

/************* ***************************
 *
 *  WebIntrMain.cc - main loop for the Web Interface
 *
 *
 *
 ****************************************************************************/

#include "libts.h"
#include "I_Layout.h"
#include "LocalManager.h"
#include "WebHttp.h"
#include "WebGlobals.h"
#include "MgmtUtils.h"
#include "WebMgmtUtils.h"
#include "WebIntrMain.h"
#include "Diags.h"
#include "MgmtSocket.h"

//INKqa09866
#include "TSControlMain.h"
#include "EventControlMain.h"

#if !defined(linux)
// Solaris header files forget this one
extern "C"
{
  int usleep(unsigned int useconds);
}
#endif

/* Ugly hack - define HEADER_MD_5 to prevent the SSLeay md5.h
 *  header file from being included since it conflicts with the
 *  md5 implememntation from ink_code.h
 *
 *  Additionally define HEAP_H and STACK_H to prevent stuff
 *   from the template library from being included which
 *   SUNPRO CC does not not like.
 */

// part of ugly hack described no longer needed
//#define HEADER_MD5_H
#define HEAP_H
#define STACK_H

typedef int fd;
static RecInt autoconf_localhost_only = 1;

#define SOCKET_TIMEOUT 10*60


WebInterFaceGlobals wGlobals;

// There are two web ports maintained
//
//  One is for administration.  This port serves
//     all the configuration and monitoring info.
//     Most sites will have some security features
//     (authentication and SSL) active on this
//     port since it system administrator access
//  The other is for things that we want to serve
//     insecurely.  Client auto configuration falls
//     in this category.  The public key for the
//     administration server is another example
//
WebContext autoconfContext;

// Used for storing argument values
int aconf_port_arg = -1;

// int checkWebContext(WebContext* wctx, char* desc)
//
//    Checks out a WebContext to make sure that the
//      directory exists and that the default file
//      exists
//
//    returns 0 if everything is OK
//    returns 1 if something is missing
//
int
checkWebContext(WebContext * wctx, const char *desc)
{

  struct stat fInfo;
  textBuffer defaultFile(256);

  if (wctx->docRoot == NULL) {
    mgmt_log(stderr, "[checkWebContext] No document root specified for %s\n", desc);
    return 1;
  }

  if (stat(wctx->docRoot, &fInfo) < 0) {
    mgmt_log(stderr, "[checkWebContext] Unable to access document root '%s' for %s : %s\n",
             wctx->docRoot, desc, strerror(errno));
    return 1;
  }

  if (!(fInfo.st_mode & S_IFDIR)) {
    mgmt_log(stderr, "[checkWebContext] Document root '%s' for %s is not a directory\n", wctx->docRoot, desc);
    return 1;
  }

  if (wctx->defaultFile == NULL) {
    mgmt_log(stderr, "[checkWebContext] No default document specified for %s\n", desc);
    return 1;
  }

  defaultFile.copyFrom(wctx->docRoot, strlen(wctx->docRoot));
  defaultFile.copyFrom("/", 1);
  defaultFile.copyFrom(wctx->defaultFile, strlen(wctx->defaultFile));

  if (stat(defaultFile.bufPtr(), &fInfo) < 0) {
    mgmt_log(stderr, "Unable to access default document, %s, for %s : %s\n", wctx->defaultFile, desc, strerror(errno));
    return 1;
  }

  if (!(fInfo.st_mode & S_IFREG)) {
    mgmt_log(stderr, "[checkWebContext] Default document for %s is not a file\n", desc);
    return 1;
  }

  return 0;
}


//  fd newUNIXsocket(char* fpath)
//
//  returns a file descriptor associated with a new socket
//    with the specified file path
//
//  returns -1 if socket could not be created
//
//  Thread Safe: NO!  Call only from main Web interface thread
//
static fd
newUNIXsocket(char *fpath)
{
  // coverity[var_decl]
  struct sockaddr_un serv_addr;
  int servlen;
  fd socketFD;
  int one = 1;

  unlink(fpath);
  socketFD = socket(AF_UNIX, SOCK_STREAM, 0);

  if (socketFD < 0) {
    mgmt_log(stderr, "[newUNIXsocket] Unable to create socket: %s", strerror(errno));
    return socketFD;
  }

  serv_addr.sun_family = AF_UNIX;
  ink_strlcpy(serv_addr.sun_path, fpath, sizeof(serv_addr.sun_path));
#if defined(darwin) || defined(freebsd)
  servlen = sizeof(struct sockaddr_un);
#else
  servlen = strlen(serv_addr.sun_path) + sizeof(serv_addr.sun_family);
#endif
  if (setsockopt(socketFD, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(int)) < 0) {
    mgmt_log(stderr, "[newUNIXsocket] Unable to set socket options: %s\n", strerror(errno));
  }

  if ((bind(socketFD, (struct sockaddr *) &serv_addr, servlen)) < 0) {
    mgmt_log(stderr, "[newUNIXsocket] Unable to bind socket: %s\n", strerror(errno));
    close_socket(socketFD);
    return -1;
  }

  if (chmod(fpath, 00755) < 0) {
    mgmt_log(stderr, "[newUNIXsocket] Unable to chmod unix-domain socket: %s\n", strerror(errno));
    close_socket(socketFD);
    return -1;
  }

  if ((listen(socketFD, 5)) < 0) {
    mgmt_log(stderr, "[newUNIXsocket] Unable to listen on socket: %s", strerror(errno));
    close_socket(socketFD);
    return -1;
  }
  // Set the close on exec flag so our children do not
  //  have this socket open
  if (fcntl(socketFD, F_SETFD, 1) < 0) {
    mgmt_elog(stderr, errno, "[newUNIXSocket] Unable to set close on exec flag\n");
  }

  return socketFD;
}

//  fd newTcpSocket(int port)
//
//  returns a file descriptor associated with a new socket
//    on the specified port
//
//  If the socket could not be created, returns -1
//
//  Thread Safe: NO!  Call only from main Web interface thread
//
static fd
newTcpSocket(int port)
{
  struct sockaddr_in socketInfo;
  fd socketFD;
  int one = 1;

  memset(&socketInfo, 0, sizeof(sockaddr_in));

  // Create the new TCP Socket
  if ((socketFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    mgmt_fatal(stderr, errno, "[newTcpSocket]: %s", "Unable to Create Socket\n");
    return -1;
  }
  // Specify our port number is network order
  memset(&socketInfo, 0, sizeof(socketInfo));
  socketInfo.sin_family = AF_INET;
  socketInfo.sin_port = htons(port);
  if (autoconf_localhost_only == 1) {
    socketInfo.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else {
    socketInfo.sin_addr.s_addr = htonl(INADDR_ANY);
  }

  // Allow for immediate re-binding to port
  if (setsockopt(socketFD, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(int)) < 0) {
    mgmt_fatal(stderr, errno, "[newTcpSocket] Unable to set socket options.\n");
  }
  // Bind the port to the socket
  if (bind(socketFD, (sockaddr *) & socketInfo, sizeof(socketInfo)) < 0) {
    mgmt_elog(stderr, 0, "[newTcpSocket] Unable to bind port %d to socket: %s\n", port, strerror(errno));
    close_socket(socketFD);
    return -1;
  }
  // Listen on the new socket
  if (listen(socketFD, 5) < 0) {
    mgmt_elog(stderr, errno, "[newTcpSocket] %s\n", "Unable to listen on the socket");
    close_socket(socketFD);
    return -1;
  }
  // Set the close on exec flag so our children do not
  //  have this socket open
  if (fcntl(socketFD, F_SETFD, 1) < 0) {
    mgmt_elog(stderr, errno, "[newTcpSocket] Unable to set close on exec flag\n");
  }

  return socketFD;
}

// Keep track of the number of service threads for debugging
//  purposes
static volatile int32_t numServiceThr = 0;

void *
serviceThrReaper(void * /* arg ATS_UNUSED */)
{
  int numJoined;

  lmgmt->syslogThrInit();

  while (1) {
    numJoined = 0;

    // coverity[lock][lockagain]
    ink_mutex_acquire(&wGlobals.serviceThrLock);

    for (int i = 0; i < MAX_SERVICE_THREADS; i++) {
      if (wGlobals.serviceThrArray[i].threadId != 0) {
        if (wGlobals.serviceThrArray[i].waitingForJoin == true) {

          //fprintf(stderr, "Joining on thread %d in slot %d\n",
          //       wGlobals.serviceThrArray[i].threadId, i);

          // Join on threads that have exited so we recycle their thrIds and
          //     stack space
          ink_assert(wGlobals.serviceThrArray[i].threadId > 0);
          ink_thread_join(wGlobals.serviceThrArray[i].threadId);

          wGlobals.serviceThrArray[i].fd = -1;
          wGlobals.serviceThrArray[i].threadId = 0;
          wGlobals.serviceThrArray[i].startTime = 0;
          wGlobals.serviceThrArray[i].waitingForJoin = false;
          wGlobals.serviceThrArray[i].alreadyShutdown = false;

          numJoined++;
        }
      }
    }

    ink_mutex_release(&wGlobals.serviceThrLock);

    for (int j = 0; j < numJoined; j++) {
#if defined(darwin)
      ink_sem_post(wGlobals.serviceThrCount);
#else
      ink_sem_post(&wGlobals.serviceThrCount);
#endif
      ink_atomic_increment((int32_t *) & numServiceThr, -1);
    }

    usleep(300000);
  }

  return NULL;
}                               // END serviceThrReaper()

void *
webIntr_main(void *)
{
  fd socketFD = -1;             // FD for incoming HTTP connections
  fd autoconfFD = -1;           // FD for incoming autoconf connections
  fd clientFD = -1;             // FD for accepted connections
  fd mgmtapiFD = -1;            // FD for the api interface to issue commands
  fd eventapiFD = -1;           // FD for the api and clients to handle event callbacks

  // dg: added init to get rid of compiler warnings
  fd acceptFD = 0;              // FD that is ready for accept
  UIthr_t serviceThr = NO_THR;  // type for new service thread

  struct sockaddr_in *clientInfo;       // Info about client connection
  ink_thread thrId;             // ID of service thread we just spawned
  fd_set selectFDs;             // FD set passed to select
  int publicPort = -1;          // Port for incoming autoconf connections
#if !defined(linux)
  sigset_t allSigs;             // Set of all signals
#endif
  char pacFailMsg[] = "Auto-Configuration Service Failed to Initialize";
  //  char gphFailMsg[] = "Dynamic Graph Service Failed to Initialize";
  char mgmtapiFailMsg[] = "Traffic server management API service Interface Failed to Initialize.";

  RecInt tempInt;
  bool found;

  int addrLen;
  int i;

#if !defined(linux)
  // Start by blocking all signals
  sigfillset(&allSigs);
  ink_thread_sigsetmask(SIG_SETMASK, &allSigs, NULL);
#endif

  lmgmt->syslogThrInit();

  // Set up the threads management
#if defined(darwin)
  static int qnum = 0;
  char sname[NAME_MAX];
  qnum++;
  snprintf(sname,NAME_MAX,"%s%d","WebInterfaceMutex",qnum);
  ink_sem_unlink(sname); // FIXME: remove, semaphore should be properly deleted after usage
  wGlobals.serviceThrCount = ink_sem_open(sname, O_CREAT | O_EXCL, 0777, MAX_SERVICE_THREADS);
#else /* !darwin */
  ink_sem_init(&wGlobals.serviceThrCount, MAX_SERVICE_THREADS);
#endif /* !darwin */
  ink_mutex_init(&wGlobals.serviceThrLock, "Web Interface Mutex");
  wGlobals.serviceThrArray = new serviceThr_t[MAX_SERVICE_THREADS];
  for (i = 0; i < MAX_SERVICE_THREADS; i++) {
    // coverity[missing_lock]
    wGlobals.serviceThrArray[i].threadId = 0;
    // coverity[missing_lock]
    wGlobals.serviceThrArray[i].fd = -1;
    // coverity[missing_lock]
    wGlobals.serviceThrArray[i].startTime = 0;
    // coverity[missing_lock]
    wGlobals.serviceThrArray[i].waitingForJoin = false;
  }
  ink_thread_create(serviceThrReaper, NULL);

  // Init mutex to only allow one submissions at a time
  ink_mutex_init(&wGlobals.submitLock, "Submission Mutex");

  // Fix for INKqa10514
  found = (RecGetRecordInt("proxy.config.admin.autoconf.localhost_only", &autoconf_localhost_only) == REC_ERR_OKAY);
  ink_assert(found);

  // Set up the client autoconfiguration context
  //
  //  Since autoconf is public access, turn security
  //     features off
  if (aconf_port_arg > 0) {
    publicPort = aconf_port_arg;
  } else {
    found = (RecGetRecordInt("proxy.config.admin.autoconf_port", &tempInt) == REC_ERR_OKAY);
    publicPort = (int) tempInt;
    ink_assert(found);
  }
  Debug("ui", "[WebIntrMain] Starting Client AutoConfig Server on Port %d", publicPort);

  found = (RecGetRecordString_Xmalloc("proxy.config.admin.autoconf.doc_root", &(autoconfContext.docRoot)) == REC_ERR_OKAY);
  ink_assert(found);

  if (autoconfContext.docRoot == NULL) {
    mgmt_fatal(stderr, 0, "[WebIntrMain] No Client AutoConf Root\n");
  } else {
    struct stat s;
    int err;

    if ((err = stat(autoconfContext.docRoot, &s)) < 0) {
      ats_free(autoconfContext.docRoot);
      autoconfContext.docRoot = ats_strdup(Layout::get()->sysconfdir);
      if ((err = stat(autoconfContext.docRoot, &s)) < 0) {
        mgmt_elog(0, "[WebIntrMain] unable to stat() directory '%s': %d %d, %s\n",
                autoconfContext.docRoot, err, errno, strerror(errno));
        mgmt_elog(0, "[WebIntrMain] please set the 'TS_ROOT' environment variable\n");
        mgmt_fatal(stderr, 0, "[WebIntrMain] No Client AutoConf Root\n");
      }
    }
    autoconfContext.docRootLen = strlen(autoconfContext.docRoot);
  }
  autoconfContext.defaultFile = "/proxy.pac";

  // INKqa09866
  // fire up interface for ts configuration through API; use absolute path from root to
  // set up socket paths;
  char api_sock_path[1024];
  char event_sock_path[1024];
  xptr<char> rundir(RecConfigReadRuntimeDir());

  bzero(api_sock_path, 1024);
  bzero(event_sock_path, 1024);
  snprintf(api_sock_path, sizeof(api_sock_path), "%s/mgmtapisocket", (const char *)rundir);
  snprintf(event_sock_path, sizeof(event_sock_path), "%s/eventapisocket", (const char *)rundir);

  // INKqa12562: MgmtAPI sockets should be created with 775 permission
  mode_t oldmask = umask(S_IWOTH);
  if ((mgmtapiFD = newUNIXsocket(api_sock_path)) < 0) {
    mgmt_log(stderr, "[WebIntrMain] Unable to set up socket for handling management API calls. API socket path = %s\n",
             api_sock_path);
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, mgmtapiFailMsg);
  }

  if ((eventapiFD = newUNIXsocket(event_sock_path)) < 0) {
    mgmt_log(stderr,
             "[WebIntrMain] Unable to set up so for handling management API event calls. Event Socket path: %s\n",
             event_sock_path);
  }
  umask(oldmask);

  // launch threads
  // create thread for mgmtapi
  ink_thread_create(ts_ctrl_main, &mgmtapiFD);
  ink_thread_create(event_callback_main, &eventapiFD);

  // initialize mgmt api plugins
  // mgmt_plugin_init(config_path);

  // Check our web contexts to make sure everything is
  //  OK.  If it is, go ahead and fire up the interfaces
  if (checkWebContext(&autoconfContext, "Browser Auto-Configuration") != 0) {
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, pacFailMsg);
  } else {
    if ((autoconfFD = newTcpSocket(publicPort)) < 0) {
      mgmt_elog(stderr, errno, "[WebIntrMain] Unable to start client autoconf server\n");
      lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, pacFailMsg);
    }
  }

  // Initialze WebHttp Module
  WebHttpInit();

  while (1) {
    FD_ZERO(&selectFDs);

    if (socketFD >= 0) {
      FD_SET(socketFD, &selectFDs);
    }

    if (autoconfFD >= 0) {
      FD_SET(autoconfFD, &selectFDs);
    }

    // TODO: Should we check return value?
    mgmt_select(FD_SETSIZE, &selectFDs, (fd_set *) NULL, (fd_set *) NULL, NULL);

    if (autoconfFD >= 0 && FD_ISSET(autoconfFD, &selectFDs)) {
      acceptFD = autoconfFD;
      serviceThr = AUTOCONF_THR;
    } else {
      ink_assert(!"[webIntrMain] Error on mgmt_select()\n");
    }
#if defined(darwin)
    ink_sem_wait(wGlobals.serviceThrCount);
#else
    ink_sem_wait(&wGlobals.serviceThrCount);
#endif
    ink_atomic_increment((int32_t *) & numServiceThr, 1);

    // coverity[alloc_fn]
    clientInfo = (struct sockaddr_in *)ats_malloc(sizeof(struct sockaddr_in));
    addrLen = sizeof(struct sockaddr_in);

    // coverity[noescape]
    if ((clientFD = mgmt_accept(acceptFD, (sockaddr *) clientInfo, &addrLen)) < 0) {
      mgmt_log(stderr, "[WebIntrMain]: %s%s\n", "Accept on incoming connection failed: ", strerror(errno));
#if defined(darwin)
      ink_sem_post(wGlobals.serviceThrCount);
#else
      ink_sem_post(&wGlobals.serviceThrCount);
#endif
      ink_atomic_increment((int32_t *) & numServiceThr, -1);
    } else {                    // Accept succeeded
      if (safe_setsockopt(clientFD, IPPROTO_TCP, TCP_NODELAY, SOCKOPT_ON, sizeof(int)) < 0) {
        mgmt_log(stderr, "[WebIntrMain]Failed to set sock options: %s\n", strerror(errno));
      }

      // Accept OK
      ink_mutex_acquire(&wGlobals.serviceThrLock);

      // If this a web manager, make sure that it is from an allowed ip addr
      if (serviceThr == AUTOCONF_THR && autoconf_localhost_only != 0 &&
          strcmp(inet_ntoa(clientInfo->sin_addr), "127.0.0.1") != 0) {
        mgmt_log("WARNING: connect by disallowed client %s, closing\n", inet_ntoa(clientInfo->sin_addr));
#if defined(darwin)
        ink_sem_post(wGlobals.serviceThrCount);
#else
        ink_sem_post(&wGlobals.serviceThrCount);
#endif
        ink_atomic_increment((int32_t *) & numServiceThr, -1);
        ats_free(clientInfo);
        close_socket(clientFD);
      } else {                  // IP is allowed

        for (i = 0; i < MAX_SERVICE_THREADS; i++) {
          if (wGlobals.serviceThrArray[i].threadId == 0) {
            //
            wGlobals.serviceThrArray[i].fd = clientFD;
            wGlobals.serviceThrArray[i].startTime = time(NULL);
            wGlobals.serviceThrArray[i].waitingForJoin = false;
            wGlobals.serviceThrArray[i].alreadyShutdown = false;
            wGlobals.serviceThrArray[i].type = serviceThr;
            wGlobals.serviceThrArray[i].clientInfo = clientInfo;
            thrId = ink_thread_create(serviceThrMain, &wGlobals.serviceThrArray[i], 0);
            // fprintf (stderr, "New Service Thread %d in slot %d\n", thrId, i);

            if (thrId > 0) {
              wGlobals.serviceThrArray[i].threadId = thrId;
            } else {
              // Failed to create thread
              mgmt_elog(stderr, errno, "[WebIntrMain] Failed to create service thread\n");
              wGlobals.serviceThrArray[i].threadId = 0;
              wGlobals.serviceThrArray[i].fd = -1;
              close_socket(clientFD);
#if defined(darwin)
              ink_sem_post(wGlobals.serviceThrCount);
#else
              ink_sem_post(&wGlobals.serviceThrCount);
#endif
              ink_atomic_increment((int32_t *) & numServiceThr, -1);
            }

            break;
          } else if (i == MAX_SERVICE_THREADS - 1) {
            mgmt_fatal(stderr, 0, "[WebIntrMain] Syncronizaion Failure\n");
            _exit(1);
          }
        }
      }

      ink_mutex_release(&wGlobals.serviceThrLock);
    }
  }                             // end while(1)
  ink_release_assert(!"impossible");    // should never get here
  return NULL;
}

// void* serviceThrMain(void* info)
//
// Thread main for any type of service thread
//
void *
serviceThrMain(void *info)
{
  serviceThr_t *threadInfo = (serviceThr_t *) info;
  WebHttpConInfo httpInfo;

  lmgmt->syslogThrInit();

  // Do our work
  switch (threadInfo->type) {
  case NO_THR:                 // dg: added to handle init value
    ink_assert(false);
    break;
  case AUTOCONF_THR:
    httpInfo.fd = threadInfo->fd;
    httpInfo.context = &autoconfContext;
    httpInfo.clientInfo = threadInfo->clientInfo;
    WebHttpHandleConnection(&httpInfo);
    break;
  default:
    // Handled here:
    // GRAPH_THR
    break;
  }

  ats_free(threadInfo->clientInfo);

  // Mark ourselves ready to be reaped
  ink_mutex_acquire(&wGlobals.serviceThrLock);

  threadInfo->waitingForJoin = true;
  threadInfo->fd = -1;

  ink_mutex_release(&wGlobals.serviceThrLock);

  // Call exit so that we properly release system resources
  ink_thread_exit(NULL);
  return NULL;                  // No Warning
}
