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

.. include:: ../../common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. |RPC| replace:: JSONRPC 2.0

.. _JSONRPC: https://www.jsonrpc.org/specification
.. _JSON: https://www.json.org/json-en.html

.. |str| replace:: ``string``
.. |arraynum| replace:: ``array[number]``
.. |arraynumstr| replace:: ``array[number|string]``
.. |arraystr| replace:: ``array[string]``
.. |num| replace:: *number*
.. |strnum| replace:: *string|number*
.. |object| replace:: *object*
.. |array| replace:: *array*
.. |optional| replace:: ``optional``
.. |method| replace:: ``method``
.. |notification| replace:: ``notification``
.. |arrayrecord| replace:: ``array[record]``
.. |arrayerror| replace:: ``array[errors]``

.. _jsonrpc-api:

API
***

.. _jsonrpc-api-description:

Description
===========

|TS| Implements and exposes management calls using a JSONRPC API.  This API is base on the following two things:

* `JSON  <https://www.json.org/json-en.html>`_  format. Lightweight data-interchange format. It is easy for humans to read and write.
  It is easy for machines to parse and generate. It's basically a  collection of name/value pairs.

* `JSONRPC 2.0 <https://www.jsonrpc.org/specification>`_ protocol. Stateless, light-weight remote procedure call (RPC) protocol.
  Primarily this specification defines several data structures and the rules around their processing.


In order for programs to communicate with |TS|, the server exposes a ``JSONRRPC 2.0`` API where programs can communicate with it.


.. _admin-jsonrpc-api:

Administrative API
==================

This section describes how to interact with the administrative RPC API to interact with |TS|

..
   _This: We should explain how to deal with permission once it's implemented.



.. _Records:

Records
-------

When interacting with the admin api, there are a few structures that need to be understood, this section will describe each of them.


.. _RecordRequest:

RPC Record Request
~~~~~~~~~~~~~~~~~~

To obtain information regarding a particular record(s) from |TS|, we should use the following fields in an *unnamed* json structure.


====================== ============= ================================================================================================================
Field                  Type          Description
====================== ============= ================================================================================================================
``record_name``        |str|         The name we want to query from |TS|. This is |optional| if ``record_name_regex`` is used.
``record_name_regex``  |str|         The regular expression we want to query from |TS|. This is |optional| if ``record_name`` is used.
``rec_types``          |arraynumstr| |optional| A list of types that should be used to match against the found record. These types refer to ``RecT``.
                                       Other values (in decimal) than the ones defined by the ``RecT`` ``enum`` will be ignored. If no type is
                                       specified, the server will not match the type against the found record.
====================== ============= ================================================================================================================

.. note::

   If ``record_name`` and ``record_name_regex`` are both provided, the server will not use any of them. Only one should be provided.


Example:

   #. Single record:

   .. code-block:: json

      {
         "id":"2947819a-8563-4f21-ba45-bde73210e387",
         "jsonrpc":"2.0",
         "method":"admin_lookup_records",
         "params":[
            {
               "record_name":"proxy.config.exec_thread.autoconfig.scale",
               "rec_types":[
                  1,
                  16
               ]
            }
         ]
      }

   #. Multiple records:

   .. code-block:: json
      :emphasize-lines: 5-12

      {
         "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
         "jsonrpc": "2.0",
         "method": "admin_lookup_records",
         "params": [{
               "record_name": "proxy.config.exec_thread.autoconfig.scale"
            },
            {
               "record_name": "proxy.config.log.rolling_interval_sec",
               "rec_types": [1]
            }
         ]
      }

   #. Batch Request

   .. code-block:: json

      [
         {
            "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
            "jsonrpc": "2.0",
            "method": "admin_lookup_records",
            "params": [{
               "record_name_regex": "proxy.config.exec_thread.autoconfig.scale",
               "rec_types": [1]
            }]
         }, {
            "id": "dam7018e-0720-11eb-abe2-001fc69dd123",
            "jsonrpc": "2.0",
            "method": "admin_lookup_records",
            "params": [{
               "record_name_regex": "proxy.config.log.rolling_interval_sec",
               "rec_types": [1]
            }]
         }
      ]


.. _RecordResponse:


RPC Record Response
~~~~~~~~~~~~~~~~~~~

When querying for a record(s), in the majority of the cases the record api will respond with the following json structure.

=================== ==================== ========================================================================
Field               Type                 Description
=================== ==================== ========================================================================
``recordList``      |arrayrecord|         A list of record |object|. See `RecordRequestObject`_
``errorList``       |arrayerror|          A list of error |object| . See `RecordErrorObject`_
=================== ==================== ========================================================================


.. _RecordErrorObject:

