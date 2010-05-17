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

#include "ink_config.h"
#include "StatSystemV2.h"
#include "P_EventSystem.h"
#include "Log.h"
#include <iostream>

extern struct EventProcessor eventProcessor;

std::map<std::string, uint32_t> StatSystemV2::stat_name_to_num;
std::vector< std::pair<std::string, INK64> > StatSystemV2::global_stats;
uint32_t StatSystemV2::MAX_STATS_ALLOWED = 500000;
uint32_t StatSystemV2::NUM_STATS_ESTIMATE = 5000;
static INKMutex statsMutex = NULL;

void StatSystemV2::incrementGlobal(uint32_t stat_num, INK64 stat_val)
{
    if(stat_num >= global_stats.size()) {
        Debug("http", "Cannot incrementing stat %u as it is greater than global_stats size", stat_num);
        return;
    }
    Debug("http", "Incrementing stat %u %s %ld", stat_num, global_stats[stat_num].first.c_str(), stat_val);
    global_stats[stat_num].second += stat_val;
}

bool StatSystemV2::increment(uint32_t stat_num, INK64 stat_val)
{
    if(stat_num >= MAX_STATS_ALLOWED) {
        return false;
    }
   
    EThread *t = this_ethread(); 
    // stat_num starts at 0
    if(t->thread_stats.size() < (unsigned int)stat_num+1) {
        if (INKMutexLock(t->thread_stats_mutex) != INK_SUCCESS) {
            return false;
        }
        t->thread_stats.resize(stat_num+1, 0);
        INKMutexUnlock(t->thread_stats_mutex);
    }
    t->thread_stats[stat_num] += stat_val;
    return true;
}

bool StatSystemV2::increment(const char *stat_name, INK64 stat_val)
{
    uint32_t stat_num;
    if(!getStatNum(stat_name, stat_num)) {
        return false;
    }
    return increment(stat_num, stat_val);
}

bool StatSystemV2::get(uint32_t stat_num, INK64 *stat_val)
{
    // Get stat lock
    if (INKMutexLock(statsMutex) != INK_SUCCESS) {
        return false;
    }

    if(stat_num >= global_stats.size()) {
        INKMutexUnlock(statsMutex);
        return false;
    }
    
    *stat_val = global_stats[stat_num].second;
    INKMutexUnlock(statsMutex);
    
    return true;
}

bool StatSystemV2::get(const char *stat_name, INK64 *stat_val)
{
    // Get value of stat with name == stat_name
    // Returns value from the global stats map. does not walk threads 
    uint32_t stat_num;
    if(!getStatNum(stat_name, stat_num)) {
        return false;
    }
    return get(stat_num, stat_val);
}

bool StatSystemV2::get_current(uint32_t stat_num, INK64 *stat_val)
{
    // Returns current value of stat. Walks all threads
    
    *stat_val = 0;
    // Collect stat from all threads
    for(int i =0; i < eventProcessor.n_ethreads; i++) {
        EThread *t = eventProcessor.all_ethreads[i];
        if (INKMutexLock(t->thread_stats_mutex) != INK_SUCCESS) {
            return false;
        }

        if(t->thread_stats.size() > stat_num) { 
            *stat_val += t->thread_stats[stat_num];
        }
        INKMutexUnlock(t->thread_stats_mutex);
    }
    return true;
}

bool StatSystemV2::get_current(const char *stat_name, INK64 *stat_val)
{
    uint32_t stat_num;
    if(!getStatNum(stat_name, stat_num)) {
        return false;
    }
    return get_current(stat_num, stat_val);
}

bool StatSystemV2::registerStat(const char *stat_name, uint32_t *stat_num)
{
    if(!stat_num ) {
        return false;
    }
    
    // Get stat lock
    if (INKMutexLock(statsMutex) != INK_SUCCESS) {
        *stat_num = MAX_STATS_ALLOWED;
        return false;
    }

    // Check if stat is already registered
    std::map<std::string, uint32_t>::const_iterator stat_name_it = stat_name_to_num.find(stat_name);
    if(stat_name_it != stat_name_to_num.end()) {
        *stat_num = stat_name_it->second;
        INKMutexUnlock(statsMutex);
        return true;
    }

    // Check to see if limit for max allowed stats was hit
    if(global_stats.size() == MAX_STATS_ALLOWED) {
        INKMutexUnlock(statsMutex);
        *stat_num = MAX_STATS_ALLOWED;
        return false;
    }

    global_stats.push_back(std::make_pair(stat_name, 0));
    *stat_num = global_stats.size() - 1;
    stat_name_to_num[stat_name] = *stat_num;
    Debug("http", "Registered stat : %s %u", global_stats[*stat_num].first.c_str(), *stat_num); 
    INKMutexUnlock(statsMutex);
    return true;
}

