/** @file

  Loads the CARP configuration

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
#ifndef __CARPHOST_H__
#define __CARPHOST_H__ 1

#define DEFAULT_GROUP 1

#include <string>
#include <netinet/in.h>

class CarpHost
{
public:

    CarpHost(std::string& name, int port, std::string& scheme, int weight, int group = DEFAULT_GROUP) : _port(port),
    _name(name), _scheme(scheme), _weight(weight), _group(group)
    {
       _healthCheckAddr.ss_family=AF_UNSPEC;
    };

    ~CarpHost()
    {

    };

    int getPort()
    {
        return _port;
    };
    
    void setPort(int p)
    {
        _port = p;
    };

    const std::string& getName()
    {
        return _name;
    };

    void setName(std::string& n)
    {
        _name = n;
    };

    const std::string& getScheme()
    {
        return _scheme;
    }

    void setScheme(std::string& scheme)
    {
        _scheme = scheme;
    }

    const struct sockaddr_storage* getHealthCheckAddr()
    {
        return &_healthCheckAddr;
    };
    
    void setHealthCheckAddr(struct sockaddr_storage& a)
    {
        _healthCheckAddr = a;
    };

    int getHealthCheckPort()
    {
        return _healthCheckPort;
    };
    
    void setHealthCheckPort(int p)
    {
        _healthCheckPort = p;
    };
    
    const std::string& getHealthCheckUrl()
    {
        return _healthCheckUrl;
    };
    
    void setHealthCheckUrl(const std::string& p)
    {
        _healthCheckUrl = p;
    };

    int getWeight()
    {
        return _weight;
    };
    
    void setWeight(int w)
    {
        _weight = w;
    };

    int getGroup()
    {
        return _group;
    };
    
    void setGroup(int g)
    {
        _group = g;
    };

    void dump(std::string &s);
private:
    int             _port;
    std::string     _name;
    std::string     _scheme;
    int             _weight;
    int             _group;

    struct sockaddr_storage _healthCheckAddr;
    int             _healthCheckPort;
    std::string     _healthCheckUrl;
};


#endif