RPC Record Error Object
~~~~~~~~~~~~~~~~~~~~~~~

All errors that are found during a record query, will be returned back to the caller in the ``error_list`` field as part of the `RecordResponse`_ object.
The record errors have the following fields.


=================== ============= ===========================================================================
Field               Type          Description
=================== ============= ===========================================================================
``code``            |str|         |optional| An error code that should be used to get a description of the error.(Add error codes)
``record_name``     |str|         |optional| The associated record name, this may be omitted sometimes.
``message``         |str|         |optional| A descriptive message. The server can omit this value.
=================== ============= ===========================================================================


Example:

   .. code-block:: json
      :linenos:

      {
         "code": "2007",
         "record_name": "proxy.config.exec_thread.autoconfig.scale"
      }


Examples:

#. Request a non existing record among with an invalid type for a record:

   .. code-block:: json
      :linenos:

      {
         "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
         "jsonrpc": "2.0",
         "method": "admin_lookup_records",
         "params": [
            {
                  "record_name": "non.existing.record"
            },
            {
                  "record_name": "proxy.process.http.total_client_connections_ipv4",
                  "rec_types": [1]
            }
         ]
      }

   Line ``7`` requests a non existing record and in line ``11`` we request a type that does not match the record's type.

   .. code-block:: json
      :linenos:

      {
         "jsonrpc":"2.0",
         "result":{
            "errorList":[
               {
                  "code":"2000",
                  "record_name":"non.existing.record"
               },
               {
                  "code":"2007",
                  "record_name":"proxy.process.http.total_client_connections_ipv4"
               }
            ]
         },
         "id":"ded7018e-0720-11eb-abe2-001fc69cc946"
      }

   In this case we get the response indicating that the requested fields couldn't be retrieved. See `RecordErrorObject`_ for more details.

.. _RecordErrorObject-Enum:


JSONRPC Record Errors
~~~~~~~~~~~~~~~~~~~~~

The following errors could be generated when requesting record from the server.

.. class:: RecordError

   .. enumerator:: RECORD_NOT_FOUND = 2000

      Record not found.

   .. enumerator:: RECORD_NOT_CONFIG = 2001

      Record is not a configuration type.

   .. enumerator:: RECORD_NOT_METRIC = 2002

      Record is not a metric type.

   .. enumerator:: INVALID_RECORD_NAME = 2003

      Invalid Record Name.

   .. enumerator:: VALIDITY_CHECK_ERROR = 2004

      Validity check failed.

   .. enumerator:: GENERAL_ERROR = 2005

      Error reading the record.

   .. enumerator:: RECORD_WRITE_ERROR = 2006

      Generic error while writting the record. ie: RecResetStatRecord() returns  REC_ERR_OKAY

   .. enumerator:: REQUESTED_TYPE_MISMATCH = 2007

      The requested record's type does not match againts the passed type list.

   .. enumerator:: INVALID_INCOMING_DATA = 2008

      This could be caused by an invalid value in the incoming request which may cause the parser to fail.


.. _RecordRequestObject:

RPC Record Object
~~~~~~~~~~~~~~~~~

This is mapped from a ``RecRecord``, when requesting for a record the following information will be populated into a json |object|.
The ``record`` structure has the following members.

=================== ======== ==================================================================
Record Field        Type     Description
=================== ======== ==================================================================
``current_value``   |str|    Current value that is held by the record.
``default_value``   |str|    Record's default value.
``name``            |str|    Record's name
``order``           |str|    Record's order
``overridable``     |str|    Records's overridable configuration.
``raw_stat_block``  |str|    Raw Stat Block Id.
``record_class``    |str|    Record type. Mapped from ``RecT``
``record_type``     |str|    Record's data type. Mapped from RecDataT
``version``         |str|    Record's version.
``stats_meta``      |object| Stats metadata `stats_meta`_
``config_meta``     |object| Config metadata `config_meta`_
=================== ======== ==================================================================

* it will be either ``config_meta`` or ``stats_meta`` object, but never both*


.. _config_meta:

Config Metadata

=================== ======== ==================================================================
Record Field        Type     Description
=================== ======== ==================================================================
`access_type`       |str|    Access type. This is mapped from ``TSRecordAccessType``.
`check_expr`        |str|    Syntax checks regular expressions.
`checktype`         |str|    Check type, This is mapped from ``RecCheckT``.
`source`            |str|    Source of the configuration value. Mapped from RecSourceT
`update_status`     |str|    Update status flag.
`update_type`       |str|    How the records get updated. Mapped from RecUpdateT
=================== ======== ==================================================================


.. _stats_meta:

Stats Metadata (TBC)

