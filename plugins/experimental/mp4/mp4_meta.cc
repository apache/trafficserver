/*
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

#include "mp4_meta.h"

static mp4_atom_handler mp4_atoms[] = {{"ftyp", &Mp4Meta::mp4_read_ftyp_atom},
                                       {"moov", &Mp4Meta::mp4_read_moov_atom},
                                       {"mdat", &Mp4Meta::mp4_read_mdat_atom},
                                       {nullptr, nullptr}};

static mp4_atom_handler mp4_moov_atoms[] = {{"mvhd", &Mp4Meta::mp4_read_mvhd_atom},
                                            {"trak", &Mp4Meta::mp4_read_trak_atom},
                                            {"cmov", &Mp4Meta::mp4_read_cmov_atom},
                                            {nullptr, nullptr}};

static mp4_atom_handler mp4_trak_atoms[] = {{"tkhd", &Mp4Meta::mp4_read_tkhd_atom},
                                            {"mdia", &Mp4Meta::mp4_read_mdia_atom},
                                            {nullptr, nullptr}};

static mp4_atom_handler mp4_mdia_atoms[] = {{"mdhd", &Mp4Meta::mp4_read_mdhd_atom},
                                            {"hdlr", &Mp4Meta::mp4_read_hdlr_atom},
                                            {"minf", &Mp4Meta::mp4_read_minf_atom},
                                            {nullptr, nullptr}};

static mp4_atom_handler mp4_minf_atoms[] = {{"vmhd", &Mp4Meta::mp4_read_vmhd_atom},
                                            {"smhd", &Mp4Meta::mp4_read_smhd_atom},
                                            {"dinf", &Mp4Meta::mp4_read_dinf_atom},
                                            {"stbl", &Mp4Meta::mp4_read_stbl_atom},
                                            {nullptr, nullptr}};

static mp4_atom_handler mp4_stbl_atoms[] = {
  {"stsd", &Mp4Meta::mp4_read_stsd_atom}, {"stts", &Mp4Meta::mp4_read_stts_atom}, {"stss", &Mp4Meta::mp4_read_stss_atom},
  {"ctts", &Mp4Meta::mp4_read_ctts_atom}, {"stsc", &Mp4Meta::mp4_read_stsc_atom}, {"stsz", &Mp4Meta::mp4_read_stsz_atom},
  {"stco", &Mp4Meta::mp4_read_stco_atom}, {"co64", &Mp4Meta::mp4_read_co64_atom}, {nullptr, nullptr}};

static void mp4_reader_set_32value(TSIOBufferReader readerp, int64_t offset, uint32_t n);
static void mp4_reader_set_64value(TSIOBufferReader readerp, int64_t offset, uint64_t n);
static uint32_t mp4_reader_get_32value(TSIOBufferReader readerp, int64_t offset);
static uint64_t mp4_reader_get_64value(TSIOBufferReader readerp, int64_t offset);
static int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length);

int
Mp4Meta::parse_meta(bool body_complete)
{
  int ret, rc;

  meta_avail = TSIOBufferReaderAvail(meta_reader);

  if (wait_next && wait_next <= meta_avail) {
    mp4_meta_consume(wait_next);
    wait_next = 0;
  }

  if (meta_avail < MP4_MIN_BUFFER_SIZE && !body_complete) {
    return 0;
  }

  ret = this->parse_root_atoms();

  if (ret < 0) {
    return -1;

  } else if (ret == 0) {
    if (body_complete) {
      return -1;

    } else {
      return 0;
    }
  }

  // generate new meta data
  rc = this->post_process_meta();
  if (rc != 0) {
    return -1;
  }

  return 1;
}

void
Mp4Meta::mp4_meta_consume(int64_t size)
{
  TSIOBufferReaderConsume(meta_reader, size);
  meta_avail -= size;
  passed += size;
}

int
Mp4Meta::post_process_meta()
{
  off_t start_offset, adjustment;
  uint32_t i, j;
  int64_t avail;
  Mp4Trak *trak;

  if (this->trak_num == 0) {
    return -1;
  }

  if (mdat_atom.buffer == nullptr) {
    return -1;
  }

  out_handle.buffer = TSIOBufferCreate();
  out_handle.reader = TSIOBufferReaderAlloc(out_handle.buffer);

  if (ftyp_atom.buffer) {
    TSIOBufferCopy(out_handle.buffer, ftyp_atom.reader, TSIOBufferReaderAvail(ftyp_atom.reader), 0);
  }

  if (moov_atom.buffer) {
    TSIOBufferCopy(out_handle.buffer, moov_atom.reader, TSIOBufferReaderAvail(moov_atom.reader), 0);
  }

  if (mvhd_atom.buffer) {
    avail = TSIOBufferReaderAvail(mvhd_atom.reader);
    TSIOBufferCopy(out_handle.buffer, mvhd_atom.reader, avail, 0);
    this->moov_size += avail;
  }

  start_offset = cl;

  for (i = 0; i < trak_num; i++) {
    trak = trak_vec[i];

    if (mp4_update_stts_atom(trak) != 0) {
      return -1;
    }

    if (mp4_update_stss_atom(trak) != 0) {
      return -1;
    }

    mp4_update_ctts_atom(trak);

    if (mp4_update_stsc_atom(trak) != 0) {
      return -1;
    }

    if (mp4_update_stsz_atom(trak) != 0) {
      return -1;
    }

    if (trak->atoms[MP4_CO64_DATA].buffer) {
      if (mp4_update_co64_atom(trak) != 0) {
        return -1;
      }

    } else if (mp4_update_stco_atom(trak) != 0) {
      return -1;
    }

    mp4_update_stbl_atom(trak);
    mp4_update_minf_atom(trak);
    trak->size += trak->mdhd_size;
    trak->size += trak->hdlr_size;
    mp4_update_mdia_atom(trak);
    trak->size += trak->tkhd_size;
    mp4_update_trak_atom(trak);

    this->moov_size += trak->size;

    if (start_offset > trak->start_offset) {
      start_offset = trak->start_offset;
    }

    for (j = 0; j <= MP4_LAST_ATOM; j++) {
      if (trak->atoms[j].buffer) {
        TSIOBufferCopy(out_handle.buffer, trak->atoms[j].reader, TSIOBufferReaderAvail(trak->atoms[j].reader), 0);
      }
    }

    mp4_update_tkhd_duration(trak);
    mp4_update_mdhd_duration(trak);
  }

  this->moov_size += 8;

  mp4_reader_set_32value(moov_atom.reader, 0, this->moov_size);
  this->content_length += this->moov_size;

  adjustment = this->ftyp_size + this->moov_size + mp4_update_mdat_atom(start_offset) - start_offset;

  TSIOBufferCopy(out_handle.buffer, mdat_atom.reader, TSIOBufferReaderAvail(mdat_atom.reader), 0);

  for (i = 0; i < trak_num; i++) {
    trak = trak_vec[i];

    if (trak->atoms[MP4_CO64_DATA].buffer) {
      mp4_adjust_co64_atom(trak, adjustment);

    } else {
      mp4_adjust_stco_atom(trak, adjustment);
    }
  }

  mp4_update_mvhd_duration();

  return 0;
}

/*
 * -1: error
 *  0: unfinished
 *  1: success.
 */
