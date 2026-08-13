//  Licensed to the Apache Software Foundation (ASF) under one
//  or more contributor license agreements.  See the NOTICE file
//  distributed with this work for additional information
//  regarding copyright ownership.  The ASF licenses this file
//  to you under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance
//  with the License.  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

package main

import (
	"bytes"
	"fmt"
	"io"
	"net"
	"sync"

	"github.com/quic-go/quic-go/quicvarint"
)

const qmuxTransportParametersFrameType = 0x3f5153300d0a0d0a

const (
	qmuxStreamFrameType      = 0x08
	qmuxStreamFrameTypeMask  = 0xf8
	qmuxStreamFrameOffsetBit = 0x04
	qmuxStreamFrameLengthBit = 0x02
)

// qmuxCompatConn adapts qmux-go v0.2.0's initial transport-parameter frame to
// draft-ietf-quic-qmux-01. The release omits the transport-parameter frame's
// payload length and cannot parse STREAM frames without a LEN field, so the
// adapter normalizes both differences before qmux-go sees them.
type qmuxCompatConn struct {
	net.Conn
	readMutex  sync.Mutex
	readBuffer bytes.Buffer
	readReady  bool
	writeMutex sync.Mutex
	writeDone  bool
}

func newQMuxCompatConn(conn net.Conn) net.Conn {
	return &qmuxCompatConn{Conn: conn}
}

func (conn *qmuxCompatConn) Read(data []byte) (int, error) {
	conn.readMutex.Lock()
	defer conn.readMutex.Unlock()

	if conn.readBuffer.Len() == 0 {
		var adapted []byte
		var err error
		if conn.readReady {
			adapted, err = conn.readRecord()
		} else {
			adapted, err = conn.readInitialRecord()
			conn.readReady = true
		}
		if err != nil {
			return 0, err
		}
		conn.readBuffer.Write(adapted)
	}
	return conn.readBuffer.Read(data)
}

func (conn *qmuxCompatConn) readRecord() ([]byte, error) {
	reader := quicvarint.NewReader(conn.Conn)
	recordLength, err := quicvarint.Read(reader)
	if err != nil {
		return nil, err
	}
	payload := make([]byte, recordLength)
	if _, err := io.ReadFull(reader, payload); err != nil {
		return nil, err
	}
	return adaptStreamFrameRecord(payload)
}

func adaptStreamFrameRecord(payload []byte) ([]byte, error) {
	frameType, frameTypeBytes, err := quicvarint.Parse(payload)
	if err != nil {
		return nil, err
	}
	if frameType&qmuxStreamFrameTypeMask != qmuxStreamFrameType || frameType&qmuxStreamFrameLengthBit != 0 {
		return appendRecord(nil, payload), nil
	}

	headerEnd := frameTypeBytes
	_, streamIDBytes, err := quicvarint.Parse(payload[headerEnd:])
	if err != nil {
		return nil, err
	}
	headerEnd += streamIDBytes
	if frameType&qmuxStreamFrameOffsetBit != 0 {
		_, offsetBytes, err := quicvarint.Parse(payload[headerEnd:])
		if err != nil {
			return nil, err
		}
		headerEnd += offsetBytes
	}

	adaptedPayload := quicvarint.Append(nil, frameType|qmuxStreamFrameLengthBit)
	adaptedPayload = append(adaptedPayload, payload[frameTypeBytes:headerEnd]...)
	adaptedPayload = quicvarint.Append(adaptedPayload, uint64(len(payload)-headerEnd))
	adaptedPayload = append(adaptedPayload, payload[headerEnd:]...)
	return appendRecord(nil, adaptedPayload), nil
}

func (conn *qmuxCompatConn) readInitialRecord() ([]byte, error) {
	reader := quicvarint.NewReader(conn.Conn)
	recordLength, err := quicvarint.Read(reader)
	if err != nil {
		return nil, err
	}
	payload := make([]byte, recordLength)
	if _, err := io.ReadFull(reader, payload); err != nil {
		return nil, err
	}

	payloadReader := bytes.NewReader(payload)
	frameType, err := quicvarint.Read(quicvarint.NewReader(payloadReader))
	if err != nil {
		return nil, err
	}
	if frameType != qmuxTransportParametersFrameType {
		return nil, fmt.Errorf("expected initial QX_TRANSPORT_PARAMETERS frame, got %#x", frameType)
	}
	parameterLength, err := quicvarint.Read(quicvarint.NewReader(payloadReader))
	if err != nil {
		return nil, err
	}
	if parameterLength > uint64(payloadReader.Len()) {
		return nil, fmt.Errorf("QMux transport parameters length %d exceeds record payload", parameterLength)
	}

	parameterBytes := make([]byte, parameterLength)
	if _, err := io.ReadFull(payloadReader, parameterBytes); err != nil {
		return nil, err
	}
	transportParameters := quicvarint.Append(nil, frameType)
	transportParameters = append(transportParameters, parameterBytes...)
	adapted := appendRecord(nil, transportParameters)
	if payloadReader.Len() > 0 {
		remainingFrames := make([]byte, payloadReader.Len())
		if _, err := io.ReadFull(payloadReader, remainingFrames); err != nil {
			return nil, err
		}
		adapted = appendRecord(adapted, remainingFrames)
	}
	return adapted, nil
}

func (conn *qmuxCompatConn) Write(data []byte) (int, error) {
	conn.writeMutex.Lock()
	defer conn.writeMutex.Unlock()

	if conn.writeDone {
		return conn.Conn.Write(data)
	}
	adapted, err := adaptInitialWrite(data)
	if err != nil {
		return 0, err
	}
	if err := writeAll(conn.Conn, adapted); err != nil {
		return 0, err
	}
	conn.writeDone = true
	return len(data), nil
}

func adaptInitialWrite(data []byte) ([]byte, error) {
	recordLength, recordLengthBytes, err := quicvarint.Parse(data)
	if err != nil {
		return nil, err
	}
	if recordLength > uint64(len(data)-recordLengthBytes) {
		return nil, fmt.Errorf("incomplete initial QMux record")
	}
	payload := data[recordLengthBytes : recordLengthBytes+int(recordLength)]
	frameType, frameTypeBytes, err := quicvarint.Parse(payload)
	if err != nil {
		return nil, err
	}
	if frameType != qmuxTransportParametersFrameType {
		return nil, fmt.Errorf("expected initial QX_TRANSPORT_PARAMETERS frame, got %#x", frameType)
	}

	parameters := payload[frameTypeBytes:]
	adaptedPayload := quicvarint.Append(nil, frameType)
	adaptedPayload = quicvarint.Append(adaptedPayload, uint64(len(parameters)))
	adaptedPayload = append(adaptedPayload, parameters...)
	adapted := appendRecord(nil, adaptedPayload)
	return append(adapted, data[recordLengthBytes+int(recordLength):]...), nil
}

func appendRecord(destination []byte, payload []byte) []byte {
	destination = quicvarint.Append(destination, uint64(len(payload)))
	return append(destination, payload...)
}

func writeAll(writer io.Writer, data []byte) error {
	for len(data) > 0 {
		written, err := writer.Write(data)
		if err != nil {
			return err
		}
		if written == 0 {
			return io.ErrShortWrite
		}
		data = data[written:]
	}
	return nil
}