=================== ======== ==================================================================
Record Field        Type     Description
=================== ======== ==================================================================
`persist_type`      |str|    Persistent type. This is mapped from ``RecPersistT``
=================== ======== ==================================================================


Example with config meta:

   .. code-block:: json
      :linenos:

      {
         "record":{
            "record_name":"proxy.config.diags.debug.tags",
            "record_type":"3",
            "version":"0",
            "raw_stat_block":"0",
            "order":"421",
            "config_meta":{
               "access_type":"0",
               "update_status":"0",
               "update_type":"1",
               "checktype":"0",
               "source":"3",
               "check_expr":"null"
            },
            "record_class":"1",
            "overridable":"false",
            "data_type":"STRING",
            "current_value":"rpc",
            "default_value":"http|dns"
         }
      }

Example with stats meta:

   .. code-block:: json
      :linenos:

         {
            "record": {
               "current_value": "0",
               "data_type": "COUNTER",
               "default_value": "0",
               "order": "8",
               "overridable": "false",
               "raw_stat_block": "10",
               "record_class": "2",
               "record_name": "proxy.process.http.total_client_connections_ipv6",
               "record_type": "4",
               "stat_meta": {
                  "persist_type": "1"
               },
               "version": "0"
            }
         }

.. _jsonrpc-admin-api:

JSONRPC API
===========

* `admin_lookup_records`_

* `admin_clear_all_metrics_records`_

* `admin_config_set_records`_

* `admin_config_reload`_

* `admin_clear_metrics_records`_

* `admin_clear_all_metrics_records`_

* `admin_host_set_status`_

* `admin_server_stop_drain`_

* `admin_server_start_drain`_

* `admin_plugin_send_basic_msg`_

* `admin_storage_get_device_status`_

* `admin_storage_set_device_offline`_

* `show_registered_handlers`_

* `get_service_descriptor`_

.. _jsonapi-management-records:


Records
=======

.. _admin_lookup_records:


admin_lookup_records
--------------------

|method|

Description
~~~~~~~~~~~

Obtain  record(s) from TS.


Parameters
~~~~~~~~~~

* ``params``: A list of `RecordRequest`_ objects.


Result
~~~~~~

A list of `RecordResponse`_ . In case of any error obtaining the requested record, the `RecordErrorObject`_ |object| will be included.


Examples
~~~~~~~~

#. Request a configuration record, no errors:

   .. code-block:: json

      {
         "id":"b2bb16a5-135a-4c84-b0a7-8d31ebd82542",
         "jsonrpc":"2.0",
         "method":"admin_lookup_records",
         "params":[
            {
               "record_name":"proxy.config.log.rolling_interval_sec",
               "rec_types":[
                  "1",
                  "16"
               ]
            }
         ]
      }

Response:

   .. code-block:: json

      {
         "jsonrpc":"2.0",
         "result":{
            "recordList":[
               {
                  "record":{
                     "record_name":"proxy.config.log.rolling_interval_sec",
                     "record_type":"1",
                     "version":"0",
                     "raw_stat_block":"0",
                     "order":"410",
                     "config_meta":{
                        "access_type":"0",
                        "update_status":"0",
                        "update_type":"1",
                        "checktype":"1",
                        "source":"3",
                        "check_expr":"^[0-9]+$"
                     },
                     "record_class":"1",
                     "overridable":"false",
                     "data_type":"INT",
                     "current_value":"86400",
                     "default_value":"86400"
                  }
               }
            ]
         },
         "id":"b2bb16a5-135a-4c84-b0a7-8d31ebd82542"
      }


#. Request a configuration record, some errors coming back:

   .. code-block:: json

      {
         "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
         "jsonrpc": "2.0",
         "method": "admin_lookup_records",
         "params": [
            {
               "rec_types": [1],
               "record_name": "proxy.config.log.rolling_interval_sec"
            },
            {
               "record_name": "proxy.config.log.rolling_interv"
            }
         ]
      }


Response:

   .. code-block:: json

      {
         "jsonrpc":"2.0",
         "result":{
            "recordList":[
               {
                  "record":{
                     "record_name":"proxy.config.log.rolling_interval_sec",
                     "record_type":"1",
                     "version":"0",
                     "raw_stat_block":"0",
                     "order":"410",
                     "config_meta":{
                        "access_type":"0",
                        "update_status":"0",
                        "update_type":"1",
                        "checktype":"1",
                        "source":"3",
                        "check_expr":"^[0-9]+$"
                     },
                     "record_class":"1",
                     "overridable":"false",
                     "data_type":"INT",
                     "current_value":"86400",
                     "default_value":"86400"
                  }
               }
            ],
            "errorList":[
               {
                  "code":"2000",
                  "record_name":"proxy.config.log.rolling_interv"
               }
            ]
         },
         "id":"ded7018e-0720-11eb-abe2-001fc69cc946"
      }


