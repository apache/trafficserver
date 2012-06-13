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

//////////////////////////////////////////////////////////////////////////////////////////////
// Plugin to do routing decisions
// -----------
//
// To use this plugin, configure a remap.config rule like
//
//   map http://foo.com http://bar.com @plugin=balancer.so @pparam=rotation:news1
//
// For full description of all available options, please visit the Twiki or Dist docs.
//
//
// Note that the path to the plugin itself must be absolute, and by default it is
//
//


#include <netdb.h>

#include <string>

#include <ts/remap.h>
#include <ts/ts.h>

#include "resources.h"
#include "hashkey.h"


static int MAX_HASH_KEY_VALUES = 16;
static int MD5_DIGEST_LENGTH = 16;


///////////////////////////////////////////////////////////////////////////////
// Class encapsulating one configuration / setup.
//
struct KeyData
{
  const void* data;
  int len;
};

class BalancerInstance
{
public:
  BalancerInstance(HashKey* hash=NULL, HashKey* hash2=NULL) :
    _first_hash(hash), _second_hash(hash2),  _bucket_hosts(0), _rotation(NULL),
    _host_ip(false)
  { };

  ~BalancerInstance()
  {
    HashKey* tmp;

    while (_first_hash) {
      tmp = _first_hash->next;
      delete _first_hash;
      _first_hash = tmp;
    }
    if (_rotation)
      TSfree(_rotation);
  };

  // Some simple setters and getters
  void set_host_ip() { _host_ip = true; };
  bool host_ip() const { return _host_ip; };

  char* rotation() const { return _rotation; };
  void set_rotation(const std::string& rot) {
    if (rot.size() > 255) {
      TSError("Rotation name is too long");
      return;
    }
    _rotation = TSstrdup(rot.c_str());
  }

  int bucket_hosts() const { return _bucket_hosts; };
  void set_bucket_hosts(const std::string& width) {
    _bucket_hosts = atoi(width.c_str());
  }

  bool has_primary_hash() const { return (_first_hash != NULL); }
  bool has_secondary_hash() const { return (_second_hash != NULL); }
  void append_hash(HashKey* hash, bool secondary = false) {
    if (secondary) {
      if (_second_hash) {
        _second_hash->append(hash);
      } else {
        _second_hash = hash;
      }
    } else {
      if (_first_hash) {
        _first_hash->append(hash);
      } else {
        _first_hash = hash;
      }
    }
  };

  void make_hash_key(char* id, bool secondary, Resources& resr) {
    HashKey* hk = secondary ? _second_hash : _first_hash;

    if (hk) {
      KeyData keys[MAX_HASH_KEY_VALUES];
      int ix = 0;
      int key_len = 0;

      do {
        keys[ix].len = hk->key(&(keys[ix].data), resr);
        if (keys[ix].len > 0)
          key_len += keys[ix].len;
        ++ix;
      } while ((hk = hk->next));

      // Now create the buffer and copy over all the hash values.
      if (key_len > 0) {
        char buf[key_len + 1];
        char* p = buf;

        hk = secondary ? _second_hash : _first_hash;
        for (int i = 0; i < ix; ++i) {
          if ((keys[i].len > 0) && keys[i].data) {
            memcpy(p, keys[i].data, keys[i].len);
            p += keys[i].len;
          }
          hk->free_key(keys[i].data, keys[i].len, resr); // Cleanup some private data (possibly)
          hk = hk->next;
        }
        *p = '\0';
        if (TSIsDebugTagSet("balancer")) {
          TSDebug("balancer", "Making %s hash ID's using %s", secondary ? "secondary" : "primary", buf);
        }
        ycrMD5_r(buf, key_len, id);
      } else {
        if (secondary) {
          // Secondary ID defaults to IP (if none of the specified hashes computes)
          char buf[4];

          *buf = resr.getRRI()->client_ip; // ToDo: this only works for IPv4

          TSDebug("balancer", "Making secondary hash ID's using IP (default) = %s", buf);
          ycrMD5_r(buf, key_len, id);
        } else {
          // Primary ID defaults to URL (if none of the specified hashes computes)
          char buf[resr.getRRI()->orig_url_size + 1];

          memcpy(buf, resr.getRRI()->orig_url, resr.getRRI()->orig_url_size);
          buf[resr.getRRI()->orig_url_size] = '\0';
          TSDebug("balancer", "Making primary hash ID's using URL (default) = %s", buf);
          ycrMD5_r(buf, key_len, id);
        }
      }
    } else {
      *id = '\0';
    }
  }


private:
  HashKey* _first_hash;
  HashKey* _second_hash;
  int _bucket_hosts;
  char* _rotation;
  bool _host_ip;
};


