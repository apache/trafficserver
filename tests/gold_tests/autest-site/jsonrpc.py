'''
JSONRPC Request convenient class helper.
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF typing.ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import typing
from collections import OrderedDict
import json
import uuid


class BaseRequestType(type):
    '''
    Base class for both, request and notifications
    '''
    def __getattr__(cls: typing.Callable, name: str) -> typing.Callable:
        def attr_handler(*args: typing.Any, **kwargs: typing.Any) -> "Request":
            return cls(name, *args, **kwargs)
        return attr_handler


class Notification(dict, metaclass=BaseRequestType):
    '''
    Convenient class to create JSONRPC objects(Notifications) and strings without the need to specify redundant information.

    Examples:

        Notification.foo_bar() = > {"jsonrpc": "2.0", "method": "foo_bar"}

        Notification.foo_bar({"hello"="world"}) =>
            {
                    "jsonrpc": "2.0",
                    "method": "foo_bar",
                    "params": {
                        "hello": "world"
                    }
                }

        Notification.foo_bar(var={"hello":"world"}) =>
            {
                "jsonrpc": "2.0",
                "method": "foo_bar",
                "params": {
                    "var": {
                        "hello": "world"
                    }
                }
            }
        Notification.foo_bar(fqdn=["yahoo.com", "trafficserver.org"]) =>
            {
                "jsonrpc": "2.0",
                "method": "foo_bar",
                "params": {
                    "fqdn": ["yahoo.com", "trafficserver.org"]
                }
            }

    '''

    def __init__(self, method: str, *args: typing.Any, **kwargs: typing.Any):
        super(Notification, self).__init__(jsonrpc='2.0', method=method)
        if args and kwargs:
            plist = list(args)
            plist.append(kwargs)
            self.update(params=plist)
        elif args:
            if isinstance(args, tuple):
                # fix this. this is to avoid having params=[[values] which seems invalid. Double check this as [[],[]] would be ok.
                self.update(params=args[0])
            else:
                self.update(params=list(args))
        elif kwargs:
            self.update(params=kwargs)

    # Allow using the dict item as an attribute.
    def __getattr__(self, name):
        if name in self:
            return self[name]
        else:
            raise AttributeError("No such attribute: " + name)

    def is_notification(self):
        return True

    def __str__(self) -> str:
        return json.dumps(self)


class Request(Notification):
    '''
    Convenient class to create JSONRPC objects and strings without the need to specify redundant information like
    version or the id. All this will be generated automatically.

    Examples:

        Request.foo_bar() = > {"id": "e9ad55fe-d5a6-11eb-a2fd-fa163e6d2ec5", "jsonrpc": "2.0", "method": "foo_bar"}

        Request.foo_bar({"hello"="world"}) =>
            {
                    "id": "850d2998-d5a7-11eb-bebc-fa163e6d2ec5",
                    "jsonrpc": "2.0",
                    "method": "foo_bar",
                    "params": {
                        "hello": "world"
                    }
                }

        Request.foo_bar(var={"hello":"world"}) =>
            {
                "id": "850d2e84-d5a7-11eb-bebc-fa163e6d2ec5",
                "jsonrpc": "2.0",
                "method": "foo_bar",
                "params": {
                    "var": {
                        "hello": "world"
                    }
                }
            }
        Request.foo_bar(fqdn=["yahoo.com", "trafficserver.org"]) =>
            {
                "id": "850d32a8-d5a7-11eb-bebc-fa163e6d2ec5",
                "jsonrpc": "2.0",
                "method": "foo_bar",
                "params": {
                    "fqdn": ["yahoo.com", "trafficserver.org"]
                }
            }

    Note: Use full namespace to avoid name collision => jsonrpc.Request, jsonrpc.Response, etc.
    '''

    def __init__(self, method: str, *args: typing.Any, **kwargs: typing.Any):
        if 'id' in kwargs:
            self.update(id=kwargs.pop('id'))  # avoid duplicated
        else:
            self.update(id=str(uuid.uuid1()))

        super(Request, self).__init__(method, *args, **kwargs)

    def is_notification(self):
        return False


class BatchRequest(list):
    def __init__(self, *args: typing.Union[Request, Notification]):
        for r in args:
            self.append(r)

    def add_request(self, req: typing.Union[Request, Notification]):
        self.append(req)

    def __str__(self) -> str:
        return json.dumps(self)


class Response(dict):
    '''
    Convenient class to help handling jsonrpc responses. This can be source from text directly or by an already parsed test into
    a json object.
    '''

    def __init__(self, *arg, **kwargs):
        if 'text' in kwargs:
            self.__dict__ = json.loads(kwargs['text'])
        elif 'json' in kwargs:
            self.__dict__ = kwargs['json']

    def is_error(self) -> bool:
        '''
        Check whether the error field is present in the response. ie:
            {
                "jsonrpc":"2.0",
                "error":{...},
                "id":"284e0b86-d03a-11eb-9206-fa163e6d2ec5"
            }
        '''
        if 'error' in self.__dict__:
            return True
        return False

    def is_only_success(self) -> bool:
        '''
        Some responses may only have set the result as success. This functions checks that the value in the response field only
        contains the 'success' string, ie:

            {
                "jsonrpc": "2.0",
                "result": "success",
                "id": "8504569c-d5a7-11eb-bebc-fa163e6d2ec5"
            }
        '''
        if self.is_ok() and self.result == 'success':
            return True

        return False

    def is_ok(self) -> bool:
        '''
        No error present in the response, the result field was properly set.
        '''
        return 'result' in self.__dict__

    def error_as_str(self):
        '''
        Build up the error string.
        {
            "jsonrpc":"2.0",
            "error":{
                "code":9,
                "message":"Error during execution",
                "data":[
                    {
                        "code":10001,
                        "message":"No values provided"
                    }
                ]
            },
            "id":"284e0b86-d03a-11eb-9206-fa163e6d2ec5"
            }
        '''
        if self.is_ok():
            return "no error"

        errStr = ""
        errStr += f"\n[code: {self.error['code']}, message: \"{self.error['message']}\"]"
        if 'data' in self.error:
            errStr += "\n\tAdditional Information:"
            for err in self.error['data']:
                errStr += f"\n\t - code: {err['code']}, message: \"{err['message']}\"."
        return errStr

    def __str__(self) -> str:
        return json.dumps(self.__dict__)

    def is_execution_error(self):
        '''
        Checks if the provided error is an Execution error(9).
        '''
        if self.is_ok():
            return False

        return self.error['code'] == 9

    def contains_nested_error(self, code=None, msg=None):
        if self.is_execution_error():
            for err in self.error['data']:
                if code and msg:
                    return err['code'] == code and err['message'] == msg
                elif code and err['code'] == code:
                    return True
                elif msg and err['message'] == msg:
                    return True
                else:
                    return False
        return False


def make_response(text):
    if text == '':
        return None

    s = json.loads(text)
    if isinstance(s, dict):
        return Response(json=s)
    elif isinstance(s, list):
        batch = []
        for r in s:
            batch.append(Response(json=r))
        return batch