void StatSystemV2::setMaxStatsAllowed(uint32_t max_stats_allowed)
{
    MAX_STATS_ALLOWED = max_stats_allowed;
}

void StatSystemV2::setNumStatsEstimate(uint32_t num_stats_estimate)
{
    if(num_stats_estimate < MAX_STATS_ALLOWED)
        NUM_STATS_ESTIMATE = num_stats_estimate;
    else
        NUM_STATS_ESTIMATE = MAX_STATS_ALLOWED;
}

void StatSystemV2::init()
{
  if (statsMutex == NULL)
    statsMutex = INKMutexCreate();

    if (INKMutexLock(statsMutex) != INK_SUCCESS) {
        return;
    }

    // Resize thread_stats vector in each thread to NUM_STATS_ESTIMATE
    for(int i =0; i < eventProcessor.n_ethreads; i++) {
        EThread *t = eventProcessor.all_ethreads[i];
        INKMutexLock(t->thread_stats_mutex);
        t->thread_stats.resize(NUM_STATS_ESTIMATE);
        INKMutexUnlock(t->thread_stats_mutex);
    }
    
    INKMutexUnlock(statsMutex);    
}

void StatSystemV2::clear()
{
    for(std::vector< std::pair<std::string, INK64> >::iterator it = StatSystemV2::global_stats.begin();
            it != StatSystemV2::global_stats.end(); it++) {
        it->second = 0;
    }
}

void StatSystemV2::collect()
{
    if (INKMutexLock(statsMutex) != INK_SUCCESS) {
        return;
    }

    StatSystemV2::clear();
    for(int i =0; i < eventProcessor.n_ethreads; i++) {
        EThread *t = eventProcessor.all_ethreads[i];

        // Lock thread stats to prevent resizing on increment
        INKMutexLock(t->thread_stats_mutex);
        int i = 0;
        for(std::vector<INK64>::iterator it = t->thread_stats.begin();
            it != t->thread_stats.end(); it++, i++) {
            if(*it != 0) {
                incrementGlobal(i, *it);
            }
        }
        
        // Release thread stats
        INKMutexUnlock(t->thread_stats_mutex);
    }
    INKMutexUnlock(statsMutex);
}

bool StatSystemV2::getStatNum(const char *stat_name, uint32_t &stat_num)
{
    // Get stat lock
    if (INKMutexLock(statsMutex) != INK_SUCCESS) {
        return false;
    }

    // Get stat num and release lock
    std::map<std::string, uint32_t>::const_iterator stat_name_it = stat_name_to_num.find(stat_name);
    if(stat_name_it == stat_name_to_num.end()) {
        INKMutexUnlock(statsMutex);
        return false;
    }

    stat_num = stat_name_it->second;
    INKMutexUnlock(statsMutex);
    return true;
}

static INKThread statsCommandThread;
static int MAX_STAT_NAME_LENGTH = 512;
int StatCollectorContinuation::mainEvent(int event, Event * e)
{
    StatSystemV2::collect();
    return EVENT_CONT;
}

int StatCollectorContinuation::doWrite(int fd, const char* buf, size_t towrite) {
    int written = -1;
    while(fd && buf && towrite > 0) {
        if ((written = write(fd, buf, towrite)) < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                written = 0;
            } else {
                Debug("http", "Failed write on stats connection");
                return -1;
            }
        } else if (written == 0) { //closed
            return -1;
        }
        towrite -= written;
        buf += written;
    }
    
    return 0;
}

//------------------------------------------------------------------------------
void StatCollectorContinuation::print_stats(std::stringstream &printbuf) {
   printbuf <<  "Cache stats: \n"
                "-----------------------------------------------------------------------------\n";

  printbuf << "TIME " << _startTime <<"\n";
  if (INKMutexLock(statsMutex) == INK_SUCCESS) {
      for(std::vector< std::pair<std::string, INK64> >::const_iterator it = StatSystemV2::global_stats.begin();
          it != StatSystemV2::global_stats.end(); it++) {
          if(it->second != 0 ) {
              printbuf << "STAT " << it->first << " " << it->second << "\n";
          }
      }
      INKMutexUnlock(statsMutex);
  }
  printbuf << "END\n";
}

