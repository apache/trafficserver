.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../../../common.defs

Example: Query Remap Plugin
***************************

The sample remap plugin, ``query_remap.c``, maps client requests to a
number of servers based on a hash of the request's URL query parameter.
This can be useful for spreading load for a given type of request among
backend servers, while still maintaining "stickiness" to a single server
for similar requests. For example, a search engine may want to send
repeated queries for the same keywords to a server that has likely
cached the result from a prior query.

Configuration of query\_remap
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The query remap plugin will allow the query parameter name to be
specified, along with the hostnames of the servers to hash across.
Sample ``remap.config`` rules using ``query_remap`` will look like:

::

    map http://www.example.com/search http://srch1.example.com/search @plugin=query_remap.so @pparam=q @pparam=srch1.example.com @pparam=srch2.example.com @pparam=srch3.example.com
    map http://www.example.com/profiles http://prof1.example.com/profiles @plugin=query_remap.so @pparam=user_id @pparam=prof1.example.com @pparam=prof2.example.com

The first ``@pparam`` specifies the query param key for which the value
will be hashed. The remaining parameters list the hostnames of the
servers. A request for ``http://www.example.com/search?q=apache`` will
match the first rule. The plugin will look for the *``q``* parameter and
hash the value '``apache``\ ' to pick from among
``srch_[1-3]_.example.com`` to send the request.

If the request does not include a *``q``* query parameter and the plugin
decides not to modify the request, the default toURL
'``http://srch1.example.com/search``\ ' will be used by TS.

The parameters are passed to the plugin's ``tsremap_new_instance``
function. In ``query_remap``, ``tsremap_new_instance`` creates a
plugin-defined ``query_remap_info`` struct to store its configuration
parameters. The ihandle, an opaque pointer that can be used to pass
per-instance data, is set to this struct pointer and will be passed to
the ``tsremap_remap`` function when it is triggered for a request.

.. code-block:: c

    typedef struct _query_remap_info {
      char *param_name;
      size_t param_len;
      char **hosts;
      int num_hosts;
    } query_remap_info;
        
        
    int tsremap_new_instance(int argc,char *argv[],ihandle *ih,char *errbuf,int errbuf_size)
    {
      int i;
        
      if (argc param_name = strdup(argv[2]);
      qri->param_len = strlen(qri->param_name);
      qri->num_hosts = argc - 3;
      qri->hosts = (char**) TSmalloc(qri->num_hosts*sizeof(char*));
        
      for (i=0; i num_hosts; ++i) {
        qri->hosts[i] = strdup(argv[i+3]);
      }
        
      *ih = (ihandle)qri;
      return 0;
    }

Another way remap plugins may want handle more complex configuration is
to specify a configuration filename as a ``pparam`` and parse the
specified file during instance initialization.

Performing the Remap
~~~~~~~~~~~~~~~~~~~~

The plugin implements the ``tsremap_remap`` function, which is called
when TS has read the client HTTP request headers and matched the request
to a remap rule configured for the plugin. The ``TSRemapRequestInfo``
struct contains input and output members for the remap operation.

``tsremap_remap`` uses the configuration information passed via the
``ihandle`` and checks the ``request_query`` for the configured query
parameter. If the parameter is found, the plugin sets a ``new_host`` to
modify the request host:

.. code-block:: c

    int tsremap_remap(ihandle ih, rhandle rh, TSRemapRequestInfo *rri)
    {
      int hostidx = -1;
      query_remap_info *qri = (query_remap_info*)ih;
        
      if (!qri) {
        TSError("[remap] NULL ihandle");
        return 0;
      }
          
      if (rri && rri->request_query && rri->request_query_size > 0) {
        char *q, *s, *key;
            
        //make a copy of the query, as it is read only
        q = (char*) TSmalloc(rri->request_query_size+1);
        strncpy(q, rri->request_query, rri->request_query_size);
        q[rri->request_query_size] = '\0';
            
        s = q;
        //parse query parameters
        for (key = strsep(&s, "&"); key != NULL; key = strsep(&s, "&")) {
          char *val = strchr(key, '=');
          if (val && (size_t)(val-key) == qri->param_len &&
              !strncmp(key, qri->param_name, qri->param_len)) {
            ++val;
            //the param key matched the configured param_name
            //hash the param value to pick a host
            hostidx = hash_fnv32(val, strlen(val)) % (uint32_t)qri->num_hosts;
            break;
          }
        }
            
        TSfree(q);
            
        if (hostidx >= 0) {
          rri->new_host_size = strlen(qri->hosts[hostidx]);
          if (rri->new_host_size new_host, qri->hosts[hostidx], rri->new_host_size);
            return 1; //host has been modified
          }
        }
      }
        
      //the request was not modified, TS will use the toURL from the remap rule
      return 0;
    }