int
Mp4Meta::parse_root_atoms()
{
  int i, ret, rc;
  int64_t atom_size, atom_header_size, copied_size;
  char buf[64];
  char *atom_header, *atom_name;

  memset(buf, 0, sizeof(buf));

  for (;;) {
    if (meta_avail < (int64_t)sizeof(uint32_t)) {
      return 0;
    }

    copied_size = IOBufferReaderCopy(meta_reader, buf, sizeof(mp4_atom_header64));
    atom_size   = copied_size > 0 ? mp4_get_32value(buf) : 0;

    if (atom_size == 0) {
      return 1;
    }

    atom_header = buf;

    if (atom_size < (int64_t)sizeof(mp4_atom_header)) {
      if (atom_size == 1) {
        if (meta_avail < (int64_t)sizeof(mp4_atom_header64)) {
          return 0;
        }

      } else {
        return -1;
      }

      atom_size        = mp4_get_64value(atom_header + 8);
      atom_header_size = sizeof(mp4_atom_header64);

    } else { // regular atom

      if (meta_avail < (int64_t)sizeof(mp4_atom_header)) { // not enough for atom header
        return 0;
      }

      atom_header_size = sizeof(mp4_atom_header);
    }

    atom_name = atom_header + 4;

    if (atom_size + this->passed > this->cl) {
      return -1;
    }

    for (i = 0; mp4_atoms[i].name; i++) {
      if (memcmp(atom_name, mp4_atoms[i].name, 4) == 0) {
        ret = (this->*mp4_atoms[i].handler)(atom_header_size, atom_size - atom_header_size); // -1: error, 0: unfinished, 1: success

        if (ret <= 0) {
          return ret;

        } else if (meta_complete) { // success
          return 1;
        }

        goto next;
      }
    }

    // nonsignificant atom box
    rc = mp4_atom_next(atom_size, true); // 0: unfinished, 1: success
    if (rc == 0) {
      return rc;
    }

  next:
    continue;
  }

  return 1;
}

int
Mp4Meta::mp4_atom_next(int64_t atom_size, bool wait)
{
  if (meta_avail >= atom_size) {
    mp4_meta_consume(atom_size);
    return 1;
  }

  if (wait) {
    wait_next = atom_size;
    return 0;
  }

  return -1;
}

/*
 *  -1: error
 *   1: success
 */
int
Mp4Meta::mp4_read_atom(mp4_atom_handler *atom, int64_t size)
{
  int i, ret, rc;
  int64_t atom_size, atom_header_size, copied_size;
  char buf[32];
  char *atom_header, *atom_name;

  if (meta_avail < size) { // data insufficient, not reasonable for internal atom box.
    return -1;
  }

  while (size > 0) {
    if (meta_avail < (int64_t)sizeof(uint32_t)) { // data insufficient, not reasonable for internal atom box.
      return -1;
    }

    copied_size = IOBufferReaderCopy(meta_reader, buf, sizeof(mp4_atom_header64));
    atom_size   = copied_size > 0 ? mp4_get_32value(buf) : 0;

    if (atom_size == 0) {
      return 1;
    }

    atom_header = buf;

    if (atom_size < (int64_t)sizeof(mp4_atom_header)) {
      if (atom_size == 1) {
        if (meta_avail < (int64_t)sizeof(mp4_atom_header64)) {
          return -1;
        }

      } else {
        return -1;
      }

      atom_size        = mp4_get_64value(atom_header + 8);
      atom_header_size = sizeof(mp4_atom_header64);

    } else { // regular atom

      if (meta_avail < (int64_t)sizeof(mp4_atom_header)) {
        return -1;
      }

      atom_header_size = sizeof(mp4_atom_header);
    }

    atom_name = atom_header + 4;

    if (atom_size + this->passed > this->cl) {
      return -1;
    }

    for (i = 0; atom[i].name; i++) {
      if (memcmp(atom_name, atom[i].name, 4) == 0) {
        if (meta_avail < atom_size) {
          return -1;
        }

        ret = (this->*atom[i].handler)(atom_header_size, atom_size - atom_header_size); // -1: error, 0: success.

        if (ret < 0) {
          return ret;
        }

        goto next;
      }
    }

    // insignificant atom box
    rc = mp4_atom_next(atom_size, false);
    if (rc < 0) {
      return rc;
    }

  next:
    size -= atom_size;
    continue;
  }

  return 1;
}

int
Mp4Meta::mp4_read_ftyp_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int64_t atom_size;

  if (atom_data_size > MP4_MIN_BUFFER_SIZE) {
    return -1;
  }

  atom_size = atom_header_size + atom_data_size;

  if (meta_avail < atom_size) { // data insufficient, reasonable from the first level
    return 0;
  }

  ftyp_atom.buffer = TSIOBufferCreate();
  ftyp_atom.reader = TSIOBufferReaderAlloc(ftyp_atom.buffer);

  TSIOBufferCopy(ftyp_atom.buffer, meta_reader, atom_size, 0);
  mp4_meta_consume(atom_size);

  content_length = atom_size;
  ftyp_size      = atom_size;

  return 1;
}

int
Mp4Meta::mp4_read_moov_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int64_t atom_size;
  int ret;

  if (mdat_atom.buffer != nullptr) { // not reasonable for streaming media
    return -1;
  }

  atom_size = atom_header_size + atom_data_size;

  if (atom_data_size >= MP4_MAX_BUFFER_SIZE) {
    return -1;
  }

  if (meta_avail < atom_size) { // data insufficient, wait
    return 0;
  }

  moov_atom.buffer = TSIOBufferCreate();
  moov_atom.reader = TSIOBufferReaderAlloc(moov_atom.buffer);

  TSIOBufferCopy(moov_atom.buffer, meta_reader, atom_header_size, 0);
  mp4_meta_consume(atom_header_size);

  ret = mp4_read_atom(mp4_moov_atoms, atom_data_size);

  return ret;
}

int
Mp4Meta::mp4_read_mvhd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int64_t atom_size;
  uint32_t timescale;
  mp4_mvhd_atom *mvhd;
  mp4_mvhd64_atom mvhd64;

  if (sizeof(mp4_mvhd_atom) - 8 > (size_t)atom_data_size) {
    return -1;
  }

  memset(&mvhd64, 0, sizeof(mvhd64));
  IOBufferReaderCopy(meta_reader, &mvhd64, sizeof(mp4_mvhd64_atom));
  mvhd = (mp4_mvhd_atom *)&mvhd64;

  if (mvhd->version[0] == 0) {
    timescale = mp4_get_32value(mvhd->timescale);

  } else { // 64-bit duration
    timescale = mp4_get_32value(mvhd64.timescale);
  }

  this->timescale = timescale;

  atom_size = atom_header_size + atom_data_size;

  mvhd_atom.buffer = TSIOBufferCreate();
  mvhd_atom.reader = TSIOBufferReaderAlloc(mvhd_atom.buffer);

  TSIOBufferCopy(mvhd_atom.buffer, meta_reader, atom_size, 0);
  mp4_meta_consume(atom_size);

  return 1;
}

int
Mp4Meta::mp4_read_trak_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int rc;
  Mp4Trak *trak;

  if (trak_num >= MP4_MAX_TRAK_NUM - 1) {
    return -1;
  }

  trak                 = new Mp4Trak();
  trak_vec[trak_num++] = trak;

  trak->atoms[MP4_TRAK_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_TRAK_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_TRAK_ATOM].buffer);

  TSIOBufferCopy(trak->atoms[MP4_TRAK_ATOM].buffer, meta_reader, atom_header_size, 0);
  mp4_meta_consume(atom_header_size);

  rc = mp4_read_atom(mp4_trak_atoms, atom_data_size);

  return rc;
}

