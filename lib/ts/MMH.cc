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

#include <cstdlib>
#include <cstring>
#include "ts/ink_assert.h"
#include "ts/ink_platform.h"
#include "ts/MMH.h"

#define MMH_X_SIZE 512

/* BUG: INKqa11504: need it be to 64 bits...otherwise it overflows */
static uint64_t MMH_x[MMH_X_SIZE + 8] = {
  0x3ee18b32, 0x746d0d6b, 0x591be6a3, 0x760bd17f, 0x363c765d, 0x4bf3d5c5, 0x10f0510a, 0x39a84605, 0x2282b48f, 0x6903652e,
  0x1b491170, 0x1ab8407a, 0x776b8aa8, 0x5b126ffe, 0x5095db1a, 0x565fe90c, 0x3ae1f068, 0x73fdf0cb, 0x72f39a81, 0x6a40a4a3,
  0x4ef557fe, 0x360c1a2c, 0x4579b0ea, 0x61dfd174, 0x269b242f, 0x752d6298, 0x15f10fa3, 0x618b7ab3, 0x6699171f, 0x488f2c6c,
  0x790f8cdb, 0x5ed15565, 0x04eba3c0, 0x5009ac0b, 0x3a5d6c1f, 0x1a4f7853, 0x1affabd4, 0x74aace1f, 0x2310b46d, 0x466b611a,
  0x18c5d4a0, 0x7eb9fffe, 0x76098df6, 0x4172f860, 0x689e3c2f, 0x722cdc29, 0x64548175, 0x28f46721, 0x58fdf93f, 0x12c2dcee,
  0x58cb1327, 0x02d4af27, 0x4d1c6fcd, 0x72fe572d, 0x7038d366, 0x0bfa1898, 0x788d2438, 0x1f131f37, 0x25729ee6, 0x635ea6a9,
  0x3b0b5714, 0x6ac759d2, 0x5faf688a, 0x0c2fe571, 0x7487538e, 0x65491b59, 0x60cd86e4, 0x5d6482d8, 0x4a59fa77, 0x78439700,
  0x56a51f48, 0x360544ae, 0x6c01b3ef, 0x2228c036, 0x15b7e88b, 0x326e0dd8, 0x509491af, 0x72d06023, 0x26181f5d, 0x7924c4a4,
  0x70c60bf2, 0x7b5bc151, 0x28e42640, 0x48af0c3e, 0x009b6301, 0x06dd3366, 0x2ad1eb24, 0x102ce33c, 0x1e504f5a, 0x5ab4c90f,
  0x669ccca1, 0x118d5954, 0x1a1e4a7c, 0x1807d1f9, 0x525a58d0, 0x2f13ae2d, 0x17335a52, 0x714eb2f9, 0x1865dfc7, 0x61b64b52,
  0x0dc9e939, 0x4fccde4c, 0x6d5873af, 0x47c62b43, 0x0fa1d4b0, 0x2f00cdf8, 0x68029083, 0x52645fa6, 0x4bb37c9b, 0x53d60251,
  0x48364566, 0x35b4889b, 0x46783d34, 0x30c12697, 0x210459a1, 0x36962f2d, 0x36305d8f, 0x170e9dbd, 0x687c8739, 0x261c14e4,
  0x3cc51cc7, 0x02add945, 0x01a88529, 0x6617aa77, 0x6be627ca, 0x14c7fc46, 0x46fb3b41, 0x1bffff9e, 0x1e6c61be, 0x10966c8f,
  0x3f69199b, 0x1b5e9e06, 0x4880890f, 0x055613e6, 0x6c742db5, 0x7be1e15e, 0x2522e317, 0x41fe3369, 0x2b462f30, 0x605b7e8e,
  0x1c19b868, 0x3fadcb16, 0x781c5e24, 0x1c6b0c08, 0x499f0bb9, 0x04b0b766, 0x7d6cad1e, 0x097f7d36, 0x2e02956a, 0x03adc713,
  0x4ce950b7, 0x6e57a313, 0x557badb5, 0x73212afb, 0x3f7f6ed2, 0x0558e3d6, 0x28376f73, 0x54dac21d, 0x6c3f4771, 0x67147bc8,
  0x5ae9fd88, 0x51ede3c0, 0x1067d134, 0x5246b937, 0x056e74ed, 0x5d7869b2, 0x62356370, 0x76a0c583, 0x3defb558, 0x5200dcae,
  0x1432a7e8, 0x3ae4ad55, 0x0c4cca8a, 0x0607c4d7, 0x1944ae2b, 0x726479f0, 0x558e6035, 0x5ae64061, 0x4e1e9a8a, 0x0cf04d9f,
  0x46ef4a87, 0x0554224f, 0x70d70ab3, 0x03cc954e, 0x39d0cd57, 0x1b11fb56, 0x62a0e9ee, 0x55888135, 0x3e93ebeb, 0x29578ee1,
  0x0bcb0ef4, 0x529e16af, 0x165bab64, 0x39ed562e, 0x52c59d67, 0x48c84d29, 0x0fd0d1c7, 0x795fd395, 0x79a1c4f3, 0x5010c835,
  0x4fbe4ba8, 0x49a597e9, 0x29f017ff, 0x59dde0be, 0x2c660275, 0x15fcfbf7, 0x1eab540f, 0x38e2cf56, 0x74608d5c, 0x7cd4b02c,
  0x52a115b9, 0x2bdee9ac, 0x5456c6da, 0x63626453, 0x15279241, 0x19b60519, 0x508af0e1, 0x2e3ce97b, 0x568710d4, 0x6abb3059,
  0x7897b1a5, 0x17034ff6, 0x2aef7d5e, 0x5a281657, 0x0fa5d304, 0x76f0a37e, 0x31be0f08, 0x46ce7c20, 0x563e4e90, 0x31540773,
  0x1cdc9c51, 0x10366bfa, 0x1b6cd03a, 0x615f1540, 0x18c3d6c8, 0x3cb2bf8e, 0x29bf799c, 0x40b87edb, 0x42c34863, 0x1e9edb40,
  0x64734fe2, 0x3ddf176a, 0x1c458c7f, 0x06138c9f, 0x5e695e56, 0x02c98403, 0x0474de75, 0x660e1df8, 0x6df73788, 0x3770f68c,
  0x758bb7d5, 0x0763d105, 0x16e61f16, 0x153974c1, 0x29ded842, 0x1a0d12c3, 0x599ec61d, 0x05904d54, 0x79e9b0ea, 0x4976da61,
  0x5834243c, 0x67c17d2f, 0x65fbcda0, 0x17bdc554, 0x465e9741, 0x7a0ee6d5, 0x3b357597, 0x0d1da287, 0x01211373, 0x04a05de6,
  0x5deb5dbd, 0x6d993eb0, 0x2064ce7c, 0x3011a8c1, 0x36ece6b1, 0x4a0963be, 0x0cf46ef0, 0x0d53ba44, 0x63260063, 0x187f1d6e,
  0x7e866a7e, 0x4b6885af, 0x254d6d47, 0x715474fd, 0x6896dcb2, 0x7554eea6, 0x2161bf36, 0x5387f5f8, 0x5c4bc064, 0x059a7755,
  0x7d4307e1, 0x17326e2f, 0x5e2315c1, 0x14c26eae, 0x1e5cd6f2, 0x352b7ac8, 0x66591ef3, 0x381e80cd, 0x19b3bfc1, 0x3668946f,
  0x4b6d7d70, 0x20feab7d, 0x1b6340af, 0x356b6cab, 0x299099dc, 0x295ab8d4, 0x184c8623, 0x134f8e4c, 0x7caf609c, 0x716d81f9,
  0x2e04231f, 0x1dd45301, 0x43e9fcf9, 0x1c225c06, 0x0994797e, 0x5b3f6006, 0x1d22dcec, 0x32993108, 0x3f0c2bcc, 0x4d44fbfa,
  0x389de78c, 0x7f8be723, 0x5dab92c1, 0x7866afce, 0x3bfc7011, 0x4a27d7d3, 0x0c79d05c, 0x268dc4da, 0x3fe10f84, 0x1f18394d,
  0x20b9ba99, 0x312e520a, 0x64cf2f05, 0x322a7c04, 0x4cc077ce, 0x7218aa35, 0x550cacb8, 0x5943be47, 0x15b346a8, 0x0d6a1d8e,
  0x3f08a54d, 0x7a6e9807, 0x274f8bbc, 0x6feb2033, 0x64b10c2b, 0x2cbaa0b7, 0x0db7decc, 0x22b807e3, 0x10d15c39, 0x6a9b314c,
  0x5ff27199, 0x5072b2cd, 0x4eaf4b49, 0x5a890464, 0x7df0ca60, 0x548e8983, 0x5e3f0a21, 0x70027683, 0x503e6bf2, 0x47ad6e0d,
  0x77173b26, 0x6dc04878, 0x4d73a573, 0x439b4a1a, 0x2e6569a7, 0x1630e5de, 0x1be363af, 0x6f5f0e52, 0x5b266bc3, 0x2f2a51be,
  0x204e7e14, 0x1b3314c6, 0x4472b8f9, 0x4162fb52, 0x72549950, 0x3223f889, 0x0e655f4a, 0x65c3dce4, 0x04825988, 0x22b41458,
  0x53a4e10d, 0x3e2a66d5, 0x29de3e31, 0x0252fa74, 0x267fe54f, 0x42d6d8ba, 0x5951218f, 0x73db5791, 0x618444e4, 0x79abcaa1,
  0x0ddcf5c8, 0x2cbed2e6, 0x73159e0e, 0x7aadc871, 0x10e3f9a4, 0x762e9d65, 0x2a7138c9, 0x59fe016f, 0x5b6c3ee4, 0x28888205,
  0x695fa5b1, 0x50f92ddd, 0x07eefc3b, 0x42bb693a, 0x71312191, 0x3653ecbd, 0x1d80c4ed, 0x5a536187, 0x6a286789, 0x4a1ffbb3,
  0x1e976003, 0x5a8c5f29, 0x2ac83bdb, 0x5ab9cb08, 0x63039928, 0x5a4c04f4, 0x7b329952, 0x40d40fcb, 0x01810524, 0x2555e83c,
  0x748d0b4f, 0x534f1612, 0x272353f2, 0x6992e1ea, 0x33cc5e71, 0x5163b55e, 0x29886a7f, 0x7cfb1eae, 0x330271e0, 0x6f05e91c,
  0x35b01e02, 0x64bbc053, 0x76eb9337, 0x62612f48, 0x044e0af2, 0x1dac022e, 0x1ca56f0c, 0x0210ef2c, 0x5af7a1a9, 0x2632f2b0,
  0x23d0401c, 0x0c594a46, 0x77582293, 0x297df41b, 0x4c7b8718, 0x6c48d948, 0x4835e412, 0x74795651, 0x28ca3506, 0x4071f739,
  0x032fdbf2, 0x097f7bc8, 0x44ced256, 0x47f25cb9, 0x43500684, 0x45481b9a, 0x5a5ecc82, 0x4fe9ed61, 0x337ee559, 0x556852b9,
  0x0b24b460, 0x696db949, 0x7a2def9d, 0x4fcd5640, 0x1babd707, 0x5c9254a3, 0x44d26e0d, 0x0e26b8e4, 0x3b1c3b5c, 0x0078c784,
  0x27a7dc96, 0x1d525589, 0x4384ae38, 0x447b77c3, 0x78488b8c, 0x5eab10f1, 0x16812737, 0x37cc8efa, 0x219cda83, 0x00bcc48f,
  0x3c667020, 0x492d7eaa, 0x710d06ce, 0x4172c47a, 0x358098ec, 0x1fff647b, 0x65672792, 0x1a7b927d, 0x24006275, 0x04e630a0,
  0x2f2a9185, 0x5873704b, 0x0a8c69bc, 0x06b49059, 0x49837c48, 0x4f90a2d0, 0x29ad7dd7, 0x3674be92, 0x46d5635f, 0x782758a2,
  0x721a2a75, 0x13427ca9, 0x20e03cc9, 0x5f884596, 0x19dc210f, 0x066c954d, 0x52f43f40, 0x5d9c256f, 0x7f0acaae, 0x1e186b81,
  0x55e9920f, 0x0e4f77b2, 0x6700ec53, 0x268837c0, 0x554ce08b, 0x4284e695, 0x2127e806, 0x384cb53b, 0x51076b2f, 0x23f9eb15};