Request using a `regex` instead of the full name.

.. note::

   Regex lookups use ``record_name_regex` and not ``record_name``. Check `RecordRequestObject`_ .

Examples
~~~~~~~~

#. Request a mix(config and stats) of records record using a regex, no errors:

   .. code-block:: json

      {
         "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
         "jsonrpc": "2.0",
         "method": "admin_lookup_records",
         "params": [
            {
                  "rec_types": [1],
                  "record_name_regex": "proxy.config.exec_thread.autoconfig.sca*"
            },
            {
                  "rec_types": [2],
                  "record_name_regex": "proxy.process.http.total_client_connections_ipv"
            }
         ]
      }


   Response:

   .. code-block:: json

      {
         "jsonrpc":"2.0",
         "result":{
            "recordList":[
               {
                  "record":{
                     "record_name":"proxy.config.exec_thread.autoconfig.scale",
                     "record_type":"2",
                     "version":"0",
                     "raw_stat_block":"0",
                     "order":"355",
                     "config_meta":{
                        "access_type":"2",
                        "update_status":"0",
                        "update_type":"2",
                        "checktype":"0",
                        "source":"3",
                        "check_expr":"null"
                     },
                     "record_class":"1",
                     "overridable":"false",
                     "data_type":"FLOAT",
                     "current_value":"1",
                     "default_value":"1"
                  }
               },
               {
                  "record":{
                     "record_name":"proxy.process.http.total_client_connections_ipv4",
                     "record_type":"4",
                     "version":"0",
                     "raw_stat_block":"9",
                     "order":"7",
                     "stat_meta":{
                        "persist_type":"1"
                     },
                     "record_class":"2",
                     "overridable":"false",
                     "data_type":"COUNTER",
                     "current_value":"0",
                     "default_value":"0"
                  }
               },
               {
                  "record":{
                     "record_name":"proxy.process.http.total_client_connections_ipv6",
                     "record_type":"4",
                     "version":"0",
                     "raw_stat_block":"10",
                     "order":"8",
                     "stat_meta":{
                        "persist_type":"1"
                     },
                     "record_class":"2",
                     "overridable":"false",
                     "data_type":"COUNTER",
                     "current_value":"0",
                     "default_value":"0"
                  }
               }
            ]
         },
         "id":"ded7018e-0720-11eb-abe2-001fc69cc946"
      }



#. Request a configuration record using a regex with some errors coming back:

   .. code-block:: json
      :linenos:

      {
         "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
         "jsonrpc": "2.0",
         "method": "admin_lookup_records",
         "params": [
            {
                  "rec_types": [1],
                  "record_name_regex": "proxy.config.exec_thread.autoconfig.sca*"
            },
            {
                  "rec_types": [987],
                  "record_name_regex": "proxy.process.http.total_client_connections_ipv"
            }
         ]
      }


   Note the invalid ``rec_type`` at line ``11``

   Response:

   .. code-block:: json
      :linenos:

      {
         "jsonrpc":"2.0",
         "result":{
            "recordList":[
               {
                  "record":{
                     "record_name":"proxy.config.exec_thread.autoconfig.scale",
                     "record_type":"2",
                     "version":"0",
                     "raw_stat_block":"0",
                     "order":"355",
                     "config_meta":{
                        "access_type":"2",
                        "update_status":"0",
                        "update_type":"2",
                        "checktype":"0",
                        "source":"3",
                        "check_expr":"null"
                     },
                     "record_class":"1",
                     "overridable":"false",
                     "data_type":"FLOAT",
                     "current_value":"1",
                     "default_value":"1"
                  }
               }
            ],
            "errorList":[
               {
                  "code":"2008",
                  "message":"Invalid request data provided"
               }
            ]
         },
         "id":"ded7018e-0720-11eb-abe2-001fc69cc946"
      }



   We get a valid record that was found based on the passed criteria, ``proxy.config.exec_thread.autoconfig.sca*`` and the ``rec_type`` *1*.
   Also we get a particular error that was caused by the invalid rec types ``987``


#. Request all config records

   .. code-block:: json
      :linenos:

      {
         "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
         "jsonrpc": "2.0",
         "method": "admin_lookup_records",
         "params": [{

            "record_name_regex": ".*",
            "rec_types": [1, 16]

         }]

      }



   *Note the `.*` regex we use to match them all. `rec_types` refer to ``RecT` , which in this case we are interested in `CONFIG`
   records and `LOCAL` records.*


   Response:

   All the configuration records. See `RecordResponse`_. The JSONRPC record handler is not limiting the response size.


.. note::

   It will retrieve ALL the configuration records, keep in mind that it might be a large response.