int Mp4Meta::mp4_read_cmov_atom(int64_t /*atom_header_size ATS_UNUSED */, int64_t /* atom_data_size ATS_UNUSED */)
{
  return -1;
}

int
Mp4Meta::mp4_read_tkhd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int64_t atom_size;
  Mp4Trak *trak;

  atom_size = atom_header_size + atom_data_size;

  trak            = trak_vec[trak_num - 1];
  trak->tkhd_size = atom_size;

  trak->atoms[MP4_TKHD_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_TKHD_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_TKHD_ATOM].buffer);

  TSIOBufferCopy(trak->atoms[MP4_TKHD_ATOM].buffer, meta_reader, atom_size, 0);
  mp4_meta_consume(atom_size);

  mp4_reader_set_32value(trak->atoms[MP4_TKHD_ATOM].reader, offsetof(mp4_tkhd_atom, size), atom_size);

  return 1;
}

int
Mp4Meta::mp4_read_mdia_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  Mp4Trak *trak;

  trak = trak_vec[trak_num - 1];

  trak->atoms[MP4_MDIA_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_MDIA_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_MDIA_ATOM].buffer);

  TSIOBufferCopy(trak->atoms[MP4_MDIA_ATOM].buffer, meta_reader, atom_header_size, 0);
  mp4_meta_consume(atom_header_size);

  return mp4_read_atom(mp4_mdia_atoms, atom_data_size);
}

int
Mp4Meta::mp4_read_mdhd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int64_t atom_size, duration;
  uint32_t ts;
  Mp4Trak *trak;
  mp4_mdhd_atom *mdhd;
  mp4_mdhd64_atom mdhd64;

  memset(&mdhd64, 0, sizeof(mdhd64));
  IOBufferReaderCopy(meta_reader, &mdhd64, sizeof(mp4_mdhd64_atom));
  mdhd = (mp4_mdhd_atom *)&mdhd64;

  if (mdhd->version[0] == 0) {
    ts       = mp4_get_32value(mdhd->timescale);
    duration = mp4_get_32value(mdhd->duration);

  } else {
    ts       = mp4_get_32value(mdhd64.timescale);
    duration = mp4_get_64value(mdhd64.duration);
  }

  atom_size = atom_header_size + atom_data_size;

  trak            = trak_vec[trak_num - 1];
  trak->mdhd_size = atom_size;
  trak->timescale = ts;
  trak->duration  = duration;

  trak->atoms[MP4_MDHD_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_MDHD_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_MDHD_ATOM].buffer);

  TSIOBufferCopy(trak->atoms[MP4_MDHD_ATOM].buffer, meta_reader, atom_size, 0);
  mp4_meta_consume(atom_size);

  mp4_reader_set_32value(trak->atoms[MP4_MDHD_ATOM].reader, offsetof(mp4_mdhd_atom, size), atom_size);

  return 1;
}

int
Mp4Meta::mp4_read_hdlr_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int64_t atom_size;
  Mp4Trak *trak;

  atom_size = atom_header_size + atom_data_size;

  trak            = trak_vec[trak_num - 1];
  trak->hdlr_size = atom_size;

  trak->atoms[MP4_HDLR_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_HDLR_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_HDLR_ATOM].buffer);

  TSIOBufferCopy(trak->atoms[MP4_HDLR_ATOM].buffer, meta_reader, atom_size, 0);
  mp4_meta_consume(atom_size);

  return 1;
}

int
Mp4Meta::mp4_read_minf_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  Mp4Trak *trak;

  trak = trak_vec[trak_num - 1];

  trak->atoms[MP4_MINF_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_MINF_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_MINF_ATOM].buffer);

  TSIOBufferCopy(trak->atoms[MP4_MINF_ATOM].buffer, meta_reader, atom_header_size, 0);
  mp4_meta_consume(atom_header_size);

  return mp4_read_atom(mp4_minf_atoms, atom_data_size);
}

int
Mp4Meta::mp4_read_vmhd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int64_t atom_size;
  Mp4Trak *trak;

  atom_size = atom_data_size + atom_header_size;

  trak = trak_vec[trak_num - 1];
  trak->vmhd_size += atom_size;

  trak->atoms[MP4_VMHD_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_VMHD_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_VMHD_ATOM].buffer);

  TSIOBufferCopy(trak->atoms[MP4_VMHD_ATOM].buffer, meta_reader, atom_size, 0);
  mp4_meta_consume(atom_size);

  return 1;
}

int
Mp4Meta::mp4_read_smhd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int64_t atom_size;
  Mp4Trak *trak;

  atom_size = atom_data_size + atom_header_size;

  trak = trak_vec[trak_num - 1];
  trak->smhd_size += atom_size;

  trak->atoms[MP4_SMHD_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_SMHD_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_SMHD_ATOM].buffer);

  TSIOBufferCopy(trak->atoms[MP4_SMHD_ATOM].buffer, meta_reader, atom_size, 0);
  mp4_meta_consume(atom_size);

  return 1;
}

int
Mp4Meta::mp4_read_dinf_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int64_t atom_size;
  Mp4Trak *trak;

  atom_size = atom_data_size + atom_header_size;

  trak = trak_vec[trak_num - 1];
  trak->dinf_size += atom_size;

  trak->atoms[MP4_DINF_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_DINF_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_DINF_ATOM].buffer);

  TSIOBufferCopy(trak->atoms[MP4_DINF_ATOM].buffer, meta_reader, atom_size, 0);
  mp4_meta_consume(atom_size);

  return 1;
}

int
Mp4Meta::mp4_read_stbl_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  Mp4Trak *trak;

  trak = trak_vec[trak_num - 1];

  trak->atoms[MP4_STBL_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_STBL_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STBL_ATOM].buffer);

  TSIOBufferCopy(trak->atoms[MP4_STBL_ATOM].buffer, meta_reader, atom_header_size, 0);
  mp4_meta_consume(atom_header_size);

  return mp4_read_atom(mp4_stbl_atoms, atom_data_size);
}

int
Mp4Meta::mp4_read_stsd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int64_t atom_size;
  Mp4Trak *trak;

  atom_size = atom_data_size + atom_header_size;

  trak = trak_vec[trak_num - 1];
  trak->size += atom_size;

  trak->atoms[MP4_STSD_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_STSD_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STSD_ATOM].buffer);

  TSIOBufferCopy(trak->atoms[MP4_STSD_ATOM].buffer, meta_reader, atom_size, 0);

  mp4_meta_consume(atom_size);

  return 1;
}

int
Mp4Meta::mp4_read_stts_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int32_t entries;
  int64_t esize, copied_size;
  mp4_stts_atom stts;
  Mp4Trak *trak;

  if (sizeof(mp4_stts_atom) - 8 > (size_t)atom_data_size) {
    return -1;
  }

  copied_size = IOBufferReaderCopy(meta_reader, &stts, sizeof(mp4_stts_atom));
  entries     = copied_size > 0 ? mp4_get_32value(stts.entries) : 0;
  esize       = entries * sizeof(mp4_stts_entry);

  if (sizeof(mp4_stts_atom) - 8 + esize > (size_t)atom_data_size) {
    return -1;
  }

  trak                         = trak_vec[trak_num - 1];
  trak->time_to_sample_entries = entries;

  trak->atoms[MP4_STTS_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_STTS_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STTS_ATOM].buffer);
  TSIOBufferCopy(trak->atoms[MP4_STTS_ATOM].buffer, meta_reader, sizeof(mp4_stts_atom), 0);

  trak->atoms[MP4_STTS_DATA].buffer = TSIOBufferCreate();
  trak->atoms[MP4_STTS_DATA].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STTS_DATA].buffer);
  TSIOBufferCopy(trak->atoms[MP4_STTS_DATA].buffer, meta_reader, esize, sizeof(mp4_stts_atom));

  mp4_meta_consume(atom_data_size + atom_header_size);

  return 1;
}

