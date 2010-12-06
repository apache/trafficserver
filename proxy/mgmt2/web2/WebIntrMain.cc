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
#include "Main.h"
#include "BaseRecords.h"
#include "WebHttp.h"
#include "WebGlobals.h"
#include "MgmtUtils.h"
#include "WebMgmtUtils.h"
#include "WebIntrMain.h"
#include "CLI.h"
#include "WebReconfig.h"
#include "MgmtAllow.h"
#include "Diags.h"
#include "MgmtSocket.h"

//INKqa09866
#include "TSControlMain.h"
#include "EventControlMain.h"
#include "MgmtPlugin.h"

#if !defined(linux)
// Solaris header files forget this one
extern "C"
{
  int usleep(unsigned int useconds);
}
#endif

#include "openssl/ssl.h"
#include "openssl/err.h"
#include "openssl/crypto.h"

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

#define SOCKET_TIMEOUT 10*60


WebInterFaceGlobals wGlobals;

// There are two web ports maintained
//
//  One is for adminstration.  This port serves
//     all the configuration and monitoring info.
//     Most sites will have some security features
//     (authentication and SSL) active on this
//     port since it system administrator access
//  The other is for things that we want to serve
//     insecurely.  Client auto configuration falls
//     in this catagory.  The public key for the
//     administration server is another example
//
WebContext adminContext;
WebContext autoconfContext;

// Used for storing argument values
int web_port_arg = -1;
int aconf_port_arg = -1;

// INKqa10098: UBSWarburg: Overseer port enabled by default
static int overseerMode = 0;

// Locks that SSleay uses
static ink_mutex ssl_locks[CRYPTO_NUM_LOCKS];

void
SSLeay_mutex_cb(int mode, int type, const char *file, int line)
{
  NOWARN_UNUSED(file);
  NOWARN_UNUSED(line);
  ink_release_assert(type < CRYPTO_NUM_LOCKS);
  ink_release_assert(type >= 0);
  if (mode & CRYPTO_LOCK) {
    Debug("ssl_lock", "Acquiring ssl lock %d", type);
    ink_mutex_acquire(&ssl_locks[type]);
  } else {
    Debug("ssl_lock", "Releasing ssl lock %d", type);
    ink_mutex_release(&ssl_locks[type]);
  }
}

unsigned long
SSLeay_tid_cb()
{
  return (unsigned long) ink_thread_self();
}


// init_SSL()
//
//  Set up SSL info - code derived from SSL
//
int
init_SSL(char *sslCertFile, WebContext * wContext)
{

  // Hard coded error buffer size because that is how SSLeay
  //   does it internally
  //
  char ssl_Error[256];
  unsigned long sslErrno;

  if (sslCertFile == NULL) {
    mgmt_log(stderr, "[initSSL] No Certificate File was specified\n");
    return -1;
  }
  // Setup thread/locking callbacks
  for (int i = 0; i < CRYPTO_NUM_LOCKS; i++) {
    ink_mutex_init(&ssl_locks[i], "SSLeay mutex");
  }
  CRYPTO_set_id_callback(SSLeay_tid_cb);
  CRYPTO_set_locking_callback(SSLeay_mutex_cb);

  SSL_load_error_strings();

  SSLeay_add_ssl_algorithms();
  wContext->SSL_Context = (SSL_CTX *) SSL_CTX_new(SSLv23_server_method());

  if (SSL_CTX_use_PrivateKey_file(wContext->SSL_Context, sslCertFile, SSL_FILETYPE_PEM) <= 0) {
    sslErrno = ERR_get_error();
    ERR_error_string(sslErrno, ssl_Error);
    mgmt_log(stderr, "[initSSL] Unable to set public key file: %s\n", ssl_Error);
    goto SSL_FAILED;
  }

  if (SSL_CTX_use_certificate_file(wContext->SSL_Context, sslCertFile, SSL_FILETYPE_PEM) <= 0) {
    sslErrno = ERR_get_error();
    ERR_error_string(sslErrno, ssl_Error);
    mgmt_log(stderr, "[initSSL] Unable to set certificate file: %s\n", ssl_Error);
    goto SSL_FAILED;
  }

  /* Now we know that a key and cert have been set against
   * the SSL context */
  if (!SSL_CTX_check_private_key(wContext->SSL_Context)) {
    sslErrno = ERR_get_error();
    ERR_error_string(sslErrno, ssl_Error);
    mgmt_log(stderr, "[initSSL] Private key does not match the certificate public key: %s\n", ssl_Error);
    goto SSL_FAILED;
  }
  // Set a timeout so users connecting with http:// will not
  //   have to wait forever for a timeout
  SSL_CTX_set_timeout(wContext->SSL_Context, 3);

  // Set SSL Read Ahead for higher performance
  SSL_CTX_set_default_read_ahead(wContext->SSL_Context, 1);

/* Since we are only shipping domestically right now, allow
   higher grade ciphers
   // Allow only Export Grade 40 bit secret ciphers
  if(!SSL_CTX_set_cipher_list(wContext->SSL_Context,
			      "EXP-RC4-MD5")) {
    sslErrno = ERR_get_error();
    ERR_error_string(sslErrno, ssl_Error);
    mgmt_fatal(stderr,"[initSSL] Unable to set the prefered cipher list: %s\n", ssl_Error);
  }
  */

  return 0;

SSL_FAILED:
  // Free up the SSL context on failure so we do not try to recycle
  //   it if SSL gets turned off and back on again
  if (wContext->SSL_Context != NULL) {
    SSL_CTX_free(adminContext.SSL_Context);
    wContext->SSL_Context = NULL;
  }
  return -1;
}

