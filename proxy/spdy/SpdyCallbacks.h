/** @file

  SpdyCallbacks.h

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

#ifndef __P_SPDY_CALLBACKS_H__
#define __P_SPDY_CALLBACKS_H__

#include <spdylay/spdylay.h>
#include "I_STLAllocator.h"
class SpdyClientSession;

void spdy_callbacks_init(spdylay_session_callbacks *callbacks);
void spdy_prepare_status_response_and_clean_request(SpdyClientSession *sm, int stream_id, const char *status);

/**
 * @functypedef
 *
 * Callback function invoked when |session| wants to send data to the
 * remote peer. The implementation of this function must send at most
 * |length| bytes of data stored in |data|. The |flags| is currently
 * not used and always 0. It must return the number of bytes sent if
 * it succeeds.  If it cannot send any single byte without blocking,
 * it must return :enum:`SPDYLAY_ERR_WOULDBLOCK`. For other errors, it
 * must return :enum:`SPDYLAY_ERR_CALLBACK_FAILURE`.
 */
ssize_t spdy_send_callback(spdylay_session *session, const uint8_t *data, size_t length, int flags, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when |session| wants to receive data from
 * the remote peer. The implementation of this function must read at
 * most |length| bytes of data and store it in |buf|. The |flags| is
 * currently not used and always 0. It must return the number of bytes
 * written in |buf| if it succeeds. If it cannot read any single byte
 * without blocking, it must return :enum:`SPDYLAY_ERR_WOULDBLOCK`. If
 * it gets EOF before it reads any single byte, it must return
 * :enum:`SPDYLAY_ERR_EOF`. For other errors, it must return
 * :enum:`SPDYLAY_ERR_CALLBACK_FAILURE`.
 */
ssize_t spdy_recv_callback(spdylay_session *session, uint8_t *buf, size_t length, int flags, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked by `spdylay_session_recv()` when a
 * control frame is received.
 */
void spdy_on_ctrl_recv_callback(spdylay_session *session, spdylay_frame_type type, spdylay_frame *frame, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked by `spdylay_session_recv()` when an
 * invalid control frame is received. The |status_code| is one of the
 * :enum:`spdylay_status_code` and indicates the error. When this
 * callback function is invoked, the library automatically submits
 * either RST_STREAM or GOAWAY frame.
 */
void spdy_on_invalid_ctrl_recv_callback(spdylay_session *session, spdylay_frame_type type, spdylay_frame *frame,
                                        uint32_t status_code, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when a chunk of data in DATA frame is
 * received. The |stream_id| is the stream ID this DATA frame belongs
 * to. The |flags| is the flags of DATA frame which this data chunk is
 * contained. ``(flags & SPDYLAY_DATA_FLAG_FIN) != 0`` does not
 * necessarily mean this chunk of data is the last one in the
 * stream. You should use :type:`spdylay_on_data_recv_callback` to
 * know all data frames are received.
 */
void spdy_on_data_chunk_recv_callback(spdylay_session *session, uint8_t flags, int32_t stream_id, const uint8_t *data, size_t len,
                                      void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when DATA frame is received. The actual
 * data it contains are received by
 * :type:`spdylay_on_data_chunk_recv_callback`.
 */
void spdy_on_data_recv_callback(spdylay_session *session, uint8_t flags, int32_t stream_id, int32_t length, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked before the control frame |frame| of type
 * |type| is sent. This may be useful, for example, to know the stream
 * ID of SYN_STREAM frame (see also
 * `spdylay_session_get_stream_user_data()`), which is not assigned
 * when it was queued.
 */
void spdy_before_ctrl_send_callback(spdylay_session *session, spdylay_frame_type type, spdylay_frame *frame, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked after the control frame |frame| of type
 * |type| is sent.
 */
void spdy_on_ctrl_send_callback(spdylay_session *session, spdylay_frame_type type, spdylay_frame *frame, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked after the control frame |frame| of type
 * |type| is not sent because of the error. The error is indicated by
 * the |error_code|, which is one of the values defined in
 * :type:`spdylay_error`.
 */
void spdy_on_ctrl_not_send_callback(spdylay_session *session, spdylay_frame_type type, spdylay_frame *frame, int error_code,
                                    void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked after DATA frame is sent.
 */
void spdy_on_data_send_callback(spdylay_session *session, uint8_t flags, int32_t stream_id, int32_t length, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when the stream |stream_id| is
 * closed. The reason of closure is indicated by the
 * |status_code|. The stream_user_data, which was specified in
 * `spdylay_submit_request()` or `spdylay_submit_syn_stream()`, is
 * still available in this function.
 */
void spdy_on_stream_close_callback(spdylay_session *session, int32_t stream_id, spdylay_status_code status_code, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when the library needs the cryptographic
 * proof that the client has possession of the private key associated
 * with the certificate for the given |origin|.  If called with
 * |prooflen| == 0, the implementation of this function must return
 * the length of the proof in bytes. If called with |prooflen| > 0,
 * write proof into |proof| exactly |prooflen| bytes and return 0.
 *
 * Because the client certificate vector has limited number of slots,
 * the application code may be required to pass the same proof more
 * than once.
 */
ssize_t spdy_get_credential_proof(spdylay_session *session, const spdylay_origin *origin, uint8_t *proof, size_t prooflen,
                                  void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when the library needs the length of the
 * client certificate chain for the given |origin|.  The
 * implementation of this function must return the length of the
 * client certificate chain.  If no client certificate is required for
 * the given |origin|, return 0.  If positive integer is returned,
 * :type:`spdylay_get_credential_proof` and
 * :type:`spdylay_get_credential_cert` callback functions will be used
 * to get the cryptographic proof and certificate respectively.
 */
ssize_t spdy_get_credential_ncerts(spdylay_session *session, const spdylay_origin *origin, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when the library needs the client
 * certificate for the given |origin|. The |idx| is the index of the
 * certificate chain and 0 means the leaf certificate of the chain.
 * If called with |certlen| == 0, the implementation of this function
 * must return the length of the certificate in bytes. If called with
 * |certlen| > 0, write certificate into |cert| exactly |certlen|
 * bytes and return 0.
 */
ssize_t spdy_get_credential_cert(spdylay_session *session, const spdylay_origin *origin, size_t idx, uint8_t *cert, size_t certlen,
                                 void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when the request from the remote peer is
 * received.  In other words, the frame with FIN flag set is received.
 * In HTTP, this means HTTP request, including request body, is fully
 * received.
 */
void spdy_on_request_recv_callback(spdylay_session *session, int32_t stream_id, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when the received control frame octets
 * could not be parsed correctly. The |type| indicates the type of
 * received control frame. The |head| is the pointer to the header of
 * the received frame. The |headlen| is the length of the
 * |head|. According to the SPDY spec, the |headlen| is always 8. In
 * other words, the |head| is the first 8 bytes of the received frame.
 * The |payload| is the pointer to the data portion of the received
 * frame.  The |payloadlen| is the length of the |payload|. This is
 * the data after the length field. The |error_code| is one of the
 * error code defined in :enum:`spdylay_error` and indicates the
 * error.
 */
void spdy_on_ctrl_recv_parse_error_callback(spdylay_session *session, spdylay_frame_type type, const uint8_t *head, size_t headlen,
                                            const uint8_t *payload, size_t payloadlen, int error_code, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when the received control frame type is
 * unknown. The |head| is the pointer to the header of the received
 * frame. The |headlen| is the length of the |head|. According to the
 * SPDY spec, the |headlen| is always 8. In other words, the |head| is
 * the first 8 bytes of the received frame.  The |payload| is the
 * pointer to the data portion of the received frame.  The
 * |payloadlen| is the length of the |payload|. This is the data after
 * the length field.
 */
void spdy_on_unknown_ctrl_recv_callback(spdylay_session *session, const uint8_t *head, size_t headlen, const uint8_t *payload,
                                        size_t payloadlen, void *user_data);

#endif
