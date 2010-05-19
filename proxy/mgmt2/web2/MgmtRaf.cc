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

   MgmtRaf.cc

   Description:


 ****************************************************************************/

#include "rafencode.h"
#include "ink_sock.h"
#include "TextBuffer.h"
#include "MgmtUtils.h"
#include "LocalManager.h"
#include "MgmtRaf.h"
#include "Tokenizer.h"

// Alarm buffer for ts-plugin in smonitor (CNP 2.0)
Queue<RafAlarm> rafAlarmList;
static size_t rafAlarmListLen = 0;
const size_t MAX_ALARM_BUFFER = 10;

void
freeRafCmdStrs(char **argv, int argc)
{
  for (int i = 0; i < argc; i++) {
    xfree(argv[i]);
    argv[i] = NULL;
  }
}

typedef int (*RafCmdHandler) (textBuffer * resp, char **argv, int argc);

void
RafOutputHeader(textBuffer * resp, const char *id, const char *result_code)
{
  resp->copyFrom(id, strlen(id));
  resp->copyFrom(" ", 1);
  resp->copyFrom(result_code, strlen(result_code));
  resp->copyFrom(" ", 1);
}

void
RafOutputArg(textBuffer * resp, const char *arg, bool last = true)
{

  int arg_len = strlen(arg);
  int enc_len = raf_encodelen(arg, arg_len, 0);
  char *earg = (char *) xmalloc(enc_len);
  enc_len = raf_encode(arg, arg_len, earg, enc_len, 0);
  resp->copyFrom(earg, enc_len);

  // append a space if we haven't finish the output
  if (!last) {
    resp->copyFrom("\\ ", 2);
  }

  xfree(earg);
}

int
build_and_send_raf_error(int fd, textBuffer * resp, const char *id, const char *msg)
{

  RafOutputHeader(resp, id, "1");
  RafOutputArg(resp, msg);

  int resp_len = resp->spaceUsed();

  if (mgmt_writeline(fd, resp->bufPtr(), resp_len) != 0) {
    return -1;
  } else {
    return 1;
  }
}

// void RafProcessQueryStat(textBuffer* resp, const char* id, const char* var) {
//
//    This code largely copied from proxy/Raf.cc.  I'm only
//      dupping the code since it should be short lived.  The
//      query command should use a real InfoTree
//
void
RafProcessQueryStat(textBuffer * resp, const char *id, int argc, int start_index, char **argv)
{

  const char stats[] = "/stats/";
  char *var = NULL;
  char val_output[257];
  bool r = true;

  const size_t max_resp_len = 16384;
  char temp_resp[max_resp_len];
  memset(temp_resp, 0, max_resp_len);
  int i;

  // InfoTree -> variable list
  char wildcard[3];
  memset(wildcard, 0, 3);
  snprintf(wildcard, sizeof(wildcard), "%c%c", REC_VAR_NAME_DELIMITOR, REC_VAR_NAME_WILDCARD);
  if (strstr(argv[start_index], wildcard)) {
    i = start_index;
    var = argv[start_index] + ((strncmp(argv[start_index], stats, sizeof(stats) - 1) == 0) ? sizeof(stats) - 1 : 0);

    int count = 0;
    char **variable_list = NULL;
    RecGetRecordList(var, &variable_list, &count);

    // adjust parameters
    argc = count;
    argv = variable_list;
    start_index = 0;
    r = (argc != 0 && argv != NULL);
  }

  for (i = start_index; i < argc && r; i++) {

    var = argv[i] + ((strncmp(argv[i], stats, sizeof(stats) - 1) == 0) ? sizeof(stats) - 1 : 0);
    Debug("raf", "%d \"%s\"\n", i, var);

    RecDataT val_type = RECD_NULL;
    r = (RecGetRecordDataType(var, &val_type) == REC_ERR_OKAY);

    if (r) {
      switch (val_type) {
      case RECD_INT:
      case RECD_COUNTER:
        {
          RecInt i = 0;

          if (val_type == RECD_COUNTER) {
            RecGetRecordCounter(var, &i);
          } else {
            RecGetRecordInt(var, &i);
          }
          snprintf(val_output, 256, "%lld", i);
          break;
        }
      case RECD_LLONG:
        {
          RecLLong ll = 0;
          RecGetRecordLLong(var, &ll);
          snprintf(val_output, 256, "%lld", ll);
          break;
        }
      case RECD_FLOAT:
        {
          RecFloat f = 0;
          RecGetRecordFloat(var, &f);
          snprintf(val_output, 256, "%f", f);
          break;
        }
      case RECD_STRING:
        {
          char *s = NULL;
          RecGetRecordString_Xmalloc(var, &s);
          snprintf(val_output, 256, "\"%s\"", s);
          val_output[256] = '\0';
          xfree(s);
          break;
        }
      default:
        r = false;
        break;
      }
    }

    if (r) {
      // buffer overflow?
      if ((strlen(temp_resp) + strlen(var) + 1 + strlen(val_output)) > (max_resp_len - 1)) {
        char msg[257];
        snprintf(msg, 256, "response length exceed %d bytes", max_resp_len);
        msg[256] = '\0';
        RafOutputHeader(resp, id, "1");
        RafOutputArg(resp, msg);
        Debug("raf", "%s", msg);
        return;
      } else {
        if (i > start_index) {
          strncat(temp_resp, " ", max_resp_len - strlen(temp_resp) - 1);
        }
        strncat(temp_resp, var, max_resp_len - strlen(temp_resp) - 1);
        strncat(temp_resp, " ", max_resp_len - strlen(temp_resp) - 1);
        strncat(temp_resp, val_output, max_resp_len - strlen(temp_resp) - 1);
      }
    }

  }                             // foreach variable in list

  if (r) {
    RafOutputHeader(resp, id, "0");
    RafOutputArg(resp, temp_resp);
  } else {
    char msg[257];
    snprintf(msg, 256, "%s not found", var);
    msg[256] = '\0';
    RafOutputHeader(resp, id, "1");
    RafOutputArg(resp, msg);
  }
}


