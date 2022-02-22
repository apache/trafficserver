/**
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

/**
 * @file Headers.h
 */

#pragma once

#include "tscpp/api/noncopyable.h"
#include <string>

namespace atscppapi
{
struct HeadersState;
struct HeaderFieldIteratorState;
struct HeaderFieldValueIteratorState;
class Request;
class ClientRequest;
class Response;

/**
 * @brief A HeaderFieldName is a lightweight wrapper around a string that allows for case insensitive comparisons.
 * Because header field names must be case insensitive this allows easy case insensitive comparisons of names.
 *
 */
class HeaderFieldName
{
private:
  std::string name_;

public:
  typedef std::string::size_type size_type;

  /**
   * Constructor: build a new HeaderField name with the given string
   */
  HeaderFieldName(const std::string &name);

  /**
   * std::string conversion
   * @return a string which is this HeaderFieldName
   */
  operator std::string();

  /**
   * const char * conversion
   * @return a const char * which is this HeaderFieldName
   */
  operator const char *();

  /**
   * @return the length of this HeaderFieldName
   */
  size_type length();

  /**
   * @return a string which is this HeaderFieldName
   */
  std::string str();

  /**
   * @return a const char * which points to the name of this HeaderFieldName
   */
  const char *c_str();

  /**
   * Case insensitive comparison of this HeaderFieldName
   * @return true if the two strings are equal.
   */
  bool operator==(const char *field_name);

  /**
   * Case insensitive comparison of this HeaderFieldName
   * @return true if the two strings are equal.
   */
  bool operator==(const std::string &field_name);

  /**
   * Case insensitive comparison of this HeaderFieldName
   * @return true if the two strings are not equal.
   */
  bool operator!=(const char *field_name);

  /**
   * Case insensitive comparison of this HeaderFieldName
   * @return true if the two strings are not equal.
   */
  bool operator!=(const std::string &field_name);
};

class HeaderField;

/**
 * @brief A header field value iterator iterates through all header fields.
 */
class header_field_value_iterator
{
private:
  HeaderFieldValueIteratorState *state_;

public:
  using iterator_category = std::forward_iterator_tag;
  using value_type        = int;

  /**
   * Constructor for header_field_value_iterator, this shouldn't need to be used directly.
   * @param bufp the TSMBuffer associated with the headers
   * @param mloc the TSMLoc associated with the headers.
   * @param field_loc the TSMLoc associated with the field.
   * @param index the index of the value in the HeaderField
   * @warning This shouldn't need to be used directly!
   */
  header_field_value_iterator(void *bufp, void *hdr_loc, void *field_loc, int index);

  /**
   * Copy Constructor for header_field_value_iterator, this shouldn't need to be used directly.
   * @param header_field_value_iterator an existing iterator to copy
   * @warning This shouldn't need to be used directly!
   */
  header_field_value_iterator(const header_field_value_iterator &it);
  ~header_field_value_iterator();

  /**
   * Dereference this iterator into a string (get the value pointed to by this iterator)
   * @return a string which is the value pointed to by this iterator
   */
  std::string operator*();

  /**
   * Advance the iterator to the next header field value
   * @return a reference to a the next iterator
   */
  header_field_value_iterator &operator++();

  /**
   * Advance the current iterator to the next header field
   * @return a new iterator which points to the next element
   */
  header_field_value_iterator operator++(int);

  /**
   * Compare two iterators returning true if they are equal
   * @return true if two iterators are equal
   */
  bool operator==(const header_field_value_iterator &rhs) const;

  /**
   * Compare two iterators returning true if they are NOT equal
   * @return true if two iterators are not equal.
   */
  bool operator!=(const header_field_value_iterator &rhs) const;

  friend class HeaderField;
};

/**
 * @brief A header field iterator is an iterator that dereferences to a HeaderField.
 */
class header_field_iterator
{
private:
  HeaderFieldIteratorState *state_;
  header_field_iterator(void *hdr_buf, void *hdr_loc, void *field_loc);

public:
  using iterator_category = std::forward_iterator_tag;
  using value_type        = int;