///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin.
//
int
tsremap_init(TSREMAP_INTERFACE *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSREMAP_INTERFACE argument", errbuf_size - 1);
    return -1;
  }

  if (api_info->size < sizeof(TSREMAP_INTERFACE)) {
    strncpy(errbuf, "[tsremap_init] - Incorrect size of TSREMAP_INTERFACE structure", errbuf_size - 1);
    return -2;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[tsremap_init] - Incorrect API version %ld.%ld",
             api_info->tsremap_version >> 16, (api_info->tsremap_version & 0xffff));
    return -3;
  }

  TSDebug("balancer", "plugin is succesfully initialized");
  return 0;
}


///////////////////////////////////////////////////////////////////////////////
// One instance per remap.config invocation.
//
int
tsremap_new_instance(int argc, char *argv[], ihandle *ih, char *errbuf, int errbuf_size)
{
  BalancerInstance* ri = new BalancerInstance;

  *ih = static_cast<ihandle>(ri);

  if (ri == NULL) {
    TSError("Unable to create remap instance");
    return -5;
  }

  for (int ix=2; ix < argc; ++ix) {
    std::string arg = argv[ix];

    // Check "flags" first, they take no additional arguments
    if (arg.compare(0, 6, "hostip") ==  0) {
      ri->set_host_ip();
    } else {
      std::string::size_type sep = arg.find_first_of(":");

      if (sep == std::string::npos) {
        TSError("Malformed options in balancer: %s", argv[ix]);
      } else {
        std::string arg_val = arg.substr(sep + 1, std::string::npos);

        if (arg.compare(0, 8, "rotation") == 0) {
          ri->set_rotation(arg_val);
        } else if (arg.compare(0, 7, "bucketw") == 0) {
          ri->set_bucket_hosts(arg_val);
        } else if (arg.compare(0, 4, "hash") == 0) {
          bool secondary = !arg.compare(0, 5, "hash2");

          if (arg_val.compare(0, 3, "url") == 0) {
            URLHashKey* hk = new URLHashKey();

            if (NULL == hk) {
              TSError("Couldn't create balancer URL hash key");
            } else {
              ri->append_hash(hk, secondary);
            }
          } else if (arg_val.compare(0, 4, "path") == 0) {
            PathHashKey* hk = new PathHashKey();

            if (NULL == hk) {
              TSError("Couldn't create balancer path hash key");
            } else {
              ri->append_hash(hk, secondary);
            }
          } else if (arg_val.compare(0, 2, "ip") == 0) {
            IPHashKey* hk = new IPHashKey();

            if (NULL == hk) {
              TSError("Couldn't create balancer IP hash key");
            } else {
              ri->append_hash(hk, secondary);
            }
          } else {
            // The hash parameter can take a second argument
            std::string::size_type sep2 = arg_val.find_first_of("/");

            if (sep2 == std::string::npos) {
              TSError("Malformed hash options in balancer: %s", argv[ix]);
            } else {
              std::string arg_val2 = arg_val.substr(sep2 + 1, std::string::npos);

              if (arg_val.compare(0, 6, "cookie") == 0) {
                CookieHashKey* hk = new CookieHashKey(arg_val2);

                if (NULL == hk) {
                  TSError("Couldn't create balancer cookie hash key");
                } else {
                  ri->append_hash(hk, secondary);
                }
              } else if (arg_val.compare(0, 6, "header") == 0) {
                HeaderHashKey* hk = new HeaderHashKey(arg_val2);

                if (NULL == hk) {
                  TSError("Couldn't create balancer header hash key");
                } else {
                  ri->append_hash(hk, secondary);
                }
              } else {
                TSError("Unknown balancer hash option: %s", argv[ix]);
              }
            }
          }
        } else {
          TSError("Unknown balancer option: %s", argv[ix]);
        }
      }
    }
  }

  return 0;
}