// We don't need this generator in release.
#ifdef TEST
// generator for above
static void
ink_init_MMH()
{
  srand48(13); // must remain the same!
  for (int i = 0; i < MMH_X_SIZE; i++)
    MMH_x[i] = lrand48();
}
#endif /* TEST */

int
ink_code_incr_MMH_init(MMH_CTX *ctx)
{
  ctx->buffer_size = 0;
  ctx->blocks      = 0;
  ctx->state[0]    = ((uint64_t)MMH_x[MMH_X_SIZE + 0] << 32) + MMH_x[MMH_X_SIZE + 1];
  ctx->state[1]    = ((uint64_t)MMH_x[MMH_X_SIZE + 2] << 32) + MMH_x[MMH_X_SIZE + 3];
  ctx->state[2]    = ((uint64_t)MMH_x[MMH_X_SIZE + 4] << 32) + MMH_x[MMH_X_SIZE + 5];
  ctx->state[3]    = ((uint64_t)MMH_x[MMH_X_SIZE + 6] << 32) + MMH_x[MMH_X_SIZE + 7];
  return 0;
}

int
ink_code_MMH(unsigned char *input, int len, unsigned char *sixteen_byte_hash)
{
  MMH_CTX ctx;
  ink_code_incr_MMH_init(&ctx);
  ink_code_incr_MMH_update(&ctx, (const char *)input, len);
  ink_code_incr_MMH_final(sixteen_byte_hash, &ctx);
  return 0;
}