// void tmpFileDestructor(void* ptr)
//
//   Deletes the memory associated with the
//     tmp file TSD
//
void
tmpFileDestructor(void *ptr)
{
  xfree(ptr);
}

// static int setUpLogging()
//    Returns the file descriptor of the file to log mgmt
//      Web server access to.  Creates it if necessary
//
static int
setUpLogging()
{
  struct stat s;
  int err;
  char *log_dir;
  char log_file[PATH_NAME_MAX+1];

  if ((err = stat(system_log_dir, &s)) < 0) {
    ink_assert(RecGetRecordString_Xmalloc("proxy.config.log.logfile_dir", &log_dir)
	       == REC_ERR_OKAY);
    Layout::relative_to(system_log_dir, sizeof(system_log_dir),
                        Layout::get()->prefix, log_dir);
    if ((err = stat(log_dir, &s)) < 0) {
      mgmt_elog("unable to stat() log dir'%s': %d %d, %s\n",
                system_log_dir, err, errno, strerror(errno));
      mgmt_elog("please set 'proxy.config.log.logfile_dir'\n");
      //_exit(1);
    } else {
      ink_strncpy(system_log_dir,log_dir,sizeof(system_log_dir));
    }
  }
  Layout::relative_to(log_file, sizeof(log_file),
                      system_log_dir, log_dir);

  int diskFD = open(log_file, O_WRONLY | O_APPEND | O_CREAT, 0644);

  if (diskFD < 0) {
    mgmt_log(stderr, "[setUpLogging] Unable to open log file (%s).  No logging will occur: %s\n", log_file,strerror(errno));
  }

  fcntl(diskFD, F_SETFD, 1);

  return diskFD;
}

// int checkWebContext(WebContext* wctx, char* desc)
//
//    Checks out a WebContext to make sure that the
//      directory exists and that the default file
//      exists
//
//    returns 0 if everthing is OK
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
  defaultFile.copyFrom(DIR_SEP, 1);
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