void
tsremap_delete_instance(ihandle ih)
{
  BalancerInstance* ri = static_cast<BalancerInstance*>(ih);

  delete ri;
}


///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
int
tsremap_remap(ihandle ih, rhandle rh, REMAP_REQUEST_INFO *rri)
{
  BalancerInstance* balancer;
  int error = 0;
  int port = 0;
  char resbuf[256 + 256 * sizeof(in_addr_t)];
  struct hostent* res = NULL;
  char rotation[256];
  char *rot;

  if (NULL == ih) {
    TSDebug("balancer", "Falling back to default URL on remap without rules");
    return 0;
  }
  balancer = static_cast<BalancerInstance*>(ih);

  // Get the rotation name to use.

  if (balancer->rotation()) {
    rot = balancer->rotation();
  } else {
    if (rri->remap_from_host_size > 255) {
      memcpy(rotation, rri->remap_from_host, 255);
      rotation[255] = '\0';
    } else {
      memcpy(rotation, rri->remap_from_host, rri->remap_from_host_size);
      rotation[rri->remap_from_host_size] = '\0';
    }
    rot = rotation;
  }

  // Setup the basic info
  memset(&balancer_info, 0, sizeof(balancer_info));
  balancer_info.flags = BALANCER_LOOKUP_NOCHECK;   // Make sure we don't trigger a balancer health check
  balancer_info.errp = &balancer_error;
  balancer_info.port = &balancer_port;
  balancer_info.resbuf = &balancer_resbuf;
  balancer_info.resbuf_len = sizeof(balancer_resbuf);

  if (balancer->has_primary_hash()) {
    char id1[MD5_DIGEST_LENGTH+1];
    Resources resr((TSHttpTxn)rh, rri);
    
    balancer_info.num_bucket_hosts = balancer->bucket_hosts();

    balancer->make_hash_key(id1, false, resr);
    balancer_info.primary_id = id1;
    balancer_info.primary_id_len = MD5_DIGEST_LENGTH;

    if (balancer->has_secondary_hash()) {
      char id2[MD5_DIGEST_LENGTH+1];

      balancer->make_hash_key(id2, true, resr);
      balancer_info.secondary_id = id2;
      balancer_info.secondary_id_len = MD5_DIGEST_LENGTH;

      TSDebug("balancer", "Calling balancer_lookup(\"%s\") with primary and secondary hash", rot);
      res = balancer_lookup(rot, &balancer_info);
    } else {
      TSDebug("balancer", "Calling balancer_lookup(\"%s\") with primary hash", rot);
      res = balancer_lookup(rot, &balancer_info);
    }
  } else {
    TSDebug("balancer", "Calling balancer_lookup(\"%s\") without hash", rot);
    res = balancer_lookup(rot, &balancer_info);
  }

  // Check (and use) the balancer lookup results
  if (!res) {
    TSDebug("balancer", "BALANCER has no data for %s, using To-URL (error is %d)", rot, balancer_error);
    return 0;
  } else {
    if ((balancer_port > 0) && (balancer_port != rri->remap_to_port)) {
      rri->new_port = balancer_port;
      TSDebug("balancer", "Changing request to port %d", balancer_port);
    }
    if (balancer->host_ip()) {
      unsigned char *ip = (unsigned char*)res->h_addr;

      rri->new_host_size = snprintf(rri->new_host, 16, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      TSDebug("balancer", "Setting real-host IP to %.*s (IP for %s)", rri->new_host_size, rri->new_host, res->h_name);
    } else {
      TSDebug("balancer", "Setting real-host to %s", res->h_name);
      rri->new_host_size = strlen(res->h_name);
      if (rri->new_host_size > TSREMAP_RRI_MAX_HOST_SIZE)
        rri->new_host_size = TSREMAP_RRI_MAX_HOST_SIZE;
      memcpy(rri->new_host, res->h_name, rri->new_host_size);
    }

    return 1;
  }

  // Shouldn't happen
  return 0;
}