  ~header_field_iterator();

  /**
   * Copy Constructor for header_field_iterator, this shouldn't need to be used directly.
   * @param header_field_iterator: for constructing the iterator.
   * @warning This shouldn't need to be used directly!
   */
  header_field_iterator(const header_field_iterator &it);

  header_field_iterator &operator=(const header_field_iterator &rhs);

  /**
   * Advance the iterator to the next header field
   * @return a reference to a the next iterator
   */
  header_field_iterator &operator++();

  /**
   * Advance the current iterator to the next header field
   * @return a new iterator which points to the next element
   */
  header_field_iterator operator++(int);

  /**
   * Advance the iterator to the next header field with the same name
   * @return a reference to a the next iterator
   */
  header_field_iterator &nextDup();

  /**
   * Comparison operator, compare two iterators
   * @return true if the two iterators point to the same HeaderField
   */
  bool operator==(const header_field_iterator &rhs) const;

  /**
   * Inequality Operator, compare two iterators
   * @return false if the two iterators are the same.
   */
  bool operator!=(const header_field_iterator &rhs) const;

  /**
   * Dereference an iterator
   * @return a HeaderField pointed to by this iterator
   */
  HeaderField operator*();

  friend class HeaderField;
  friend class Headers;
};

/**
 * @brief A HeaderField is a class that contains the header field name and all of the values.
 * @note You may have several HeaderFields with the same name for a given set of Headers.
 */
class HeaderField
{
private:
  header_field_iterator iter_;
  HeaderField(const header_field_iterator &iter) : iter_(iter) {}

public:
  typedef unsigned int size_type;
  typedef header_field_value_iterator iterator;

  ~HeaderField();

  /**
   * Get the size of the HeaderField, this is the number of values associated with the header field.
   * @return the number of values in this HeaderField.
   */
  size_type size() const;

  /**
   * Returns an iterator to the start of the values
   * @return an iterator point to the start of this header field's values
   */
  iterator begin();

  /**
   * Returns an iterator to the end of this header field's values.
   * @return an iterator that points beyond the last element of the header field's values.
   */
  iterator end();

  /**
   * Get the name of this HeaderField
   * @return a HeaderFieldName object.
   * @see HeaderFieldName which is a thin wrapper around a string to allow for case insensitive comparisons.
   */
  HeaderFieldName name() const;

  /**
   * Join all the values of this HeaderField into a single string separated by the join string.
   * @param an optional join string (defaults to ",")
   * @return a string which is all of the joined values of this HeaderField
   */
  std::string values(const char *join = ",");

  /**
   * Join all the values of this HeaderField into a single string separated by the join string.
   * @param a join string
   * @return a string which is all of the joined values of this HeaderField
   */
  std::string values(const std::string &join);

  /**
   * Join all the values of this HeaderField into a single string separated by the join string.
   * @param a optional join character
   * @return a string which is all of the joined values of this HeaderField
   */
  std::string values(const char join);

  /**
   * Check if this HeaderField is empty (no values).
   * @return a boolean value representing whether or not the header field has values.
   */
  bool empty();

  /**
   * Remove all values from this HeaderField
   * @return true if the clear succeeds.
   */
  bool clear();

  /**
   * Remove a single value from this HeaderField which is pointed to by the given iterator
   * @param an iterator which points to a single HeaderField value.
   * @return true if the header value was successfully erased.
   */
  bool erase(const iterator &it);

  /**
   * Append a value or a separated list of values to this HeaderField
   * @param a string containing the value(s).
   * @return true if the values was appended.
   */
  bool append(const std::string &value);

  /**
   * Append a value or a separated list of values to this HeaderField
   * @param a string containing the value.
   * @param the length of the value that is being appended.
   * @return true if the values was appended.
   */
  bool append(const char *value, int length = -1);