int
Mp4Meta::mp4_read_stss_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int32_t entries;
  int64_t esize, copied_size;
  mp4_stss_atom stss;
  Mp4Trak *trak;

  if (sizeof(mp4_stss_atom) - 8 > (size_t)atom_data_size) {
    return -1;
  }

  copied_size = IOBufferReaderCopy(meta_reader, &stss, sizeof(mp4_stss_atom));
  entries     = copied_size > 0 ? mp4_get_32value(stss.entries) : 0;
  esize       = entries * sizeof(int32_t);

  if (sizeof(mp4_stss_atom) - 8 + esize > (size_t)atom_data_size) {
    return -1;
  }

  trak                       = trak_vec[trak_num - 1];
  trak->sync_samples_entries = entries;

  trak->atoms[MP4_STSS_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_STSS_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STSS_ATOM].buffer);
  TSIOBufferCopy(trak->atoms[MP4_STSS_ATOM].buffer, meta_reader, sizeof(mp4_stss_atom), 0);

  trak->atoms[MP4_STSS_DATA].buffer = TSIOBufferCreate();
  trak->atoms[MP4_STSS_DATA].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STSS_DATA].buffer);
  TSIOBufferCopy(trak->atoms[MP4_STSS_DATA].buffer, meta_reader, esize, sizeof(mp4_stss_atom));

  mp4_meta_consume(atom_data_size + atom_header_size);

  return 1;
}

int
Mp4Meta::mp4_read_ctts_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int32_t entries;
  int64_t esize, copied_size;
  mp4_ctts_atom ctts;
  Mp4Trak *trak;

  if (sizeof(mp4_ctts_atom) - 8 > (size_t)atom_data_size) {
    return -1;
  }

  copied_size = IOBufferReaderCopy(meta_reader, &ctts, sizeof(mp4_ctts_atom));
  entries     = copied_size > 0 ? mp4_get_32value(ctts.entries) : 0;
  esize       = entries * sizeof(mp4_ctts_entry);

  if (sizeof(mp4_ctts_atom) - 8 + esize > (size_t)atom_data_size) {
    return -1;
  }

  trak                             = trak_vec[trak_num - 1];
  trak->composition_offset_entries = entries;

  trak->atoms[MP4_CTTS_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_CTTS_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_CTTS_ATOM].buffer);
  TSIOBufferCopy(trak->atoms[MP4_CTTS_ATOM].buffer, meta_reader, sizeof(mp4_ctts_atom), 0);

  trak->atoms[MP4_CTTS_DATA].buffer = TSIOBufferCreate();
  trak->atoms[MP4_CTTS_DATA].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_CTTS_DATA].buffer);
  TSIOBufferCopy(trak->atoms[MP4_CTTS_DATA].buffer, meta_reader, esize, sizeof(mp4_ctts_atom));

  mp4_meta_consume(atom_data_size + atom_header_size);

  return 1;
}

int
Mp4Meta::mp4_read_stsc_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int32_t entries;
  int64_t esize, copied_size;
  mp4_stsc_atom stsc;
  Mp4Trak *trak;

  if (sizeof(mp4_stsc_atom) - 8 > (size_t)atom_data_size) {
    return -1;
  }

  copied_size = IOBufferReaderCopy(meta_reader, &stsc, sizeof(mp4_stsc_atom));
  entries     = copied_size > 0 ? mp4_get_32value(stsc.entries) : 0;
  esize       = entries * sizeof(mp4_stsc_entry);

  if (sizeof(mp4_stsc_atom) - 8 + esize > (size_t)atom_data_size) {
    return -1;
  }

  trak                          = trak_vec[trak_num - 1];
  trak->sample_to_chunk_entries = entries;

  trak->atoms[MP4_STSC_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_STSC_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STSC_ATOM].buffer);
  TSIOBufferCopy(trak->atoms[MP4_STSC_ATOM].buffer, meta_reader, sizeof(mp4_stsc_atom), 0);

  trak->atoms[MP4_STSC_DATA].buffer = TSIOBufferCreate();
  trak->atoms[MP4_STSC_DATA].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STSC_DATA].buffer);
  TSIOBufferCopy(trak->atoms[MP4_STSC_DATA].buffer, meta_reader, esize, sizeof(mp4_stsc_atom));

  mp4_meta_consume(atom_data_size + atom_header_size);

  return 1;
}

int
Mp4Meta::mp4_read_stsz_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int32_t entries, size;
  int64_t esize, atom_size, copied_size;
  mp4_stsz_atom stsz;
  Mp4Trak *trak;

  if (sizeof(mp4_stsz_atom) - 8 > (size_t)atom_data_size) {
    return -1;
  }

  copied_size = IOBufferReaderCopy(meta_reader, &stsz, sizeof(mp4_stsz_atom));
  entries     = copied_size > 0 ? mp4_get_32value(stsz.entries) : 0;
  esize       = entries * sizeof(int32_t);

  trak = trak_vec[trak_num - 1];
  size = copied_size > 0 ? mp4_get_32value(stsz.uniform_size) : 0;

  trak->sample_sizes_entries = entries;

  trak->atoms[MP4_STSZ_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_STSZ_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STSZ_ATOM].buffer);
  TSIOBufferCopy(trak->atoms[MP4_STSZ_ATOM].buffer, meta_reader, sizeof(mp4_stsz_atom), 0);

  if (size == 0) {
    if (sizeof(mp4_stsz_atom) - 8 + esize > (size_t)atom_data_size) {
      return -1;
    }

    trak->atoms[MP4_STSZ_DATA].buffer = TSIOBufferCreate();
    trak->atoms[MP4_STSZ_DATA].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STSZ_DATA].buffer);
    TSIOBufferCopy(trak->atoms[MP4_STSZ_DATA].buffer, meta_reader, esize, sizeof(mp4_stsz_atom));

  } else {
    atom_size = atom_header_size + atom_data_size;
    trak->size += atom_size;
    mp4_reader_set_32value(trak->atoms[MP4_STSZ_ATOM].reader, 0, atom_size);
  }

  mp4_meta_consume(atom_data_size + atom_header_size);

  return 1;
}

int
Mp4Meta::mp4_read_stco_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int32_t entries;
  int64_t esize, copied_size;
  mp4_stco_atom stco;
  Mp4Trak *trak;

  if (sizeof(mp4_stco_atom) - 8 > (size_t)atom_data_size) {
    return -1;
  }

  copied_size = IOBufferReaderCopy(meta_reader, &stco, sizeof(mp4_stco_atom));
  entries     = copied_size > 0 ? mp4_get_32value(stco.entries) : 0;
  esize       = entries * sizeof(int32_t);

  if (sizeof(mp4_stco_atom) - 8 + esize > (size_t)atom_data_size) {
    return -1;
  }

  trak         = trak_vec[trak_num - 1];
  trak->chunks = entries;

  trak->atoms[MP4_STCO_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_STCO_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STCO_ATOM].buffer);
  TSIOBufferCopy(trak->atoms[MP4_STCO_ATOM].buffer, meta_reader, sizeof(mp4_stco_atom), 0);

  trak->atoms[MP4_STCO_DATA].buffer = TSIOBufferCreate();
  trak->atoms[MP4_STCO_DATA].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STCO_DATA].buffer);
  TSIOBufferCopy(trak->atoms[MP4_STCO_DATA].buffer, meta_reader, esize, sizeof(mp4_stco_atom));

  mp4_meta_consume(atom_data_size + atom_header_size);

  return 1;
}