.. admin_config_set_records:

admin_config_set_records
------------------------

|method|

Description
~~~~~~~~~~~

Set a value for a particular record.



Parameters
~~~~~~~~~~

=================== ============= ================================================================================================================
Field               Type          Description
=================== ============= ================================================================================================================
``record_name``     |str|         The name of the record that wants to be updated.
``new_value``       |str|         The associated record value. Use always a |str| as the internal library will translate to the appropriate type.
=================== ============= ================================================================================================================


Example:

   .. code-block:: json

      [
        {
            "record_name": "proxy.config.exec_thread.autoconfig.scale",
            "record_value": "1.5"
        }
      ]


Result
~~~~~~

A list of updated record names. :ref:`RecordErrorObject-Enum` will be included.

Examples
~~~~~~~~


Request:

.. code-block:: json
   :linenos:

   {
      "id": "a32de1da-08be-11eb-9e1e-001fc69cc946",
      "jsonrpc": "2.0",
      "method": "admin_config_set_records",
      "params": [
         {
               "record_name": "proxy.config.exec_thread.autoconfig.scale",
               "record_value": "1.3"
         }
      ]
   }


Response:

.. code-block:: json
   :linenos:

   {
      "jsonrpc":"2.0",
      "result":[
         {
            "record_name":"proxy.config.exec_thread.autoconfig.scale"
         }
      ],
      "id":"a32de1da-08be-11eb-9e1e-001fc69cc946"
   }

.. _admin_config_reload:

admin_config_reload
-------------------

|method|

Description
~~~~~~~~~~~

Instruct |TS| to start the reloading process. You can find more information about config reload here(add link TBC)


Parameters
~~~~~~~~~~

* ``params``: Omitted

.. note::

   There is no need to add any parameters here.

Result
~~~~~~

A |str| with the success message indicating that the command was acknowledged by the server.

Examples
~~~~~~~~


Request:

.. code-block:: json
   :linenos:

   {
      "id": "89fc5aea-0740-11eb-82c0-001fc69cc946",
      "jsonrpc": "2.0",
      "method": "admin_config_reload"
   }


Response:

The response will contain the default `success_response`  or an :cpp:class:`RPCErrorCode`.


Validation:

You can request for the record `proxy.node.config.reconfigure_time` which will be updated with the time of the requested update.


.. _jsonrpc-api-management-metrics:

Metrics
=======

.. admin_clear_metrics_records:

admin_clear_metrics_records
---------------------------

|method|

Description
~~~~~~~~~~~

Clear one or more metric values. This API will take the incoming metric names and reset their associated value. The format for the incoming
request should follow the  `RecordRequest`_ .



Parameters
~~~~~~~~~~

* ``params``: A list of `RecordRequest`_ objects.

.. note::

   Only the ``rec_name`` will be used, if this is not provided, the API will report it back as part of the `RecordErrorObject`_ .


Result
~~~~~~

This api will only inform for errors during the metric update, all errors will be inside the  `RecordErrorObject`_ object.
Successfully metric updates will not report back to the client. So it can be assumed that the records were properly updated.

.. note::

   As per our internal API if the metric could not be updated because there is no change in the value, ie: it's already ``0`` this will be reported back to the client as part of the  `RecordErrorObject`_

Examples
~~~~~~~~


Request:

.. code-block:: json
   :linenos:

   {
      "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
      "jsonrpc": "2.0",
      "method": "admin_clear_metrics_records",
      "params": [
            {
               "record_name": "proxy.process.http.total_client_connections_ipv6"
            },
            {
               "record_name": "proxy.config.log.rolling_intervi_should_fail"
            }
      ]
   }


Response:

.. code-block:: json

   {
      "jsonrpc": "2.0",
      "result": {
         "errorList": [{
            "code": "2006",
            "record_name": "proxy.config.log.rolling_intervi_should_fail"
         }]
      },
      "id": "ded7018e-0720-11eb-abe2-001fc69cc946"
   }


.. admin_clear_all_metrics_records:

admin_clear_all_metrics_records
-------------------------------

|method|

Description
~~~~~~~~~~~

Clear all the metrics.


Parameters
~~~~~~~~~~

* ``params``: This can be Omitted


Result
~~~~~~

This api will only inform for errors during the metric update. Errors will be tracked down in the :cpp:class:`RPCErrorCode` field.

.. note::

   As per our internal API if the metric could not be updated because there is no change in the value, ie: it's already ``0`` this
   will be reported back to the client as part of the  `RecordErrorObject`_

Examples
~~~~~~~~

Request:

.. code-block:: json
   :linenos:

   {
      "id": "dod7018e-0720-11eb-abe2-001fc69cc997",
      "jsonrpc": "2.0",
      "method": "admin_clear_all_metrics_records"
   }



Response:

The response will contain the default `success_response`  or an :cpp:class:`RPCErrorCode`.


.. admin_host_set_status:

admin_host_set_status
---------------------

Description
~~~~~~~~~~~

A stat to track status is created for each host. The name is the host fqdn with a prefix of `proxy.process.host_status`. The value of
the stat is a string which is the serialized representation of the status. This contains the overall status and the status for each reason.
The stats may be viewed using the `admin_lookup_records`_ rpc api or through the ``stats_over_http`` endpoint.

Parameters
~~~~~~~~~~


=================== ============= =================================================================================================
Field               Type          Description
=================== ============= =================================================================================================
``operation``       |str|         The name of the record that is meant to be updated.
``host``            |arraystr|    A list of hosts that we want to interact with.
``reason``          |str|         Reason for the operation.
``time``            |str|         Set the duration of an operation to ``count`` seconds. A value of ``0`` means no duration, the
                                    condition persists until explicitly changed. The default is ``0`` if an operation requires a time
                                    and none is provided by this option. optional when ``op=up``
=================== ============= =================================================================================================

operation:

=================== ============= =================================================================================================
Field               Type          Description
=================== ============= =================================================================================================
``up``              |str|         Marks the listed hosts as ``up`` so that they will be available for use as a next hop parent. Use
                                    ``reason`` to mark the host reason code. The 'self_detect' is an internal reason code
                                    used by parent selection to mark down a parent when it is identified as itself and
``down``            |str|         Marks the listed hosts as down so that they will not be chosen as a next hop parent. If
                                    ``time`` is included the host is marked down for the specified number of seconds after
                                    which the host will automatically be marked up. A host is not marked up until all reason codes
                                    are cleared by marking up the host for the specified reason code.
=================== ============= =================================================================================================

reason:

=================== ============= =================================================================================================
Field               Type          Description
=================== ============= =================================================================================================
``active``          |str|         Set the active health check reason.
``local``           |str|         Set the local health check reason.
``manual``          |str|         Set the administrative reason. This is the default reason if a reason is needed and not provided
                                    by this option. If an invalid reason is provided ``manual`` will be defaulted.
=================== ============= =================================================================================================

Internally the reason can be ``self_detect`` if
:ts:cv:`proxy.config.http.parent_proxy.self_detect` is set to the value 2 (the default). This is
used to prevent parent selection from creating a loop by selecting itself as the upstream by
marking this reason as "down" in that case.

.. note::

   The up / down status values are independent, and a host is consider available if and only if
   all of the statuses are "up".


Result
~~~~~~

The response will contain the default `success_response`  or an :cpp:class:`RPCErrorCode`.


Examples
~~~~~~~~

Request:

.. code-block:: json
   :linenos:

   {
      "id": "c6d56fba-0cbd-11eb-926d-001fc69cc946",
      "jsonrpc": "2.0",
      "method": "admin_host_set_status",
      "params": {
         "operation": "up",
         "host": ["host1"],
         "reason": "manual",
         "time": "100"
      }
   }


Response:

.. code-block:: json
   :linenos:

   {
      "jsonrpc": "2.0",
      "result": "success",
      "id": "c6d56fba-0cbd-11eb-926d-001fc69cc946"
   }



Getting the host status
~~~~~~~~~~~~~~~~~~~~~~~

Get the current status of the specified hosts with respect to their use as targets for parent selection. This returns the same
information as the per host stat.

Although there is no specialized API that you can call to get a status from a particular host you can work away by pulling the right records.
For instance, the ``host1``  that we just set up can be easily fetch for a status:

Request:

.. code-block:: json
   :linenos:

   {
      "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
      "jsonrpc": "2.0",
      "method": "admin_lookup_records",
      "params": [{
            "record_name": "proxy.process.host_status.host1"
         }
      ]
   }

Response:

.. code-block:: json
   :linenos:

   {
      "jsonrpc": "2.0",
      "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
      "result": {
         "recordList": [{
            "record": {
               "record_name": "proxy.process.host_status.host1",
               "record_type": "3",
               "version": "0",
               "raw_stat_block": "0",
               "order": "1134",
               "stat_meta": {
                  "persist_type": "1"
               },
               "record_class": "2",
               "overridable": "false",
               "data_type": "STRING",
               "current_value": "HOST_STATUS_UP,ACTIVE:UP:0:0,LOCAL:UP:0:0,MANUAL:UP:0:0,SELF_DETECT:UP:0",
               "default_value": "HOST_STATUS_UP,ACTIVE:UP:0:0,LOCAL:UP:0:0,MANUAL:UP:0:0,SELF_DETECT:UP:0"
            }
         }]
      }
   }