static inline void
MMH_update(MMH_CTX *ctx, unsigned char *ab)
{
  uint32_t *b = (uint32_t *)ab;
  ctx->state[0] += b[0] * MMH_x[(ctx->blocks + 0) % MMH_X_SIZE];
  ctx->state[1] += b[1] * MMH_x[(ctx->blocks + 1) % MMH_X_SIZE];
  ctx->state[2] += b[2] * MMH_x[(ctx->blocks + 2) % MMH_X_SIZE];
  ctx->state[3] += b[3] * MMH_x[(ctx->blocks + 3) % MMH_X_SIZE];
  ctx->blocks += 4;
}

static inline void
MMH_updateb1(MMH_CTX *ctx, unsigned char *ab)
{
  uint32_t *b = (uint32_t *)(ab - 1);
  uint32_t b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3], b4 = b[4];
  b0 = (b0 << 8) + (b1 >> 24);
  b1 = (b1 << 8) + (b2 >> 24);
  b2 = (b2 << 8) + (b3 >> 24);
  b3 = (b3 << 8) + (b4 >> 24);
  ctx->state[0] += b0 * MMH_x[(ctx->blocks + 0) % MMH_X_SIZE];
  ctx->state[1] += b1 * MMH_x[(ctx->blocks + 1) % MMH_X_SIZE];
  ctx->state[2] += b2 * MMH_x[(ctx->blocks + 2) % MMH_X_SIZE];
  ctx->state[3] += b3 * MMH_x[(ctx->blocks + 3) % MMH_X_SIZE];
  ctx->blocks += 4;
}