int
Mp4Meta::mp4_read_co64_atom(int64_t atom_header_size, int64_t atom_data_size)
{
  int32_t entries;
  int64_t esize, copied_size;
  mp4_co64_atom co64;
  Mp4Trak *trak;

  if (sizeof(mp4_co64_atom) - 8 > (size_t)atom_data_size) {
    return -1;
  }

  copied_size = IOBufferReaderCopy(meta_reader, &co64, sizeof(mp4_co64_atom));
  entries     = copied_size > 0 ? mp4_get_32value(co64.entries) : 0;
  esize       = entries * sizeof(int64_t);

  if (sizeof(mp4_co64_atom) - 8 + esize > (size_t)atom_data_size) {
    return -1;
  }

  trak         = trak_vec[trak_num - 1];
  trak->chunks = entries;

  trak->atoms[MP4_CO64_ATOM].buffer = TSIOBufferCreate();
  trak->atoms[MP4_CO64_ATOM].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_CO64_ATOM].buffer);
  TSIOBufferCopy(trak->atoms[MP4_CO64_ATOM].buffer, meta_reader, sizeof(mp4_co64_atom), 0);

  trak->atoms[MP4_CO64_DATA].buffer = TSIOBufferCreate();
  trak->atoms[MP4_CO64_DATA].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_CO64_DATA].buffer);
  TSIOBufferCopy(trak->atoms[MP4_CO64_DATA].buffer, meta_reader, esize, sizeof(mp4_co64_atom));

  mp4_meta_consume(atom_data_size + atom_header_size);

  return 1;
}

int Mp4Meta::mp4_read_mdat_atom(int64_t /* atom_header_size ATS_UNUSED */, int64_t /* atom_data_size ATS_UNUSED */)
{
  mdat_atom.buffer = TSIOBufferCreate();
  mdat_atom.reader = TSIOBufferReaderAlloc(mdat_atom.buffer);

  meta_complete = true;
  return 1;
}

int
Mp4Meta::mp4_update_stts_atom(Mp4Trak *trak)
{
  uint32_t i, entries, count, duration, pass;
  uint32_t start_sample, left, start_count;
  uint32_t key_sample, old_sample;
  uint64_t start_time, sum;
  int64_t atom_size;
  TSIOBufferReader readerp;

  if (trak->atoms[MP4_STTS_DATA].buffer == nullptr) {
    return -1;
  }

  sum = start_count = 0;

  entries    = trak->time_to_sample_entries;
  start_time = this->start * trak->timescale / 1000;
  if (this->rs > 0) {
    start_time = (uint64_t)(this->rs * trak->timescale / 1000);
  }

  start_sample = 0;
  readerp      = TSIOBufferReaderClone(trak->atoms[MP4_STTS_DATA].reader);

  for (i = 0; i < entries; i++) {
    duration = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, duration));
    count    = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, count));

    if (start_time < (uint64_t)count * duration) {
      pass = (uint32_t)(start_time / duration);
      start_sample += pass;

      goto found;
    }

    start_sample += count;
    start_time -= (uint64_t)count * duration;
    TSIOBufferReaderConsume(readerp, sizeof(mp4_stts_entry));
  }

found:

  TSIOBufferReaderFree(readerp);

  old_sample = start_sample;
  key_sample = this->mp4_find_key_sample(start_sample, trak); // find the last key frame before start_sample

  if (old_sample != key_sample) {
    start_sample = key_sample - 1;
  }

  readerp = TSIOBufferReaderClone(trak->atoms[MP4_STTS_DATA].reader);

  trak->start_sample = start_sample;

  for (i = 0; i < entries; i++) {
    duration = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, duration));
    count    = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, count));

    if (start_sample < count) {
      count -= start_sample;
      mp4_reader_set_32value(readerp, offsetof(mp4_stts_entry, count), count);

      sum += (uint64_t)start_sample * duration;
      break;
    }

    start_sample -= count;
    sum += (uint64_t)count * duration;

    TSIOBufferReaderConsume(readerp, sizeof(mp4_stts_entry));
  }

  if (this->rs == 0) {
    this->rs = ((double)sum / trak->duration) * ((double)trak->duration / trak->timescale) * 1000;
  }

  left = entries - i;

  atom_size = sizeof(mp4_stts_atom) + left * sizeof(mp4_stts_entry);
  trak->size += atom_size;

  mp4_reader_set_32value(trak->atoms[MP4_STTS_ATOM].reader, offsetof(mp4_stts_atom, size), atom_size);
  mp4_reader_set_32value(trak->atoms[MP4_STTS_ATOM].reader, offsetof(mp4_stts_atom, entries), left);

  TSIOBufferReaderConsume(trak->atoms[MP4_STTS_DATA].reader, i * sizeof(mp4_stts_entry));
  TSIOBufferReaderFree(readerp);

  return 0;
}

int
Mp4Meta::mp4_update_stss_atom(Mp4Trak *trak)
{
  int64_t atom_size;
  uint32_t i, j, entries, sample, start_sample, left;
  TSIOBufferReader readerp;

  if (trak->atoms[MP4_STSS_DATA].buffer == nullptr) {
    return 0;
  }

  readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSS_DATA].reader);

  start_sample = trak->start_sample + 1;
  entries      = trak->sync_samples_entries;

  for (i = 0; i < entries; i++) {
    sample = (uint32_t)mp4_reader_get_32value(readerp, 0);

    if (sample >= start_sample) {
      goto found;
    }

    TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
  }

  TSIOBufferReaderFree(readerp);
  return -1;

found:

  left = entries - i;

  start_sample = trak->start_sample;
  for (j = 0; j < left; j++) {
    sample = (uint32_t)mp4_reader_get_32value(readerp, 0);
    sample -= start_sample;
    mp4_reader_set_32value(readerp, 0, sample);
    TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
  }

  atom_size = sizeof(mp4_stss_atom) + left * sizeof(uint32_t);
  trak->size += atom_size;

  mp4_reader_set_32value(trak->atoms[MP4_STSS_ATOM].reader, offsetof(mp4_stss_atom, size), atom_size);

  mp4_reader_set_32value(trak->atoms[MP4_STSS_ATOM].reader, offsetof(mp4_stss_atom, entries), left);

  TSIOBufferReaderConsume(trak->atoms[MP4_STSS_DATA].reader, i * sizeof(uint32_t));
  TSIOBufferReaderFree(readerp);

  return 0;
}

