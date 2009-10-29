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

/*************************** -*- Mod: C++ -*- *********************

  LoadGenerator.cc
******************************************************************/
#include "LoadGenerator.h"
void
LoadGenerator::initialize_stats()
{
  int i;
  hotset_generated = 0;
  random_generated = 0;
  generated_set = 0;
  generated_size = 0;
  for (i = 0; i < MAX_SIZES; i++) {
    size_generated[i] = 0;
  }
  generated_origin_servers = 0;
  for (i = 0; i < MAX_ORIGIN_SERVERS; i++) {
    origin_servers_generated[i] = 0;
  }
}


/* Take the hostname and port and construct a sockaddr_in* */
int
mksockaddr_in(const char *host, char *service, struct sockaddr_in *sin)
{
  memset(sin, 0, sizeof(struct sockaddr_in));

  sin->sin_family = AF_INET;
  if (isdigit(host[0]))
    sin->sin_addr.s_addr = inet_addr(host);
  else {
    struct hostent *hp;

    if ((hp = gethostbyname(host)) == NULL)
      return -1;

    memcpy(&sin->sin_addr, hp->h_addr_list[0], hp->h_length);
  }

  if (isdigit(service[0]))
    sin->sin_port = htons(atoi(service));
  else {
    struct servent *sp;

    if ((sp = getservbyname(service, "tcp")) == NULL)
      return -1;

    sin->sin_port = sp->s_port;
  }

  return 0;
}


void
LoadGenerator::initialize_targets()
{
  int i;
  if (direct) {
    for (i = 0; i < num_origin_servers; i++) {
      if (mksockaddr_in(origin_server_names[i], origin_server_ports[i], &target_addr[i]) < 0) {
        fprintf(stderr, "Error creating socket address (host %s, port %s)\n",
                origin_server_names[i], origin_server_ports[i]);
        exit(1);
      }
    }
  } else {
    if (mksockaddr_in(target_host, target_port, &target_addr[0]) < 0) {
      fprintf(stderr, "Error creating socket address (host %s, port %s)\n", target_host, target_port);
      exit(1);
    }
  }
}

// generates strings "size0" to "sizeN" where total number of sizes
// is N+1. Used only for synthetic documents. Also returns the
// corresponding expected size 

void
LoadGenerator::generate_size_str(char *size_str, long *size_requested_p)
{
  double rand;
  int i;
  rand = drand48();
  for (i = 0; i<num_sizes && rand> cumulative_size_prob[i]; i++);
  if (i == num_sizes) {
    fprintf(stderr, "Error: drand48() generated greater than 1.0 %lf in generate_size_str\n", rand);
    for (i = 0; i < num_sizes; i++)
      printf("cumulative_size_prob[%d] = %lf\n", i, cumulative_size_prob[i]);
    exit(1);
  }
  size_generated[i]++;
  generated_size++;
#ifdef old_synth_server
  sprintf(size_str, "size%d", i);
#else
  sprintf(size_str, "length%d", sizes[i]);
#endif
  *size_requested_p = sizes[i];
  if (debug) {
    printf("generated size_str [%s] expecting %d bytes\n", size_str, *size_requested_p);
  }
  return;
}

/* generates "0" to "D-1" where document set size is D. "0" to
"H-1" is generated with probability = hotset_access_ratio and "H"
to "D-1" is generated otherwise. Only for synthetic documents */

void
LoadGenerator::generate_serial_number_str(char *serial_number_str)
{
  double rand;
  long serial_number;
  rand = drand48();
  if (rand < hotset_access_ratio) {
    /* Generate a document from hotset */
    serial_number = lrand48() % max_hotset_serial_num;
    hotset_generated++;
  } else if (rand < 1.0) {
    serial_number = max_hotset_serial_num + lrand48() % (max_docset_serial_num - max_hotset_serial_num);
    random_generated++;
  } else {
    fprintf(stderr, "Error: rand48() generated a number %lf greater than or equal to 1\n", rand);
    exit(1);
  }
  sprintf(serial_number_str, "%d", serial_number);
  generated_set++;
  if (debug) {
    printf("generated: serial_number_str [%s]\n", serial_number_str);
  }
}

/* Generate a server str of the form: "server0"..
"serverN-1", where N is the number of origin servers. 
Only applicable for synthetic load */

void
LoadGenerator::generate_origin_server_target(char *origin_server_str,   /* Return value */
                                             struct sockaddr_in **target        /* Return value */
  )
{
  int origin_server_num = 0;
  origin_server_num = (lrand48() % num_origin_servers);
  generated_origin_servers++;
  origin_servers_generated[origin_server_num]++;
  sprintf(origin_server_str, "%s:%s", origin_server_names[origin_server_num], origin_server_ports[origin_server_num]);
  if (direct) {
    *target = &(target_addr[origin_server_num]);
  } else {
    *target = &(target_addr[0]);
  }
  if (debug) {
    printf("Generated server str [%s]\n", origin_server_str);
  }
}