//
// RafProcessAlarmCmd()
//   raf request that consume buffer alarm. raf client should re-request
//   for alarms until "none" is returned.
//
int
RafProcessAlarmCmd(textBuffer * resp, char **argv, int argc)
{

  RafAlarm *alarm = NULL;

  char msg[1025];
  msg[1024] = '\0';

  alarm = rafAlarmList.dequeue();

  if (alarm != NULL) {
    RafOutputHeader(resp, argv[0], "0");
    snprintf(msg, 1024, "%d %s", alarm->type, alarm->desc);
    rafAlarmListLen--;
    RafOutputArg(resp, msg);
    xfree(alarm);
  } else {
    RafOutputHeader(resp, argv[0], "1");
    RafOutputArg(resp, "none");
  }

  return 1;
}


//
// RafProcessSignalAlarmCmd()
//   raf request that consume buffer alarm. raf client should re-request
//   for alarms until "none" is returned.
//
int
RafProcessSignalAlarmCmd(textBuffer * resp, char **argv, int argc)
{

  char msg[1025];
  msg[1024] = '\0';

  if (argc == 4) {

    int alarm_id = atoi(argv[2]);
    lmgmt->alarm_keeper->signalAlarm(alarm_id, argv[3]);

    RafOutputHeader(resp, argv[0], "0");
    snprintf(msg, sizeof(msg), "alarm %d signaled", alarm_id);
    RafOutputArg(resp, msg);

  } else {

    RafOutputHeader(resp, argv[0], "1");
    RafOutputArg(resp, "(signal_alarm) invalid number of argument.");

  }

  return 1;
}


// int RafProcessQueryCmd(textBuffer* resp, char** argv, int argc)
//
//    This code largely copied from proxy/Raf.cc.  I'm only
//      dupping the code since it should be short lived.  The
//      query command should use a real InfoTree
//
int
RafProcessQueryCmd(textBuffer * resp, char **argv, int argc)
{

  const char stats[] = "/stats/";
  // This doesn't seem to be used.
  //const char config[] = "/etc/trafficserver/";

  int qstring_index = 2;
  while (qstring_index < argc) {
    if ((argv[qstring_index])[0] == '-') {
      qstring_index++;
    } else {
      break;
    }
  }

  if (qstring_index >= argc) {
    char msg[] = "no arguments sent to query cmd";
    RafOutputHeader(resp, argv[0], "1");
    RafOutputArg(resp, msg);
    return 1;
  }

  if (strcmp(argv[qstring_index], "/*") == 0) {
    char msg[] = " /stats {} /etc/trafficserver {}";
    RafOutputHeader(resp, argv[0], "0");
    RafOutputArg(resp, msg);
  } else {
    if (strncmp(argv[qstring_index], stats, sizeof(stats) - 1) == 0) {
      RafProcessQueryStat(resp, argv[0], argc, qstring_index, argv);
    } else {
      char msg[257];
      snprintf(msg, 256, "Node %s not found", argv[qstring_index]);
      msg[256] = '\0';
      RafOutputHeader(resp, argv[0], "1");
      RafOutputArg(resp, msg);
    }
  }

  return 1;
}