//
//
//  Sets the LocalManager variable:  proxy.node.hostname
//
//    To the fully qualified hostname for the machine
//       that we are running on
int
setHostnameVar()
{
  char ourHostName[MAXDNAME];
  char *firstDot;

  // Get Our HostName
  if (gethostname(ourHostName, MAXDNAME) < 0) {
    mgmt_fatal(stderr, "[setHostnameVar] Can not determine our hostname");
  }

  res_init();
  appendDefaultDomain(ourHostName, MAXDNAME);

  // FQ is a Fully Qualified hostname (ie: proxydev.inktomi.com)
  varSetFromStr("proxy.node.hostname_FQ", ourHostName);

  // non-FQ is just the hostname (ie: proxydev)
  firstDot = strchr(ourHostName, '.');
  if (firstDot != NULL) {
    *firstDot = '\0';
  }
  varSetFromStr("proxy.node.hostname", ourHostName);

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
  ink_strncpy(serv_addr.sun_path, fpath, sizeof(serv_addr.sun_path));
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
    mgmt_elog(stderr, "[newUNIXSocket] Unable to set close on exec flag\n");
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
    mgmt_fatal(stderr, "[newTcpSocket]: %s", "Unable to Create Socket\n");
    return -1;
  }
  // Specify our port number is network order
  memset(&socketInfo, 0, sizeof(socketInfo));
  socketInfo.sin_family = AF_INET;
  socketInfo.sin_port = htons(port);
  socketInfo.sin_addr.s_addr = htonl(INADDR_ANY);

  // Allow for immediate re-binding to port
  if (setsockopt(socketFD, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(int)) < 0) {
    mgmt_fatal(stderr, "[newTcpSocket] Unable to set socket options.\n");
  }
  // Bind the port to the socket
  if (bind(socketFD, (sockaddr *) & socketInfo, sizeof(socketInfo)) < 0) {
    mgmt_elog(stderr, "[newTcpSocket] Unable to bind port %d to socket: %s\n", port, strerror(errno));
    close_socket(socketFD);
    return -1;
  }
  // Listen on the new socket
  if (listen(socketFD, 5) < 0) {
    mgmt_elog(stderr, "[newTcpSocket] %s\n", "Unable to listen on the socket");
    close_socket(socketFD);
    return -1;
  }
  // Set the close on exec flag so our children do not
  //  have this socket open
  if (fcntl(socketFD, F_SETFD, 1) < 0) {
    mgmt_elog(stderr, "[newTcpSocket] Unable to set close on exec flag\n");
  }

  return socketFD;
}

// Keep track of the number of service threads for debugging
//  purposes
static volatile int32 numServiceThr = 0;

void
printServiceThr(int sig)
{
  NOWARN_UNUSED(sig);

  fprintf(stderr, "Service Thread Array\n");
  fprintf(stderr, " Service Thread Count : %d\n", numServiceThr);
  for (int i = 0; i < MAX_SERVICE_THREADS; i++) {
    if (wGlobals.serviceThrArray[i].threadId != 0 || wGlobals.serviceThrArray[i].fd != -1) {
      fprintf(stderr,
              " Slot %d : FD %d : ThrId %lu : StartTime %d : WaitForJoin %s : Shutdown %s\n",
              i, wGlobals.serviceThrArray[i].fd,
              (unsigned long) wGlobals.serviceThrArray[i].threadId,
              (int) wGlobals.serviceThrArray[i].startTime,
              wGlobals.serviceThrArray[i].waitingForJoin ? "true" : "false",
              wGlobals.serviceThrArray[i].alreadyShutdown ? "true" : "false");
    }
  }
}

void *
serviceThrReaper(void *arg)
{
  NOWARN_UNUSED(arg);
  time_t currentTime;
  int numJoined;

  lmgmt->syslogThrInit();

  while (1) {

    numJoined = 0;

    // coverity[lock][lockagain]
    ink_mutex_acquire(&wGlobals.serviceThrLock);

    currentTime = time(NULL);
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

        } else if ((currentTime > wGlobals.serviceThrArray[i].startTime + SOCKET_TIMEOUT) &&
                   wGlobals.serviceThrArray[i].type == HTTP_THR &&
                   wGlobals.serviceThrArray[i].alreadyShutdown == false) {

          // Socket is presumed stuck.  Shutdown incoming
          // traffic on the socket so the thread handeling
          // socket will give up
          shutdown(wGlobals.serviceThrArray[i].fd, 0);

#if !defined(freebsd) && !defined(darwin)
          ink_thread_cancel(wGlobals.serviceThrArray[i].threadId);
#endif
#if defined(darwin)
          ink_sem_post(wGlobals.serviceThrCount);
#else
          ink_sem_post(&wGlobals.serviceThrCount);
#endif
          ink_atomic_increment((int32 *) & numServiceThr, -1);

          wGlobals.serviceThrArray[i].alreadyShutdown = true;
          Debug("ui", "%s %d %s %d\n", "Shuting Down Socket FD ",
                wGlobals.serviceThrArray[i].fd, "for thread", wGlobals.serviceThrArray[i].threadId);
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
      ink_atomic_increment((int32 *) & numServiceThr, -1);
    }

    usleep(300000);
  }

  return NULL;
}                               // END serviceThrReaper()

