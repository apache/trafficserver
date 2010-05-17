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

/***************************************/

/*******************************************************************
 * mcast_snoop - a small utility to dump traffic manager
 *     multicast packets
 *
 *
 *
 ******************************************************************/

#include "inktomi++.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "I_Layout.h"
#include "I_Version.h"
#include "Tokenizer.h"


char version_str[] = "2.0";
int packet_count = 0;

char mcast_group[256] = "224.0.1.37";
int mcast_port = 8089;
char packet_types[256] = "";
int ignore_dups = 0;
int print_packets = 0;
char output_file_name[256] = "";
FILE *output = stdout;
int version = 0;
int max_packets = -1;

InkHashTable *allow_packet_types = NULL;
InkHashTable *last_packet_hash = NULL;

ArgumentDescription argument_descriptions[] = {
  {"mcast_group", 'g', "MulticastGroup", "S255", &mcast_group, NULL, NULL},
  {"mcast_port", 'p', "Multicast Port", "I", &mcast_port, NULL, NULL},
  {"packet_types", 't', "Packet Types", "S255", &packet_types, NULL, NULL},
  {"ignore_dups", 'i', "Ignore duplicate packets", "F", &ignore_dups, NULL, NULL},
  {"print_packets", 'P', "Print packets", "F", &print_packets, NULL, NULL},
  {"output_file", 'O', "Output File", "S255", &output_file_name, NULL, NULL},
  {"max_packets", 'X', "Max Packets", "I", &max_packets, NULL, NULL},
  {"version", 'V', "Version", "F", &version, NULL, NULL},
};

int n_argument_descriptions = SIZE(argument_descriptions);

void
init()
{
  Tokenizer commaTok(", \t\n\r");
  int num_types;

  if (version) {
    fprintf(stderr, "mcast_snoop %s\n", version_str);
    exit(0);
  }

  if (packet_types[0] != '\0') {
    allow_packet_types = ink_hash_table_create(InkHashTableKeyType_String);
    num_types = commaTok.Initialize(packet_types);

    for (int i = 0; i < num_types; i++) {
      ink_hash_table_insert(allow_packet_types, (char *) commaTok[i], (void *) 1);
    }
  }

  if (ignore_dups) {
    last_packet_hash = ink_hash_table_create(InkHashTableKeyType_String);
  }

  if (output_file_name[0] != '\0') {
    FILE *out_file = fopen(output_file_name, "a+");
    if (out_file) {
      output = out_file;
    } else {
      perror("Warning: Unable to use output file");
      exit(1);
    }
  }
}


int
establishReceiveChannel(char *mc_group, int mc_port)
{
  int one = 1;
  int receive_fd = -1;
  struct sockaddr_in receive_addr;
  struct ip_mreq mc_request;

  if ((receive_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("Unable to open socket");
    exit(1);
  }

  if (setsockopt(receive_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(int)) < 0) {
    perror("Unable to set socket to reuse addr");
    exit(1);
  }

  memset(&receive_addr, 0, sizeof(receive_addr));
  receive_addr.sin_family = AF_INET;
  receive_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  receive_addr.sin_port = htons(mc_port);

  if (bind(receive_fd, (struct sockaddr *) &receive_addr, sizeof(receive_addr)) < 0) {
    perror("Unable to bind to socket");
    exit(1);
  }

  /* Add ourselves to the group */
  mc_request.imr_multiaddr.s_addr = inet_addr(mc_group);
  mc_request.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(receive_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *) &mc_request, sizeof(mc_request)) < 0) {
    perror("Can't add ourselves to multicast group");
    exit(1);
  }

  return receive_fd;
}

int
extractPacketField(const char *packet, const char *field, char *buf, int size)
{
  const char *cur = NULL;
  const char *end = NULL;
  int len = 0;

  if ((cur = strstr(packet, field))) {
    // Skip over the field label
    cur = cur + strlen(field);
    if ((end = strchr(cur, '\n'))) {
      len = end - cur;

      if (len < size) {
        strncpy(buf, cur, len);
        buf[len] = '\0';
        return 0;
      }
    }
  }

  return 1;
}

