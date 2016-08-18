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

/****************************************************************************

   load_http_hdr.cc

   Description:

   Opens a file with a dbx dump of an http hdr and prints it out


 ****************************************************************************/

#include "Marshal.h"
#include "MIME.h"
#include "HTTP.h"
#include "ts/Tokenizer.h"
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

enum hdr_type {
  UNKNOWN_HDR,
  REQUEST_HDR,
  RESPONSE_HDR,
  HTTP_INFO_HDR,
  RAW_MBUFFER,
};

void walk_mime_field(MIMEField f);
void walk_mstring(MBuffer *bufp, int32_t offset);
void walk_mbuffer(MBuffer *bufp);
void print_http_info_impl(HTTPInfo hi);
void print_http_hdr_impl(HTTPHdr h);

void
print_hdr(HTTPHdr to_print)
{
  char b[4096];
  int used, tmp, offset;
  int done;
  offset = 0;
  do {
    used = 0;
    tmp  = offset;
    done = to_print.print(b, 4095, &used, &tmp);
    offset += used;
    b[used] = '\0';
    printf("%s", b);
  } while (!done);
}

void
dump_hdr(char *mbuf, hdr_type h_type)
{
  HTTPHdr to_dump;
  HTTPInfo to_dump_info;

  if (h_type == RESPONSE_HDR) {
    to_dump.locate_resp(mbuf);
    print_hdr(to_dump);
  } else if (h_type == REQUEST_HDR) {
    to_dump.locate_req(mbuf);
    print_hdr(to_dump);
  } else {
    to_dump_info.locate(mbuf);

    to_dump = to_dump_info.request_get();
    if (to_dump.valid()) {
      print_hdr(to_dump);
    } else {
      fprintf(stderr, "HttpInfo request invalid\n");
    }

    to_dump = to_dump_info.response_get();

    if (to_dump.valid()) {
      print_hdr(to_dump);
    } else {
      fprintf(stderr, "HttpInfo response invalid\n");
    }
  }
}

void
load_buffer(int fd, hdr_type h_type)
{
  struct stat s_info;

  if (fstat(fd, &s_info) < 0) {
    fprintf(stderr, "Failed to stat data file : %s\n", strerror(errno));
    exit(1);
  }

  char *file_buf           = (char *)ats_malloc(sizeof(char) * (s_info.st_size + 1));
  file_buf[s_info.st_size] = '\0';

  // Read in the entire file
  int bytes_to_read = s_info.st_size;
  while (bytes_to_read > 0) {
    int done = read(fd, file_buf, bytes_to_read);

    if (done < 0) {
      fprintf(stderr, "Failed to read data file : %s\n", strerror(errno));
      exit(1);
    } else if (done == 0) {
      fprintf(stderr, "EOF encounted\n", strerror(errno));
      exit(1);
    }

    bytes_to_read -= done;
  }

  Tokenizer line_tok("\n");
  Tokenizer el_tok(" \t");

  int num_lines = line_tok.Initialize(file_buf);
  int num_el    = el_tok.Initialize(line_tok[0]);

  if (num_el < 3) {
    fprintf(stderr, "Corrupted data file\n");
    exit(1);
  }

  int mbuf_size, mbuf_length;
  if (sscanf(el_tok[2], "%x", &mbuf_length) != 1) {
    fprintf(stderr, "Corrupted data file\n");
    exit(1);
  }

  mbuf_size = MARSHAL_DEFAULT_SIZE;
  while (mbuf_size < mbuf_length) {
    mbuf_size *= 2;
  }

  char *mbuf     = (char *)ats_malloc(mbuf_size);
  int bytes_read = 0;
  int cur_line   = 0;

  while (cur_line < num_lines && bytes_read < mbuf_size) {
    int *cur_ptr;
    num_el = el_tok.Initialize(line_tok[cur_line]);

    int el;
    for (int i = 1; i < num_el; i++) {
      if (sscanf(el_tok[i], "%x", &el) != 1) {
        fprintf(stderr, "Corrupted data file\n");
        exit(1);
      }
      cur_ptr  = (int *)(mbuf + bytes_read);
      *cur_ptr = el;
      bytes_read += 4;
    }
    cur_line++;
  }

  if (bytes_read != mbuf_length) {
    fprintf(stderr, "Size mismatch: read %d  mbuf_length %d  mbuf_size %d\n", bytes_read, mbuf_length, mbuf_size);
    //      exit(1);
  }

  /*
     int raw_fd = open("foo", O_RDWR |O_CREAT | O_TRUNC );
     if (raw_fd > 0) {
     write(raw_fd, mbuf, mbuf_size);
     close(raw_fd);
     } else {
     perror("open: ");
     }
   */

  if (h_type == RAW_MBUFFER) {
    MBuffer m_buf_struct;
    memset(&m_buf_struct, 0, sizeof(MBuffer));
    mbuffer_set(&m_buf_struct, mbuf);
    m_buf_struct.m_ext_refcount = 1;
    m_buf_struct.m_size         = bytes_read;
    walk_mbuffer(&m_buf_struct);
  } else {
    dump_hdr(mbuf, h_type);
  }
}