static inline void
MMH_updateb2(MMH_CTX *ctx, unsigned char *ab)
{
  uint32_t *b = (uint32_t *)(ab - 2);
  uint32_t b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3], b4 = b[4];
  b0 = (b0 << 16) + (b1 >> 16);
  b1 = (b1 << 16) + (b2 >> 16);
  b2 = (b2 << 16) + (b3 >> 16);
  b3 = (b3 << 16) + (b4 >> 16);
  ctx->state[0] += b0 * MMH_x[(ctx->blocks + 0) % MMH_X_SIZE];
  ctx->state[1] += b1 * MMH_x[(ctx->blocks + 1) % MMH_X_SIZE];
  ctx->state[2] += b2 * MMH_x[(ctx->blocks + 2) % MMH_X_SIZE];
  ctx->state[3] += b3 * MMH_x[(ctx->blocks + 3) % MMH_X_SIZE];
  ctx->blocks += 4;
}

static inline void
MMH_updateb3(MMH_CTX *ctx, unsigned char *ab)
{
  uint32_t *b = (uint32_t *)(ab - 3);
  uint32_t b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3], b4 = b[4];
  b0 = (b0 << 24) + (b1 >> 8);
  b1 = (b1 << 24) + (b2 >> 8);
  b2 = (b2 << 24) + (b3 >> 8);
  b3 = (b3 << 24) + (b4 >> 8);
  ctx->state[0] += b0 * MMH_x[(ctx->blocks + 0) % MMH_X_SIZE];
  ctx->state[1] += b1 * MMH_x[(ctx->blocks + 1) % MMH_X_SIZE];
  ctx->state[2] += b2 * MMH_x[(ctx->blocks + 2) % MMH_X_SIZE];
  ctx->state[3] += b3 * MMH_x[(ctx->blocks + 3) % MMH_X_SIZE];
  ctx->blocks += 4;
}