void *
webIntr_main(void *x)
{
  fd socketFD = -1;             // FD for incoming HTTP connections
  fd cliFD = -1;                // FD for incoming command  line interface connections
  fd autoconfFD = -1;           // FD for incoming autoconf connections
  fd clientFD = -1;             // FD for accepted connections
  fd overseerFD = -1;
  fd mgmtapiFD = -1;            // FD for the api interface to issue commands
  fd eventapiFD = -1;           // FD for the api and clients to handle event callbacks

  // dg: added init to get rid of compiler warnings
  fd acceptFD = 0;              // FD that is ready for accept
  UIthr_t serviceThr = NO_THR;  // type for new service thread

  struct sockaddr_in *clientInfo;       // Info about client connection
  ink_thread thrId;             // ID of service thread we just spawned
  fd_set selectFDs;             // FD set passed to select
  int webPort = -1;             // Port for incoming HTTP connections
  int publicPort = -1;          // Port for incoming autoconf connections
  int loggingEnabled;           // Whether to log accesses the mgmt server
  int cliEnabled;               // Whether cli server should be enabled
  int overseerPort = -1;
#if !defined(linux)
  sigset_t allSigs;             // Set of all signals
#endif
  char *cliPath = NULL;         // UNIX: socket path for cli
#ifndef NO_WEBUI
  char webFailMsg[] = "Management Web Services Failed to Initialize";
#endif
  char pacFailMsg[] = "Auto-Configuration Service Failed to Initialize";
  //  char gphFailMsg[] = "Dynamic Graph Service Failed to Initialize";
  char cliFailMsg[] = "Command Line Interface Failed to Initialize";
  char aolFailMsg[] = "Overseer Interface Failed to Initialize";
  char mgmtapiFailMsg[] = "Traffic server managment API service Interface Failed to Initialize.";

  RecInt tempInt;
  bool found;
  int autoconf_localhost_only = 0;

  int addrLen;
  int i;
#ifndef NO_WEBUI
  int sleepTime = 2;
#endif
  // No Warning
  x = x;

#if !defined(linux)
  // Start by blocking all signals
  sigfillset(&allSigs);
  ink_thread_sigsetmask(SIG_SETMASK, &allSigs, NULL);
#endif

  lmgmt->syslogThrInit();

  // Initialize the AutoConf Obj
  // ewong: don't need anymore
  //autoConfObj = new AutoConf();

  // Set up frameset bindings
  // ewong: don't need anymore
  //initFrameBindings();

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

  // Get our configuration information
  //
  // Set up the administration context
  //
  if (web_port_arg > 0) {
    webPort = web_port_arg;
  } else {
    found = (RecGetRecordInt("proxy.config.admin.web_interface_port", &tempInt) == REC_ERR_OKAY);
    webPort = (int) tempInt;
    ink_assert(found);
  }
  Debug("ui", "[WebIntrMain] Starting up Web Server on Port %d\n", webPort);
  wGlobals.webPort = webPort;

  // Fix for INKqa10514
  found = (RecGetRecordInt("proxy.config.admin.autoconf.localhost_only", &tempInt) == REC_ERR_OKAY);
  autoconf_localhost_only = (int) tempInt;
  ink_assert(found);

  // Figure out the document root
  found = (RecGetRecordString_Xmalloc("proxy.config.admin.html_doc_root", &(adminContext.docRoot)) == REC_ERR_OKAY);
  ink_assert(found);

  if (adminContext.docRoot == NULL) {
    mgmt_fatal(stderr, "[WebIntrMain] No Document Root\n");
  } else {
    adminContext.docRootLen = strlen(adminContext.docRoot);
  }

  adminContext.defaultFile = "/index.ink";

  // Figure out the plugin document root
  RecString plugin_dir;
  found = (RecGetRecordString_Xmalloc("proxy.config.plugin.plugin_dir", &plugin_dir) == REC_ERR_OKAY);
  ink_assert(found);

  adminContext.pluginDocRootLen = strlen(ts_base_dir) + strlen(plugin_dir) + strlen(DIR_SEP);
  adminContext.pluginDocRoot = (char *) xmalloc(adminContext.pluginDocRootLen + 1);

  snprintf(adminContext.pluginDocRoot, adminContext.pluginDocRootLen + 1,
               "%s%s%s", ts_base_dir, DIR_SEP, plugin_dir);
  xfree(plugin_dir);

  int rec_err;
  rec_err = RecGetRecordInt("proxy.config.admin.overseer_mode", &tempInt);
  overseerMode = (int) tempInt;
  if ((rec_err != REC_ERR_OKAY) || overseerMode<0 || overseerMode> 2)
    overseerMode = 2;
  rec_err = RecGetRecordInt("proxy.config.admin.overseer_port", &tempInt);
  overseerPort = (int) tempInt;

  // setup our other_users hash-table (for WebHttpAuth)
  adminContext.other_users_ht = new MgmtHashTable("other_users_ht", false, InkHashTableKeyType_String);

  // setup our language dictionary hash-table
  adminContext.lang_dict_ht = new MgmtHashTable("lang_dict_ht", false, InkHashTableKeyType_String);

  adminContext.SSL_Context = NULL;

#ifndef NO_WEBUI
  // configure components
  configAuthEnabled();
  configAuthAdminUser();
  configAuthAdminPasswd();
  configAuthOtherUsers();
  // <@record> substitution requires WebHttpInit() first
  // configLangDict();
  configUI();
#endif /* NO_WEBUI */

  configSSLenable();
  Debug("ui", "SSL enabled is %d\n", adminContext.SSLenabled);

  // Set up the ip based access control
  configMgmtIpAllow();

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
  Debug("ui", "[WebIntrMain] Starting Client AutoConfig Server on Port %d\n", publicPort);

  found = (RecGetRecordString_Xmalloc("proxy.config.config_dir", &(autoconfContext.docRoot))
           == REC_ERR_OKAY);
  ink_assert(found);

  if (autoconfContext.docRoot == NULL) {
    mgmt_fatal(stderr, "[WebIntrMain] No Client AutoConf Root\n");
  } else {
    struct stat s;
    int err;
    if ((err = stat(autoconfContext.docRoot, &s)) < 0) {
      xfree(autoconfContext.docRoot);
      autoconfContext.docRoot = xstrdup(system_config_directory);
      if ((err = stat(autoconfContext.docRoot, &s)) < 0) {
        mgmt_elog("[WebIntrMain] unable to stat() directory '%s': %d %d, %s\n",
                autoconfContext.docRoot, err, errno, strerror(errno));
        mgmt_elog("[WebIntrMain] please set config path via command line '-path <path>' or 'proxy.config.config_dir' \n");
        mgmt_fatal(stderr, "[WebIntrMain] No Client AutoConf Root\n");
      }
    }
    autoconfContext.docRootLen = strlen(autoconfContext.docRoot);
  }
  autoconfContext.adminAuthEnabled = 0;
  autoconfContext.admin_user.user[0] = '\0';
  autoconfContext.admin_user.encrypt_passwd[0] = '\0';
  autoconfContext.other_users_ht = 0;
  autoconfContext.lang_dict_ht = 0;
  autoconfContext.SSLenabled = 0;
  autoconfContext.SSL_Context = NULL;
  autoconfContext.defaultFile = "/proxy.pac";
  autoconfContext.AdvUIEnabled = 1;     // full Web UI by default
  autoconfContext.FeatureSet = 1;       // default should be ?

  // Set up a TSD key for use by WebFileEdit
  ink_thread_key_create(&wGlobals.tmpFile, tmpFileDestructor);

  // Set up a TSD for storing the request structure.  I would have
  //   perfered to pass this along the call chain but I didn't think
  //   of it until too late so I'm using a TSD
  ink_thread_key_create(&wGlobals.requestTSD, NULL);

  // Set up refresh Info
  found = (RecGetRecordInt("proxy.config.admin.ui_refresh_rate", &tempInt) == REC_ERR_OKAY);
  wGlobals.refreshRate = (int) tempInt;
  ink_assert(found);

  // Set up our logging configuration
  found = (RecGetRecordInt("proxy.config.admin.log_mgmt_access", &tempInt) == REC_ERR_OKAY);
  loggingEnabled = (int) tempInt;
  if (found == true && loggingEnabled != 0) {
    wGlobals.logFD = setUpLogging();
  } else {
    wGlobals.logFD = -1;

  }
  found = (RecGetRecordInt("proxy.config.admin.log_resolve_hostname", &tempInt) == REC_ERR_OKAY);
  loggingEnabled = (int) tempInt;
  if (found == true && loggingEnabled != 0) {
    wGlobals.logResolve = true;
  } else {
    wGlobals.logResolve = false;
  }

  // Set for reconfiguration callbacks
  setUpWebCB();


  // INKqa09866
  // fire up interface for ts configuration through API; use absolute path from root to
  // set up socket paths;
  char api_sock_path[1024];
  char event_sock_path[1024];

  bzero(api_sock_path, 1024);
  bzero(event_sock_path, 1024);
  snprintf(api_sock_path, sizeof(api_sock_path), "%s%smgmtapisocket", system_runtime_dir, DIR_SEP);
  snprintf(event_sock_path, sizeof(event_sock_path), "%s%seventapisocket", system_runtime_dir, DIR_SEP);

  // INKqa12562: MgmtAPI sockets should be created with 775 permission
  mode_t oldmask = umask(S_IWOTH);
  if ((mgmtapiFD = newUNIXsocket(api_sock_path)) < 0) {
    mgmt_log(stderr, "[WebIntrMain] Unable to set up socket for handling managment API calls. API socket path = %s\n",
             api_sock_path);
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, mgmtapiFailMsg);
  }

  if ((eventapiFD = newUNIXsocket(event_sock_path)) < 0) {
    mgmt_log(stderr,
             "[WebIntrMain] Unable to set up so for handling managment API event calls. Event Socket path: %s\n",
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

#ifndef NO_WEBUI

  if (checkWebContext(&adminContext, "Web Management") != 0) {
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, webFailMsg);
    mgmt_elog(stderr, "[WebIntrMain] Web Interface Intialization failed.\n");
  } else {
    // Fire up the mgmt interface
    while ((socketFD = newTcpSocket(webPort)) < 0) {

      if (sleepTime >= 30) {
        mgmt_elog(stderr, "[WebIntrMain] Could not create Web Interface socket.  Giving Up.\n");
        lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, webFailMsg);
        break;
      } else {
        mgmt_elog(stderr, "[WebIntrMain] Unable to create Web Interface socket.  Will try again in %d seconds\n",
                  sleepTime);
        mgmt_sleep_sec(sleepTime);
        sleepTime *= 2;
      }
    }
  }