int
main(int argc, const char *argv[])
{
  hdr_type h_type = UNKNOWN_HDR;

  http_init();
  if (argc != 3) {
    fprintf(stderr, "Usage: %s req|res <file>\n", argv[0]);
    exit(1);
  }

  if (strcasecmp(argv[1], "req") == 0) {
    h_type = REQUEST_HDR;
  } else if (strcasecmp(argv[1], "resp") == 0) {
    h_type = RESPONSE_HDR;
  } else if (strcasecmp(argv[1], "hinfo") == 0) {
    h_type = HTTP_INFO_HDR;
  } else if (strcasecmp(argv[1], "mbuf") == 0) {
    h_type = RAW_MBUFFER;
  } else {
    fprintf(stderr, "Usage: %s req|resp|hinfo|mbuf <file>\n", argv[0]);
    exit(1);
  }

  int fd = open(argv[2], O_RDONLY);

  if (fd < 0) {
    fprintf(stderr, "Could not open file %s : %s\n", argv[2], strerror(errno));
    exit(1);
  }
  load_buffer(fd, h_type);

  return 0;
}

/*********************************************************************
  Code for manual groking the mbuf objects
*******************************************************************/

// extern const char *marshal_strs[];

char *marshal_type_strs[] = {"EMPTY ", "OBJ   ", "STR   ", "URL   ", "URL_F ", "URL_H ", "M_VALS",
                             "M_FLD ", "M_HDR ", "H_HDR ", "H_REQ ", "H_RESP", "H_INFO"};

void
walk_mbuffer(MBuffer *bufp)
{
  int offset     = 3;
  int max_offset = (*bufp->m_length) / 4;

  do {
    MObjectImpl *mo = (MObjectImpl *)mbuffer_get_obj(bufp, offset);
    printf("offset %.3d  m_length %.2d  m_type %s     ", offset, (int)mo->m_length, marshal_type_strs[mo->type]);

    switch ((int)mo->type) {
    case MARSHAL_MIME_FIELD: {
      MIMEField f(bufp, offset);
      walk_mime_field(f);
      break;
    }
    case MARSHAL_STRING: {
      walk_mstring(bufp, offset);
      printf("\n");
      break;
    }
    case MARSHAL_HTTP_INFO: {
      HTTPInfo i(bufp, offset);
      print_http_info_impl(i);
      printf("\n");
      break;
    }
    case MARSHAL_HTTP_HEADER:
    case MARSHAL_HTTP_HEADER_REQ:
    case MARSHAL_HTTP_HEADER_RESP: {
      HTTPHdr h(bufp, offset);
      print_http_hdr_impl(h);
      printf("\n");
      break;
    }

    default:
      printf("\n");
    }

    offset += mo->m_length;
  } while (offset < max_offset);
}