static inline void
MMH_updatel1(MMH_CTX *ctx, unsigned char *ab)
{
  uint32_t *b = (uint32_t *)(ab - 1);
  uint32_t b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3], b4 = b[4];
  b0 = (b0 >> 8) + (b1 << 24);
  b1 = (b1 >> 8) + (b2 << 24);
  b2 = (b2 >> 8) + (b3 << 24);
  b3 = (b3 >> 8) + (b4 << 24);
  ctx->state[0] += b0 * MMH_x[(ctx->blocks + 0) % MMH_X_SIZE];
  ctx->state[1] += b1 * MMH_x[(ctx->blocks + 1) % MMH_X_SIZE];
  ctx->state[2] += b2 * MMH_x[(ctx->blocks + 2) % MMH_X_SIZE];
  ctx->state[3] += b3 * MMH_x[(ctx->blocks + 3) % MMH_X_SIZE];
  ctx->blocks += 4;
}

static inline void
MMH_updatel2(MMH_CTX *ctx, unsigned char *ab)
{
  uint32_t *b = (uint32_t *)(ab - 2);
  uint32_t b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3], b4 = b[4];
  b0 = (b0 >> 16) + (b1 << 16);
  b1 = (b1 >> 16) + (b2 << 16);
  b2 = (b2 >> 16) + (b3 << 16);
  b3 = (b3 >> 16) + (b4 << 16);
  ctx->state[0] += b0 * MMH_x[(ctx->blocks + 0) % MMH_X_SIZE];
  ctx->state[1] += b1 * MMH_x[(ctx->blocks + 1) % MMH_X_SIZE];
  ctx->state[2] += b2 * MMH_x[(ctx->blocks + 2) % MMH_X_SIZE];
  ctx->state[3] += b3 * MMH_x[(ctx->blocks + 3) % MMH_X_SIZE];
  ctx->blocks += 4;
}

static inline void
MMH_updatel3(MMH_CTX *ctx, unsigned char *ab)
{
  uint32_t *b = (uint32_t *)(ab - 3);
  uint32_t b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3], b4 = b[4];
  b0 = (b0 >> 24) + (b1 << 8);
  b1 = (b1 >> 24) + (b2 << 8);
  b2 = (b2 >> 24) + (b3 << 8);
  b3 = (b3 >> 24) + (b4 << 8);
  ctx->state[0] += b0 * MMH_x[(ctx->blocks + 0) % MMH_X_SIZE];
  ctx->state[1] += b1 * MMH_x[(ctx->blocks + 1) % MMH_X_SIZE];
  ctx->state[2] += b2 * MMH_x[(ctx->blocks + 2) % MMH_X_SIZE];
  ctx->state[3] += b3 * MMH_x[(ctx->blocks + 3) % MMH_X_SIZE];
  ctx->blocks += 4;
}