  /**
   * Change the name of this HeaderField to the given key.
   * @param string - the new name of the header field
   * @return true if the header field name was successfully changed.
   */
  bool setName(const std::string &str);

  /**
   * Compares the name of the header field only (not the values).
   * @note This is a case insensitive comparison.
   * @return true if the name is equal (case insensitive comparison).
   */
  bool operator==(const char *field_name) const;

  /** Compares the name of the header field only (not the values).
   * @note This is a case insensitive comparison.
   * @return true if the name is equal (case insensitive comparison).
   */
  bool operator==(const std::string &field_name) const;

  /** Compares the name of the header field only (not the values).
   * @note This is a case insensitive comparison.
   * @return true if the name is NOT equal (case insensitive comparison).
   */
  bool operator!=(const char *field_name) const;

  /** Compares the name of the header field only (not the values).
   * @note This is a case insensitive comparison.
   * @return true if the name is NOT equal (case insensitive comparison).
   */
  bool operator!=(const std::string &field_name) const;

  /**
   * Set the VALUES of the header field to the given value string
   * @param string - the values to set on the current header field
   * @return true if the value is successfully changed.
   */
  bool operator=(const std::string &field_value);

  /**
   * Set the VALUES of the header field to the given value string
   * @param the values to set on the current header field
   * @return true if the value is successfully changed.
   */
  bool operator=(const char *field_value);

  /**
   * Get the index value from this HeaderField
   * @param the index to retrieve a copy of
   * @return a copy of the string which is the index^th value in this HeaderField
   * @note as currently written this returns an immutable string, it will NOT allow you to
   * change the value.
   */
  std::string operator[](const int index);

  /**
   * Get a string representing all the header field's values
   * @return a string representation of all the header fields
   */
  friend std::ostream &operator<<(std::ostream &os, HeaderField &obj);

  /**
   * Get a string representing all the header field's values.
   * @return a string representation of all the header fields
   */
  std::string str();
  friend class Headers;
  friend class header_field_iterator;
};

/**
 * @brief Encapsulates the headers portion of a request or response.
 */
class Headers : noncopyable
{
public:
  /**
   * Constructor for Headers. This creates a "detached" headers, i.e., not tied to any transaction.
   */
  Headers();

  /**
   * Constructor for Headers, this shouldn't be used directly unless you're trying to mix the C++ and C apis.
   * @param bufp the TSMBuffer associated with the headers
   * @param mloc the TSMLoc associated with the headers.
   * @warning This should only be used if you're mixing the C++ and C apis, it will be constructed automatically if using only the
   * C++ api.
   */
  Headers(void *bufp, void *mloc);

  /**
   * Context Values are a way to share data between plugins, the key is always a string
   * and the value can be a std::shared_ptr to any type that extends ContextValue.
   * @param bufp the TSMBuffer associated with the headers
   * @param mloc the TSMLoc associated with the headers.
   * @warning This should only be used if you're mixing the C++ and C apis.
   */
  void reset(void *bufp, void *mloc);

  /**
   * Check if the header class has been initialized. If you're only using the C++ api then this
   * should always return true.
   * @return a boolean value representing whether or not the Headers have been initialized..
   */
  bool isInitialized() const;

  typedef unsigned int size_type;
  typedef header_field_iterator iterator;

  /**
   * Check if the headers are empty
   * @return a boolean value representing whether or not the Headers are empty
   */
  bool empty();

  /**
   * Get the size of the headers (the number of HeaderFields).
   * @return the number of HeaderFields
   */
  size_type size() const;

  /**
   * Get the size of the headers (the number of HeaderFields).
   * @return the number of HeaderFields
   */
  size_type lengthBytes() const;

  /**
   * Returns an iterator to the start of the HeaderFields.
   * @return an iterator point to the start of the header fields.
   */
  iterator begin();

  /**
   * Returns an iterator to the end of the HeaderFields (beyond the last element).
   * @return an iterator that points beyond the last element in the header fields.
   */
  iterator end();