void
walk_mstring(MBuffer *bufp, int32_t offset)
{
  int bufindex   = 0;
  int dumpoffset = 0;
  char fbuf[4096];

  //    int32_t soffset = field_offset;
  //    soffset <<= MARSHAL_ALIGNMENT;
  //    printf("offset: %d.  shifted field_offset: %d\n",
  //         field_offset, soffset);

  memset(fbuf, 0, 4096);
  mstring_print(bufp, offset, fbuf, 4095, &bufindex, &dumpoffset);

  printf("%s", fbuf);
}

void
walk_mime_field(MIMEField f)
{
  int bufindex   = 0;
  int dumpoffset = 0;
  char fbuf[4096];

  //    int32_t soffset = field_offset;
  //    soffset <<= MARSHAL_ALIGNMENT;
  //    printf("offset: %d.  shifted field_offset: %d\n",
  //         field_offset, soffset);

  MIMEFieldImpl *fi = MIMEFieldPtr(f.m_buffer, f.m_offset);
  memset(fbuf, 0, 4096);
  mime_field_print(f.m_buffer, f.m_offset, fbuf, 4095, &bufindex, &dumpoffset);

  printf("(%d,%d) [%d,%d,%d] %s", (int)fi->m_nvalues, (int)fi->m_flags, (int)fi->m_name_offset, (int)fi->m_values_offset,
         (int)fi->m_next_offset, fbuf);
}

void
walk_http_resp_hdr(HTTPHdr resp)
{
  HTTPHdrImpl *r = HTTPHdrPtr(resp.m_buffer, resp.m_offset);

  printf("Http Response Hdr\n");

  if (r->type != MARSHAL_HTTP_HEADER_RESP) {
    printf("Type match failed\n");
    return;
  }

  int16_t field_offset = r->m_fields_offset;

  while (field_offset != MARSHAL_NULL_OFFSET) {
    MIMEFieldImpl *f = MIMEFieldPtr(resp.m_buffer, field_offset);

    MIMEField field(resp.m_buffer, field_offset);
    walk_mime_field(field);

    field_offset = f->m_next_offset;
  }
}

void
walk_http_info(HTTPInfo hi)
{
  HTTPInfoImpl *info = HTTPInfoPtr(hi.m_buffer, hi.m_offset);

  printf("HttpInfo\n");

  if (info->type != MARSHAL_HTTP_INFO) {
    printf("Type match failed\n");
    return;
  }

  printf("id: %d  rid: %d\n", info->m_id, info->m_rid);
  printf("Request Sent Time %s", ctime(&info->m_request_sent_time));
  printf("Response Received Time %s\n", ctime(&info->m_response_received_time));
  printf("Request Offset: %d   Response Offset: %d", info->m_request_offset, info->m_response_offset);
}

void
print_http_info_impl(HTTPInfo hi)
{
  HTTPInfoImpl *info = HTTPInfoPtr(hi.m_buffer, hi.m_offset);

  if (info->type != MARSHAL_HTTP_INFO) {
    printf("Type match failed\n");
    return;
  }

  printf("id: %d  rid: %d  req: %d  resp: %d", info->m_id, info->m_rid, info->m_request_offset, info->m_response_offset);
}

void
print_http_hdr_impl(HTTPHdr h)
{
  HTTPHdrImpl *hdr = HTTPHdrPtr(h.m_buffer, h.m_offset);

  if (hdr->type == MARSHAL_HTTP_HEADER) {
    printf("fields: %d", (int)hdr->m_fields_offset);
    return;
  } else if (hdr->type == MARSHAL_HTTP_HEADER_REQ) {
    printf("method: %d  url: %d  fields: %d", (int)hdr->u.req.m_method_offset, (int)hdr->u.req.m_url_offset,
           (int)hdr->m_fields_offset);
  } else if (hdr->type == MARSHAL_HTTP_HEADER_RESP) {
    printf("status: %d  reason: %d  fields: %d", (int)hdr->u.resp.m_status, (int)hdr->u.resp.m_reason_offset,
           (int)hdr->m_fields_offset);
  } else {
    printf("Type match failed\n");
    return;
  }
}