int
ink_code_incr_MMH_update(MMH_CTX *ctx, const char *ainput, int input_length)
{
  unsigned char *in  = (unsigned char *)ainput;
  unsigned char *end = in + input_length;
  if (ctx->buffer_size) {
    int l = 16 - ctx->buffer_size;
    if (input_length >= l) {
      memcpy(ctx->buffer + ctx->buffer_size, in, l);
      ctx->buffer_size = 0;
      in += l;
      if (ctx->buffer_size & 0x0f)
        return 0;
      MMH_update(ctx, ctx->buffer);
    } else
      goto Lstore;
  }
  {
    // check alignment
    int alignment = (int)((intptr_t)in & 0x3);
    if (alignment) {
#if defined(_BIG_ENDIAN)
#define big_endian 1
#elif defined(_LITTLE_ENDIAN)
#define big_endian 0
#else
      unsigned int endian = 1;
      int big_endian      = !*(char *)&endian;
#endif
      if (big_endian) {
        if (alignment == 1) {
          while (in + 16 <= end) {
            MMH_updateb1(ctx, in);
            in += 16;
          }
        } else if (alignment == 2) {
          while (in + 16 <= end) {
            MMH_updateb2(ctx, in);
            in += 16;
          }
        } else if (alignment == 3)
          while (in + 16 <= end) {
            MMH_updateb3(ctx, in);
            in += 16;
          }
      } else {
        if (alignment == 1) {
          while (in + 16 <= end) {
            MMH_updatel1(ctx, in);
            in += 16;
          }
        } else if (alignment == 2) {
          while (in + 16 <= end) {
            MMH_updatel2(ctx, in);
            in += 16;
          }
        } else if (alignment == 3)
          while (in + 16 <= end) {
            MMH_updatel3(ctx, in);
            in += 16;
          }
      }
    } else {
      while (in + 16 <= end) {
        MMH_update(ctx, in);
        in += 16;
      }
    }
  }
Lstore:
  if (end - in) {
    int oldbs = ctx->buffer_size;
    ctx->buffer_size += (int)(end - in);
#ifndef TEST
    ink_assert(ctx->buffer_size < 16);
#endif
    memcpy(ctx->buffer + oldbs, in, (int)(end - in));
  }
  return 0;
}

#if defined(__GNUC__)
#define _memset memset
#else
// NOT general purpose
inline void
_memset(unsigned char *b, int c, int len)
{
  (void)c;
  int o = len & 0x3, i;
  for (i = 0; i < o; i++)
    b[i] = 0;
  for (i                     = 0; i < (len - o) / 4; i++)
    ((uint32_t *)(b + o))[i] = 0;
}
#endif

int
ink_code_incr_MMH_final(uint8_t *presult, MMH_CTX *ctx)
{
  unsigned int len = ctx->blocks * 4 + ctx->buffer_size;
  // pad out to 16 bytes
  if (ctx->buffer_size) {
    _memset(ctx->buffer + ctx->buffer_size, 0, 16 - ctx->buffer_size);
    ctx->buffer_size = 0;
    MMH_update(ctx, ctx->buffer);
  }
  // append length (before padding)
  unsigned int *pbuffer = (unsigned int *)ctx->buffer;
  pbuffer[1] = pbuffer[2] = pbuffer[3] = pbuffer[0] = len;
  MMH_update(ctx, ctx->buffer);
  // final phase
  uint32_t *b = (uint32_t *)presult;
  uint64_t d  = (((uint64_t)1) << 32) + 15;
  uint32_t b0 = uint32_t(ctx->state[0] % d);
  uint32_t b1 = uint32_t(ctx->state[1] % d);
  uint32_t b2 = uint32_t(ctx->state[2] % d);
  uint32_t b3 = uint32_t(ctx->state[3] % d);
  // scramble the bits, losslessly (reversibly)
  b[0] = b0;
  b[1] = b1 ^ (b0 >> 24) ^ (b0 << 8);
  b[2] = b2 ^ (b1 >> 16) ^ (b1 << 16) ^ (b0 >> 24) ^ (b0 << 8);
  b[3] = b3 ^ (b1 >> 8) ^ (b1 << 24) ^ (b2 >> 16) ^ (b2 << 16) ^ (b0 >> 24) ^ (b0 << 8);

  b0 = b[0];
  b1 = b[1];
  b2 = b[2];
  b3 = b[3];

  b[2] = b2 ^ (b3 >> 24) ^ (b3 << 8);
  b[1] = b1 ^ (b2 >> 16) ^ (b2 << 16) ^ (b3 >> 24) ^ (b3 << 8);
  b[0] = b0 ^ (b3 >> 8) ^ (b3 << 24) ^ (b2 >> 16) ^ (b2 << 16) ^ (b1 >> 24) ^ (b1 << 8);
  return 0;
}