int
isPacketADup(char *ip_addr, char *mgmt_type, char *packet)
{
  char *last_packet;
  char *new_packet_copy;
  char key[256];

  snprintf(key, sizeof(key), "%s:%s", ip_addr, mgmt_type);

  if (ink_hash_table_lookup(last_packet_hash, key, (void **) (&last_packet))) {
    if (strcmp(last_packet, packet) == 0) {
      // Packet is a dup
      return 1;
    } else {
      free(last_packet);
    }
  }

  new_packet_copy = strdup(packet);
  ink_hash_table_insert(last_packet_hash, key, new_packet_copy);

  return 0;
}


void
handlePacket(char *packet, int size)
{
  ink_assert(size >= 0);
  char ip_addr[17] = "0.0.0.0";
  char mgmt_type[20] = "unknown";

  struct timeval packet_time;
  char timestamp_buf[48] = "";
  char *ctime_buf;

  // Grab a timestamp and format it for later printing
  gettimeofday(&packet_time, NULL);
  time_t cur_clock = (time_t) packet_time.tv_sec;
  ctime_buf = ctime_r(&cur_clock, timestamp_buf);
  if (ctime_buf != NULL) {
    snprintf(&(timestamp_buf[19]), sizeof(timestamp_buf) - 19, ".%03ld", (long int)(packet_time.tv_usec / 1000));
  } else {
    fprintf(stderr, "Warning: Unable to make timestamp");
  }


  if (extractPacketField(packet, "ip: ", ip_addr, 17)) {
    fprintf(stderr, "Warning: Unable to read ip address from packet");
  }

  if (extractPacketField(packet, "type: ", mgmt_type, 20)) {
    fprintf(stderr, "Warning: Unable to read type from packet");
  }

  if (allow_packet_types) {
    void *hash_entry;
    if (!ink_hash_table_lookup(allow_packet_types, mgmt_type, &hash_entry)) {
      // Packet type we don't care about, ignore
      return;
    }
  }
  // Check to see if this is same as the last packet
  //  of this type we got from this node
  if (last_packet_hash && isPacketADup(ip_addr, mgmt_type, packet)) {
    // Ignore it
    return;
  }

  if (print_packets) {
    fprintf(output, "----------- %s ------------\n", timestamp_buf);
    fprintf(output, "%s\n", packet);
    fprintf(output, "------------------------------------------------\n");
  } else {
    fprintf(output, "%s: %s packet received from %s\n", timestamp_buf, mgmt_type, ip_addr);
  }
  fflush(output);

  // Check to see if we are done
  packet_count++;
  if (max_packets >= 0 && packet_count >= max_packets) {
    exit(0);
  }
}

void
snoopPackets(int fd)
{
  const int buf_size = 61440;
  char buf[buf_size + 1];
  int nbytes;
  socklen_t addr_len = 0;
  struct sockaddr_in receive_addr;


  while (1) {
    memset(buf, 0, buf_size + 1);

    memset(&receive_addr, 0, sizeof(sockaddr_in));

    if ((nbytes = recvfrom(fd, buf, buf_size, 0, (struct sockaddr *) &receive_addr,
                           &addr_len)) < 0) {
      perror("Receive failed");
    } else {
      buf[nbytes] = '\0';
      handlePacket(buf, nbytes);
    }
  }
}

int
main(int argc, char **argv)
{

  // Process command line arguments and dump into variables
  process_args(argument_descriptions, n_argument_descriptions, argv);
  // Before accessing file system initialize Layout engine
  create_default_layout();
  // TODO: Figure out why is this needed
  if (argc < 0) {
    ink_ftell(stdout);
  }

  init();

  int fd = establishReceiveChannel(mcast_group, mcast_port);

  if (fd < 0) {
    fprintf(stderr, "Failed to setup multicast channel\n");
    exit(1);
  }

  snoopPackets(fd);
}