#ifdef _PLUG_IN
/* setup a sockaddr_in for given host and port for connection */
void
LoadGenerator::generate_dynamic_origin_server_target(char *hostname, char *portname, struct sockaddr_in **target        /* Return value */
  )
{
  if (mksockaddr_in(hostname, portname, *target) < 0) {
    fprintf(stderr, "Error creating socket address (host %s, port %s)\n", hostname, portname);
  }
}
#endif


/* Create synthetic request */
void
LoadGenerator::create_synthetic_request(char *req_string,       /* Return request */
                                        void **req_id, long *bytes_requested_p, struct sockaddr_in **target)
{
  char size_str[MAX_SIZESTR_SIZE],
    serial_number_str[MAX_SERIALNUMBERSTR_SIZE],
    one_request_str[MAX_ONEREQUESTSTR_SIZE], origin_server_str[MAX_ORIGINSERVERSTR_SIZE];

#ifdef _PLUG_IN
  char dynamic_origin_server_name[MAX_HOSTNAME_SIZE];
  char dynamic_origin_server_port[MAX_PORTNAME_SIZE];
#endif

  int i;
  long size_requested;
  static long current_serial_num = 0;
  static int current_origin_server_num = 0;
  static int current_size_num = num_sizes - 1;
  strcpy(req_string, "");
  /////////////////////////////////
#ifdef _PLUG_IN
  strcpy(one_request_str, "");
  strcpy(dynamic_origin_server_name, "");
  strcpy(dynamic_origin_server_port, "");
#endif
  /////////////////////////////////

  if (warmup) {
    ////////////////////////////////
#ifdef _PLUG_IN
    int more_request = 1;
    if (plug_in->request_create_fcn) {
      more_request = (plug_in->request_create_fcn) (dynamic_origin_server_name,
                                                    MAX_HOSTNAME_SIZE,
                                                    dynamic_origin_server_port,
                                                    MAX_PORTNAME_SIZE, req_string, MAX_ONEREQUESTSTR_SIZE, req_id);
    }
    if (!more_request) {
      strcpy(req_string, "");
      // ready to finish inkbench and print stats      
      return;
    } else if (strcmp(dynamic_origin_server_name, "") != 0 &&
               strcmp(dynamic_origin_server_port, "") != 0 && strcmp(req_string, "") != 0) {
      // generate new sockaddr and origin_server_str based on the 
      // dynamic_origin_server_name and dynamic_origin_server_port
      if (direct) {
        generate_dynamic_origin_server_target(dynamic_origin_server_name, dynamic_origin_server_port, target);
      } else {
        generate_origin_server_target(origin_server_str, target);
      }
      return;
    } else if (strcmp(req_string, "") != 0) {   // only the request is provided
      // choose only an origin_server to submit the request
      generate_origin_server_target(origin_server_str, target);
      return;
    }
    //////////////////////////////////////////
    // use the inkbench default if any of the info required isn't provided
    else {
#endif
      generate_origin_server_target(origin_server_str, target);

#ifdef old_synth_server
      sprintf(size_str, "size%d", current_size_num);
#else
      sprintf(size_str, "length%d", sizes[current_size_num]);
#endif
      *bytes_requested_p = sizes[current_size_num];
      sprintf(serial_number_str, "%d", current_serial_num);
      sprintf(origin_server_str, "%s:%s",
              origin_server_names[current_origin_server_num], origin_server_ports[current_origin_server_num]);
      if (direct) {
        sprintf(req_string,
                "GET /%s%s/%s HTTP/1.0\r\nAccept: */*\r\nHost: %s\r\n\r\n",
                document_base, serial_number_str, size_str, origin_server_str);
      } else {
        sprintf(req_string,
                "GET http://%s/%s%s/%s HTTP/1.0\r\nAccept: */*\r\n\r\n",
                origin_server_str, document_base, serial_number_str, size_str);
      }

      // fprintf(stderr, "req_string = %s\n", req_string);
      /* now update the current nums */

      if (direct) {
        *target = &target_addr[current_origin_server_num];
      } else {
        *target = &target_addr[0];
      }

      current_origin_server_num++;
      if (current_origin_server_num == num_origin_servers) {
        current_origin_server_num = 0;

        current_serial_num++;
        if (current_serial_num == max_hotset_serial_num) {
          current_serial_num = 0;

          current_size_num--;
          if (current_size_num < 0) {
            current_size_num = num_sizes - 1;
          }
        }
      }
#ifdef _PLUG_IN
    }
#endif
  } else {                      /* not warmup */

    ////////////////////////////////
#ifdef _PLUG_IN
    int more_request = 1;
    if (plug_in->request_create_fcn) {
      more_request = (plug_in->request_create_fcn) (dynamic_origin_server_name,
                                                    MAX_HOSTNAME_SIZE,
                                                    dynamic_origin_server_port,
                                                    MAX_PORTNAME_SIZE, req_string, MAX_ONEREQUESTSTR_SIZE, req_id);
    }
    if (!more_request) {
      strcpy(req_string, "");
      // ready to finish inkbench and print stats
      return;
    } else if (strcmp(dynamic_origin_server_name, "") != 0 &&
               strcmp(dynamic_origin_server_port, "") != 0 && strcmp(req_string, "") != 0) {
      // generate new sockaddr and origin_server_str based on the 
      // dynamic_origin_server_name and dynamic_origin_server_port
      if (direct) {
        generate_dynamic_origin_server_target(dynamic_origin_server_name, dynamic_origin_server_port, target);
      } else {
        generate_origin_server_target(origin_server_str, target);
      }
      return;
    } else if (strcmp(req_string, "") != 0) {   // only the request is provided
      // choose only an origin_server to submit the request
      generate_origin_server_target(origin_server_str, target);
      return;
    }
    //////////////////////////////////////////
    // use the inkbench default if any of the info required isn't provided
    else {
#endif
      strcpy(req_string, "");
      *bytes_requested_p = 0;
      /* bytes_requested_p stores the total size of all
         the bodies requested which could be more than that of a single
         body if keepalive > 1 */
      if (direct) {
        generate_origin_server_target(origin_server_str, target);
      }
      for (i = 0; i < keepalive; i++) {
        generate_size_str(size_str, &size_requested);
        *bytes_requested_p += size_requested;
        generate_serial_number_str(serial_number_str);
        if (direct) {
          // use relative URL, Connection instead of Proxy-Connection, don't generate new origin server
          if (i < keepalive - 1) {
            sprintf(one_request_str,
                    "GET /%s%s/%s HTTP/1.0\r\nConnection: Keep-Alive\r\nAccept: */*\r\nHost: %s\r\n\r\n",
                    document_base, serial_number_str, size_str, origin_server_str);
          } else {
            sprintf(one_request_str,
                    "GET /%s%s/%s HTTP/1.0\r\nAccept: */*\r\nHost: %s\r\n\r\n",
                    document_base, serial_number_str, size_str, origin_server_str);
          }
        } else {
          generate_origin_server_target(origin_server_str, target);
          if (i < keepalive - 1) {
            sprintf(one_request_str,
                    "GET http://%s/%s%s/%s HTTP/1.0\r\nProxy-Connection: Keep-Alive\r\nAccept: */*\r\n\r\n",
                    origin_server_str, document_base, serial_number_str, size_str);
          } else {
            sprintf(one_request_str,
                    "GET http://%s/%s%s/%s HTTP/1.0\r\nAccept: */*\r\n\r\n",
                    origin_server_str, document_base, serial_number_str, size_str);
          }
        }
        req_string = strcat(req_string, one_request_str);
      }
#ifdef _PLUG_IN
    }
#endif
  }
#ifdef DEBUG_URL
  fprintf(stderr, "Created synthetic request [%s]\n", req_string);
#else
  if (debug) {
    printf("Created synthetic request [%s]\n", req_string);
  }
#endif
}