int
RafProcessIsaliveCmd(textBuffer * resp, char **argv, int argc)
{
  RafOutputHeader(resp, argv[0], "0");
  RafOutputArg(resp, "alive");
  return 1;
}

int
RafProcessExitCmd(textBuffer * resp, char **argv, int argc)
{
  RafOutputHeader(resp, argv[0], "0");
  RafOutputArg(resp, "Bye!");
  return 0;
}

struct RafCmdEntry
{
  const char *name;
  RafCmdHandler handler;
};

const RafCmdEntry raf_cmd_table[] = {
  {"query", &RafProcessQueryCmd},
  {"alarm", &RafProcessAlarmCmd},
  {"signal_alarm", &RafProcessSignalAlarmCmd},
  {"isalive", &RafProcessIsaliveCmd},
  {"exit", &RafProcessExitCmd},
  {"quit", &RafProcessExitCmd}
};
int raf_cmd_entries = sizeof(raf_cmd_table) / sizeof(RafCmdEntry);

void
handleRaf(int fd)
{

  char inputBuf[8192];
  int cmd_len;

  int raf_cont = 1;
  char *argv[16];
  textBuffer resp(1024);

  while (raf_cont == 1 && (cmd_len = mgmt_readline(fd, inputBuf, 8192)) > 0) {

    const char *lastp = NULL;
    const char *curp = inputBuf;
    int argc = 0;

    memset(argv, 0, sizeof(argv));
    resp.reUse();

    // mgmt_readline cuts off \r\n but doesn't reduce the length
    //  for there removal so patch it here
    if (cmd_len > 2 && inputBuf[cmd_len - 2] == '\0') {
      cmd_len -= 2;
    } else if (cmd_len > 1 && inputBuf[cmd_len - 1] == '\0') {
      cmd_len -= 1;
    }

    if (cmd_len == 8192) {
      build_and_send_raf_error(fd, &resp, argv[0], "command too large - terminating connection");
      raf_cont = 0;
      continue;
    }

    for (int i = 0; i < 16; i++) {

      // Check to see if we've run out of buffer
      if (curp >= inputBuf + cmd_len) {
        break;
      }

      int arg_len = raf_decodelen(curp, cmd_len - (curp - inputBuf), &lastp);
      argc++;
      argv[i] = (char *) xmalloc(arg_len + 1);
      raf_decode(curp, cmd_len - (curp - inputBuf), argv[i], arg_len, &lastp);
      (argv[i])[arg_len] = '\0';
      curp = lastp;
    }

    if (argc < 2) {
      if (build_and_send_raf_error(fd, &resp, argv[0], "null command") < 0) {
        raf_cont = 0;
      }
      continue;
    }


    bool found = false;
    for (int j = 0; j < raf_cmd_entries; j++) {
      if (strcmp(argv[1], raf_cmd_table[j].name) == 0) {
        found = true;
        int r = (*raf_cmd_table[j].handler) (&resp, argv, argc);

        int resp_len = resp.spaceUsed();
        if (mgmt_writeline(fd, resp.bufPtr(), resp_len) != 0 || r == 0) {
          // Error occured or exist cmd
          raf_cont = 0;
        }
        break;
      }

    }

    if (!found && build_and_send_raf_error(fd, &resp, argv[0], "No such command") < 0) {
      raf_cont = 0;
    }

    freeRafCmdStrs(argv, 16);

  }

  close_socket(fd);
}


//
// mgmtRafAlarmCallback()
//   buffer TS/TM alarms and awaits for raf command to consume them
//
void
mgmtRafAlarmCallback(alarm_t type, char *ip, char *desc)
{

  RafAlarm *alarm = NULL;

  alarm = new RafAlarm;
  alarm->ip = ip;
  alarm->type = type;
  alarm->desc = desc;

  if (rafAlarmListLen <= MAX_ALARM_BUFFER) {
    rafAlarmListLen++;
    rafAlarmList.enqueue(alarm);
  }

}