  /**
   * Clears all headers.
   * @return true if the headers were successfully cleared.
   */
  bool clear();

  /**
   * Erase a single header field pointed to by an iterator
   * @param an iterator pointing to a header field.
   * @return true if the header field pointed to by the iterator was erased.
   */
  bool erase(const iterator &it);

  /**
   * Erase all headers whose name matches key (this is a case insensitive match).
   * @param the name of the header fields to erase
   * @return the number of elements erased that matched the key
   */
  size_type erase(const std::string &key);

  /**
   * Erase all headers whose name matches key (this is a case insensitive match).
   * @param the name of the header fields to erase
   * @param the length of the key (optional).
   * @return the number of elements erased that matched the key
   */
  size_type erase(const char *key, int length = -1);

  /**
   * Count all headers whose name matches key (this is a case insensitive match).
   * @param the name of the header fields to erase
   * @param the length of the key (optional).
   * @return the number of elements erased that matched the key
   */
  size_type count(const char *key, int length = -1);

  /**
   * Count all headers whose name matches key (this is a case insensitive match).
   * @param the name of the header fields to count
   * @return the number of elements whose name is key.
   */
  size_type count(const std::string &key);

  /**
   * Join all headers whos name is key with the optionally specified join string
   * @param the name of the headers to join into a single string
   * @param an optional join string (defaults to ",")
   * @return a string which is all of the joined values of headers matching key.
   */
  std::string values(const std::string &key, const char *join = ",");

  /**
   * Join all headers whos name is key with the optionally specified join string
   * @param the name of the headers to join into a single string
   * @param the string to join the fields with
   * @return a string which is all of the joined values of headers matching key.
   */
  std::string values(const std::string &key, const std::string &join);

  /**
   * Join all headers whos name is key with the optionally specified join character
   * @param the name of the headers to join into a single string
   * @param the join character.
   * @return a string which is all of the joined values of headers matching key.
   */
  std::string values(const std::string &key, const char join);

  /**
   * Returns the value at given position of header with given name
   * @param name of header
   * @param position of value
   * @return value
   */
  std::string value(const std::string &key, size_type index = 0);

  /**
   * Returns an iterator to the first HeaderField with the name key.
   * @param key the name of first header field ot find.
   * @return an iterator that points to the first matching header field with name key.
   */
  iterator find(const std::string &key);

  /**
   * Returns an iterator to the first HeaderField with the name key.
   * @param key the name of first header field ot find.
   * @param the length of the key specified (optional).
   * @return an iterator that points to the first matching header field with name key.
   */
  iterator find(const char *key, int length = -1);

  /**
   * Append a HeaderField.
   * @param key the name of the header field to append
   * @param value the value of the header field to append
   * @return an iterator to the appended header field or the end() iterator if append fails.
   */
  iterator append(const std::string &key, const std::string &value);

  /**
   * Erase all headers with name specified by key and then re-create the header with the specified values.
   * @param key the name of the header field to erased and re-created.
   * @param value the value of the header field to set.
   * @return an iterator to the new header field or the end() iterator if append fails.
   */
  iterator set(const std::string &key, const std::string &value);

  /**
   * Set the header field values to the value given, the header field will be created
   * if it does not already exist.
   * @param key the name of the header field whose value to set.
   * @return an iterator to the new header field or the end() iterator if append fails.
   * @warning This will create a header field with the given name, thus it should not be
   * used to check for the existence of a header, use count() or find() instead.
   */
  HeaderField operator[](const std::string &key);

  /**
   * Get a human-readable/log-friendly string representing all the header fields.
   * @return a string representation of all the header fields
   */
  std::string str();

  /**
   * Get a string that can be put on the wire
   * @return a string representation of all the header fields
   */
  std::string wireStr();

  friend std::ostream &operator<<(std::ostream &os, Headers &obj);

  ~Headers();

private:
  HeadersState *state_;
  friend class Request;
  friend class ClientRequest;
  friend class Response;
};
} // namespace atscppapi