/* Create a real request (from logfile) */
void
LoadGenerator::create_request_from_logfile(char *req_string,    /* Return request */
                                           long *bytes_requested_p)
{
  printf("Shouldn't be called\n");
  exit(-1);
}

void
LoadGenerator::generate_new_request(char *req_string, void **req_id,
                                    long *bytes_requested_p, struct sockaddr_in **target)
{

  if (synthetic) {
    if (debug) {
      printf("Generating synthetic request \n");
    }
    create_synthetic_request(req_string, req_id, bytes_requested_p, target);
  } else {
    if (debug) {
      printf("Generating request from logfile \n");
    }
    create_request_from_logfile(req_string, bytes_requested_p);
  }
}
void
LoadGenerator::print_stats()
{
  int i;
  if (synthetic) {
    printf("Generated %ld document sizes overall\n", generated_size);
    for (i = 0; i < num_sizes; i++) {
      printf("\t Size %3d (%7ld bytes): %6ld (%5.2lf%%)\n", i, sizes[i],
             size_generated[i], generated_size ? size_generated[i] * 100.0 / generated_size : 0);
    }
    printf("Generated %ld document serial numbers overall\n", generated_set);
    printf("\t HotSet : %ld (%.2lf%%)\n", hotset_generated,
           generated_set ? hotset_generated * 100.0 / generated_set : 0);
    printf("\t Rest : %ld (%.2lf%%)\n", random_generated, generated_set ? random_generated * 100.0 / generated_set : 0);
    printf("Origin Servers generated %ld\n", generated_origin_servers);
    for (i = 0; i < num_origin_servers; i++) {
      printf("\t Server %d (%s), generated %ld (%.2lf%%)\n", i,
             origin_server_names[i], origin_servers_generated[i],
             generated_origin_servers ? origin_servers_generated[i] * 100.0 / generated_origin_servers : 0);
    }
  }
}