MMHContext::MMHContext()
{
  ink_code_incr_MMH_init(&_ctx);
}

bool
MMHContext::update(void const *data, int length)
{
  return 0 == ink_code_incr_MMH_update(&_ctx, static_cast<const char *>(data), length);
}

bool
MMHContext::finalize(CryptoHash &hash)
{
  return 0 == ink_code_incr_MMH_final(hash.u8, &_ctx);
}

#ifdef TEST

#define TEST_COLLISIONS 10000000

static int
xxcompar(uint32_t **x, uint32_t **y)
{
  for (int i = 0; i < 4; i++) {
    if (x[i] > y[i])
      return 1;
    if (x[i] < y[i])
      return -1;
  }
  return 0;
}

typedef uint32_t i4_t[4];
i4_t *xxh;
double *xf;

main()
{
  union {
    unsigned char hash[16];
    uint32_t h[4];
  } h;

  xxh = (i4_t *)ats_malloc(4 * sizeof(uint32_t) * TEST_COLLISIONS);
  xf  = (double *)ats_malloc(sizeof(double) * TEST_COLLISIONS);

  printf("test collisions\n");
  char *sc1 = "http://npdev:19080/1.6664000000/4000";
  char *sc2 = "http://npdev:19080/1.8666000000/4000";
  char *sc3 = "http://:@npdev/1.6664000000/4000;?";
  char *sc4 = "http://:@npdev/1.8666000000/4000;?";
  ink_code_MMH((unsigned char *)sc1, strlen(sc1), h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH((unsigned char *)sc2, strlen(sc2), h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH((unsigned char *)sc3, strlen(sc3), h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH((unsigned char *)sc4, strlen(sc4), h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);

  srand48(time(nullptr));
  for (int xx = 0; xx < TEST_COLLISIONS; xx++) {
    char xs[256];
    xf[xx] = drand48();
    sprintf(xs, "http://@npdev/%16.14f/4000;?", xf[xx]);
    ink_code_MMH((unsigned char *)xs, strlen(xs), (unsigned char *)&xxh[xx]);
  }
  qsort(xxh, TEST_COLLISIONS, 16, xxcompar);
  for (int xy = 0; xy < TEST_COLLISIONS - 1; xy++) {
    if (xxh[xy][0] == xxh[xy + 1][0] && xxh[xy][1] == xxh[xy + 1][1] && xxh[xy][2] == xxh[xy + 1][2] &&
        xxh[xy][3] == xxh[xy + 1][3])
      printf("********** collision %d\n", xy);
  }

  unsigned char *s  = (unsigned char *)MMH_x;
  int l             = sizeof(MMH_x);
  unsigned char *s1 = (unsigned char *)ats_malloc(l + 3);
  s1 += 1;
  memcpy(s1, s, l);
  unsigned char *s2 = (unsigned char *)ats_malloc(l + 3);
  s2 += 2;
  memcpy(s2, s, l);
  unsigned char *s3 = (unsigned char *)ats_malloc(l + 3);
  s3 += 3;
  memcpy(s3, s, l);

  printf("test alignment\n");
  ink_code_MMH(s, l, h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH(s1, l, h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH(s2, l, h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH(s3, l, h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);

  int i = 0;
  MMH_CTX c;
  unsigned char *t = s;
  printf("test chunking\n");
  ink_code_incr_MMH_init(&c);
  for (i = 0; i < 24; i++) {
    ink_code_incr_MMH_update(&c, (char *)t, i);
    t += i;
  }
  ink_code_incr_MMH_final((char *)h.hash, &c);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  int q = t - s;
  ink_code_MMH(s, q, h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);

  FILE *fp = fopen("/kernel/genunix", "r");
  char x[4096];
  int hist[256];
  memset(hist, 0, sizeof(hist));
  size_t xx;
  while (((xx = fread(x, 1, 128, fp)) == 128)) {
    ink_code_MMH((unsigned char *)x, 128, h.hash);
    hist[h.h[0] & 255]++;
  }
  for (int z = 0; z < 256; z++) {
    printf("%6d ", hist[z]);
    if (!(z % 7))
      printf("\n");
  }
}

#endif
