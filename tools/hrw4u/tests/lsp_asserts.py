#
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
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""Centralized JSON-RPC response validation helpers for LSP tests."""

from __future__ import annotations

import json
from typing import Any


def assert_jsonrpc_ok(response: dict[str, Any] | None, expected_id: int | None = None, context: str = "") -> dict[str, Any]:
    """
    Assert that response is a valid JSON-RPC response.
    """
    if response is None:
        raise AssertionError(f"No response received{f' for {context}' if context else ''}")

    if not isinstance(response, dict):
        raise AssertionError(
            f"Response is not a dict{f' for {context}' if context else ''}\n"
            f"Got: {type(response).__name__}: {response}")

    if "jsonrpc" not in response:
        raise AssertionError(
            f"Missing 'jsonrpc' field{f' for {context}' if context else ''}\n"
            f"Response: {json.dumps(response, indent=2)}")

    if response["jsonrpc"] != "2.0":
        raise AssertionError(
            f"Invalid JSON-RPC version{f' for {context}' if context else ''}\n"
            f"Expected: '2.0', Got: {response['jsonrpc']}\n"
            f"Response: {json.dumps(response, indent=2)}")

    has_error = "error" in response
    has_result = "result" in response

    if has_error and has_result:
        raise AssertionError(
            f"Response has both 'error' and 'result'{f' for {context}' if context else ''}\n"
            f"Response: {json.dumps(response, indent=2)}")

    if has_error:
        error = response["error"]
        raise AssertionError(
            f"JSON-RPC error response{f' for {context}' if context else ''}\n"
            f"Error code: {error.get('code', 'unknown')}\n"
            f"Error message: {error.get('message', 'no message')}\n"
            f"Full response: {json.dumps(response, indent=2)}")

    if not has_result:
        raise AssertionError(
            f"Response missing 'result' field{f' for {context}' if context else ''}\n"
            f"Response: {json.dumps(response, indent=2)}")

    if expected_id is not None:
        response_id = response.get("id")
        if response_id != expected_id:
            raise AssertionError(
                f"Response ID mismatch{f' for {context}' if context else ''}\n"
                f"Expected ID: {expected_id}, Got: {response_id}\n"
                f"Response: {json.dumps(response, indent=2)}")

    return response


def assert_result_items(response: dict[str, Any] | None, context: str = "") -> list[dict[str, Any]]:
    """
    Assert response has valid result.items structure for completion responses.
    """
    validated_response = assert_jsonrpc_ok(response, context=context)
    result = validated_response["result"]

    if not isinstance(result, dict):
        raise AssertionError(
            f"Result is not a dict{f' for {context}' if context else ''}\n"
            f"Got: {type(result).__name__}: {result}\n"
            f"Full response: {json.dumps(validated_response, indent=2)}")

    if "items" not in result:
        raise AssertionError(
            f"Result missing 'items' field{f' for {context}' if context else ''}\n"
            f"Result keys: {list(result.keys())}\n"
            f"Full response: {json.dumps(validated_response, indent=2)}")

    items = result["items"]
    if not isinstance(items, list):
        raise AssertionError(
            f"Result.items is not a list{f' for {context}' if context else ''}\n"
            f"Got: {type(items).__name__}: {items}\n"
            f"Full response: {json.dumps(validated_response, indent=2)}")

    return items


def assert_hover_contents(response: dict[str, Any] | None, expected_content: str = "", context: str = "") -> str:
    """
    Assert response has valid hover contents and optionally check for expected content.
    """
    validated_response = assert_jsonrpc_ok(response, context=context)
    result = validated_response["result"]

    if not isinstance(result, dict):
        raise AssertionError(
            f"Result is not a dict{f' for {context}' if context else ''}\n"
            f"Got: {type(result).__name__}: {result}\n"
            f"Full response: {json.dumps(validated_response, indent=2)}")

    if "contents" not in result:
        raise AssertionError(
            f"Result missing 'contents' field{f' for {context}' if context else ''}\n"
            f"Result keys: {list(result.keys())}\n"
            f"Full response: {json.dumps(validated_response, indent=2)}")

    contents = result["contents"]
    if not isinstance(contents, dict):
        raise AssertionError(
            f"Result.contents is not a dict{f' for {context}' if context else ''}\n"
            f"Got: {type(contents).__name__}: {contents}\n"
            f"Full response: {json.dumps(validated_response, indent=2)}")

    if "value" not in contents:
        raise AssertionError(
            f"Contents missing 'value' field{f' for {context}' if context else ''}\n"
            f"Contents keys: {list(contents.keys())}\n"
            f"Full response: {json.dumps(validated_response, indent=2)}")

    value = contents["value"]
    if not isinstance(value, str):
        raise AssertionError(
            f"Contents.value is not a string{f' for {context}' if context else ''}\n"
            f"Got: {type(value).__name__}: {value}\n"
            f"Full response: {json.dumps(validated_response, indent=2)}")

    if expected_content and expected_content not in value:
        raise AssertionError(
            f"Expected content not found in hover{f' for {context}' if context else ''}\n"
            f"Expected: '{expected_content}'\n"
            f"Actual contents: '{value}'\n"
            f"Full response: {json.dumps(validated_response, indent=2)}")

    return value


def assert_completion_item_exists(items: list[dict[str, Any]], label: str, context: str = "") -> dict[str, Any]:
    """
    Assert that a completion item with the given label exists in the items list.
    """
    matching_items = [item for item in items if item.get("label") == label]

    if not matching_items:
        available_labels = [item.get("label", "<no label>") for item in items]
        raise AssertionError(
            f"Completion item '{label}' not found{f' for {context}' if context else ''}\n"
            f"Available labels: {available_labels}\n"
            f"Total items: {len(items)}")

    return matching_items[0]


def assert_completion_items_with_prefix(items: list[dict[str, Any]],
                                        prefix: str,
                                        should_exist: bool = True,
                                        context: str = "") -> list[dict[str, Any]]:
    """
    Assert completion items with a given prefix exist or don't exist.
    """
    matching_items = [item for item in items if item.get("label", "").startswith(prefix)]

    if should_exist and not matching_items:
        available_labels = [item.get("label", "<no label>") for item in items]
        raise AssertionError(
            f"No completion items with prefix '{prefix}' found{f' for {context}' if context else ''}\n"
            f"Available labels: {available_labels}\n"
            f"Total items: {len(items)}")

    if not should_exist and matching_items:
        matching_labels = [item.get("label", "<no label>") for item in matching_items]
        raise AssertionError(
            f"Unexpected completion items with prefix '{prefix}' found{f' for {context}' if context else ''}\n"
            f"Matching labels: {matching_labels}\n"
            f"These items should NOT exist")

    return matching_items
