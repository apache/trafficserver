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

/****************************************************************************

  TestDns.cc

  Test module to test the DNS processor

  The main state machine is encapulated in the StateMachine instance.


 ****************************************************************************/

#include "DNS.h"
#include <iostream.h>
#include <fstream.h>
#include <string.h>
#include "VConnection.h"
#include "stdio.h"
#include "ts/ink_platform.h"
#define N_STATE_MACHINES 1000
#define MEASUREMENT_INTERVAL 100
class TestDnsStateMachine;

//////////////////////////////////////////////////////////////////////////////
//
//      Globals
//
//////////////////////////////////////////////////////////////////////////////

unsigned g_host_ip        = 0;
char *in_file_name        = "test_dns.in";
char *out_file_name       = "test_dns.out";
char *rate_file_name      = "rates.out";
char *rate_misc_file_name = "rates.misc.out";
ofstream fout;
ofstream fout_rate, fout_rate_misc;
FILE *fin;

//////////////////////////////////////////////////////////////////////////////
//
//      TestDnsStateMachine
//
//  An instance of TestDnsStateMachine is created for each host
//
//////////////////////////////////////////////////////////////////////////////
class TestDnsStateMachine : public Continuation
{
public:
  TestDnsStateMachine(char *ahost, size_t size);
  ~TestDnsStateMachine()
  {
    //        cout << "StateMachine::~StateMachine(). Terminating ... " << endl;
    return;
  }

  const char *currentStateName();

  int processEvent(int event, void *data);

  enum {
    START,
    DNS_LOOKUP,
  };

  int m_state;
  char host[100];
};

TestDnsStateMachine::TestDnsStateMachine(char *ahost, size_t size) : Continuation(new_ProxyMutex())
{
  ink_strlcpy(host, ahost, size);
  m_state = START;
  SET_HANDLER(processEvent);
  return;
}

inline const char *
TestDnsStateMachine::currentStateName()
{
  switch (m_state) {
  case START:
    return ("START");
  case DNS_LOOKUP:
    return ("DNS_LOOKUP");
  default:
    return ("unknown state");
  }
}

//////////////////////////////////////////////////////////////////////////////
//
//      TestDnsStateMachine::processEvent()
//
//      This routine is the main callback entry point of the TestTunnel
//      state machine.
//
//////////////////////////////////////////////////////////////////////////////

int
TestDnsStateMachine::processEvent(int event, void *data)
{
  int rvalue = VC_EVENT_DONE;
  void complete();

  //    printf("<<< state machine processEvent() called in state '%s' >>>\n",currentStateName());

  switch (m_state) {
  case START: {
    //        cout << "  started up TestDnsStateMachine" << endl;
    //        cout << "  dns lookup for <" << host << ">" <<  endl;

    //
    // asynchronously do DNS, calling <this> back when done
    //

    m_state = DNS_LOOKUP;
    dnsProcessor.gethostbyname(this, host);
    break;
  }
  case DNS_LOOKUP: {
    ink_assert(event == DNS_EVENT_LOOKUP);
    if (!host)
      ink_assert(!"Error - host has no value\n");
    if (data) {
      HostEnt *ent = (HostEnt *)data;
      g_host_ip    = ((struct in_addr *)ent->h_addr_list[0])->s_addr;
      //        cout << "  dns lookup is done <" << g_host_ip << ">" << endl;
      //          cout << "<" << host << "> <" << g_host_ip << ">\n";
      fout << "<" << host << "> <" << g_host_ip << ">\n";
      //        cout << "    finishing up" << endl;
      //        printf("*** NOTE: We Need To Somehow Free 'this' Here!  How?\n");
    } else {
      fout << "<" << host << "> <>\n";
    }
    fout.flush();
    rvalue  = VC_EVENT_DONE;
    m_state = 99; // Some Undefined state value
    complete();
    delete this;
    break;
  }
  default: {
    ink_assert(!"unexpected m_state");
    break;
  }
  }
  return (rvalue);
}

int state_machines_created, state_machines_finished, measurement_interval;
ink_hrtime start_time, last_measurement_time;
// Following function is called to measure the throughput
void
complete()
{
  float throughput, cumul_throughput;
  ink_hrtime now;
  state_machines_finished++;
  if (!(state_machines_finished % measurement_interval)) {
    now                   = Thread::get_hrtime();
    cumul_throughput      = state_machines_finished * 1.0 * HRTIME_SECOND / (now - start_time);
    throughput            = measurement_interval * 1.0 * HRTIME_SECOND / (now - last_measurement_time);
    last_measurement_time = now;
    //    cout << state_machines_finished << ": " <<
    //    "Cumul. Thrput " << cumul_throughput <<
    //    " per sec; Thrput for last " << measurement_interval << " requests: "
    //    << throughput << " per sec\n";
    //    cout.flush();
    //    fout_rate << state_machines_finished << ": " <<
    //    "Cumul. Thrput " << cumul_throughput <<
    //    " per sec; Thrput for last " << measurement_interval << " requests: "
    //    << throughput << " per sec\n";
    fout_rate << (now - start_time) * 1.0 / HRTIME_SECOND << " " << state_machines_finished << " " << cumul_throughput << " "
              << throughput << "\n";
    fout_rate.flush();
  }
  if (state_machines_finished == state_machines_created) {
    now = Thread::get_hrtime();
    fout_rate_misc << (now - start_time) * 1.0 / HRTIME_SECOND << "\n";
    fout_rate_misc.flush();
    fout.close();
    fout_rate.close();
    fout_rate_misc.close();
    cout << "Dns Testing Complete\n";
    exit(0);
  }
  //  printf("%d Hosts left \n",state_machines_unfinished);
}

//////////////////////////////////////////////////////////////////////////////
//
//      test
//
//  Main entry point for DNS tests
//
//////////////////////////////////////////////////////////////////////////////

void
test()
{
  char host[100];
  ink_hrtime now;
  int i;
  TestDnsStateMachine *test_dns_state_machine;
  printf("removing file '%s'\n", out_file_name);
  unlink(out_file_name);
  printf("removing file '%s'\n", rate_file_name);
  unlink(rate_file_name);
  printf("removing file '%s'\n", rate_misc_file_name);
  unlink(rate_misc_file_name);
  fin = fopen(in_file_name, "r"); // STDIO OK
  fout.open(out_file_name);
  fout_rate.open(rate_file_name);
  fout_rate_misc.open(rate_misc_file_name);
  i                       = 0;
  state_machines_created  = N_STATE_MACHINES;
  state_machines_finished = 0;
  measurement_interval    = MEASUREMENT_INTERVAL;
  start_time              = Thread::get_hrtime();
  last_measurement_time   = Thread::get_hrtime();
  while ((fscanf(fin, "%s", host) != EOF) && (i < state_machines_created)) {
    test_dns_state_machine = new TestDnsStateMachine(host, sizeof(host));
    test_dns_state_machine->handleEvent();
    i++;
  }
  now = Thread::get_hrtime();
  cout << "Finished creating all Continuations at " << (now - start_time) / HRTIME_SECOND << " sec and "
       << (now - start_time) % HRTIME_SECOND << "nanosec\n";
  fout_rate_misc << (now - start_time) * 1.0 / HRTIME_SECOND << "\n";
  fout_rate_misc.flush();
}