int
Mp4Meta::mp4_update_ctts_atom(Mp4Trak *trak)
{
  int64_t atom_size;
  uint32_t i, entries, start_sample, left;
  uint32_t count;
  TSIOBufferReader readerp;

  if (trak->atoms[MP4_CTTS_DATA].buffer == nullptr) {
    return 0;
  }

  readerp = TSIOBufferReaderClone(trak->atoms[MP4_CTTS_DATA].reader);

  start_sample = trak->start_sample + 1;
  entries      = trak->composition_offset_entries;

  for (i = 0; i < entries; i++) {
    count = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_ctts_entry, count));

    if (start_sample <= count) {
      count -= (start_sample - 1);
      mp4_reader_set_32value(readerp, offsetof(mp4_ctts_entry, count), count);
      goto found;
    }

    start_sample -= count;
    TSIOBufferReaderConsume(readerp, sizeof(mp4_ctts_entry));
  }

  if (trak->atoms[MP4_CTTS_ATOM].reader) {
    TSIOBufferReaderFree(trak->atoms[MP4_CTTS_ATOM].reader);
    TSIOBufferDestroy(trak->atoms[MP4_CTTS_ATOM].buffer);

    trak->atoms[MP4_CTTS_ATOM].buffer = nullptr;
    trak->atoms[MP4_CTTS_ATOM].reader = nullptr;
  }

  TSIOBufferReaderFree(trak->atoms[MP4_CTTS_DATA].reader);
  TSIOBufferDestroy(trak->atoms[MP4_CTTS_DATA].buffer);

  trak->atoms[MP4_CTTS_DATA].reader = nullptr;
  trak->atoms[MP4_CTTS_DATA].buffer = nullptr;

  TSIOBufferReaderFree(readerp);
  return 0;

found:

  left      = entries - i;
  atom_size = sizeof(mp4_ctts_atom) + left * sizeof(mp4_ctts_entry);
  trak->size += atom_size;

  mp4_reader_set_32value(trak->atoms[MP4_CTTS_ATOM].reader, offsetof(mp4_ctts_atom, size), atom_size);
  mp4_reader_set_32value(trak->atoms[MP4_CTTS_ATOM].reader, offsetof(mp4_ctts_atom, entries), left);

  TSIOBufferReaderConsume(trak->atoms[MP4_CTTS_DATA].reader, i * sizeof(mp4_ctts_entry));
  TSIOBufferReaderFree(readerp);

  return 0;
}

int
Mp4Meta::mp4_update_stsc_atom(Mp4Trak *trak)
{
  int64_t atom_size;
  uint32_t i, entries, samples, start_sample;
  uint32_t chunk, next_chunk, n, id, j;
  mp4_stsc_entry *first;
  TSIOBufferReader readerp;

  if (trak->atoms[MP4_STSC_DATA].buffer == nullptr) {
    return -1;
  }

  if (trak->sample_to_chunk_entries == 0) {
    return -1;
  }

  start_sample = (uint32_t)trak->start_sample;

  readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSC_DATA].reader);

  chunk   = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, chunk));
  samples = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, samples));
  id      = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, id));

  TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));

  for (i = 1; i < trak->sample_to_chunk_entries; i++) {
    next_chunk = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, chunk));

    n = (next_chunk - chunk) * samples;

    if (start_sample <= n) {
      goto found;
    }

    start_sample -= n;

    chunk   = next_chunk;
    samples = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, samples));
    id      = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, id));

    TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));
  }

  next_chunk = trak->chunks;

  n = (next_chunk - chunk) * samples;
  if (start_sample > n) {
    TSIOBufferReaderFree(readerp);
    return -1;
  }

found:

  TSIOBufferReaderFree(readerp);

  entries = trak->sample_to_chunk_entries - i + 1;
  if (samples == 0) {
    return -1;
  }

  readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSC_DATA].reader);
  TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry) * (i - 1));

  trak->start_chunk = chunk - 1;
  trak->start_chunk += start_sample / samples;
  trak->chunk_samples = start_sample % samples;

  atom_size = sizeof(mp4_stsc_atom) + entries * sizeof(mp4_stsc_entry);

  mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, chunk), 1);

  if (trak->chunk_samples && next_chunk - trak->start_chunk == 2) {
    mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, samples), samples - trak->chunk_samples);

  } else if (trak->chunk_samples) {
    first = &trak->stsc_chunk_entry;
    mp4_set_32value(first->chunk, 1);
    mp4_set_32value(first->samples, samples - trak->chunk_samples);
    mp4_set_32value(first->id, id);

    trak->atoms[MP4_STSC_CHUNK].buffer = TSIOBufferSizedCreate(TS_IOBUFFER_SIZE_INDEX_128);
    trak->atoms[MP4_STSC_CHUNK].reader = TSIOBufferReaderAlloc(trak->atoms[MP4_STSC_CHUNK].buffer);
    TSIOBufferWrite(trak->atoms[MP4_STSC_CHUNK].buffer, first, sizeof(mp4_stsc_entry));

    mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, chunk), 2);

    entries++;
    atom_size += sizeof(mp4_stsc_entry);
  }

  TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));

  for (j = i; j < trak->sample_to_chunk_entries; j++) {
    chunk = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, chunk));
    chunk -= trak->start_chunk;
    mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, chunk), chunk);
    TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));
  }

  trak->size += atom_size;

  mp4_reader_set_32value(trak->atoms[MP4_STSC_ATOM].reader, offsetof(mp4_stsc_atom, size), atom_size);
  mp4_reader_set_32value(trak->atoms[MP4_STSC_ATOM].reader, offsetof(mp4_stsc_atom, entries), entries);

  TSIOBufferReaderConsume(trak->atoms[MP4_STSC_DATA].reader, (i - 1) * sizeof(mp4_stsc_entry));
  TSIOBufferReaderFree(readerp);

  return 0;
}

int
Mp4Meta::mp4_update_stsz_atom(Mp4Trak *trak)
{
  uint32_t i;
  int64_t atom_size, avail;
  uint32_t pass;
  TSIOBufferReader readerp;

  if (trak->atoms[MP4_STSZ_DATA].buffer == nullptr) {
    return 0;
  }

  if (trak->start_sample > trak->sample_sizes_entries) {
    return -1;
  }

  readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSZ_DATA].reader);
  avail   = TSIOBufferReaderAvail(readerp);

  pass = trak->start_sample * sizeof(uint32_t);

  TSIOBufferReaderConsume(readerp, pass - sizeof(uint32_t) * (trak->chunk_samples));

  for (i = 0; i < trak->chunk_samples; i++) {
    trak->chunk_samples_size += mp4_reader_get_32value(readerp, 0);
    TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
  }

  atom_size = sizeof(mp4_stsz_atom) + avail - pass;
  trak->size += atom_size;

  mp4_reader_set_32value(trak->atoms[MP4_STSZ_ATOM].reader, offsetof(mp4_stsz_atom, size), atom_size);
  mp4_reader_set_32value(trak->atoms[MP4_STSZ_ATOM].reader, offsetof(mp4_stsz_atom, entries),
                         trak->sample_sizes_entries - trak->start_sample);

  TSIOBufferReaderConsume(trak->atoms[MP4_STSZ_DATA].reader, pass);
  TSIOBufferReaderFree(readerp);

  return 0;
}