void StatCollectorContinuation::print_stat(const char *stat_name, std::stringstream &printbuf, bool current) {
    // Print only non zero stats
    INK64 stat_val = 0;
    bool stat_get_status;
    if(current) {
        stat_get_status = StatSystemV2::get_current(stat_name, &stat_val);
    }
    else {
        stat_get_status = StatSystemV2::get(stat_name, &stat_val);
    }
    
    if(stat_get_status && stat_val != 0) {
        printbuf << "STAT " << stat_name << " " << stat_val << "\n";
    }
}

void StatCollectorContinuation::print_stats(const std::vector<std::string> &stat_names, std::stringstream &printbuf, bool current) {
  printbuf << "TIME " << _startTime <<"\n";
    for(std::vector<std::string>::const_iterator it = stat_names.begin();
        it != stat_names.end();
        it++) {
        print_stat(it->c_str(), printbuf, current);
    }
   printbuf << "END\n";
}

void
StatCollectorContinuation::get_stats_with_prefix(const std::string &stat_prefix, std::vector<std::string> &stat_names)
{
    if (INKMutexLock(statsMutex) != INK_SUCCESS) {
        return;
    }

    // Get all stats which start with stat_prefix
    for(std::vector< std::pair<std::string, INK64> >::const_iterator it = StatSystemV2::global_stats.begin();
        it != StatSystemV2::global_stats.end(); it++) {
        size_t found = it->first.find(stat_prefix);
        if(found == 0) {
            stat_names.push_back(it->first);
        }
    }    
    INKMutexUnlock(statsMutex);
}

int
StatCollectorContinuation::getCommand(int fd, char *buf, int buf_size)
{
    int n, rc;
    char c = '\0';
    double time_left = _readTimeout*1000 + _readTimeoutUSecs/1000;
    
    for (n = 1; n < buf_size && time_left > 0; n++) {
        struct timeval start, stop;
        gettimeofday(&start, NULL);
        if ((rc = read(fd, &c, 1)) == 1) {
            *buf++ = c;
            if (c == '\n') {
                break;
            }
        } else if (rc == 0) {
            if (n == 1)
                return 0; // EOF, no data read
            else
                break; // EOF, read some data
        } else if(rc < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                Debug("http", "Failed read on stats connection");
                return -1;
            }
        }
        
        gettimeofday(&stop, NULL);
        double time_elapsed = (stop.tv_sec - start.tv_sec)*1000 + (stop.tv_usec - start.tv_usec)/1000;
        time_left = time_left - time_elapsed;
    }

    if(time_left <= 0) {
    // Timeout. Client took too long to send a command. Close client connection
        return -1;
    }
    
    *buf = '\0'; // null-terminate
    return n;
}