.. admin_server_stop_drain:

admin_server_stop_drain
-----------------------

|method|

Description
~~~~~~~~~~~

Stop the drain requests process. Recover server from the drain mode

Parameters
~~~~~~~~~~

* ``params``: Omitted

Result
~~~~~~

The response will contain the default `success_response`  or an :cpp:class:`RPCErrorCode`.


Examples
~~~~~~~~

.. code-block:: json
   :linenos:

   {
      "id": "35f0b246-0cc4-11eb-9a79-001fc69cc946",
      "jsonrpc": "2.0",
      "method": "admin_server_stop_drain"
   }



.. admin_server_start_drain:

admin_server_start_drain
------------------------

|method|

Description
~~~~~~~~~~~

Drain TS requests.

Parameters
~~~~~~~~~~

======================= ============= ================================================================================================================
Field                   Type          Description
======================= ============= ================================================================================================================
``no_new_connections``  |str|         Wait for new connections down to threshold before starting draining, ``yes|true|1``. Not yet supported
======================= ============= ================================================================================================================


Result
~~~~~~

The response will contain the default `success_response`  or an :cpp:class:`RPCErrorCode`.

.. note::

   If the Server is already running a proper error will be sent back to the client.

Examples
~~~~~~~~

Request:

.. code-block:: json
   :linenos:

   {
      "id": "30700808-0cc4-11eb-b811-001fc69cc946",
      "jsonrpc": "2.0",
      "method": "admin_server_start_drain",
      "params": {
         "no_new_connections": "yes"
      }
   }


Response could be either:

#. The response will contain the default `success_response`

#. Response from a server that is already in drain mode.

.. code-block:: json
   :linenos:

   {
      "jsonrpc": "2.0",
      "id": "30700808-0cc4-11eb-b811-001fc69cc946",
      "error": {

         "code": 9,
         "message": "Error during execution",
         "data": [{

            "code": 3000,
            "message": "Server already draining."
            }]

      }

   }


.. admin_plugin_send_basic_msg:

admin_plugin_send_basic_msg
---------------------------

|method|

Description
~~~~~~~~~~~

Interact with plugins. Send a message to plugins. All plugins that have hooked the ``TSLifecycleHookID::TS_LIFECYCLE_MSG_HOOK`` will receive a callback for that hook.
The :arg:`tag` and :arg:`data` will be available to the plugin hook processing. It is expected that plugins will use :arg:`tag` to select relevant messages and determine the format of the :arg:`data`.

Parameters
~~~~~~~~~~

======================= ============= ================================================================================================================
Field                   Type          Description
======================= ============= ================================================================================================================
``tag``                 |str|         A tag name that will be read by the interested plugin
``data``                |str|         Data to be send, this is |optional|
======================= ============= ================================================================================================================


Result
~~~~~~

The response will contain the default `success_response`  or an :cpp:class:`RPCErrorCode`.

Examples
~~~~~~~~

   .. code-block:: json
      :linenos:

      {
         "id": "19095bf2-0d3b-11eb-b41a-001fc69cc946",
         "jsonrpc": "2.0",
         "method": "admin_plugin_send_basic_msg",
         "params": {
            "data": "ping",
            "tag": "pong"
         }
      }




.. admin_storage_get_device_status:

admin_storage_get_device_status
-------------------------------

|method|

Description
~~~~~~~~~~~

Show the storage configuration.

Parameters
~~~~~~~~~~

A list of |str| names for the specific storage we want to interact with. The storage identification used in the param list should match
exactly a path  specified in :file:`storage.config`.

Result
~~~~~~

cachedisk

======================= ============= =============================================================================================
Field                   Type          Description
======================= ============= =============================================================================================
``path``                |str|         Storage identification.  The storage is identified by :arg:`path` which must match exactly a
                                       path specified in :file:`storage.config`.
``status``              |str|         Disk status. ``online`` or ``offline``
``error_count``         |str|         Number of errors on the particular disk.
======================= ============= =============================================================================================


Examples
~~~~~~~~

Request:


.. code-block:: json
   :linenos:

   {
      "id": "8574edba-0d40-11eb-b2fb-001fc69cc946",
      "jsonrpc": "2.0",
      "method": "admin_storage_get_device_status",
      "params": ["/some/path/to/ats/trafficserver/cache.db", "/some/path/to/ats/var/to_remove/cache.db"]
   }


Response:

.. code-block:: json
   :linenos:

   {
      "jsonrpc": "2.0",
      "result": [{
            "cachedisk": {
               "path": "/some/path/to/ats/trafficserver/cache.db",
               "status": "online",
               "error_count": "0"
            }
         },
         {
            "cachedisk": {
               "path": "/some/path/to/ats/var/to_remove/cache.db",
               "status": "online",
               "error_count": "0"
            }
         }
      ],
      "id": "8574edba-0d40-11eb-b2fb-001fc69cc946"
   }



.. admin_storage_set_device_offline:

admin_storage_set_device_offline
--------------------------------

|method|

Description
~~~~~~~~~~~

Mark a cache storage device as ``offline``. The storage is identified by :arg:`path` which must match exactly a path specified in
:file:`storage.config`. This removes the storage from the cache and redirects requests that would have used this storage to
other storage. This has exactly the same effect as a disk failure for that storage. This does not persist across restarts of the
:program:`traffic_server` process.

Parameters
~~~~~~~~~~

A list of |str| names for the specific storage we want to interact with. The storage identification used in the param list should match
exactly a path  specified in :file:`storage.config`.

Result
~~~~~~

A list of |object| which the following fields:


=========================== ============= =============================================================================================
Field                       Type          Description
=========================== ============= =============================================================================================
``path``                    |str|         Storage identification.  The storage is identified by :arg:`path` which must match exactly a
                                          path specified in :file:`storage.config`.
``has_online_storage_left`` |str|         A flag indicating if there is any online storage left after this operation.
=========================== ============= =============================================================================================


Examples
~~~~~~~~

Request:

.. code-block:: json
   :linenos:

   {
      "id": "53dd8002-0d43-11eb-be00-001fc69cc946",
      "jsonrpc": "2.0",
      "method": "admin_storage_set_device_offline",
      "params": ["/some/path/to/ats/var/to_remove/cache.db"]
   }

Response:

.. code-block:: json
   :linenos:

   {
      "jsonrpc": "2.0",
      "result": [{
         "path": "/some/path/to/ats/var/to_remove/cache.db",
         "has_online_storage_left": "true"
      }],
      "id": "53dd8002-0d43-11eb-be00-001fc69cc946"
   }


.. show_registered_handlers:

show_registered_handlers
------------------------

|method|

Description
~~~~~~~~~~~

List all the registered RPC public handlers.

Parameters
~~~~~~~~~~

* ``params``: Omitted

Result
~~~~~~

An |object| with the following fields:


================== ============= ===========================================
Field              Type          Description
================== ============= ===========================================
``methods``        |str|         A list of exposed method handler names.
``notifications``  |str|         A list of exposed notification handler names.
================== ============= ===========================================


Examples
~~~~~~~~

Request:

.. code-block:: json
   :linenos:

   {
      "id": "f4477ac4-0d44-11eb-958d-001fc69cc946",
      "jsonrpc": "2.0",
      "method": "show_registered_handlers"
   }


Response:

.. code-block:: json
   :linenos:

   {
      "id": "f4477ac4-0d44-11eb-958d-001fc69cc946",
      "jsonrpc": "2.0",
      "result": {
         "methods": [
               "admin_host_set_status",
               "admin_server_stop_drain",
               "admin_server_start_drain",
               "admin_clear_metrics_records",
               "admin_clear_all_metrics_records",
               "admin_plugin_send_basic_msg",
               "admin_lookup_records",
               "admin_config_set_records",
               "admin_storage_get_device_status",
               "admin_storage_set_device_offline",
               "admin_config_reload",
               "show_registered_handlers"
         ],
         "notifications": []
      }
   }

.. get_service_descriptor:

get_service_descriptor
------------------------

|method|

Description
~~~~~~~~~~~

List and describe all the registered RPC handler.

Parameters
~~~~~~~~~~

* ``params``: Omitted

Result
~~~~~~

An |object| with the following fields:


``methods`` object

=============== ============= ===========================================
Field           Type          Description
=============== ============= ===========================================
``name``        |str|         Handler's name. Call name
``type``        |str|         Either 'method' or 'notification'
``provider``    |str|         Provider's information.
``schema``      |str|         A json-schema definition
=============== ============= ===========================================


Examples
~~~~~~~~

Request:

.. code-block:: json
   :linenos:

   {
      "id": "f4477ac4-0d44-11eb-958d-001fc69cc946",
      "jsonrpc": "2.0",
      "method": "get_service_descriptor"
   }


Response:

.. code-block:: json
   :linenos:

   {
   "jsonrpc":"2.0",
   "result":{
      "methods":[
         {
            "name":"admin_host_set_status",
            "type":"method",
            "provider":"Traffic Server JSONRPC 2.0 API",
            "schema":{
            }
         },
         {
            "name":"some_plugin_call",
            "type":"notification",
            "provider":"ABC Plugin's details.",
            "schema":{
            }
         }]
      }
   }