#endif //NO_WEBUI

  if (checkWebContext(&autoconfContext, "Browser Auto-Configuration") != 0) {
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, pacFailMsg);
  } else {
    if ((autoconfFD = newTcpSocket(publicPort)) < 0) {
      mgmt_elog(stderr, "[WebIntrMain] Unable to start client autoconf server\n");
      lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, pacFailMsg);
    }
  }

  found = (RecGetRecordInt("proxy.config.admin.cli_enabled", &tempInt) == REC_ERR_OKAY);
  cliEnabled = (int) tempInt;
  if (found && cliEnabled) {
    found = (RecGetRecordString_Xmalloc("proxy.config.admin.cli_path", &cliPath) == REC_ERR_OKAY);
    if (found) {
      char *sockPath = Layout::relative_to(Layout::get()->runtimedir, cliPath);
      if ((cliFD = newUNIXsocket(sockPath)) < 0) {
        found = false;
      }
      xfree(sockPath);
    }
    if (!found) {
      mgmt_elog(stderr,
                "[WebIntrMain] Unable to start Command Line Interface server.  The command line tool will not work\n");
      lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, cliFailMsg);
    }
    if (cliPath)
      xfree(cliPath);
  }

  if (overseerMode > 0) {
    if (overseerPort > 0 && (overseerFD = newTcpSocket(overseerPort)) < 0) {
      mgmt_elog("[WebIntrMain] Unable to start overseer interface\n");
      lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, aolFailMsg);
    } else if (overseerPort < 0) {
      overseerFD = -1;
    }
  }

  // Initialze WebHttp Module
  WebHttpInit();