//------------------------------------------------------------------------------
// Handles a command port client connection
void* StatCollectorContinuation::commandLoop(void *data) {
    // static const char cmdPrompt[] = "STATS : ";
    static const char cmdUnrec[] = "Unrecognized command.\r\n";
    static const char cmdHelp[] = "Valid commands are: \r\n"
        "  stats - Print stats which have been collected.\r\n"
        "  stats_current - Print stats after forcing a collect\r\n"
        "  stat (<stat_name> )* - Print values for stats that are specified. Does not collect\r\n"
        "  stat_current (<stat_name> )* - Print values for stats that are specified after collecting from all threads\r\n"
        "  help - Prints this message.\r\n"
        "  quit - Close this connection.\r\n"
        ;
    int client_sock, readbytes;
    char readbuf[1024];
    
    if (!data) return 0;
    client_sock = *(static_cast<int*>(data));
    while(1){
        if((readbytes = getCommand(client_sock, readbuf, sizeof(readbuf))) <= 0) {
            break;
        }

        if(strstr(readbuf, "stats_current") == readbuf) {
            // Force a collect before printing out the stats
            StatSystemV2::collect();
            std::stringstream printbuf;
            print_stats(printbuf);
            if (doWrite(client_sock, printbuf.str().c_str(), printbuf.str().length())) {
                //failed write, break to close connection
                break;
            }
        }
        else if (strstr(readbuf, "stats") == readbuf) {
            std::stringstream printbuf;
            print_stats(printbuf);
            if (doWrite(client_sock, printbuf.str().c_str(), printbuf.str().length())) {
                    //failed write, break to close connection
                break;
            }
        }
        else if (strstr(readbuf, "stat ") == readbuf || strstr(readbuf, "stat_current ") == readbuf) {
            std::vector<std::string> stats;
            char stat_name[MAX_STAT_NAME_LENGTH];
            bzero(stat_name, MAX_STAT_NAME_LENGTH);
            int next;
            char *start = readbuf;

            // determine if collection has to be forced or not
            bool get_current = false;
            if(strstr(readbuf, "stat ") == readbuf)
                start += strlen("stat ");
            else {
                start += strlen("stat_current ");
                get_current = true;
            }
            
            while(sscanf(start, "%s%n", stat_name, &next) == 1)
            {
                // Prefix support
                char *prefix_end = strchr(start, '*');
                if(prefix_end != NULL) {
                    std::string prefix;
                    prefix.assign(start, prefix_end-start);
                    // Get all stats with the prefix
                    get_stats_with_prefix(prefix, stats);
                }
                else {
                    stats.push_back(stat_name);
                }
                bzero(stat_name, MAX_STAT_NAME_LENGTH);
                start+=next;
            }
            std::stringstream printbuf;
            
            print_stats(stats, printbuf, get_current);
            if (doWrite(client_sock, printbuf.str().c_str(), printbuf.str().length())) {
                //failed write, break to close connection
                break;
            }
        }
        else if (strstr(readbuf, "help") == readbuf) {
            if (doWrite(client_sock, cmdHelp, sizeof(cmdHelp)-1)) {
                //failed write, break to close connection
                break;
            }
        } else if (strstr(readbuf, "quit") == readbuf) {
            break;
        } else {
            if (doWrite(client_sock, cmdUnrec, sizeof(cmdUnrec)-1) ||
                doWrite(client_sock, cmdHelp, sizeof(cmdHelp)-1)) {
                //failed write, break to close connection
                break;
            }
        }
    } // END while loop
    
    if (shutdown(client_sock, SHUT_RDWR) < 0) {
        Debug("http", "Failed shutdown on stats connection");
    }
    
    if (close(client_sock) < 0) {
        Debug("http", "Failed close on stats connection");
    }
    
    return 0;
}
    
//------------------------------------------------------------------------------
// Creates a socket for command port and listens for connection requests
void* StatCollectorContinuation::commandListen(void *data) {
    // This is single-threaded and using blocking sockets for now,
    // so there can be only one client at a time...
    // non-blocking socket with poll/epoll can be added later if needed
    
    int listen_sock, client_sock;
    struct  sockaddr_in listen_addr;
    int reuseaddr = 1;
    int port;
    
    if (!data) return 0;
    port = *(static_cast<int*>(data));
    
    if (port < 1 || port > 65535) return 0;
    
    if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        Debug("Could not create listening socket for stats : %d %s", errno, strerror(errno));
        return 0;
    }
    
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
    
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_port = htons(port);
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(listen_sock, (struct sockaddr *) &listen_addr, sizeof(listen_addr)) < 0) {
        Debug("http", "Could not bind stat socket : %d %s", errno, strerror(errno));
        return 0;
    }
    
    if (listen(listen_sock, 8) < 0) {
        Debug("http", "Failed to listen on stats port : %d %s", errno, strerror(errno));
        return 0;
    }
    
    while(1) {
        
        if ((client_sock = accept(listen_sock, NULL, NULL)) < 0 ) {
            Debug("http", "Failed to accept on stats port : %d %s", errno, strerror(errno));
            if (errno == ECONNABORTED || errno == EPROTO)
                continue;
            else //something unexpected, bail
                return 0;
        }
        
        INKThreadCreate(commandLoop, &client_sock);
    }
}

int StatCollectorContinuation::_statCommandPort = 8091;
time_t StatCollectorContinuation::_startTime = time(NULL);
int StatCollectorContinuation::_readTimeout = 600;
long StatCollectorContinuation::_readTimeoutUSecs = 0;

void StatCollectorContinuation::setStatCommandPort(int port)
{
    _statCommandPort = port;
}

void StatCollectorContinuation::setReadTimeout(int secs, long usecs)
{
    _readTimeout = secs;
    _readTimeoutUSecs = usecs;
}

StatCollectorContinuation::StatCollectorContinuation() : Continuation(NULL)
{
    Debug("http", "YTS start time : %b64d", StatCollectorContinuation::_startTime);
    SET_HANDLER(&StatCollectorContinuation::mainEvent);
    statsCommandThread = INKThreadCreate(commandListen, &_statCommandPort);
}