int
Mp4Meta::mp4_update_co64_atom(Mp4Trak *trak)
{
  int64_t atom_size, avail, pass;
  TSIOBufferReader readerp;

  if (trak->atoms[MP4_CO64_DATA].buffer == nullptr) {
    return -1;
  }

  if (trak->start_chunk > trak->chunks) {
    return -1;
  }

  readerp = trak->atoms[MP4_CO64_DATA].reader;
  avail   = TSIOBufferReaderAvail(readerp);

  pass      = trak->start_chunk * sizeof(uint64_t);
  atom_size = sizeof(mp4_co64_atom) + avail - pass;
  trak->size += atom_size;

  TSIOBufferReaderConsume(readerp, pass);
  trak->start_offset = mp4_reader_get_64value(readerp, 0);
  trak->start_offset += trak->chunk_samples_size;
  mp4_reader_set_64value(readerp, 0, trak->start_offset);

  mp4_reader_set_32value(trak->atoms[MP4_CO64_ATOM].reader, offsetof(mp4_co64_atom, size), atom_size);
  mp4_reader_set_32value(trak->atoms[MP4_CO64_ATOM].reader, offsetof(mp4_co64_atom, entries), trak->chunks - trak->start_chunk);

  return 0;
}

int
Mp4Meta::mp4_update_stco_atom(Mp4Trak *trak)
{
  int64_t atom_size, avail;
  uint32_t pass;
  TSIOBufferReader readerp;

  if (trak->atoms[MP4_STCO_DATA].buffer == nullptr) {
    return -1;
  }

  if (trak->start_chunk > trak->chunks) {
    return -1;
  }

  readerp = trak->atoms[MP4_STCO_DATA].reader;
  avail   = TSIOBufferReaderAvail(readerp);

  pass      = trak->start_chunk * sizeof(uint32_t);
  atom_size = sizeof(mp4_stco_atom) + avail - pass;
  trak->size += atom_size;

  TSIOBufferReaderConsume(readerp, pass);

  trak->start_offset = mp4_reader_get_32value(readerp, 0);
  trak->start_offset += trak->chunk_samples_size;
  mp4_reader_set_32value(readerp, 0, trak->start_offset);

  mp4_reader_set_32value(trak->atoms[MP4_STCO_ATOM].reader, offsetof(mp4_stco_atom, size), atom_size);
  mp4_reader_set_32value(trak->atoms[MP4_STCO_ATOM].reader, offsetof(mp4_stco_atom, entries), trak->chunks - trak->start_chunk);

  return 0;
}

int
Mp4Meta::mp4_update_stbl_atom(Mp4Trak *trak)
{
  trak->size += sizeof(mp4_atom_header);
  mp4_reader_set_32value(trak->atoms[MP4_STBL_ATOM].reader, 0, trak->size);

  return 0;
}

int
Mp4Meta::mp4_update_minf_atom(Mp4Trak *trak)
{
  trak->size += sizeof(mp4_atom_header) + trak->vmhd_size + trak->smhd_size + trak->dinf_size;

  mp4_reader_set_32value(trak->atoms[MP4_MINF_ATOM].reader, 0, trak->size);

  return 0;
}

int
Mp4Meta::mp4_update_mdia_atom(Mp4Trak *trak)
{
  trak->size += sizeof(mp4_atom_header);
  mp4_reader_set_32value(trak->atoms[MP4_MDIA_ATOM].reader, 0, trak->size);

  return 0;
}

int
Mp4Meta::mp4_update_trak_atom(Mp4Trak *trak)
{
  trak->size += sizeof(mp4_atom_header);
  mp4_reader_set_32value(trak->atoms[MP4_TRAK_ATOM].reader, 0, trak->size);

  return 0;
}

int
Mp4Meta::mp4_adjust_co64_atom(Mp4Trak *trak, off_t adjustment)
{
  int64_t pos, avail, offset;
  TSIOBufferReader readerp;

  readerp = TSIOBufferReaderClone(trak->atoms[MP4_CO64_DATA].reader);
  avail   = TSIOBufferReaderAvail(readerp);

  for (pos = 0; pos < avail; pos += sizeof(uint64_t)) {
    offset = mp4_reader_get_64value(readerp, 0);
    offset += adjustment;
    mp4_reader_set_64value(readerp, 0, offset);
    TSIOBufferReaderConsume(readerp, sizeof(uint64_t));
  }

  TSIOBufferReaderFree(readerp);

  return 0;
}

int
Mp4Meta::mp4_adjust_stco_atom(Mp4Trak *trak, int32_t adjustment)
{
  int64_t pos, avail, offset;
  TSIOBufferReader readerp;

  readerp = TSIOBufferReaderClone(trak->atoms[MP4_STCO_DATA].reader);
  avail   = TSIOBufferReaderAvail(readerp);

  for (pos = 0; pos < avail; pos += sizeof(uint32_t)) {
    offset = mp4_reader_get_32value(readerp, 0);
    offset += adjustment;
    mp4_reader_set_32value(readerp, 0, offset);
    TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
  }

  TSIOBufferReaderFree(readerp);

  return 0;
}

int64_t
Mp4Meta::mp4_update_mdat_atom(int64_t start_offset)
{
  int64_t atom_data_size;
  int64_t atom_size;
  int64_t atom_header_size;
  u_char *atom_header;

  atom_data_size  = this->cl - start_offset;
  this->start_pos = start_offset;

  atom_header = mdat_atom_header;

  if (atom_data_size > 0xffffffff) {
    atom_size        = 1;
    atom_header_size = sizeof(mp4_atom_header64);
    mp4_set_64value(atom_header + sizeof(mp4_atom_header), sizeof(mp4_atom_header64) + atom_data_size);

  } else {
    atom_size        = sizeof(mp4_atom_header) + atom_data_size;
    atom_header_size = sizeof(mp4_atom_header);
  }

  this->content_length += atom_header_size + atom_data_size;

  mp4_set_32value(atom_header, atom_size);
  mp4_set_atom_name(atom_header, 'm', 'd', 'a', 't');

  mdat_atom.buffer = TSIOBufferSizedCreate(TS_IOBUFFER_SIZE_INDEX_128);
  mdat_atom.reader = TSIOBufferReaderAlloc(mdat_atom.buffer);

  TSIOBufferWrite(mdat_atom.buffer, atom_header, atom_header_size);

  return atom_header_size;
}

uint32_t
Mp4Meta::mp4_find_key_sample(uint32_t start_sample, Mp4Trak *trak)
{
  uint32_t i;
  uint32_t sample, prev_sample, entries;
  TSIOBufferReader readerp;

  if (trak->atoms[MP4_STSS_DATA].buffer == nullptr) {
    return start_sample;
  }

  prev_sample = 1;
  entries     = trak->sync_samples_entries;

  readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSS_DATA].reader);

  for (i = 0; i < entries; i++) {
    sample = (uint32_t)mp4_reader_get_32value(readerp, 0);

    if (sample > start_sample) {
      goto found;
    }

    prev_sample = sample;
    TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
  }

found:

  TSIOBufferReaderFree(readerp);
  return prev_sample;
}

void
Mp4Meta::mp4_update_mvhd_duration()
{
  int64_t need;
  uint64_t duration, cut;
  mp4_mvhd_atom *mvhd;
  mp4_mvhd64_atom mvhd64;

  need = TSIOBufferReaderAvail(mvhd_atom.reader);

  if (need > (int64_t)sizeof(mp4_mvhd64_atom)) {
    need = sizeof(mp4_mvhd64_atom);
  }

  memset(&mvhd64, 0, sizeof(mvhd64));
  IOBufferReaderCopy(mvhd_atom.reader, &mvhd64, need);
  mvhd = (mp4_mvhd_atom *)&mvhd64;

  if (this->rs > 0) {
    cut = (uint64_t)(this->rs * this->timescale / 1000);

  } else {
    cut = this->start * this->timescale / 1000;
  }

  if (mvhd->version[0] == 0) {
    duration = mp4_get_32value(mvhd->duration);
    duration -= cut;
    mp4_reader_set_32value(mvhd_atom.reader, offsetof(mp4_mvhd_atom, duration), duration);

  } else { // 64-bit duration
    duration = mp4_get_64value(mvhd64.duration);
    duration -= cut;
    mp4_reader_set_64value(mvhd_atom.reader, offsetof(mp4_mvhd64_atom, duration), duration);
  }
}