#ifndef NO_WEBUI
  configLangDict();
#endif /* NO_WEBUI */

  while (1) {

    FD_ZERO(&selectFDs);

    if (socketFD >= 0) {
      FD_SET(socketFD, &selectFDs);
    }

    if (cliFD >= 0) {
      FD_SET(cliFD, &selectFDs);
    }

    if (overseerFD >= 0) {
      FD_SET(overseerFD, &selectFDs);
    }

    if (autoconfFD >= 0) {
      FD_SET(autoconfFD, &selectFDs);
    }

    // TODO: Should we check return value?
    mgmt_select(32, &selectFDs, (fd_set *) NULL, (fd_set *) NULL, NULL);

    if (socketFD >= 0 && FD_ISSET(socketFD, &selectFDs)) {
      // new HTTP Connection
      acceptFD = socketFD;
      serviceThr = HTTP_THR;
    } else if (cliFD >= 0 && FD_ISSET(cliFD, &selectFDs)) {
      acceptFD = cliFD;
      serviceThr = CLI_THR;
    } else if (autoconfFD >= 0 && FD_ISSET(autoconfFD, &selectFDs)) {
      acceptFD = autoconfFD;
      serviceThr = AUTOCONF_THR;
    } else if (overseerFD >= 0 && FD_ISSET(overseerFD, &selectFDs)) {
      acceptFD = overseerFD;
      serviceThr = OVERSEER_THR;
    } else {
      ink_assert(!"[webIntrMain] Error on mgmt_select()\n");
    }
#if defined(darwin)
    ink_sem_wait(wGlobals.serviceThrCount);
#else
    ink_sem_wait(&wGlobals.serviceThrCount);
#endif
    ink_atomic_increment((int32 *) & numServiceThr, 1);

    // INKqa11624 - setup sockaddr struct for unix/tcp socket in different sizes
    if (acceptFD == cliFD) {
      clientInfo = (struct sockaddr_in *) xmalloc(sizeof(struct sockaddr_un));
      addrLen = sizeof(struct sockaddr_un);
    } else {
      // coverity[alloc_fn]
      clientInfo = (struct sockaddr_in *) xmalloc(sizeof(struct sockaddr_in));
      addrLen = sizeof(struct sockaddr_in);
    }

    // coverity[noescape]
    if ((clientFD = mgmt_accept(acceptFD, (sockaddr *) clientInfo, &addrLen)) < 0) {
      mgmt_log(stderr, "[WebIntrMain]: %s%s\n", "Accept on incoming connection failed: ", strerror(errno));
#if defined(darwin)
      ink_sem_post(wGlobals.serviceThrCount);
#else
      ink_sem_post(&wGlobals.serviceThrCount);
#endif
      ink_atomic_increment((int32 *) & numServiceThr, -1);
    } else {                    // Accept succeeded

      if (serviceThr == HTTP_THR) {
        if (fcntl(clientFD, F_SETFD, FD_CLOEXEC) < 0) {
          mgmt_elog(stderr, "[WebIntrMain] Unable to set close on exec flag\n");
        }
      }
      // Set TCP_NODELAY if are using a TCP/IP socket
      //    setting no delay reduces the latency for servicing
      //    request on Solaris
      // For 3com, it is not TCP socket, and so cannot do this for 3com.

      if (serviceThr != CLI_THR) {      // service thread for command line utility
        if (safe_setsockopt(clientFD, IPPROTO_TCP, TCP_NODELAY, ON, sizeof(int)) < 0) {
          mgmt_log(stderr, "[WebIntrMain]Failed to set sock options: %s\n", strerror(errno));
        }
      }

      // Accept OK
      ink_mutex_acquire(&wGlobals.serviceThrLock);

#ifndef NO_WEBUI
      // Check to see if there are any unprocessed config changes
      if (webConfigChanged > 0) {
        updateWebConfig();
      }
#endif /* NO_WEBUI */

      // If this a web manager or an overseer connection, make sure that
      //   it is from an allowed ip addr
      if (((serviceThr == HTTP_THR || serviceThr == OVERSEER_THR) &&
           mgmt_allow_table->match(clientInfo->sin_addr.s_addr) == false)
          // Fix for INKqa10514
          || (serviceThr == AUTOCONF_THR && autoconf_localhost_only != 0 &&
              strcmp(inet_ntoa(clientInfo->sin_addr), "127.0.0.1") != 0)
#if defined(OEM_INTEL) || defined(OEM_SUN)
          // Fix INKqa08148: on OEM boxes, we need to allow localhost to
          //   connect so that CLI will function correctly
          && (strcmp(inet_ntoa(clientInfo->sin_addr), "127.0.0.1"))
#endif
        ) {
        mgmt_log("WARNING: connect by disallowed client %s, closing\n", inet_ntoa(clientInfo->sin_addr));
#if defined(darwin)
        ink_sem_post(wGlobals.serviceThrCount);
#else
        ink_sem_post(&wGlobals.serviceThrCount);
#endif
        ink_atomic_increment((int32 *) & numServiceThr, -1);
        xfree(clientInfo);
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
              mgmt_elog(stderr, "[WebIntrMain] Failed to create service thread\n");
              wGlobals.serviceThrArray[i].threadId = 0;
              wGlobals.serviceThrArray[i].fd = -1;
              close_socket(clientFD);
#if defined(darwin)
              ink_sem_post(wGlobals.serviceThrCount);
#else
              ink_sem_post(&wGlobals.serviceThrCount);
#endif
              ink_atomic_increment((int32 *) & numServiceThr, -1);
            }

            break;
          } else if (i == MAX_SERVICE_THREADS - 1) {
            mgmt_fatal(stderr, "[WebIntrMain] Syncronizaion Failure\n");
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
  ink_thread ourId;
  WebHttpConInfo httpInfo;

  // dg: Added init to get rid of warning, ok since HTTP_THR must be #t
  WebContext *secureCTX = NULL;

  lmgmt->syslogThrInit();

  // Find out what our Id is.  We need to wait
  //   for our spawning thread to update the
  //   thread info structure
  ink_mutex_acquire(&wGlobals.serviceThrLock);
  ourId = threadInfo->threadId;

  // While we have the lock, make a copy of
  //   the web context if are on the secure admin port
  if (threadInfo->type == HTTP_THR) {
    secureCTX = (WebContext *) xmalloc(sizeof(WebContext));
    memcpy(secureCTX, &adminContext, sizeof(WebContext));
  }
  ink_mutex_release(&wGlobals.serviceThrLock);


  // Do our work
  switch (threadInfo->type) {
  case NO_THR:                 // dg: added to handle init value
    ink_assert(false);
    break;
  case HTTP_THR:
    httpInfo.fd = threadInfo->fd;
    httpInfo.context = secureCTX;
    httpInfo.clientInfo = threadInfo->clientInfo;
    WebHttpHandleConnection(&httpInfo);
    xfree(secureCTX);
    break;
  case CLI_THR:                // service command line utility
    handleCLI(threadInfo->fd, &adminContext);
    break;
  case OVERSEER_THR:
    handleOverseer(threadInfo->fd, overseerMode);
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

  xfree(threadInfo->clientInfo);

  // Mark ourselves ready to be reaped
  ink_mutex_acquire(&wGlobals.serviceThrLock);

  ink_assert(ourId == threadInfo->threadId);

  threadInfo->waitingForJoin = true;
  threadInfo->fd = -1;

  ink_mutex_release(&wGlobals.serviceThrLock);

  // Call exit so that we properly release system resources
  ink_thread_exit(NULL);
  return NULL;                  // No Warning
}
