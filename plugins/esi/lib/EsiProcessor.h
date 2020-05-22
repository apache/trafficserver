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

#pragma once

#include <string>
#include <map>
#include "lib/ComponentBase.h"
#include "lib/StringHash.h"
#include "lib/DocNode.h"
#include "EsiParser.h"
#include "HttpDataFetcher.h"
#include "Variables.h"
#include "Expression.h"
#include "SpecialIncludeHandler.h"
#include "HandlerManager.h"

class EsiProcessor : private EsiLib::ComponentBase
{
public:
  enum UsePackedNodeResult {
    PROCESS_IN_PROGRESS,
    UNPACK_FAILURE,
    PROCESS_SUCCESS,
    PROCESS_FAILURE,
  };

  EsiProcessor(const char *debug_tag, const char *parser_debug_tag, const char *expression_debug_tag,
               EsiLib::ComponentBase::Debug debug_func, EsiLib::ComponentBase::Error error_func, HttpDataFetcher &fetcher,
               EsiLib::Variables &variables, const EsiLib::HandlerManager &handler_mgr);

  /** Initializes the processor with the context of the request to be processed */
  bool start();

  /** Adds data to be parsed */
  bool addParseData(const char *data, int data_len = -1);

  /** convenient alternative to method above */
  bool
  addParseData(const std::string &data)
  {
    return addParseData(data.data(), data.size());
  }

  /** Tells processor to wrap-up parsing; a final or the only piece of
   * data can be optionally provided */
  bool completeParse(const char *data = nullptr, int data_len = -1);

  /** convenient alternative to method above */
  bool
  completeParse(const std::string &data)
  {
    return completeParse(data.data(), data.size());
  }

  enum ReturnCode {
    FAILURE,
    SUCCESS,
    NEED_MORE_DATA,
  };

  /** Processes the currently parsed ESI document and returns processed
   * data in supplied out-parameters. Should be called when fetcher has
   * finished pulling in all data.
   *
   * try/attempt/except construct can generate new fetch requests
   * during processing. Only in such cases is NEED_MORE_DATA returned;
   * else FAILURE/SUCCESS is returned. */
  ReturnCode process(const char *&data, int &data_len);

  /** Process the ESI document and flush processed data as much as
   * possible. Can be called when fetcher hasn't finished pulling
   * in all data. */
  ReturnCode flush(std::string &data, int &overall_len);

  /** returns packed version of document currently being processed */
  void
  packNodeList(std::string &buffer, bool retain_buffer_data)
  {
    return _node_list.pack(buffer, retain_buffer_data);
  }

  /** Unpacks previously parsed and packed ESI node list from given
   * buffer and preps for process(); Unpacked document will point to
   * data in argument (i.e., caller space) */
  UsePackedNodeResult usePackedNodeList(const char *data, int data_len);

  /** convenient alternative to method above */
  inline UsePackedNodeResult
  usePackedNodeList(const std::string &data)
  {
    return usePackedNodeList(data.data(), data.size());
  }

  /** Clears state from current request */
  void stop();

  virtual ~EsiProcessor();

private:
  enum EXEC_STATE {
    STOPPED,
    PARSING,
    WAITING_TO_PROCESS,
    PROCESSED,
    ERRORED,
  };
  EXEC_STATE _curr_state;

  std::string _output_data;

  EsiParser _parser;
  EsiLib::DocNodeList _node_list;
  int _n_prescanned_nodes;
  int _n_processed_nodes;
  int _n_processed_try_nodes;
  int _overall_len;

  HttpDataFetcher &_fetcher;
  EsiLib::StringHash _include_urls;

  bool _usePackedNodeList;

  bool _processEsiNode(const EsiLib::DocNodeList::iterator &iter);
  bool _handleParseComplete();
  bool _getIncludeData(const EsiLib::DocNode &node, const char **content_ptr = nullptr, int *content_len_ptr = nullptr);
  DataStatus _getIncludeStatus(const EsiLib::DocNode &node);
  bool _handleVars(const char *str, int str_len);
  bool _handleChoose(EsiLib::DocNodeList::iterator &curr_node);
  bool _handleTry(EsiLib::DocNodeList::iterator &curr_node);
  bool _handleHtmlComment(const EsiLib::DocNodeList::iterator &curr_node);
  bool _preprocess(EsiLib::DocNodeList &node_list, int &n_prescanned_nodes);
  inline bool _isWhitespace(const char *data, int data_len);
  void _addFooterData();

  EsiLib::Variables &_esi_vars;
  EsiLib::Expression _expression;

  struct TryBlock {
    EsiLib::DocNodeList &attempt_nodes;
    EsiLib::DocNodeList &except_nodes;
    EsiLib::DocNodeList::iterator pos;
    TryBlock(EsiLib::DocNodeList &att, EsiLib::DocNodeList &exc, EsiLib::DocNodeList::iterator p)
      : attempt_nodes(att), except_nodes(exc), pos(p){};
  };
  typedef std::list<TryBlock> TryBlockList;
  TryBlockList _try_blocks;
  int _n_try_blocks_processed;

  const EsiLib::HandlerManager &_handler_manager;

  static const char *INCLUDE_DATA_ID_ATTR;

  typedef std::map<std::string, EsiLib::SpecialIncludeHandler *> IncludeHandlerMap;
  IncludeHandlerMap _include_handlers;

  void
  error()
  {
    stop();
    _curr_state = ERRORED;
  }
};