void
Mp4Meta::mp4_update_tkhd_duration(Mp4Trak *trak)
{
  int64_t need, cut;
  mp4_tkhd_atom *tkhd_atom;
  mp4_tkhd64_atom tkhd64_atom;
  int64_t duration;

  need = TSIOBufferReaderAvail(trak->atoms[MP4_TKHD_ATOM].reader);

  if (need > (int64_t)sizeof(mp4_tkhd64_atom)) {
    need = sizeof(mp4_tkhd64_atom);
  }

  memset(&tkhd64_atom, 0, sizeof(tkhd64_atom));
  IOBufferReaderCopy(trak->atoms[MP4_TKHD_ATOM].reader, &tkhd64_atom, need);
  tkhd_atom = (mp4_tkhd_atom *)&tkhd64_atom;

  if (this->rs > 0) {
    cut = (uint64_t)(this->rs * this->timescale / 1000);

  } else {
    cut = this->start * this->timescale / 1000;
  }

  if (tkhd_atom->version[0] == 0) {
    duration = mp4_get_32value(tkhd_atom->duration);
    duration -= cut;
    mp4_reader_set_32value(trak->atoms[MP4_TKHD_ATOM].reader, offsetof(mp4_tkhd_atom, duration), duration);

  } else {
    duration = mp4_get_64value(tkhd64_atom.duration);
    duration -= cut;
    mp4_reader_set_64value(trak->atoms[MP4_TKHD_ATOM].reader, offsetof(mp4_tkhd64_atom, duration), duration);
  }
}

void
Mp4Meta::mp4_update_mdhd_duration(Mp4Trak *trak)
{
  int64_t duration, need, cut;
  mp4_mdhd_atom *mdhd;
  mp4_mdhd64_atom mdhd64;

  memset(&mdhd64, 0, sizeof(mp4_mdhd64_atom));

  need = TSIOBufferReaderAvail(trak->atoms[MP4_MDHD_ATOM].reader);

  if (need > (int64_t)sizeof(mp4_mdhd64_atom)) {
    need = sizeof(mp4_mdhd64_atom);
  }

  IOBufferReaderCopy(trak->atoms[MP4_MDHD_ATOM].reader, &mdhd64, need);
  mdhd = (mp4_mdhd_atom *)&mdhd64;

  if (this->rs > 0) {
    cut = (uint64_t)(this->rs * trak->timescale / 1000);
  } else {
    cut = this->start * trak->timescale / 1000;
  }

  if (mdhd->version[0] == 0) {
    duration = mp4_get_32value(mdhd->duration);
    duration -= cut;
    mp4_reader_set_32value(trak->atoms[MP4_MDHD_ATOM].reader, offsetof(mp4_mdhd_atom, duration), duration);
  } else {
    duration = mp4_get_64value(mdhd64.duration);
    duration -= cut;
    mp4_reader_set_64value(trak->atoms[MP4_MDHD_ATOM].reader, offsetof(mp4_mdhd64_atom, duration), duration);
  }
}

static void
mp4_reader_set_32value(TSIOBufferReader readerp, int64_t offset, uint32_t n)
{
  int pos;
  int64_t avail, left;
  TSIOBufferBlock blk;
  const char *start;
  u_char *ptr;

  pos = 0;
  blk = TSIOBufferReaderStart(readerp);

  while (blk) {
    start = TSIOBufferBlockReadStart(blk, readerp, &avail);

    if (avail <= offset) {
      offset -= avail;

    } else {
      left = avail - offset;
      ptr  = (u_char *)(const_cast<char *>(start) + offset);

      while (pos < 4 && left > 0) {
        *ptr++ = (u_char)((n) >> ((3 - pos) * 8));
        pos++;
        left--;
      }

      if (pos >= 4) {
        return;
      }

      offset = 0;
    }

    blk = TSIOBufferBlockNext(blk);
  }
}

static void
mp4_reader_set_64value(TSIOBufferReader readerp, int64_t offset, uint64_t n)
{
  int pos;
  int64_t avail, left;
  TSIOBufferBlock blk;
  const char *start;
  u_char *ptr;

  pos = 0;
  blk = TSIOBufferReaderStart(readerp);

  while (blk) {
    start = TSIOBufferBlockReadStart(blk, readerp, &avail);

    if (avail <= offset) {
      offset -= avail;

    } else {
      left = avail - offset;
      ptr  = (u_char *)(const_cast<char *>(start) + offset);

      while (pos < 8 && left > 0) {
        *ptr++ = (u_char)((n) >> ((7 - pos) * 8));
        pos++;
        left--;
      }

      if (pos >= 4) {
        return;
      }

      offset = 0;
    }

    blk = TSIOBufferBlockNext(blk);
  }
}

static uint32_t
mp4_reader_get_32value(TSIOBufferReader readerp, int64_t offset)
{
  int pos;
  int64_t avail, left;
  TSIOBufferBlock blk;
  const char *start;
  const u_char *ptr;
  u_char res[4];

  pos = 0;
  blk = TSIOBufferReaderStart(readerp);

  while (blk) {
    start = TSIOBufferBlockReadStart(blk, readerp, &avail);

    if (avail <= offset) {
      offset -= avail;

    } else {
      left = avail - offset;
      ptr  = (u_char *)(start + offset);

      while (pos < 4 && left > 0) {
        res[3 - pos] = *ptr++;
        pos++;
        left--;
      }

      if (pos >= 4) {
        return *(uint32_t *)res;
      }

      offset = 0;
    }

    blk = TSIOBufferBlockNext(blk);
  }

  return -1;
}

static uint64_t
mp4_reader_get_64value(TSIOBufferReader readerp, int64_t offset)
{
  int pos;
  int64_t avail, left;
  TSIOBufferBlock blk;
  const char *start;
  u_char *ptr;
  u_char res[8];

  pos = 0;
  blk = TSIOBufferReaderStart(readerp);

  while (blk) {
    start = TSIOBufferBlockReadStart(blk, readerp, &avail);

    if (avail <= offset) {
      offset -= avail;

    } else {
      left = avail - offset;
      ptr  = (u_char *)(start + offset);

      while (pos < 8 && left > 0) {
        res[7 - pos] = *ptr++;
        pos++;
        left--;
      }

      if (pos >= 8) {
        return *(uint64_t *)res;
      }

      offset = 0;
    }

    blk = TSIOBufferBlockNext(blk);
  }

  return -1;
}

static int64_t
IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length)
{
  int64_t avail, need, n;
  const char *start;
  TSIOBufferBlock blk;

  n   = 0;
  blk = TSIOBufferReaderStart(readerp);

  while (blk) {
    start = TSIOBufferBlockReadStart(blk, readerp, &avail);
    need  = length < avail ? length : avail;

    if (need > 0) {
      memcpy((char *)buf + n, start, need);
      length -= need;
      n += need;
    }

    if (length == 0) {
      break;
    }

    blk = TSIOBufferBlockNext(blk);
  }

  return n;
}
