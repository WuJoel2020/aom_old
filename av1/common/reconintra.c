/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <math.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/aom_once.h"
#include "aom_ports/mem.h"
#include "aom_ports/system_state.h"
#include "av1/common/reconintra.h"
#include "av1/common/onyxc_int.h"
#include "av1/common/cfl.h"

enum {
  NEED_LEFT = 1 << 1,
  NEED_ABOVE = 1 << 2,
  NEED_ABOVERIGHT = 1 << 3,
  NEED_ABOVELEFT = 1 << 4,
  NEED_BOTTOMLEFT = 1 << 5,
};

#define INTRA_EDGE_FILT 3
#define INTRA_EDGE_TAPS 5
#define MAX_UPSAMPLE_SZ 16

static const uint8_t extend_modes[INTRA_MODES] = {
  NEED_ABOVE | NEED_LEFT,                   // DC
  NEED_ABOVE,                               // V
  NEED_LEFT,                                // H
  NEED_ABOVE | NEED_ABOVERIGHT,             // D45
  NEED_LEFT | NEED_ABOVE | NEED_ABOVELEFT,  // D135
  NEED_LEFT | NEED_ABOVE | NEED_ABOVELEFT,  // D113
  NEED_LEFT | NEED_ABOVE | NEED_ABOVELEFT,  // D157
  NEED_LEFT | NEED_BOTTOMLEFT,              // D203
  NEED_ABOVE | NEED_ABOVERIGHT,             // D67
  NEED_LEFT | NEED_ABOVE,                   // SMOOTH
  NEED_LEFT | NEED_ABOVE,                   // SMOOTH_V
  NEED_LEFT | NEED_ABOVE,                   // SMOOTH_H
  NEED_LEFT | NEED_ABOVE | NEED_ABOVELEFT,  // PAETH
};

// Tables to store if the top-right reference pixels are available. The flags
// are represented with bits, packed into 8-bit integers. E.g., for the 32x32
// blocks in a 128x128 superblock, the index of the "o" block is 10 (in raster
// order), so its flag is stored at the 3rd bit of the 2nd entry in the table,
// i.e. (table[10 / 8] >> (10 % 8)) & 1.
//       . . . .
//       . . . .
//       . . o .
//       . . . .
static uint8_t has_tr_4x4[128] = {
  255, 255, 255, 255, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  127, 127, 127, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  255, 127, 255, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  127, 127, 127, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  255, 255, 255, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  127, 127, 127, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  255, 127, 255, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  127, 127, 127, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
};
static uint8_t has_tr_4x8[64] = {
  255, 255, 255, 255, 119, 119, 119, 119, 127, 127, 127, 127, 119,
  119, 119, 119, 255, 127, 255, 127, 119, 119, 119, 119, 127, 127,
  127, 127, 119, 119, 119, 119, 255, 255, 255, 127, 119, 119, 119,
  119, 127, 127, 127, 127, 119, 119, 119, 119, 255, 127, 255, 127,
  119, 119, 119, 119, 127, 127, 127, 127, 119, 119, 119, 119,
};
static uint8_t has_tr_8x4[64] = {
  255, 255, 0, 0, 85, 85, 0, 0, 119, 119, 0, 0, 85, 85, 0, 0,
  127, 127, 0, 0, 85, 85, 0, 0, 119, 119, 0, 0, 85, 85, 0, 0,
  255, 127, 0, 0, 85, 85, 0, 0, 119, 119, 0, 0, 85, 85, 0, 0,
  127, 127, 0, 0, 85, 85, 0, 0, 119, 119, 0, 0, 85, 85, 0, 0,
};
static uint8_t has_tr_8x8[32] = {
  255, 255, 85, 85, 119, 119, 85, 85, 127, 127, 85, 85, 119, 119, 85, 85,
  255, 127, 85, 85, 119, 119, 85, 85, 127, 127, 85, 85, 119, 119, 85, 85,
};
static uint8_t has_tr_8x16[16] = {
  255, 255, 119, 119, 127, 127, 119, 119,
  255, 127, 119, 119, 127, 127, 119, 119,
};
static uint8_t has_tr_16x8[16] = {
  255, 0, 85, 0, 119, 0, 85, 0, 127, 0, 85, 0, 119, 0, 85, 0,
};
static uint8_t has_tr_16x16[8] = {
  255, 85, 119, 85, 127, 85, 119, 85,
};
static uint8_t has_tr_16x32[4] = { 255, 119, 127, 119 };
static uint8_t has_tr_32x16[4] = { 15, 5, 7, 5 };
static uint8_t has_tr_32x32[2] = { 95, 87 };
static uint8_t has_tr_32x64[1] = { 127 };
static uint8_t has_tr_64x32[1] = { 19 };
static uint8_t has_tr_64x64[1] = { 7 };
static uint8_t has_tr_64x128[1] = { 3 };
static uint8_t has_tr_128x64[1] = { 1 };
static uint8_t has_tr_128x128[1] = { 1 };
static uint8_t has_tr_4x16[32] = {
  255, 255, 255, 255, 127, 127, 127, 127, 255, 127, 255,
  127, 127, 127, 127, 127, 255, 255, 255, 127, 127, 127,
  127, 127, 255, 127, 255, 127, 127, 127, 127, 127,
};
static uint8_t has_tr_16x4[32] = {
  255, 0, 0, 0, 85, 0, 0, 0, 119, 0, 0, 0, 85, 0, 0, 0,
  127, 0, 0, 0, 85, 0, 0, 0, 119, 0, 0, 0, 85, 0, 0, 0,
};
static uint8_t has_tr_8x32[8] = {
  255, 255, 127, 127, 255, 127, 127, 127,
};
static uint8_t has_tr_32x8[8] = {
  15, 0, 5, 0, 7, 0, 5, 0,
};
static uint8_t has_tr_16x64[2] = { 255, 127 };
static uint8_t has_tr_64x16[2] = { 3, 1 };

#if CONFIG_FLEX_PARTITION
static uint8_t has_tr_4x32[16] = {
  255, 255, 255, 255, 255, 127, 255, 127,
  255, 255, 255, 127, 255, 127, 255, 127,
};
static uint8_t has_tr_32x4[16] = { 15, 0, 0, 0, 5, 0, 0, 0,
                                   7,  0, 0, 0, 5, 0, 0, 0 };
static uint8_t has_tr_4x64[8] = {
  255, 255, 255, 255, 255, 255, 255, 127,
};
static uint8_t has_tr_64x4[8] = { 3, 0, 0, 0, 1, 0, 0, 0 };
static uint8_t has_tr_8x64[4] = {
  255,
  255,
  255,
  127,
};
static uint8_t has_tr_64x8[4] = {
  3,
  0,
  1,
  0,
};
#endif  // CONFIG_FLEX_PARTITION

static const uint8_t *const has_tr_tables[BLOCK_SIZES_ALL] = {
  has_tr_4x4,     has_tr_4x8,   has_tr_8x4,   has_tr_8x8,    has_tr_8x16,
  has_tr_16x8,    has_tr_16x16, has_tr_16x32, has_tr_32x16,  has_tr_32x32,
  has_tr_32x64,   has_tr_64x32, has_tr_64x64, has_tr_64x128, has_tr_128x64,
  has_tr_128x128, has_tr_4x16,  has_tr_16x4,  has_tr_8x32,   has_tr_32x8,
  has_tr_16x64,   has_tr_64x16,
#if CONFIG_FLEX_PARTITION
  has_tr_4x32,    has_tr_32x4,  has_tr_8x64,  has_tr_64x8,   has_tr_4x64,
  has_tr_64x4
#endif  // CONFIG_FLEX_PARTITION
};

static uint8_t has_tr_vert_8x8[32] = {
  255, 255, 0, 0, 119, 119, 0, 0, 127, 127, 0, 0, 119, 119, 0, 0,
  255, 127, 0, 0, 119, 119, 0, 0, 127, 127, 0, 0, 119, 119, 0, 0,
};
static uint8_t has_tr_vert_16x16[8] = {
  255, 0, 119, 0, 127, 0, 119, 0,
};
static uint8_t has_tr_vert_32x32[2] = { 15, 7 };
static uint8_t has_tr_vert_64x64[1] = { 3 };

// The _vert_* tables are like the ordinary tables above, but describe the
// order we visit square blocks when doing a PARTITION_VERT_A or
// PARTITION_VERT_B. This is the same order as normal except for on the last
// split where we go vertically (TL, BL, TR, BR). We treat the rectangular block
// as a pair of squares, which means that these tables work correctly for both
// mixed vertical partition types.
//
// There are tables for each of the square sizes. Vertical rectangles (like
// BLOCK_16X32) use their respective "non-vert" table
static const uint8_t *const has_tr_vert_tables[BLOCK_SIZES] = {
  NULL,               // 4X4
  has_tr_4x8,         // 4X8
  NULL,               // 8X4
  has_tr_vert_8x8,    // 8X8
  has_tr_8x16,        // 8X16
  NULL,               // 16X8
  has_tr_vert_16x16,  // 16X16
  has_tr_16x32,       // 16X32
  NULL,               // 32X16
  has_tr_vert_32x32,  // 32X32
  has_tr_32x64,       // 32X64
  NULL,               // 64X32
  has_tr_vert_64x64,  // 64X64
  has_tr_64x128,      // 64X128
  NULL,               // 128X64
  has_tr_128x128,     // 128X128
};

static const uint8_t *get_has_tr_table(PARTITION_TYPE partition,
                                       BLOCK_SIZE bsize) {
  const uint8_t *ret = NULL;
  // If this is a mixed vertical partition, look up bsize in orders_vert.
  if (partition == PARTITION_VERT_A || partition == PARTITION_VERT_B) {
    assert(bsize < BLOCK_SIZES);
    ret = has_tr_vert_tables[bsize];
  } else {
    ret = has_tr_tables[bsize];
  }
  assert(ret);
  return ret;
}

static int has_top_right(const AV1_COMMON *cm, BLOCK_SIZE bsize, int mi_row,
                         int mi_col, int top_available, int right_available,
                         PARTITION_TYPE partition, TX_SIZE txsz, int row_off,
                         int col_off, int ss_x, int ss_y) {
  if (!top_available || !right_available) return 0;

  const int bw_unit = block_size_wide[bsize] >> tx_size_wide_log2[0];
  const int plane_bw_unit = AOMMAX(bw_unit >> ss_x, 1);
  const int top_right_count_unit = tx_size_wide_unit[txsz];

  if (row_off > 0) {  // Just need to check if enough pixels on the right.
    if (block_size_wide[bsize] > block_size_wide[BLOCK_64X64]) {
      // Special case: For 128x128 blocks, the transform unit whose
      // top-right corner is at the center of the block does in fact have
      // pixels available at its top-right corner.
      if (row_off == mi_size_high[BLOCK_64X64] >> ss_y &&
          col_off + top_right_count_unit == mi_size_wide[BLOCK_64X64] >> ss_x) {
        return 1;
      }
      const int plane_bw_unit_64 = mi_size_wide[BLOCK_64X64] >> ss_x;
      const int col_off_64 = col_off % plane_bw_unit_64;
      return col_off_64 + top_right_count_unit < plane_bw_unit_64;
    }
    return col_off + top_right_count_unit < plane_bw_unit;
  } else {
    // All top-right pixels are in the block above, which is already available.
    if (col_off + top_right_count_unit < plane_bw_unit) return 1;

    const int bw_in_mi_log2 = mi_size_wide_log2[bsize];
    const int bh_in_mi_log2 = mi_size_high_log2[bsize];
    const int sb_mi_size = mi_size_high[cm->seq_params.sb_size];
    const int blk_row_in_sb =
#if CONFIG_3WAY_PARTITIONS
        (partition == PARTITION_HORZ_3 || partition == PARTITION_VERT_3)
            ? ROUND_POWER_OF_TWO(mi_row & (sb_mi_size - 1), bh_in_mi_log2)
            :
#endif  // CONFIG_3WAY_PARTITIONS
            (mi_row & (sb_mi_size - 1)) >> bh_in_mi_log2;

    const int blk_col_in_sb =
#if CONFIG_3WAY_PARTITIONS
        (partition == PARTITION_HORZ_3 || partition == PARTITION_VERT_3)
            ? ROUND_POWER_OF_TWO(mi_col & (sb_mi_size - 1), bw_in_mi_log2)
            :
#endif  // CONFIG_3WAY_PARTITIONS
            (mi_col & (sb_mi_size - 1)) >> bw_in_mi_log2;

    // Top row of superblock: so top-right pixels are in the top and/or
    // top-right superblocks, both of which are already available.
    if (blk_row_in_sb == 0) return 1;

    // Rightmost column of superblock (and not the top row): so top-right pixels
    // fall in the right superblock, which is not available yet.
    if (((blk_col_in_sb + 1) << bw_in_mi_log2) >= sb_mi_size) {
      return 0;
    }

    // General case (neither top row nor rightmost column): check if the
    // top-right block is coded before the current block.
    const int this_blk_index =
        ((blk_row_in_sb + 0) << (MAX_MIB_SIZE_LOG2 - bw_in_mi_log2)) +
        blk_col_in_sb + 0;
    const int idx1 = this_blk_index / 8;
    const int idx2 = this_blk_index % 8;
    const uint8_t *has_tr_table = get_has_tr_table(partition, bsize);
    return (has_tr_table[idx1] >> idx2) & 1;
  }
}

// Similar to the has_tr_* tables, but store if the bottom-left reference
// pixels are available.
static uint8_t has_bl_4x4[128] = {
  84, 85, 85, 85, 16, 17, 17, 17, 84, 85, 85, 85, 0,  1,  1,  1,  84, 85, 85,
  85, 16, 17, 17, 17, 84, 85, 85, 85, 0,  0,  1,  0,  84, 85, 85, 85, 16, 17,
  17, 17, 84, 85, 85, 85, 0,  1,  1,  1,  84, 85, 85, 85, 16, 17, 17, 17, 84,
  85, 85, 85, 0,  0,  0,  0,  84, 85, 85, 85, 16, 17, 17, 17, 84, 85, 85, 85,
  0,  1,  1,  1,  84, 85, 85, 85, 16, 17, 17, 17, 84, 85, 85, 85, 0,  0,  1,
  0,  84, 85, 85, 85, 16, 17, 17, 17, 84, 85, 85, 85, 0,  1,  1,  1,  84, 85,
  85, 85, 16, 17, 17, 17, 84, 85, 85, 85, 0,  0,  0,  0,
};
static uint8_t has_bl_4x8[64] = {
  16, 17, 17, 17, 0, 1, 1, 1, 16, 17, 17, 17, 0, 0, 1, 0,
  16, 17, 17, 17, 0, 1, 1, 1, 16, 17, 17, 17, 0, 0, 0, 0,
  16, 17, 17, 17, 0, 1, 1, 1, 16, 17, 17, 17, 0, 0, 1, 0,
  16, 17, 17, 17, 0, 1, 1, 1, 16, 17, 17, 17, 0, 0, 0, 0,
};
static uint8_t has_bl_8x4[64] = {
  254, 255, 84, 85, 254, 255, 16, 17, 254, 255, 84, 85, 254, 255, 0, 1,
  254, 255, 84, 85, 254, 255, 16, 17, 254, 255, 84, 85, 254, 255, 0, 0,
  254, 255, 84, 85, 254, 255, 16, 17, 254, 255, 84, 85, 254, 255, 0, 1,
  254, 255, 84, 85, 254, 255, 16, 17, 254, 255, 84, 85, 254, 255, 0, 0,
};
static uint8_t has_bl_8x8[32] = {
  84, 85, 16, 17, 84, 85, 0, 1, 84, 85, 16, 17, 84, 85, 0, 0,
  84, 85, 16, 17, 84, 85, 0, 1, 84, 85, 16, 17, 84, 85, 0, 0,
};
static uint8_t has_bl_8x16[16] = {
  16, 17, 0, 1, 16, 17, 0, 0, 16, 17, 0, 1, 16, 17, 0, 0,
};
static uint8_t has_bl_16x8[16] = {
  254, 84, 254, 16, 254, 84, 254, 0, 254, 84, 254, 16, 254, 84, 254, 0,
};
static uint8_t has_bl_16x16[8] = {
  84, 16, 84, 0, 84, 16, 84, 0,
};
static uint8_t has_bl_16x32[4] = { 16, 0, 16, 0 };
static uint8_t has_bl_32x16[4] = { 78, 14, 78, 14 };
static uint8_t has_bl_32x32[2] = { 4, 4 };
static uint8_t has_bl_32x64[1] = { 0 };
static uint8_t has_bl_64x32[1] = { 34 };
static uint8_t has_bl_64x64[1] = { 0 };
static uint8_t has_bl_64x128[1] = { 0 };
static uint8_t has_bl_128x64[1] = { 0 };
static uint8_t has_bl_128x128[1] = { 0 };
static uint8_t has_bl_4x16[32] = {
  0, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0,
  0, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0,
};
static uint8_t has_bl_16x4[32] = {
  254, 254, 254, 84, 254, 254, 254, 16, 254, 254, 254, 84, 254, 254, 254, 0,
  254, 254, 254, 84, 254, 254, 254, 16, 254, 254, 254, 84, 254, 254, 254, 0,
};
static uint8_t has_bl_8x32[8] = {
  0, 1, 0, 0, 0, 1, 0, 0,
};
static uint8_t has_bl_32x8[8] = {
  238, 78, 238, 14, 238, 78, 238, 14,
};
static uint8_t has_bl_16x64[2] = { 0, 0 };
static uint8_t has_bl_64x16[2] = { 42, 42 };
#if CONFIG_FLEX_PARTITION
static uint8_t has_bl_4x32[16] = { 0, 0, 1, 0, 0, 0, 0, 0,
                                   0, 0, 1, 0, 0, 0, 0, 0 };
static uint8_t has_bl_32x4[16] = {
  238, 238, 238, 78, 238, 238, 238, 14, 238, 238, 238, 78, 238, 238, 238, 14,
};
static uint8_t has_bl_4x64[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static uint8_t has_bl_64x4[8] = { 170, 170, 170, 42, 170, 170, 170, 42 };
static uint8_t has_bl_8x64[4] = {
  0,
  0,
  0,
  0,
};
static uint8_t has_bl_64x8[4] = {
  170,
  42,
  170,
  42,
};
#endif  // CONFIG_FLEX_PARTITION

static const uint8_t *const has_bl_tables[BLOCK_SIZES_ALL] = {
  has_bl_4x4,     has_bl_4x8,   has_bl_8x4,   has_bl_8x8,    has_bl_8x16,
  has_bl_16x8,    has_bl_16x16, has_bl_16x32, has_bl_32x16,  has_bl_32x32,
  has_bl_32x64,   has_bl_64x32, has_bl_64x64, has_bl_64x128, has_bl_128x64,
  has_bl_128x128, has_bl_4x16,  has_bl_16x4,  has_bl_8x32,   has_bl_32x8,
  has_bl_16x64,   has_bl_64x16,
#if CONFIG_FLEX_PARTITION
  has_bl_4x32,    has_bl_32x4,  has_bl_8x64,  has_bl_64x8,   has_bl_4x64,
  has_bl_64x4,
#endif  // CONFIG_FLEX_PARTITION
};

static uint8_t has_bl_vert_8x8[32] = {
  254, 255, 16, 17, 254, 255, 0, 1, 254, 255, 16, 17, 254, 255, 0, 0,
  254, 255, 16, 17, 254, 255, 0, 1, 254, 255, 16, 17, 254, 255, 0, 0,
};
static uint8_t has_bl_vert_16x16[8] = {
  254, 16, 254, 0, 254, 16, 254, 0,
};
static uint8_t has_bl_vert_32x32[2] = { 14, 14 };
static uint8_t has_bl_vert_64x64[1] = { 2 };

// The _vert_* tables are like the ordinary tables above, but describe the
// order we visit square blocks when doing a PARTITION_VERT_A or
// PARTITION_VERT_B. This is the same order as normal except for on the last
// split where we go vertically (TL, BL, TR, BR). We treat the rectangular block
// as a pair of squares, which means that these tables work correctly for both
// mixed vertical partition types.
//
// There are tables for each of the square sizes. Vertical rectangles (like
// BLOCK_16X32) use their respective "non-vert" table
static const uint8_t *const has_bl_vert_tables[BLOCK_SIZES] = {
  NULL,               // 4X4
  has_bl_4x8,         // 4X8
  NULL,               // 8X4
  has_bl_vert_8x8,    // 8X8
  has_bl_8x16,        // 8X16
  NULL,               // 16X8
  has_bl_vert_16x16,  // 16X16
  has_bl_16x32,       // 16X32
  NULL,               // 32X16
  has_bl_vert_32x32,  // 32X32
  has_bl_32x64,       // 32X64
  NULL,               // 64X32
  has_bl_vert_64x64,  // 64X64
  has_bl_64x128,      // 64X128
  NULL,               // 128X64
  has_bl_128x128,     // 128X128
};

static const uint8_t *get_has_bl_table(PARTITION_TYPE partition,
                                       BLOCK_SIZE bsize) {
  const uint8_t *ret = NULL;
  // If this is a mixed vertical partition, look up bsize in orders_vert.
  if (partition == PARTITION_VERT_A || partition == PARTITION_VERT_B) {
    assert(bsize < BLOCK_SIZES);
    ret = has_bl_vert_tables[bsize];
  } else {
    ret = has_bl_tables[bsize];
  }
  assert(ret);
  return ret;
}

static int has_bottom_left(const AV1_COMMON *cm, BLOCK_SIZE bsize, int mi_row,
                           int mi_col, int bottom_available, int left_available,
                           PARTITION_TYPE partition, TX_SIZE txsz, int row_off,
                           int col_off, int ss_x, int ss_y) {
  if (!bottom_available || !left_available) return 0;

  // Special case for 128x* blocks, when col_off is half the block width.
  // This is needed because 128x* superblocks are divided into 64x* blocks in
  // raster order
  if (block_size_wide[bsize] > block_size_wide[BLOCK_64X64] && col_off > 0) {
    const int plane_bw_unit_64 = mi_size_wide[BLOCK_64X64] >> ss_x;
    const int col_off_64 = col_off % plane_bw_unit_64;
    if (col_off_64 == 0) {
      // We are at the left edge of top-right or bottom-right 64x* block.
      const int plane_bh_unit_64 = mi_size_high[BLOCK_64X64] >> ss_y;
      const int row_off_64 = row_off % plane_bh_unit_64;
      const int plane_bh_unit =
          AOMMIN(mi_size_high[bsize] >> ss_y, plane_bh_unit_64);
      // Check if all bottom-left pixels are in the left 64x* block (which is
      // already coded).
      return row_off_64 + tx_size_high_unit[txsz] < plane_bh_unit;
    }
  }

  if (col_off > 0) {
    // Bottom-left pixels are in the bottom-left block, which is not available.
    return 0;
  } else {
    const int bh_unit = block_size_high[bsize] >> tx_size_high_log2[0];
    const int plane_bh_unit = AOMMAX(bh_unit >> ss_y, 1);
    const int bottom_left_count_unit = tx_size_high_unit[txsz];

    // All bottom-left pixels are in the left block, which is already available.
    if (row_off + bottom_left_count_unit < plane_bh_unit) return 1;

    const int bw_in_mi_log2 = mi_size_wide_log2[bsize];
    const int bh_in_mi_log2 = mi_size_high_log2[bsize];
    const int sb_mi_size = mi_size_high[cm->seq_params.sb_size];
    const int blk_row_in_sb =
#if CONFIG_3WAY_PARTITIONS
        (partition == PARTITION_HORZ_3 || partition == PARTITION_VERT_3)
            ? ROUND_POWER_OF_TWO(mi_row & (sb_mi_size - 1), bh_in_mi_log2)
            :
#endif  // CONFIG_3WAY_PARTITIONS
            (mi_row & (sb_mi_size - 1)) >> bh_in_mi_log2;

    const int blk_col_in_sb =
#if CONFIG_3WAY_PARTITIONS
        (partition == PARTITION_HORZ_3 || partition == PARTITION_VERT_3)
            ? ROUND_POWER_OF_TWO(mi_col & (sb_mi_size - 1), bw_in_mi_log2)
            :
#endif  // CONFIG_3WAY_PARTITIONS
            (mi_col & (sb_mi_size - 1)) >> bw_in_mi_log2;

    // Leftmost column of superblock: so bottom-left pixels maybe in the left
    // and/or bottom-left superblocks. But only the left superblock is
    // available, so check if all required pixels fall in that superblock.
    if (blk_col_in_sb == 0) {
      const int blk_start_row_off = blk_row_in_sb
                                        << (bh_in_mi_log2 + MI_SIZE_LOG2 -
                                            tx_size_wide_log2[0]) >>
                                    ss_y;
      const int row_off_in_sb = blk_start_row_off + row_off;
      const int sb_height_unit = sb_mi_size >> ss_y;
      return row_off_in_sb + bottom_left_count_unit < sb_height_unit;
    }

    // Bottom row of superblock (and not the leftmost column): so bottom-left
    // pixels fall in the bottom superblock, which is not available yet.
    if (((blk_row_in_sb + 1) << bh_in_mi_log2) >= sb_mi_size) return 0;

    // General case (neither leftmost column nor bottom row): check if the
    // bottom-left block is coded before the current block.
    const int this_blk_index =
        ((blk_row_in_sb + 0) << (MAX_MIB_SIZE_LOG2 - bw_in_mi_log2)) +
        blk_col_in_sb + 0;
    const int idx1 = this_blk_index / 8;
    const int idx2 = this_blk_index % 8;
    const uint8_t *has_bl_table = get_has_bl_table(partition, bsize);
    return (has_bl_table[idx1] >> idx2) & 1;
  }
}

typedef void (*intra_pred_fn)(uint8_t *dst, ptrdiff_t stride,
                              const uint8_t *above, const uint8_t *left);

static intra_pred_fn pred[INTRA_MODES][TX_SIZES_ALL];
static intra_pred_fn dc_pred[2][2][TX_SIZES_ALL];

typedef void (*intra_high_pred_fn)(uint16_t *dst, ptrdiff_t stride,
                                   const uint16_t *above, const uint16_t *left,
                                   int bd);
static intra_high_pred_fn pred_high[INTRA_MODES][TX_SIZES_ALL];
static intra_high_pred_fn dc_pred_high[2][2][TX_SIZES_ALL];

static void init_intra_predictors_internal(void) {
  assert(NELEMENTS(mode_to_angle_map) == INTRA_MODES);

#if CONFIG_FLEX_PARTITION
#define INIT_RECTANGULAR(p, type)             \
  p[TX_4X8] = aom_##type##_predictor_4x8;     \
  p[TX_8X4] = aom_##type##_predictor_8x4;     \
  p[TX_8X16] = aom_##type##_predictor_8x16;   \
  p[TX_16X8] = aom_##type##_predictor_16x8;   \
  p[TX_16X32] = aom_##type##_predictor_16x32; \
  p[TX_32X16] = aom_##type##_predictor_32x16; \
  p[TX_32X64] = aom_##type##_predictor_32x64; \
  p[TX_64X32] = aom_##type##_predictor_64x32; \
  p[TX_4X16] = aom_##type##_predictor_4x16;   \
  p[TX_16X4] = aom_##type##_predictor_16x4;   \
  p[TX_8X32] = aom_##type##_predictor_8x32;   \
  p[TX_32X8] = aom_##type##_predictor_32x8;   \
  p[TX_16X64] = aom_##type##_predictor_16x64; \
  p[TX_64X16] = aom_##type##_predictor_64x16; \
  p[TX_4X32] = aom_##type##_predictor_4x32;   \
  p[TX_32X4] = aom_##type##_predictor_32x4;   \
  p[TX_8X32] = aom_##type##_predictor_8x64;   \
  p[TX_32X8] = aom_##type##_predictor_64x8;   \
  p[TX_4X64] = aom_##type##_predictor_4x64;   \
  p[TX_64X4] = aom_##type##_predictor_64x4;
#else
#define INIT_RECTANGULAR(p, type)             \
  p[TX_4X8] = aom_##type##_predictor_4x8;     \
  p[TX_8X4] = aom_##type##_predictor_8x4;     \
  p[TX_8X16] = aom_##type##_predictor_8x16;   \
  p[TX_16X8] = aom_##type##_predictor_16x8;   \
  p[TX_16X32] = aom_##type##_predictor_16x32; \
  p[TX_32X16] = aom_##type##_predictor_32x16; \
  p[TX_32X64] = aom_##type##_predictor_32x64; \
  p[TX_64X32] = aom_##type##_predictor_64x32; \
  p[TX_4X16] = aom_##type##_predictor_4x16;   \
  p[TX_16X4] = aom_##type##_predictor_16x4;   \
  p[TX_8X32] = aom_##type##_predictor_8x32;   \
  p[TX_32X8] = aom_##type##_predictor_32x8;   \
  p[TX_16X64] = aom_##type##_predictor_16x64; \
  p[TX_64X16] = aom_##type##_predictor_64x16;
#endif  // CONFIG_FLEX_PARTITION

#define INIT_NO_4X4(p, type)                  \
  p[TX_8X8] = aom_##type##_predictor_8x8;     \
  p[TX_16X16] = aom_##type##_predictor_16x16; \
  p[TX_32X32] = aom_##type##_predictor_32x32; \
  p[TX_64X64] = aom_##type##_predictor_64x64; \
  INIT_RECTANGULAR(p, type)

#define INIT_ALL_SIZES(p, type)           \
  p[TX_4X4] = aom_##type##_predictor_4x4; \
  INIT_NO_4X4(p, type)

  INIT_ALL_SIZES(pred[V_PRED], v);
  INIT_ALL_SIZES(pred[H_PRED], h);
  INIT_ALL_SIZES(pred[PAETH_PRED], paeth);
  INIT_ALL_SIZES(pred[SMOOTH_PRED], smooth);
  INIT_ALL_SIZES(pred[SMOOTH_V_PRED], smooth_v);
  INIT_ALL_SIZES(pred[SMOOTH_H_PRED], smooth_h);
  INIT_ALL_SIZES(dc_pred[0][0], dc_128);
  INIT_ALL_SIZES(dc_pred[0][1], dc_top);
  INIT_ALL_SIZES(dc_pred[1][0], dc_left);
  INIT_ALL_SIZES(dc_pred[1][1], dc);

  INIT_ALL_SIZES(pred_high[V_PRED], highbd_v);
  INIT_ALL_SIZES(pred_high[H_PRED], highbd_h);
  INIT_ALL_SIZES(pred_high[PAETH_PRED], highbd_paeth);
  INIT_ALL_SIZES(pred_high[SMOOTH_PRED], highbd_smooth);
  INIT_ALL_SIZES(pred_high[SMOOTH_V_PRED], highbd_smooth_v);
  INIT_ALL_SIZES(pred_high[SMOOTH_H_PRED], highbd_smooth_h);
  INIT_ALL_SIZES(dc_pred_high[0][0], highbd_dc_128);
  INIT_ALL_SIZES(dc_pred_high[0][1], highbd_dc_top);
  INIT_ALL_SIZES(dc_pred_high[1][0], highbd_dc_left);
  INIT_ALL_SIZES(dc_pred_high[1][1], highbd_dc);
#undef intra_pred_allsizes
}

// Directional prediction, zone 1: 0 < angle < 90
void av1_dr_prediction_z1_c(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                            const uint8_t *above, const uint8_t *left,
                            int upsample_above, int dx, int dy) {
  int r, c, x, base, shift, val;

  (void)left;
  (void)dy;
  assert(dy == 1);
  assert(dx > 0);

  const int max_base_x = ((bw + bh) - 1) << upsample_above;
  const int frac_bits = 6 - upsample_above;
  const int base_inc = 1 << upsample_above;
  x = dx;
  for (r = 0; r < bh; ++r, dst += stride, x += dx) {
    base = x >> frac_bits;
    shift = ((x << upsample_above) & 0x3F) >> 1;

    if (base >= max_base_x) {
      for (int i = r; i < bh; ++i) {
        memset(dst, above[max_base_x], bw * sizeof(dst[0]));
        dst += stride;
      }
      return;
    }

    for (c = 0; c < bw; ++c, base += base_inc) {
      if (base < max_base_x) {
        val = above[base] * (32 - shift) + above[base + 1] * shift;
        dst[c] = ROUND_POWER_OF_TWO(val, 5);
      } else {
        dst[c] = above[max_base_x];
      }
    }
  }
}

// Directional prediction, zone 2: 90 < angle < 180
void av1_dr_prediction_z2_c(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                            const uint8_t *above, const uint8_t *left,
                            int upsample_above, int upsample_left, int dx,
                            int dy) {
  assert(dx > 0);
  assert(dy > 0);

  const int min_base_x = -(1 << upsample_above);
  const int min_base_y = -(1 << upsample_left);
  (void)min_base_y;
  const int frac_bits_x = 6 - upsample_above;
  const int frac_bits_y = 6 - upsample_left;

  for (int r = 0; r < bh; ++r) {
    for (int c = 0; c < bw; ++c) {
      int val;
      int y = r + 1;
      int x = (c << 6) - y * dx;
      const int base_x = x >> frac_bits_x;
      if (base_x >= min_base_x) {
        const int shift = ((x * (1 << upsample_above)) & 0x3F) >> 1;
        val = above[base_x] * (32 - shift) + above[base_x + 1] * shift;
        val = ROUND_POWER_OF_TWO(val, 5);
      } else {
        x = c + 1;
        y = (r << 6) - x * dy;
        const int base_y = y >> frac_bits_y;
        assert(base_y >= min_base_y);
        const int shift = ((y * (1 << upsample_left)) & 0x3F) >> 1;
        val = left[base_y] * (32 - shift) + left[base_y + 1] * shift;
        val = ROUND_POWER_OF_TWO(val, 5);
      }
      dst[c] = val;
    }
    dst += stride;
  }
}

// Directional prediction, zone 3: 180 < angle < 270
void av1_dr_prediction_z3_c(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                            const uint8_t *above, const uint8_t *left,
                            int upsample_left, int dx, int dy) {
  int r, c, y, base, shift, val;

  (void)above;
  (void)dx;

  assert(dx == 1);
  assert(dy > 0);

  const int max_base_y = (bw + bh - 1) << upsample_left;
  const int frac_bits = 6 - upsample_left;
  const int base_inc = 1 << upsample_left;
  y = dy;
  for (c = 0; c < bw; ++c, y += dy) {
    base = y >> frac_bits;
    shift = ((y << upsample_left) & 0x3F) >> 1;

    for (r = 0; r < bh; ++r, base += base_inc) {
      if (base < max_base_y) {
        val = left[base] * (32 - shift) + left[base + 1] * shift;
        dst[r * stride + c] = val = ROUND_POWER_OF_TWO(val, 5);
      } else {
        for (; r < bh; ++r) dst[r * stride + c] = left[max_base_y];
        break;
      }
    }
  }
}

static void dr_predictor(uint8_t *dst, ptrdiff_t stride, TX_SIZE tx_size,
                         const uint8_t *above, const uint8_t *left,
                         int upsample_above, int upsample_left, int angle) {
  const int dx = av1_get_dx(angle);
  const int dy = av1_get_dy(angle);
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];
  assert(angle > 0 && angle < 270);

  if (angle > 0 && angle < 90) {
    av1_dr_prediction_z1(dst, stride, bw, bh, above, left, upsample_above, dx,
                         dy);
  } else if (angle > 90 && angle < 180) {
    av1_dr_prediction_z2(dst, stride, bw, bh, above, left, upsample_above,
                         upsample_left, dx, dy);
  } else if (angle > 180 && angle < 270) {
    av1_dr_prediction_z3(dst, stride, bw, bh, above, left, upsample_left, dx,
                         dy);
  } else if (angle == 90) {
    pred[V_PRED][tx_size](dst, stride, above, left);
  } else if (angle == 180) {
    pred[H_PRED][tx_size](dst, stride, above, left);
  }
}

// Directional prediction, zone 1: 0 < angle < 90
void av1_highbd_dr_prediction_z1_c(uint16_t *dst, ptrdiff_t stride, int bw,
                                   int bh, const uint16_t *above,
                                   const uint16_t *left, int upsample_above,
                                   int dx, int dy, int bd) {
  int r, c, x, base, shift, val;

  (void)left;
  (void)dy;
  (void)bd;
  assert(dy == 1);
  assert(dx > 0);

  const int max_base_x = ((bw + bh) - 1) << upsample_above;
  const int frac_bits = 6 - upsample_above;
  const int base_inc = 1 << upsample_above;
  x = dx;
  for (r = 0; r < bh; ++r, dst += stride, x += dx) {
    base = x >> frac_bits;
    shift = ((x << upsample_above) & 0x3F) >> 1;

    if (base >= max_base_x) {
      for (int i = r; i < bh; ++i) {
        aom_memset16(dst, above[max_base_x], bw);
        dst += stride;
      }
      return;
    }

    for (c = 0; c < bw; ++c, base += base_inc) {
      if (base < max_base_x) {
        val = above[base] * (32 - shift) + above[base + 1] * shift;
        dst[c] = ROUND_POWER_OF_TWO(val, 5);
      } else {
        dst[c] = above[max_base_x];
      }
    }
  }
}

// Directional prediction, zone 2: 90 < angle < 180
void av1_highbd_dr_prediction_z2_c(uint16_t *dst, ptrdiff_t stride, int bw,
                                   int bh, const uint16_t *above,
                                   const uint16_t *left, int upsample_above,
                                   int upsample_left, int dx, int dy, int bd) {
  (void)bd;
  assert(dx > 0);
  assert(dy > 0);

  const int min_base_x = -(1 << upsample_above);
  const int min_base_y = -(1 << upsample_left);
  (void)min_base_y;
  const int frac_bits_x = 6 - upsample_above;
  const int frac_bits_y = 6 - upsample_left;

  for (int r = 0; r < bh; ++r) {
    for (int c = 0; c < bw; ++c) {
      int val;
      int y = r + 1;
      int x = (c << 6) - y * dx;
      const int base_x = x >> frac_bits_x;
      if (base_x >= min_base_x) {
        const int shift = ((x * (1 << upsample_above)) & 0x3F) >> 1;
        val = above[base_x] * (32 - shift) + above[base_x + 1] * shift;
        val = ROUND_POWER_OF_TWO(val, 5);
      } else {
        x = c + 1;
        y = (r << 6) - x * dy;
        const int base_y = y >> frac_bits_y;
        assert(base_y >= min_base_y);
        const int shift = ((y * (1 << upsample_left)) & 0x3F) >> 1;
        val = left[base_y] * (32 - shift) + left[base_y + 1] * shift;
        val = ROUND_POWER_OF_TWO(val, 5);
      }
      dst[c] = val;
    }
    dst += stride;
  }
}

// Directional prediction, zone 3: 180 < angle < 270
void av1_highbd_dr_prediction_z3_c(uint16_t *dst, ptrdiff_t stride, int bw,
                                   int bh, const uint16_t *above,
                                   const uint16_t *left, int upsample_left,
                                   int dx, int dy, int bd) {
  int r, c, y, base, shift, val;

  (void)above;
  (void)dx;
  (void)bd;
  assert(dx == 1);
  assert(dy > 0);

  const int max_base_y = (bw + bh - 1) << upsample_left;
  const int frac_bits = 6 - upsample_left;
  const int base_inc = 1 << upsample_left;
  y = dy;
  for (c = 0; c < bw; ++c, y += dy) {
    base = y >> frac_bits;
    shift = ((y << upsample_left) & 0x3F) >> 1;

    for (r = 0; r < bh; ++r, base += base_inc) {
      if (base < max_base_y) {
        val = left[base] * (32 - shift) + left[base + 1] * shift;
        dst[r * stride + c] = ROUND_POWER_OF_TWO(val, 5);
      } else {
        for (; r < bh; ++r) dst[r * stride + c] = left[max_base_y];
        break;
      }
    }
  }
}

static void highbd_dr_predictor(uint16_t *dst, ptrdiff_t stride,
                                TX_SIZE tx_size, const uint16_t *above,
                                const uint16_t *left, int upsample_above,
                                int upsample_left, int angle, int bd) {
  const int dx = av1_get_dx(angle);
  const int dy = av1_get_dy(angle);
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];
  assert(angle > 0 && angle < 270);

  if (angle > 0 && angle < 90) {
    av1_highbd_dr_prediction_z1(dst, stride, bw, bh, above, left,
                                upsample_above, dx, dy, bd);
  } else if (angle > 90 && angle < 180) {
    av1_highbd_dr_prediction_z2(dst, stride, bw, bh, above, left,
                                upsample_above, upsample_left, dx, dy, bd);
  } else if (angle > 180 && angle < 270) {
    av1_highbd_dr_prediction_z3(dst, stride, bw, bh, above, left, upsample_left,
                                dx, dy, bd);
  } else if (angle == 90) {
    pred_high[V_PRED][tx_size](dst, stride, above, left, bd);
  } else if (angle == 180) {
    pred_high[H_PRED][tx_size](dst, stride, above, left, bd);
  }
}

DECLARE_ALIGNED(16, const int8_t,
                av1_filter_intra_taps[FILTER_INTRA_MODES][8][8]) = {
  {
      { -6, 10, 0, 0, 0, 12, 0, 0 },
      { -5, 2, 10, 0, 0, 9, 0, 0 },
      { -3, 1, 1, 10, 0, 7, 0, 0 },
      { -3, 1, 1, 2, 10, 5, 0, 0 },
      { -4, 6, 0, 0, 0, 2, 12, 0 },
      { -3, 2, 6, 0, 0, 2, 9, 0 },
      { -3, 2, 2, 6, 0, 2, 7, 0 },
      { -3, 1, 2, 2, 6, 3, 5, 0 },
  },
  {
      { -10, 16, 0, 0, 0, 10, 0, 0 },
      { -6, 0, 16, 0, 0, 6, 0, 0 },
      { -4, 0, 0, 16, 0, 4, 0, 0 },
      { -2, 0, 0, 0, 16, 2, 0, 0 },
      { -10, 16, 0, 0, 0, 0, 10, 0 },
      { -6, 0, 16, 0, 0, 0, 6, 0 },
      { -4, 0, 0, 16, 0, 0, 4, 0 },
      { -2, 0, 0, 0, 16, 0, 2, 0 },
  },
  {
      { -8, 8, 0, 0, 0, 16, 0, 0 },
      { -8, 0, 8, 0, 0, 16, 0, 0 },
      { -8, 0, 0, 8, 0, 16, 0, 0 },
      { -8, 0, 0, 0, 8, 16, 0, 0 },
      { -4, 4, 0, 0, 0, 0, 16, 0 },
      { -4, 0, 4, 0, 0, 0, 16, 0 },
      { -4, 0, 0, 4, 0, 0, 16, 0 },
      { -4, 0, 0, 0, 4, 0, 16, 0 },
  },
  {
      { -2, 8, 0, 0, 0, 10, 0, 0 },
      { -1, 3, 8, 0, 0, 6, 0, 0 },
      { -1, 2, 3, 8, 0, 4, 0, 0 },
      { 0, 1, 2, 3, 8, 2, 0, 0 },
      { -1, 4, 0, 0, 0, 3, 10, 0 },
      { -1, 3, 4, 0, 0, 4, 6, 0 },
      { -1, 2, 3, 4, 0, 4, 4, 0 },
      { -1, 2, 2, 3, 4, 3, 3, 0 },
  },
  {
      { -12, 14, 0, 0, 0, 14, 0, 0 },
      { -10, 0, 14, 0, 0, 12, 0, 0 },
      { -9, 0, 0, 14, 0, 11, 0, 0 },
      { -8, 0, 0, 0, 14, 10, 0, 0 },
      { -10, 12, 0, 0, 0, 0, 14, 0 },
      { -9, 1, 12, 0, 0, 0, 12, 0 },
      { -8, 0, 0, 12, 0, 1, 11, 0 },
      { -7, 0, 0, 1, 12, 1, 9, 0 },
  },
};

void av1_filter_intra_predictor_c(uint8_t *dst, ptrdiff_t stride,
                                  TX_SIZE tx_size, const uint8_t *above,
                                  const uint8_t *left, int mode) {
  int r, c;
  uint8_t buffer[33][33];
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];

  assert(bw <= 32 && bh <= 32);

  // The initialization is just for silencing Jenkins static analysis warnings
  for (r = 0; r < bh + 1; ++r)
    memset(buffer[r], 0, (bw + 1) * sizeof(buffer[0][0]));

  for (r = 0; r < bh; ++r) buffer[r + 1][0] = left[r];
  memcpy(buffer[0], &above[-1], (bw + 1) * sizeof(uint8_t));

  for (r = 1; r < bh + 1; r += 2)
    for (c = 1; c < bw + 1; c += 4) {
      const uint8_t p0 = buffer[r - 1][c - 1];
      const uint8_t p1 = buffer[r - 1][c];
      const uint8_t p2 = buffer[r - 1][c + 1];
      const uint8_t p3 = buffer[r - 1][c + 2];
      const uint8_t p4 = buffer[r - 1][c + 3];
      const uint8_t p5 = buffer[r][c - 1];
      const uint8_t p6 = buffer[r + 1][c - 1];
      for (int k = 0; k < 8; ++k) {
        int r_offset = k >> 2;
        int c_offset = k & 0x03;
        buffer[r + r_offset][c + c_offset] =
            clip_pixel(ROUND_POWER_OF_TWO_SIGNED(
                av1_filter_intra_taps[mode][k][0] * p0 +
                    av1_filter_intra_taps[mode][k][1] * p1 +
                    av1_filter_intra_taps[mode][k][2] * p2 +
                    av1_filter_intra_taps[mode][k][3] * p3 +
                    av1_filter_intra_taps[mode][k][4] * p4 +
                    av1_filter_intra_taps[mode][k][5] * p5 +
                    av1_filter_intra_taps[mode][k][6] * p6,
                FILTER_INTRA_SCALE_BITS));
      }
    }

  for (r = 0; r < bh; ++r) {
    memcpy(dst, &buffer[r + 1][1], bw * sizeof(uint8_t));
    dst += stride;
  }
}

static void highbd_filter_intra_predictor(uint16_t *dst, ptrdiff_t stride,
                                          TX_SIZE tx_size,
                                          const uint16_t *above,
                                          const uint16_t *left, int mode,
                                          int bd) {
  int r, c;
  uint16_t buffer[33][33];
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];

  assert(bw <= 32 && bh <= 32);

  // The initialization is just for silencing Jenkins static analysis warnings
  for (r = 0; r < bh + 1; ++r)
    memset(buffer[r], 0, (bw + 1) * sizeof(buffer[0][0]));

  for (r = 0; r < bh; ++r) buffer[r + 1][0] = left[r];
  memcpy(buffer[0], &above[-1], (bw + 1) * sizeof(buffer[0][0]));

  for (r = 1; r < bh + 1; r += 2)
    for (c = 1; c < bw + 1; c += 4) {
      const uint16_t p0 = buffer[r - 1][c - 1];
      const uint16_t p1 = buffer[r - 1][c];
      const uint16_t p2 = buffer[r - 1][c + 1];
      const uint16_t p3 = buffer[r - 1][c + 2];
      const uint16_t p4 = buffer[r - 1][c + 3];
      const uint16_t p5 = buffer[r][c - 1];
      const uint16_t p6 = buffer[r + 1][c - 1];
      for (int k = 0; k < 8; ++k) {
        int r_offset = k >> 2;
        int c_offset = k & 0x03;
        buffer[r + r_offset][c + c_offset] =
            clip_pixel_highbd(ROUND_POWER_OF_TWO_SIGNED(
                                  av1_filter_intra_taps[mode][k][0] * p0 +
                                      av1_filter_intra_taps[mode][k][1] * p1 +
                                      av1_filter_intra_taps[mode][k][2] * p2 +
                                      av1_filter_intra_taps[mode][k][3] * p3 +
                                      av1_filter_intra_taps[mode][k][4] * p4 +
                                      av1_filter_intra_taps[mode][k][5] * p5 +
                                      av1_filter_intra_taps[mode][k][6] * p6,
                                  FILTER_INTRA_SCALE_BITS),
                              bd);
      }
    }

  for (r = 0; r < bh; ++r) {
    memcpy(dst, &buffer[r + 1][1], bw * sizeof(dst[0]));
    dst += stride;
  }
}

static int is_smooth(const MB_MODE_INFO *mbmi, int plane) {
  if (plane == 0) {
    const PREDICTION_MODE mode = mbmi->mode;
    return (mode == SMOOTH_PRED || mode == SMOOTH_V_PRED ||
            mode == SMOOTH_H_PRED);
  } else {
    // uv_mode is not set for inter blocks, so need to explicitly
    // detect that case.
    if (is_inter_block(mbmi)) return 0;

    const UV_PREDICTION_MODE uv_mode = mbmi->uv_mode;
    return (uv_mode == UV_SMOOTH_PRED || uv_mode == UV_SMOOTH_V_PRED ||
            uv_mode == UV_SMOOTH_H_PRED);
  }
}

static int get_filt_type(const MACROBLOCKD *xd, int plane) {
  int ab_sm, le_sm;

  if (plane == 0) {
    const MB_MODE_INFO *ab = xd->above_mbmi;
    const MB_MODE_INFO *le = xd->left_mbmi;
    ab_sm = ab ? is_smooth(ab, plane) : 0;
    le_sm = le ? is_smooth(le, plane) : 0;
  } else {
    const MB_MODE_INFO *ab = xd->chroma_above_mbmi;
    const MB_MODE_INFO *le = xd->chroma_left_mbmi;
    ab_sm = ab ? is_smooth(ab, plane) : 0;
    le_sm = le ? is_smooth(le, plane) : 0;
  }

  return (ab_sm || le_sm) ? 1 : 0;
}

static int intra_edge_filter_strength(int bs0, int bs1, int delta, int type) {
  const int d = abs(delta);
  int strength = 0;

  const int blk_wh = bs0 + bs1;
  if (type == 0) {
    if (blk_wh <= 8) {
      if (d >= 56) strength = 1;
    } else if (blk_wh <= 12) {
      if (d >= 40) strength = 1;
    } else if (blk_wh <= 16) {
      if (d >= 40) strength = 1;
    } else if (blk_wh <= 24) {
      if (d >= 8) strength = 1;
      if (d >= 16) strength = 2;
      if (d >= 32) strength = 3;
    } else if (blk_wh <= 32) {
      if (d >= 1) strength = 1;
      if (d >= 4) strength = 2;
      if (d >= 32) strength = 3;
    } else {
      if (d >= 1) strength = 3;
    }
  } else {
    if (blk_wh <= 8) {
      if (d >= 40) strength = 1;
      if (d >= 64) strength = 2;
    } else if (blk_wh <= 16) {
      if (d >= 20) strength = 1;
      if (d >= 48) strength = 2;
    } else if (blk_wh <= 24) {
      if (d >= 4) strength = 3;
    } else {
      if (d >= 1) strength = 3;
    }
  }
  return strength;
}

void av1_filter_intra_edge_c(uint8_t *p, int sz, int strength) {
  if (!strength) return;

  const int kernel[INTRA_EDGE_FILT][INTRA_EDGE_TAPS] = { { 0, 4, 8, 4, 0 },
                                                         { 0, 5, 6, 5, 0 },
                                                         { 2, 4, 4, 4, 2 } };
  const int filt = strength - 1;
  uint8_t edge[129];

  memcpy(edge, p, sz * sizeof(*p));
  for (int i = 1; i < sz; i++) {
    int s = 0;
    for (int j = 0; j < INTRA_EDGE_TAPS; j++) {
      int k = i - 2 + j;
      k = (k < 0) ? 0 : k;
      k = (k > sz - 1) ? sz - 1 : k;
      s += edge[k] * kernel[filt][j];
    }
    s = (s + 8) >> 4;
    p[i] = s;
  }
}

static void filter_intra_edge_corner(uint8_t *p_above, uint8_t *p_left) {
  const int kernel[3] = { 5, 6, 5 };

  int s = (p_left[0] * kernel[0]) + (p_above[-1] * kernel[1]) +
          (p_above[0] * kernel[2]);
  s = (s + 8) >> 4;
  p_above[-1] = s;
  p_left[-1] = s;
}

void av1_filter_intra_edge_high_c(uint16_t *p, int sz, int strength) {
  if (!strength) return;

  const int kernel[INTRA_EDGE_FILT][INTRA_EDGE_TAPS] = { { 0, 4, 8, 4, 0 },
                                                         { 0, 5, 6, 5, 0 },
                                                         { 2, 4, 4, 4, 2 } };
  const int filt = strength - 1;
  uint16_t edge[129];

  memcpy(edge, p, sz * sizeof(*p));
  for (int i = 1; i < sz; i++) {
    int s = 0;
    for (int j = 0; j < INTRA_EDGE_TAPS; j++) {
      int k = i - 2 + j;
      k = (k < 0) ? 0 : k;
      k = (k > sz - 1) ? sz - 1 : k;
      s += edge[k] * kernel[filt][j];
    }
    s = (s + 8) >> 4;
    p[i] = s;
  }
}

static void filter_intra_edge_corner_high(uint16_t *p_above, uint16_t *p_left) {
  const int kernel[3] = { 5, 6, 5 };

  int s = (p_left[0] * kernel[0]) + (p_above[-1] * kernel[1]) +
          (p_above[0] * kernel[2]);
  s = (s + 8) >> 4;
  p_above[-1] = s;
  p_left[-1] = s;
}

void av1_upsample_intra_edge_c(uint8_t *p, int sz) {
  // interpolate half-sample positions
  assert(sz <= MAX_UPSAMPLE_SZ);

  uint8_t in[MAX_UPSAMPLE_SZ + 3];
  // copy p[-1..(sz-1)] and extend first and last samples
  in[0] = p[-1];
  in[1] = p[-1];
  for (int i = 0; i < sz; i++) {
    in[i + 2] = p[i];
  }
  in[sz + 2] = p[sz - 1];

  // interpolate half-sample edge positions
  p[-2] = in[0];
  for (int i = 0; i < sz; i++) {
    int s = -in[i] + (9 * in[i + 1]) + (9 * in[i + 2]) - in[i + 3];
    s = clip_pixel((s + 8) >> 4);
    p[2 * i - 1] = s;
    p[2 * i] = in[i + 2];
  }
}

void av1_upsample_intra_edge_high_c(uint16_t *p, int sz, int bd) {
  // interpolate half-sample positions
  assert(sz <= MAX_UPSAMPLE_SZ);

  uint16_t in[MAX_UPSAMPLE_SZ + 3];
  // copy p[-1..(sz-1)] and extend first and last samples
  in[0] = p[-1];
  in[1] = p[-1];
  for (int i = 0; i < sz; i++) {
    in[i + 2] = p[i];
  }
  in[sz + 2] = p[sz - 1];

  // interpolate half-sample edge positions
  p[-2] = in[0];
  for (int i = 0; i < sz; i++) {
    int s = -in[i] + (9 * in[i + 1]) + (9 * in[i + 2]) - in[i + 3];
    s = (s + 8) >> 4;
    s = clip_pixel_highbd(s, bd);
    p[2 * i - 1] = s;
    p[2 * i] = in[i + 2];
  }
}

#if CONFIG_ADAPT_FILTER_INTRA
#define ADAPT_FILTER_INTRA_GET_SRC_VAL_0 src[(i + 1) * stride + j - 1]
#define ADAPT_FILTER_INTRA_GET_SRC_VAL_1 src[i * stride + j - 1]
#define ADAPT_FILTER_INTRA_GET_SRC_VAL_2 src[(i - 1) * stride + j - 1]
#define ADAPT_FILTER_INTRA_GET_SRC_VAL_3 src[(i - 1) * stride + j]
#define ADAPT_FILTER_INTRA_GET_SRC_VAL_4 src[(i - 1) * stride + j + 1]

#define ADAPT_FILTER_INTRA_DEFINE_3_TAP_ACCUM_FUNC(func_name, tap1, tap2, \
                                                   tap3, data_type)       \
  void func_name(const data_type *src, int stride, int w, int h,          \
                 int64_t *dst_buf) {                                      \
    for (int i = 0; i < h; i++) {                                         \
      for (int j = 0; j < w; j++) {                                       \
        const int x = src[i * stride + j];                                \
        const int v1 = ADAPT_FILTER_INTRA_GET_SRC_VAL_##tap1;             \
        const int v2 = ADAPT_FILTER_INTRA_GET_SRC_VAL_##tap2;             \
        const int v3 = ADAPT_FILTER_INTRA_GET_SRC_VAL_##tap3;             \
        dst_buf[0] += v1 * v1;                                            \
        dst_buf[1] += v1 * v2;                                            \
        dst_buf[2] += v2 * v2;                                            \
        dst_buf[3] += v1 * v3;                                            \
        dst_buf[4] += v2 * v3;                                            \
        dst_buf[5] += v3 * v3;                                            \
        dst_buf[6] += v1 * x;                                             \
        dst_buf[7] += v2 * x;                                             \
        dst_buf[8] += v3 * x;                                             \
      }                                                                   \
    }                                                                     \
  }

#define ADAPT_FILTER_INTRA_DEFINE_4_TAP_ACCUM_FUNC(func_name, tap1, tap2, \
                                                   tap3, tap4, data_type) \
  void func_name(const data_type *src, int stride, int w, int h,          \
                 int64_t *dst_buf) {                                      \
    for (int i = 0; i < h; i++) {                                         \
      for (int j = 0; j < w; j++) {                                       \
        const int x = src[i * stride + j];                                \
        const int v1 = ADAPT_FILTER_INTRA_GET_SRC_VAL_##tap1;             \
        const int v2 = ADAPT_FILTER_INTRA_GET_SRC_VAL_##tap2;             \
        const int v3 = ADAPT_FILTER_INTRA_GET_SRC_VAL_##tap3;             \
        const int v4 = ADAPT_FILTER_INTRA_GET_SRC_VAL_##tap4;             \
        dst_buf[0] += v1 * v1;                                            \
        dst_buf[1] += v1 * v2;                                            \
        dst_buf[2] += v2 * v2;                                            \
        dst_buf[3] += v1 * v3;                                            \
        dst_buf[4] += v2 * v3;                                            \
        dst_buf[5] += v3 * v3;                                            \
        dst_buf[6] += v1 * v4;                                            \
        dst_buf[7] += v2 * v4;                                            \
        dst_buf[8] += v3 * v4;                                            \
        dst_buf[9] += v4 * v4;                                            \
        dst_buf[10] += v1 * x;                                            \
        dst_buf[11] += v2 * x;                                            \
        dst_buf[12] += v3 * x;                                            \
        dst_buf[13] += v4 * x;                                            \
      }                                                                   \
    }                                                                     \
  }

#define ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_ROW_MAJOR(                      \
    func_name, pred_expression, data_type)                                  \
  void func_name(data_type *dst, int stride, TX_SIZE tx_size, double *filt, \
                 const data_type *above, const data_type *left) {           \
    int r, c;                                                               \
    double buf[65][66];                                                     \
    const int bw = tx_size_wide[tx_size];                                   \
    const int bh = tx_size_high[tx_size];                                   \
    for (r = 0; r < bh; ++r) buf[r + 1][0] = (double)left[r];               \
    for (c = 0; c < bw + 1; ++c) buf[0][c] = (double)above[c - 1];          \
    for (r = 0; r < bh + 1; ++r) buf[r][bw + 1] = (double)above[bw];        \
    for (r = 1; r < bh + 1; ++r) {                                          \
      for (c = 1; c < bw + 1; ++c) {                                        \
        buf[r][c] = (pred_expression);                                      \
        dst[(r - 1) * stride + c - 1] =                                     \
            (uint16_t)(AOMMIN(AOMMAX(buf[r][c], 0.001), 254.999) + 0.5);    \
      }                                                                     \
    }                                                                       \
  }

#define ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_COL_MAJOR(                      \
    func_name, pred_expression, data_type)                                  \
  void func_name(data_type *dst, int stride, TX_SIZE tx_size, double *filt, \
                 const data_type *above, const data_type *left) {           \
    int r, c;                                                               \
    double buf[66][65];                                                     \
    const int bw = tx_size_wide[tx_size];                                   \
    const int bh = tx_size_high[tx_size];                                   \
    for (r = 0; r < bh; ++r) buf[r + 1][0] = (double)left[r];               \
    for (c = 0; c < bw + 1; ++c) buf[0][c] = (double)above[c - 1];          \
    for (c = 0; c < bw + 1; ++c) buf[bh + 1][c] = (double)left[bh];         \
    for (c = 1; c < bw + 1; ++c) {                                          \
      for (r = 1; r < bh + 1; ++r) {                                        \
        buf[r][c] = (pred_expression);                                      \
        dst[(r - 1) * stride + c - 1] =                                     \
            (uint16_t)(AOMMIN(AOMMAX(buf[r][c], 0.001), 254.999) + 0.5);    \
      }                                                                     \
    }                                                                       \
  }

// Set up functions for accumulating statistics necessary to adaptively fit
// filter coefficients for the transform unit:
typedef void (*adapt_filter_intra_accum_fn)(const uint8_t *src, int stride,
                                            int w, int h, int64_t *dst_buf);
ADAPT_FILTER_INTRA_DEFINE_3_TAP_ACCUM_FUNC(adapt_filter_intra_accum_0, 1, 2, 3,
                                           uint8_t)
ADAPT_FILTER_INTRA_DEFINE_3_TAP_ACCUM_FUNC(adapt_filter_intra_accum_1, 0, 1, 3,
                                           uint8_t)
ADAPT_FILTER_INTRA_DEFINE_3_TAP_ACCUM_FUNC(adapt_filter_intra_accum_2, 1, 3, 4,
                                           uint8_t)
ADAPT_FILTER_INTRA_DEFINE_4_TAP_ACCUM_FUNC(adapt_filter_intra_accum_3, 0, 1, 2,
                                           3, uint8_t)
ADAPT_FILTER_INTRA_DEFINE_4_TAP_ACCUM_FUNC(adapt_filter_intra_accum_4, 1, 2, 3,
                                           4, uint8_t)
ADAPT_FILTER_INTRA_DEFINE_3_TAP_ACCUM_FUNC(adapt_filter_intra_accum_5, 0, 2, 3,
                                           uint8_t)
ADAPT_FILTER_INTRA_DEFINE_3_TAP_ACCUM_FUNC(adapt_filter_intra_accum_6, 1, 2, 4,
                                           uint8_t)
static const adapt_filter_intra_accum_fn
    adapt_filter_intra_accum_fns[ADAPT_FILTER_INTRA_MODES] = {
      adapt_filter_intra_accum_0, adapt_filter_intra_accum_1,
      adapt_filter_intra_accum_2, adapt_filter_intra_accum_3,
      adapt_filter_intra_accum_4, adapt_filter_intra_accum_5,
      adapt_filter_intra_accum_6
    };

typedef void (*adapt_filter_intra_accum_fn_hbd)(const uint16_t *src, int stride,
                                                int w, int h, int64_t *dst_buf);
ADAPT_FILTER_INTRA_DEFINE_3_TAP_ACCUM_FUNC(adapt_filter_intra_accum_0_hbd, 1, 2,
                                           3, uint16_t)
ADAPT_FILTER_INTRA_DEFINE_3_TAP_ACCUM_FUNC(adapt_filter_intra_accum_1_hbd, 0, 1,
                                           3, uint16_t)
ADAPT_FILTER_INTRA_DEFINE_3_TAP_ACCUM_FUNC(adapt_filter_intra_accum_2_hbd, 1, 3,
                                           4, uint16_t)
ADAPT_FILTER_INTRA_DEFINE_4_TAP_ACCUM_FUNC(adapt_filter_intra_accum_3_hbd, 0, 1,
                                           2, 3, uint16_t)
ADAPT_FILTER_INTRA_DEFINE_4_TAP_ACCUM_FUNC(adapt_filter_intra_accum_4_hbd, 1, 2,
                                           3, 4, uint16_t)
ADAPT_FILTER_INTRA_DEFINE_3_TAP_ACCUM_FUNC(adapt_filter_intra_accum_5_hbd, 0, 2,
                                           3, uint16_t)
ADAPT_FILTER_INTRA_DEFINE_3_TAP_ACCUM_FUNC(adapt_filter_intra_accum_6_hbd, 1, 2,
                                           4, uint16_t)
static const adapt_filter_intra_accum_fn_hbd
    adapt_filter_intra_accum_fns_hbd[ADAPT_FILTER_INTRA_MODES] = {
      adapt_filter_intra_accum_0_hbd, adapt_filter_intra_accum_1_hbd,
      adapt_filter_intra_accum_2_hbd, adapt_filter_intra_accum_3_hbd,
      adapt_filter_intra_accum_4_hbd, adapt_filter_intra_accum_5_hbd,
      adapt_filter_intra_accum_6_hbd
    };

// Set up functions for performing prediction given the fit filter coefficients.
// Whenever coefficent for the bottom-left pixel is non-zero, we are forced to
// do the prediction in the column-major order.
typedef void (*adapt_filter_intra_pred_fn)(uint8_t *dst, int stride,
                                           TX_SIZE tx_size, double *filt,
                                           const uint8_t *above,
                                           const uint8_t *left);
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_ROW_MAJOR(adapt_filter_intra_pred_0,
                                              (filt[0] * buf[r][c - 1] +
                                               filt[1] * buf[r - 1][c - 1] +
                                               filt[2] * buf[r - 1][c]),
                                              uint8_t)

ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_COL_MAJOR(adapt_filter_intra_pred_1,
                                              (filt[0] * buf[r + 1][c - 1] +
                                               filt[1] * buf[r][c - 1] +
                                               filt[2] * buf[r - 1][c]),
                                              uint8_t)
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_ROW_MAJOR(adapt_filter_intra_pred_2,
                                              (filt[0] * buf[r][c - 1] +
                                               filt[1] * buf[r - 1][c] +
                                               filt[2] * buf[r - 1][c + 1]),
                                              uint8_t)
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_COL_MAJOR(adapt_filter_intra_pred_3,
                                              (filt[0] * buf[r + 1][c - 1] +
                                               filt[1] * buf[r][c - 1] +
                                               filt[2] * buf[r - 1][c - 1] +
                                               filt[3] * buf[r - 1][c]),
                                              uint8_t)
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_ROW_MAJOR(adapt_filter_intra_pred_4,
                                              (filt[0] * buf[r][c - 1] +
                                               filt[1] * buf[r - 1][c - 1] +
                                               filt[2] * buf[r - 1][c] +
                                               filt[3] * buf[r - 1][c + 1]),
                                              uint8_t)
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_COL_MAJOR(adapt_filter_intra_pred_5,
                                              (filt[0] * buf[r + 1][c - 1] +
                                               filt[1] * buf[r - 1][c - 1] +
                                               filt[2] * buf[r - 1][c]),
                                              uint8_t)
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_ROW_MAJOR(adapt_filter_intra_pred_6,
                                              (filt[0] * buf[r][c - 1] +
                                               filt[1] * buf[r - 1][c - 1] +
                                               filt[2] * buf[r - 1][c + 1]),
                                              uint8_t)
static const adapt_filter_intra_pred_fn
    adapt_filter_intra_pred_fns[ADAPT_FILTER_INTRA_MODES] = {
      adapt_filter_intra_pred_0, adapt_filter_intra_pred_1,
      adapt_filter_intra_pred_2, adapt_filter_intra_pred_3,
      adapt_filter_intra_pred_4, adapt_filter_intra_pred_5,
      adapt_filter_intra_pred_6
    };

typedef void (*adapt_filter_intra_pred_fn_hbd)(uint16_t *dst, int stride,
                                               TX_SIZE tx_size, double *filt,
                                               const uint16_t *above,
                                               const uint16_t *left);
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_ROW_MAJOR(adapt_filter_intra_pred_0_hbd,
                                              (filt[0] * buf[r][c - 1] +
                                               filt[1] * buf[r - 1][c - 1] +
                                               filt[2] * buf[r - 1][c]),
                                              uint16_t)
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_COL_MAJOR(adapt_filter_intra_pred_1_hbd,
                                              (filt[0] * buf[r + 1][c - 1] +
                                               filt[1] * buf[r][c - 1] +
                                               filt[2] * buf[r - 1][c]),
                                              uint16_t)
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_ROW_MAJOR(adapt_filter_intra_pred_2_hbd,
                                              (filt[0] * buf[r][c - 1] +
                                               filt[1] * buf[r - 1][c] +
                                               filt[2] * buf[r - 1][c + 1]),
                                              uint16_t)
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_COL_MAJOR(adapt_filter_intra_pred_3_hbd,
                                              (filt[0] * buf[r + 1][c - 1] +
                                               filt[1] * buf[r][c - 1] +
                                               filt[2] * buf[r - 1][c - 1] +
                                               filt[3] * buf[r - 1][c]),
                                              uint16_t)
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_ROW_MAJOR(adapt_filter_intra_pred_4_hbd,
                                              (filt[0] * buf[r][c - 1] +
                                               filt[1] * buf[r - 1][c - 1] +
                                               filt[2] * buf[r - 1][c] +
                                               filt[3] * buf[r - 1][c + 1]),
                                              uint16_t)
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_COL_MAJOR(adapt_filter_intra_pred_5_hbd,
                                              (filt[0] * buf[r + 1][c - 1] +
                                               filt[1] * buf[r - 1][c - 1] +
                                               filt[2] * buf[r - 1][c]),
                                              uint16_t)
ADAPT_FILTER_INTRA_DEFINE_PRED_FUNC_ROW_MAJOR(adapt_filter_intra_pred_6_hbd,
                                              (filt[0] * buf[r][c - 1] +
                                               filt[1] * buf[r - 1][c - 1] +
                                               filt[2] * buf[r - 1][c + 1]),
                                              uint16_t)
static const adapt_filter_intra_pred_fn_hbd
    adapt_filter_intra_pred_fns_hbd[ADAPT_FILTER_INTRA_MODES] = {
      adapt_filter_intra_pred_0_hbd, adapt_filter_intra_pred_1_hbd,
      adapt_filter_intra_pred_2_hbd, adapt_filter_intra_pred_3_hbd,
      adapt_filter_intra_pred_4_hbd, adapt_filter_intra_pred_5_hbd,
      adapt_filter_intra_pred_6_hbd
    };

// Define the parameters that describe the shape of the region used to fit the
// filter, i.e. the training region (separately for each transform size and
// adaptive filter intra mode)
static const int
    adapt_filter_intra_thickness_hor[TX_SIZES_ALL][ADAPT_FILTER_INTRA_MODES] = {
      { 2, 2, 5, 5, 6, 7, 6 },         // TX_4X4
      { 9, 5, 6, 6, 2, 7, 8 },         // TX_8X8
      { 12, 9, 9, 6, 10, 7, 6 },       // TX_16X16
      { 16, 14, 13, 15, 19, 17, 16 },  // TX_32X32
      { 33, 33, 33, 33, 33, 33, 33 },  // TX_64X64
      { 3, 6, 6, 5, 1, 2, 6 },         // TX_4X8
      { 3, 4, 9, 9, 2, 5, 7 },         // TX_8X4
      { 9, 5, 6, 7, 4, 7, 7 },         // TX_8X16
      { 6, 9, 5, 11, 10, 10, 9 },      // TX_16X8
      { 12, 9, 6, 5, 11, 11, 7 },      // TX_16X32
      { 19, 14, 19, 15, 14, 13, 13 },  // TX_32X16
      { 17, 17, 17, 17, 17, 17, 17 },  // TX_32X64
      { 33, 33, 33, 33, 33, 33, 33 },  // TX_64X32
      { 2, 3, 6, 5, 4, 5, 7 },         // TX_4X16
      { 11, 13, 6, 9, 11, 9, 6 },      // TX_16X4
      { 9, 9, 5, 3, 8, 5, 4 },         // TX_8X32
      { 16, 20, 13, 18, 16, 13, 20 },  // TX_32X8
      { 9, 9, 9, 9, 9, 9, 9 },         // TX_16X64
      { 33, 33, 33, 33, 33, 33, 33 },  // TX_64X16
#if CONFIG_FLEX_PARTITION
      // TODO(huisu): Correct these
      { 9, 5, 6, 7, 4, 7, 7 },         // TX_4X32
      { 6, 9, 5, 11, 10, 10, 9 },      // TX_32X4
      { 12, 9, 6, 5, 11, 11, 7 },      // TX_8X64
      { 19, 14, 19, 15, 14, 13, 13 },  // TX_64X8
      { 9, 9, 5, 3, 8, 5, 4 },         // TX_4X64
      { 16, 20, 13, 18, 16, 13, 20 },  // TX_64X4
#endif                                 // CONFIG_FLEX_PARTITION
    };
static const int
    adapt_filter_intra_thickness_ver[TX_SIZES_ALL][ADAPT_FILTER_INTRA_MODES] = {
      { 4, 6, 2, 4, 5, 6, 6 },         // TX_4X4
      { 3, 5, 6, 5, 8, 8, 9 },         // TX_8X8
      { 11, 10, 12, 10, 6, 8, 9 },     // TX_16X16
      { 13, 16, 13, 14, 13, 16, 13 },  // TX_32X32
      { 33, 33, 33, 33, 33, 33, 33 },  // TX_64X64
      { 5, 9, 3, 3, 9, 8, 8 },         // TX_4X8
      { 3, 7, 2, 5, 5, 7, 6 },         // TX_8X4
      { 11, 5, 11, 8, 7, 8, 8 },       // TX_8X16
      { 9, 5, 9, 7, 5, 8, 3 },         // TX_16X8
      { 14, 15, 20, 19, 14, 14, 13 },  // TX_16X32
      { 5, 7, 8, 9, 5, 12, 11 },       // TX_32X16
      { 33, 33, 33, 33, 33, 33, 33 },  // TX_32X64
      { 17, 17, 17, 17, 17, 17, 17 },  // TX_64X32
      { 7, 11, 9, 10, 11, 6, 6 },      // TX_4X16
      { 2, 7, 5, 1, 4, 6, 7 },         // TX_16X4
      { 13, 13, 19, 15, 13, 15, 17 },  // TX_8X32
      { 5, 4, 6, 6, 3, 9, 9 },         // TX_32X8
      { 33, 33, 33, 33, 33, 33, 33 },  // TX_16X64
      { 9, 9, 9, 9, 9, 9, 9 },         // TX_64X16
#if CONFIG_FLEX_PARTITION
      // TODO(huisu): Correct these
      { 11, 5, 11, 8, 7, 8, 8 },       // TX_4X32
      { 9, 5, 9, 7, 5, 8, 3 },         // TX_32X4
      { 14, 15, 20, 19, 14, 14, 13 },  // TX_8X64
      { 5, 7, 8, 9, 5, 12, 11 },       // TX_64X8
      { 13, 13, 19, 15, 13, 15, 17 },  // TX_4X64
      { 5, 4, 6, 6, 3, 9, 9 },         // TX_64X4
#endif                                 // CONFIG_FLEX_PARTITION
    };
static const int adapt_filter_intra_top_right_offset
    [TX_SIZES_ALL][ADAPT_FILTER_INTRA_MODES] = {
      { 1, 4, 3, 2, 1, -4, 0 },        // TX_4X4
      { 2, 1, 7, -1, 1, 1, 3 },        // TX_8X8
      { -2, -1, 8, -3, 5, 2, 5 },      // TX_16X16
      { -2, -3, 17, -3, 14, 2, 14 },   // TX_32X32
      { 0, 0, 33, 0, 33, 0, 33 },      // TX_64X64
      { -3, 1, 4, 2, 1, 3, 6 },        // TX_4X8
      { 0, 1, 6, 2, 5, -3, 3 },        // TX_8X4
      { -1, 0, 8, -4, 3, 2, 5 },       // TX_8X16
      { 0, 0, 8, -3, 7, 0, 11 },       // TX_16X8
      { 0, -2, 10, -2, 6, -4, 7 },     // TX_16X32
      { -1, -4, 20, -3, 16, -3, 15 },  // TX_32X16
      { 0, 0, 17, 0, 17, 0, 17 },      // TX_32X64
      { 0, 0, 33, 0, 33, 0, 33 },      // TX_64X32
      { 4, -1, 2, -2, 1, -3, 2 },      // TX_4X16
      { 2, 3, 11, 3, 7, -3, 8 },       // TX_16X4
      { -1, -4, 5, -2, 2, -4, 7 },     // TX_8X32
      { -2, 2, 15, -4, 19, -1, 17 },   // TX_32X8
      { 0, 0, 9, 0, 9, 0, 9 },         // TX_16X64
      { 0, 0, 33, 0, 33, 0, 33 },      // TX_64X16
#if CONFIG_FLEX_PARTITION
      // TODO(huisu): Correct these
      { -1, 0, 8, -4, 3, 2, 5 },       // TX_4X32
      { 0, 0, 8, -3, 7, 0, 11 },       // TX_32X4
      { 0, -2, 10, -2, 6, -4, 7 },     // TX_8X64
      { -1, -4, 20, -3, 16, -3, 15 },  // TX_64X8
      { -1, -4, 5, -2, 2, -4, 7 },     // TX_4X64
      { -2, 2, 15, -4, 19, -1, 17 },   // TX_64X4
#endif                                 // CONFIG_FLEX_PARTITION
    };
static const int adapt_filter_intra_bottom_left_offset
    [TX_SIZES_ALL][ADAPT_FILTER_INTRA_MODES] = {
      { -1, 3, 0, 2, 3, 5, -3 },       // TX_4X4
      { 1, 7, 1, 1, 2, 7, -2 },        // TX_8X8
      { -1, 6, -3, 8, -1, 10, -1 },    // TX_16X16
      { -1, 13, -4, 15, -2, 15, -3 },  // TX_32X32
      { 0, 33, 0, 33, 0, 33, 0 },      // TX_64X64
      { -3, 8, -1, 4, 1, 7, 0 },       // TX_4X8
      { 0, 4, 2, 1, 0, 3, 0 },         // TX_8X4
      { -4, 10, -2, 7, -1, 9, 0 },     // TX_8X16
      { -1, 5, -3, 4, 2, 7, 4 },       // TX_16X8
      { -4, 16, 1, 13, -3, 15, -1 },   // TX_16X32
      { -3, 8, 1, 7, -1, 9, -1 },      // TX_32X16
      { 0, 33, 0, 33, 0, 33, 0 },      // TX_32X64
      { 0, 17, 0, 17, 0, 17, 0 },      // TX_64X32
      { -1, 11, 0, 9, 1, 8, -2 },      // TX_4X16
      { 2, 4, 1, 1, 2, 0, -3 },        // TX_16X4
      { -1, 18, -3, 13, 1, 14, 1 },    // TX_8X32
      { -2, 5, 0, 4, -2, 5, -1 },      // TX_32X8
      { 0, 33, 0, 33, 0, 33, 0 },      // TX_16X64
      { 0, 9, 0, 9, 0, 9, 0 },         // TX_64X16
#if CONFIG_FLEX_PARTITION
      // TODO(huisu): Correct these
      { -4, 10, -2, 7, -1, 9, 0 },    // TX_4X32
      { -1, 5, -3, 4, 2, 7, 4 },      // TX_32X4
      { -4, 16, 1, 13, -3, 15, -1 },  // TX_8X64
      { -3, 8, 1, 7, -1, 9, -1 },     // TX_64X8
      { -1, 18, -3, 13, 1, 14, 1 },   // TX_4X64
      { -2, 5, 0, 4, -2, 5, -1 },     // TX_64X4
#endif                                // CONFIG_FLEX_PARTITION
    };

// Specify the number of taps each mode is using (only 3 and 4 are currently
// supported):
static const int adapt_filter_intra_num_taps[ADAPT_FILTER_INTRA_MODES] = {
  3, 3, 3, 4, 4, 3, 3
};

// Specify whether each mode uses top-right or bottom-left pixels (it affects
// the size of the training region):
static const int adapt_filter_intra_use_top_right[ADAPT_FILTER_INTRA_MODES] = {
  0, 0, 1, 0, 1, 0, 1
};
static const int
    adapt_filter_intra_use_bottom_left[ADAPT_FILTER_INTRA_MODES] = { 0, 1, 0, 1,
                                                                     0, 1, 0 };

// Some modes use only left/top context of the block for training:
static const int adapt_filter_intra_top_allowed[ADAPT_FILTER_INTRA_MODES] = {
  1, 1, 1, 0, 1, 1, 1
};
static const int adapt_filter_intra_left_allowed[ADAPT_FILTER_INTRA_MODES] = {
  1, 1, 1, 1, 0, 1, 1
};

// To prevent degenerate systems from appearing introduce extra L2
// regularization:
static const int adapt_filter_intra_regularization_coef = 2;

static void adapt_filter_intra_accumulate_stats(
    const uint8_t *ref, int stride, TX_SIZE tx_size, int n_top_px,
    int n_topright_px, int n_left_px, int n_bottomleft_px, int64_t *dst_stats,
    int px_row, int px_col, int mode) {
  const int txwpx = tx_size_wide[tx_size];
  const int txhpx = tx_size_high[tx_size];
  const int up_offs =
      AOMMIN(adapt_filter_intra_thickness_ver[tx_size][mode], px_row) - 1;
  const int left_offs =
      AOMMIN(adapt_filter_intra_thickness_hor[tx_size][mode], px_col) - 1;
  const int top_right_offs = adapt_filter_intra_top_right_offset[tx_size][mode];
  const int bottom_left_offs =
      adapt_filter_intra_bottom_left_offset[tx_size][mode];

  const int w_adjust = adapt_filter_intra_use_top_right[mode] ? -1 : 0;
  const int h_adjust = adapt_filter_intra_use_bottom_left[mode] ? -1 : 0;

  const int top_width =
      AOMMIN(n_top_px + n_topright_px, txwpx + top_right_offs);
  const int left_height =
      AOMMIN(n_left_px + n_bottomleft_px, txhpx + bottom_left_offs);
  const adapt_filter_intra_accum_fn accum_fn =
      adapt_filter_intra_accum_fns[mode];

  if (adapt_filter_intra_top_allowed[mode] &&
      adapt_filter_intra_left_allowed[mode]) {
    if (n_top_px > 0 && n_left_px > 0) {
      accum_fn(ref - up_offs * stride, stride, top_width + w_adjust,
               up_offs + h_adjust, dst_stats);
      accum_fn(ref - left_offs, stride, left_offs + w_adjust,
               left_height + h_adjust, dst_stats);
      accum_fn(ref - up_offs * stride - left_offs, stride, left_offs, up_offs,
               dst_stats);
    } else if (n_top_px > 0) {
      accum_fn(ref - up_offs * stride + 1, stride, top_width - 1 + w_adjust,
               up_offs + h_adjust, dst_stats);
    } else if (n_left_px > 0) {
      accum_fn(ref + stride - left_offs, stride, left_offs + w_adjust,
               left_height - 1 + h_adjust, dst_stats);
    }
  } else if (adapt_filter_intra_top_allowed[mode]) {
    const int extra_offs = (n_left_px > 0 || bottom_left_offs <= 0)
                               ? AOMMIN(bottom_left_offs, px_col)
                               : 0;
    accum_fn(ref - up_offs * stride - (extra_offs - 1), stride,
             top_width + extra_offs - 1 + w_adjust, up_offs + h_adjust,
             dst_stats);
  } else if (adapt_filter_intra_left_allowed[mode]) {
    const int extra_offs = (n_top_px > 0 || top_right_offs <= 0)
                               ? AOMMIN(top_right_offs, px_row)
                               : 0;
    accum_fn(ref - (extra_offs - 1) * stride - left_offs, stride,
             left_offs + w_adjust, left_height + extra_offs - 1 + h_adjust,
             dst_stats);
  }
}

static void adapt_filter_intra_accumulate_stats_hbd(
    const uint16_t *ref, int stride, TX_SIZE tx_size, int n_top_px,
    int n_topright_px, int n_left_px, int n_bottomleft_px, int64_t *dst_stats,
    int px_row, int px_col, int mode) {
  const int txwpx = tx_size_wide[tx_size];
  const int txhpx = tx_size_high[tx_size];
  const int up_offs =
      AOMMIN(adapt_filter_intra_thickness_ver[tx_size][mode], px_row) - 1;
  const int left_offs =
      AOMMIN(adapt_filter_intra_thickness_hor[tx_size][mode], px_col) - 1;
  const int top_right_offs = adapt_filter_intra_top_right_offset[tx_size][mode];
  const int bottom_left_offs =
      adapt_filter_intra_bottom_left_offset[tx_size][mode];

  const int w_adjust = adapt_filter_intra_use_top_right[mode] ? -1 : 0;
  const int h_adjust = adapt_filter_intra_use_bottom_left[mode] ? -1 : 0;

  const int top_width =
      AOMMIN(n_top_px + n_topright_px, txwpx + top_right_offs);
  const int left_height =
      AOMMIN(n_left_px + n_bottomleft_px, txhpx + bottom_left_offs);

  const adapt_filter_intra_accum_fn_hbd accum_fn =
      adapt_filter_intra_accum_fns_hbd[mode];
  if (adapt_filter_intra_top_allowed[mode] &&
      adapt_filter_intra_left_allowed[mode]) {
    if (n_top_px > 0 && n_left_px > 0) {
      accum_fn(ref - up_offs * stride, stride, top_width + w_adjust,
               up_offs + h_adjust, dst_stats);
      accum_fn(ref - left_offs, stride, left_offs + w_adjust,
               left_height + h_adjust, dst_stats);
      accum_fn(ref - up_offs * stride - left_offs, stride, left_offs, up_offs,
               dst_stats);
    } else if (n_top_px > 0) {
      accum_fn(ref - up_offs * stride + 1, stride, top_width - 1 + w_adjust,
               up_offs + h_adjust, dst_stats);
    } else if (n_left_px > 0) {
      accum_fn(ref + stride - left_offs, stride, left_offs + w_adjust,
               left_height - 1 + h_adjust, dst_stats);
    }
  } else if (adapt_filter_intra_top_allowed[mode]) {
    const int extra_offs = (n_left_px > 0 || bottom_left_offs <= 0)
                               ? AOMMIN(bottom_left_offs, px_col)
                               : 0;
    accum_fn(ref - up_offs * stride - (extra_offs - 1), stride,
             top_width + extra_offs - 1 + w_adjust, up_offs + h_adjust,
             dst_stats);
  } else if (adapt_filter_intra_left_allowed[mode]) {
    const int extra_offs = (n_top_px > 0 || top_right_offs <= 0)
                               ? AOMMIN(top_right_offs, px_row)
                               : 0;
    accum_fn(ref - (extra_offs - 1) * stride - left_offs, stride,
             left_offs + w_adjust, left_height + extra_offs - 1 + h_adjust,
             dst_stats);
  }
}

static void adapt_filter_intra_solve_3x3(const int64_t *coefs,
                                         double *dst_solution) {
  // coefs layout:
  // 0 1 3 | 6
  // 1 2 4 | 7
  // 3 4 5 | 8
  const int64_t *lhs = coefs;
  const int64_t *rhs = coefs + 6;

  // Precompute determinants of 6 2x2 submatrices:
  int64_t a[6];
  a[0] = lhs[1] * lhs[4] - lhs[2] * lhs[3];
  a[1] = lhs[1] * lhs[5] - lhs[4] * lhs[3];
  a[2] = lhs[1] * rhs[2] - rhs[1] * lhs[3];
  a[3] = lhs[2] * lhs[5] - lhs[4] * lhs[4];
  a[4] = lhs[2] * rhs[2] - rhs[1] * lhs[4];
  a[5] = lhs[4] * rhs[2] - rhs[1] * lhs[5];

  // Compute 4 determinats that we actually care about
  const int64_t base_det = lhs[0] * a[3] - lhs[1] * a[1] + lhs[3] * a[0];
  assert(base_det != 0);
  const int64_t det1 = rhs[0] * a[3] + lhs[1] * a[5] - lhs[3] * a[4];
  const int64_t det2 = -lhs[0] * a[5] - rhs[0] * a[1] + lhs[3] * a[2];
  const int64_t det3 = lhs[0] * a[4] - lhs[1] * a[2] + rhs[0] * a[0];

  dst_solution[0] = (double)det1 / (double)base_det;
  dst_solution[1] = (double)det2 / (double)base_det;
  dst_solution[2] = (double)det3 / (double)base_det;
}

static void adapt_filter_intra_solve_4x4(const int64_t *coefs,
                                         double *dst_solution) {
  // coefs layout:
  // 0 1 3 6 | 10
  // 1 2 4 7 | 11
  // 3 4 5 8 | 12
  // 6 7 8 9 | 13
  const int64_t *lhs = coefs;
  const int64_t *rhs = coefs + 10;

  // Precompute determinants of 20 2x2 submatrices:
  int64_t a[10], b[10];
  a[0] = lhs[0] * lhs[2] - lhs[1] * lhs[1];
  a[1] = lhs[0] * lhs[4] - lhs[3] * lhs[1];
  a[2] = lhs[0] * lhs[7] - lhs[6] * lhs[1];
  a[3] = lhs[0] * rhs[1] - rhs[0] * lhs[1];
  a[4] = lhs[1] * lhs[4] - lhs[3] * lhs[2];
  a[5] = lhs[1] * lhs[7] - lhs[6] * lhs[2];
  a[6] = lhs[1] * rhs[1] - rhs[0] * lhs[2];
  a[7] = lhs[3] * lhs[7] - lhs[6] * lhs[4];
  a[8] = lhs[3] * rhs[1] - rhs[0] * lhs[4];
  a[9] = lhs[6] * rhs[1] - rhs[0] * lhs[7];

  b[0] = lhs[3] * lhs[7] - lhs[4] * lhs[6];
  b[1] = lhs[3] * lhs[8] - lhs[5] * lhs[6];
  b[2] = lhs[3] * lhs[9] - lhs[8] * lhs[6];
  b[3] = lhs[3] * rhs[3] - rhs[2] * lhs[6];
  b[4] = lhs[4] * lhs[8] - lhs[5] * lhs[7];
  b[5] = lhs[4] * lhs[9] - lhs[8] * lhs[7];
  b[6] = lhs[4] * rhs[3] - rhs[2] * lhs[7];
  b[7] = lhs[5] * lhs[9] - lhs[8] * lhs[8];
  b[8] = lhs[5] * rhs[3] - rhs[2] * lhs[8];
  b[9] = lhs[8] * rhs[3] - rhs[2] * lhs[9];

  // Compute 5 determinats that we actually care about
  const int64_t base_det = a[0] * b[7] + a[7] * b[0] + a[2] * b[4] +
                           a[4] * b[2] - a[5] * b[1] - a[1] * b[5];
  assert(base_det != 0);
  const int64_t det1 = a[5] * b[8] + a[8] * b[5] - a[6] * b[7] - a[7] * b[6] -
                       a[4] * b[9] - a[9] * b[4];
  const int64_t det2 = a[1] * b[9] + a[9] * b[1] + a[3] * b[7] + a[7] * b[3] -
                       a[2] * b[8] - a[8] * b[2];
  const int64_t det3 = a[2] * b[6] + a[6] * b[2] - a[0] * b[9] - a[9] * b[0] -
                       a[3] * b[5] - a[5] * b[3];
  const int64_t det4 = a[0] * b[8] + a[8] * b[0] + a[3] * b[4] + a[4] * b[3] -
                       a[6] * b[1] - a[1] * b[6];

  dst_solution[0] = (double)det1 / (double)base_det;
  dst_solution[1] = (double)det2 / (double)base_det;
  dst_solution[2] = (double)det3 / (double)base_det;
  dst_solution[3] = (double)det4 / (double)base_det;
}

static void adapt_filter_intra_predictor(
    uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *ref,
    ptrdiff_t ref_stride, int n_top_px, int n_topright_px, int n_left_px,
    int n_bottomleft_px, TX_SIZE tx_size, const uint8_t *above_row,
    const uint8_t *left_col, int mode, int px_row, int px_col) {
  // Form a linear system of equations from the statistics collected over the
  // training region around the current transform unit:
  int64_t accumulated_stats[14] = { 0 };
  adapt_filter_intra_accumulate_stats(ref, ref_stride, tx_size, n_top_px,
                                      n_topright_px, n_left_px, n_bottomleft_px,
                                      accumulated_stats, px_row, px_col, mode);

  // Apply regularization and solve the resulting system to get the adaptive
  // filter coefficients:
  double adapt_filter[4] = { 0 };
  if (adapt_filter_intra_num_taps[mode] == 3) {
    accumulated_stats[0] += adapt_filter_intra_regularization_coef;
    accumulated_stats[2] += adapt_filter_intra_regularization_coef;
    accumulated_stats[5] += adapt_filter_intra_regularization_coef;
    adapt_filter_intra_solve_3x3(accumulated_stats, adapt_filter);
  } else if (adapt_filter_intra_num_taps[mode] == 4) {
    accumulated_stats[0] += adapt_filter_intra_regularization_coef;
    accumulated_stats[2] += adapt_filter_intra_regularization_coef;
    accumulated_stats[5] += adapt_filter_intra_regularization_coef;
    accumulated_stats[9] += adapt_filter_intra_regularization_coef;
    adapt_filter_intra_solve_4x4(accumulated_stats, adapt_filter);
  } else {
    assert(0);
  }

  // Finally, perform prediction using the fit filter coefficients:
  adapt_filter_intra_pred_fns[mode](dst, dst_stride, tx_size, adapt_filter,
                                    above_row, left_col);
}

static void adapt_filter_intra_predictor_hbd(
    uint16_t *dst, ptrdiff_t dst_stride, const uint16_t *ref,
    ptrdiff_t ref_stride, int n_top_px, int n_topright_px, int n_left_px,
    int n_bottomleft_px, TX_SIZE tx_size, const uint16_t *above_row,
    const uint16_t *left_col, int mode, int px_row, int px_col) {
  // Form a linear system of equations from the statistics collected over the
  // training region around the current transform unit:
  int64_t accumulated_stats[14] = { 0 };
  adapt_filter_intra_accumulate_stats_hbd(
      ref, ref_stride, tx_size, n_top_px, n_topright_px, n_left_px,
      n_bottomleft_px, accumulated_stats, px_row, px_col, mode);

  // Apply regularization and solve the resulting system to get the adaptive
  // filter coefficients:
  double adapt_filter[4] = { 0 };
  if (adapt_filter_intra_num_taps[mode] == 3) {
    accumulated_stats[0] += adapt_filter_intra_regularization_coef;
    accumulated_stats[2] += adapt_filter_intra_regularization_coef;
    accumulated_stats[5] += adapt_filter_intra_regularization_coef;
    adapt_filter_intra_solve_3x3(accumulated_stats, adapt_filter);
  } else if (adapt_filter_intra_num_taps[mode] == 4) {
    accumulated_stats[0] += adapt_filter_intra_regularization_coef;
    accumulated_stats[2] += adapt_filter_intra_regularization_coef;
    accumulated_stats[5] += adapt_filter_intra_regularization_coef;
    accumulated_stats[9] += adapt_filter_intra_regularization_coef;
    adapt_filter_intra_solve_4x4(accumulated_stats, adapt_filter);
  } else {
    assert(0);
  }

  // Finally, perform prediction using the fit filter coefficients:
  adapt_filter_intra_pred_fns_hbd[mode](dst, dst_stride, tx_size, adapt_filter,
                                        above_row, left_col);
}
#endif  // CONFIG_ADAPT_FILTER_INTRA

static void build_intra_predictors_high(
    const MACROBLOCKD *xd, const uint8_t *ref8, int ref_stride, uint8_t *dst8,
    int dst_stride, PREDICTION_MODE mode, int angle_delta,
    FILTER_INTRA_MODE filter_intra_mode, TX_SIZE tx_size,
    int disable_edge_filter, int n_top_px, int n_topright_px, int n_left_px,
    int n_bottomleft_px,
#if CONFIG_ADAPT_FILTER_INTRA
    ADAPT_FILTER_INTRA_MODE adapt_filter_intra_mode, int col_off, int row_off,
#endif
    int plane) {
  int i;
  uint16_t *dst = CONVERT_TO_SHORTPTR(dst8);
  uint16_t *ref = CONVERT_TO_SHORTPTR(ref8);
  DECLARE_ALIGNED(16, uint16_t, left_data[MAX_TX_SIZE * 2 + 32]);
  DECLARE_ALIGNED(16, uint16_t, above_data[MAX_TX_SIZE * 2 + 32]);
  uint16_t *const above_row = above_data + 16;
  uint16_t *const left_col = left_data + 16;
  const int txwpx = tx_size_wide[tx_size];
  const int txhpx = tx_size_high[tx_size];
  int need_left = extend_modes[mode] & NEED_LEFT;
  int need_above = extend_modes[mode] & NEED_ABOVE;
  int need_above_left = extend_modes[mode] & NEED_ABOVELEFT;
  const uint16_t *above_ref = ref - ref_stride;
  const uint16_t *left_ref = ref - 1;
  int p_angle = 0;
  const int is_dr_mode = av1_is_directional_mode(mode);
  const int use_filter_intra = filter_intra_mode != FILTER_INTRA_MODES;
  int base = 128 << (xd->bd - 8);

  // The default values if ref pixels are not available:
  // base-1 base-1 base-1 .. base-1 base-1 base-1 base-1 base-1 base-1
  // base+1   A      B  ..     Y      Z
  // base+1   C      D  ..     W      X
  // base+1   E      F  ..     U      V
  // base+1   G      H  ..     S      T      T      T      T      T

  if (is_dr_mode) {
    p_angle = mode_to_angle_map[mode] + angle_delta;
    if (p_angle <= 90)
      need_above = 1, need_left = 0, need_above_left = 1;
    else if (p_angle < 180)
      need_above = 1, need_left = 1, need_above_left = 1;
    else
      need_above = 0, need_left = 1, need_above_left = 1;
  }
  if (use_filter_intra) need_left = need_above = need_above_left = 1;
#if CONFIG_ADAPT_FILTER_INTRA
  const int use_adapt_filter_intra =
      adapt_filter_intra_mode != ADAPT_FILTER_INTRA_MODES;
  if (use_adapt_filter_intra) need_left = need_above = need_above_left = 1;
#endif  // CONFIG_ADAPT_FILTER_INTRA

  assert(n_top_px >= 0);
  assert(n_topright_px >= 0);
  assert(n_left_px >= 0);
  assert(n_bottomleft_px >= 0);

  if ((!need_above && n_left_px == 0) || (!need_left && n_top_px == 0)) {
    int val;
    if (need_left) {
      val = (n_top_px > 0) ? above_ref[0] : base + 1;
    } else {
      val = (n_left_px > 0) ? left_ref[0] : base - 1;
    }
    for (i = 0; i < txhpx; ++i) {
      aom_memset16(dst, val, txwpx);
      dst += dst_stride;
    }
    return;
  }

  // NEED_LEFT
  if (need_left) {
    int need_bottom = !!(extend_modes[mode] & NEED_BOTTOMLEFT);
    if (use_filter_intra) need_bottom = 0;
#if CONFIG_ADAPT_FILTER_INTRA
    if (use_adapt_filter_intra) need_bottom = 1;
#endif
    if (is_dr_mode) need_bottom = p_angle > 180;
    const int num_left_pixels_needed = txhpx + (need_bottom ? txwpx : 0);
    i = 0;
    if (n_left_px > 0) {
      for (; i < n_left_px; i++) left_col[i] = left_ref[i * ref_stride];
      if (need_bottom && n_bottomleft_px > 0) {
        assert(i == txhpx);
        for (; i < txhpx + n_bottomleft_px; i++)
          left_col[i] = left_ref[i * ref_stride];
      }
      if (i < num_left_pixels_needed)
        aom_memset16(&left_col[i], left_col[i - 1], num_left_pixels_needed - i);
    } else {
      if (n_top_px > 0) {
        aom_memset16(left_col, above_ref[0], num_left_pixels_needed);
      } else {
        aom_memset16(left_col, base + 1, num_left_pixels_needed);
      }
    }
  }

  // NEED_ABOVE
  if (need_above) {
    int need_right = !!(extend_modes[mode] & NEED_ABOVERIGHT);
    if (use_filter_intra) need_right = 0;
#if CONFIG_ADAPT_FILTER_INTRA
    if (use_adapt_filter_intra) need_right = 1;
#endif
    if (is_dr_mode) need_right = p_angle < 90;
    const int num_top_pixels_needed = txwpx + (need_right ? txhpx : 0);
    if (n_top_px > 0) {
      memcpy(above_row, above_ref, n_top_px * sizeof(above_ref[0]));
      i = n_top_px;
      if (need_right && n_topright_px > 0) {
        assert(n_top_px == txwpx);
        memcpy(above_row + txwpx, above_ref + txwpx,
               n_topright_px * sizeof(above_ref[0]));
        i += n_topright_px;
      }
      if (i < num_top_pixels_needed)
        aom_memset16(&above_row[i], above_row[i - 1],
                     num_top_pixels_needed - i);
    } else {
      if (n_left_px > 0) {
        aom_memset16(above_row, left_ref[0], num_top_pixels_needed);
      } else {
        aom_memset16(above_row, base - 1, num_top_pixels_needed);
      }
    }
  }

  if (need_above_left) {
    if (n_top_px > 0 && n_left_px > 0) {
      above_row[-1] = above_ref[-1];
    } else if (n_top_px > 0) {
      above_row[-1] = above_ref[0];
    } else if (n_left_px > 0) {
      above_row[-1] = left_ref[0];
    } else {
      above_row[-1] = base;
    }
    left_col[-1] = above_row[-1];
  }

  if (use_filter_intra) {
    highbd_filter_intra_predictor(dst, dst_stride, tx_size, above_row, left_col,
                                  filter_intra_mode, xd->bd);
    return;
  }
#if CONFIG_ADAPT_FILTER_INTRA
  if (use_adapt_filter_intra) {
    const int px_row = (-xd->mb_to_top_edge >> 3) + (row_off << MI_SIZE_LOG2);
    const int px_col = (-xd->mb_to_left_edge >> 3) + (col_off << MI_SIZE_LOG2);
    adapt_filter_intra_predictor_hbd(dst, dst_stride, ref, ref_stride, n_top_px,
                                     n_topright_px, n_left_px, n_bottomleft_px,
                                     tx_size, above_row, left_col,
                                     adapt_filter_intra_mode, px_row, px_col);
    return;
  }
#endif  // CONFIG_ADAPT_FILTER_INTRA

  if (is_dr_mode) {
    int upsample_above = 0;
    int upsample_left = 0;
    if (!disable_edge_filter) {
      const int need_right = p_angle < 90;
      const int need_bottom = p_angle > 180;
      const int filt_type = get_filt_type(xd, plane);
      if (p_angle != 90 && p_angle != 180) {
        const int ab_le = need_above_left ? 1 : 0;
        if (need_above && need_left && (txwpx + txhpx >= 24)) {
          filter_intra_edge_corner_high(above_row, left_col);
        }
        if (need_above && n_top_px > 0) {
          const int strength =
              intra_edge_filter_strength(txwpx, txhpx, p_angle - 90, filt_type);
          const int n_px = n_top_px + ab_le + (need_right ? txhpx : 0);
          av1_filter_intra_edge_high(above_row - ab_le, n_px, strength);
        }
        if (need_left && n_left_px > 0) {
          const int strength = intra_edge_filter_strength(
              txhpx, txwpx, p_angle - 180, filt_type);
          const int n_px = n_left_px + ab_le + (need_bottom ? txwpx : 0);
          av1_filter_intra_edge_high(left_col - ab_le, n_px, strength);
        }
      }
      upsample_above =
          av1_use_intra_edge_upsample(txwpx, txhpx, p_angle - 90, filt_type);
      if (need_above && upsample_above) {
        const int n_px = txwpx + (need_right ? txhpx : 0);
        av1_upsample_intra_edge_high(above_row, n_px, xd->bd);
      }
      upsample_left =
          av1_use_intra_edge_upsample(txhpx, txwpx, p_angle - 180, filt_type);
      if (need_left && upsample_left) {
        const int n_px = txhpx + (need_bottom ? txwpx : 0);
        av1_upsample_intra_edge_high(left_col, n_px, xd->bd);
      }
    }
    highbd_dr_predictor(dst, dst_stride, tx_size, above_row, left_col,
                        upsample_above, upsample_left, p_angle, xd->bd);
    return;
  }

  // predict
  if (mode == DC_PRED) {
    dc_pred_high[n_left_px > 0][n_top_px > 0][tx_size](
        dst, dst_stride, above_row, left_col, xd->bd);
  } else {
    pred_high[mode][tx_size](dst, dst_stride, above_row, left_col, xd->bd);
  }
}

static void build_intra_predictors(
    const MACROBLOCKD *xd, const uint8_t *ref, int ref_stride, uint8_t *dst,
    int dst_stride, PREDICTION_MODE mode, int angle_delta,
    FILTER_INTRA_MODE filter_intra_mode, TX_SIZE tx_size,
    int disable_edge_filter, int n_top_px, int n_topright_px, int n_left_px,
    int n_bottomleft_px,
#if CONFIG_ADAPT_FILTER_INTRA
    ADAPT_FILTER_INTRA_MODE adapt_filter_intra_mode, int col_off, int row_off,
#endif
    int plane) {
  int i;
  const uint8_t *above_ref = ref - ref_stride;
  const uint8_t *left_ref = ref - 1;
  DECLARE_ALIGNED(16, uint8_t, left_data[MAX_TX_SIZE * 2 + 32]);
  DECLARE_ALIGNED(16, uint8_t, above_data[MAX_TX_SIZE * 2 + 32]);
  uint8_t *const above_row = above_data + 16;
  uint8_t *const left_col = left_data + 16;
  const int txwpx = tx_size_wide[tx_size];
  const int txhpx = tx_size_high[tx_size];
  int need_left = extend_modes[mode] & NEED_LEFT;
  int need_above = extend_modes[mode] & NEED_ABOVE;
  int need_above_left = extend_modes[mode] & NEED_ABOVELEFT;
  int p_angle = 0;
  const int is_dr_mode = av1_is_directional_mode(mode);
  const int use_filter_intra = filter_intra_mode != FILTER_INTRA_MODES;

  // The default values if ref pixels are not available:
  // 127 127 127 .. 127 127 127 127 127 127
  // 129  A   B  ..  Y   Z
  // 129  C   D  ..  W   X
  // 129  E   F  ..  U   V
  // 129  G   H  ..  S   T   T   T   T   T
  // ..

  if (is_dr_mode) {
    p_angle = mode_to_angle_map[mode] + angle_delta;
    if (p_angle <= 90)
      need_above = 1, need_left = 0, need_above_left = 1;
    else if (p_angle < 180)
      need_above = 1, need_left = 1, need_above_left = 1;
    else
      need_above = 0, need_left = 1, need_above_left = 1;
  }
  if (use_filter_intra) need_left = need_above = need_above_left = 1;
#if CONFIG_ADAPT_FILTER_INTRA
  const int use_adapt_filter_intra =
      adapt_filter_intra_mode != ADAPT_FILTER_INTRA_MODES;
  if (use_adapt_filter_intra) need_left = need_above = need_above_left = 1;
#endif  // CONFIG_ADAPT_FILTER_INTRA

  assert(n_top_px >= 0);
  assert(n_topright_px >= 0);
  assert(n_left_px >= 0);
  assert(n_bottomleft_px >= 0);

  if ((!need_above && n_left_px == 0) || (!need_left && n_top_px == 0)) {
    int val;
    if (need_left) {
      val = (n_top_px > 0) ? above_ref[0] : 129;
    } else {
      val = (n_left_px > 0) ? left_ref[0] : 127;
    }
    for (i = 0; i < txhpx; ++i) {
      memset(dst, val, txwpx);
      dst += dst_stride;
    }
    return;
  }

  // NEED_LEFT
  if (need_left) {
    int need_bottom = !!(extend_modes[mode] & NEED_BOTTOMLEFT);
    if (use_filter_intra) need_bottom = 0;
#if CONFIG_ADAPT_FILTER_INTRA
    if (use_adapt_filter_intra) need_bottom = 1;
#endif
    if (is_dr_mode) need_bottom = p_angle > 180;
    const int num_left_pixels_needed = txhpx + (need_bottom ? txwpx : 0);
    i = 0;
    if (n_left_px > 0) {
      for (; i < n_left_px; i++) left_col[i] = left_ref[i * ref_stride];
      if (need_bottom && n_bottomleft_px > 0) {
        assert(i == txhpx);
        for (; i < txhpx + n_bottomleft_px; i++)
          left_col[i] = left_ref[i * ref_stride];
      }
      if (i < num_left_pixels_needed)
        memset(&left_col[i], left_col[i - 1], num_left_pixels_needed - i);
    } else {
      if (n_top_px > 0) {
        memset(left_col, above_ref[0], num_left_pixels_needed);
      } else {
        memset(left_col, 129, num_left_pixels_needed);
      }
    }
  }

  // NEED_ABOVE
  if (need_above) {
    int need_right = !!(extend_modes[mode] & NEED_ABOVERIGHT);
    if (use_filter_intra) need_right = 0;
#if CONFIG_ADAPT_FILTER_INTRA
    if (use_adapt_filter_intra) need_right = 1;
#endif
    if (is_dr_mode) need_right = p_angle < 90;
    const int num_top_pixels_needed = txwpx + (need_right ? txhpx : 0);
    if (n_top_px > 0) {
      memcpy(above_row, above_ref, n_top_px);
      i = n_top_px;
      if (need_right && n_topright_px > 0) {
        assert(n_top_px == txwpx);
        memcpy(above_row + txwpx, above_ref + txwpx, n_topright_px);
        i += n_topright_px;
      }
      if (i < num_top_pixels_needed)
        memset(&above_row[i], above_row[i - 1], num_top_pixels_needed - i);
    } else {
      if (n_left_px > 0) {
        memset(above_row, left_ref[0], num_top_pixels_needed);
      } else {
        memset(above_row, 127, num_top_pixels_needed);
      }
    }
  }

  if (need_above_left) {
    if (n_top_px > 0 && n_left_px > 0) {
      above_row[-1] = above_ref[-1];
    } else if (n_top_px > 0) {
      above_row[-1] = above_ref[0];
    } else if (n_left_px > 0) {
      above_row[-1] = left_ref[0];
    } else {
      above_row[-1] = 128;
    }
    left_col[-1] = above_row[-1];
  }

  if (use_filter_intra) {
    av1_filter_intra_predictor(dst, dst_stride, tx_size, above_row, left_col,
                               filter_intra_mode);
    return;
  }

#if CONFIG_ADAPT_FILTER_INTRA
  if (use_adapt_filter_intra) {
    const int px_row = (-xd->mb_to_top_edge >> 3) + (row_off << MI_SIZE_LOG2);
    const int px_col = (-xd->mb_to_left_edge >> 3) + (col_off << MI_SIZE_LOG2);
    adapt_filter_intra_predictor(dst, dst_stride, ref, ref_stride, n_top_px,
                                 n_topright_px, n_left_px, n_bottomleft_px,
                                 tx_size, above_row, left_col,
                                 adapt_filter_intra_mode, px_row, px_col);
    return;
  }
#endif  // CONFIG_ADAPT_FILTER_INTRA

  if (is_dr_mode) {
    int upsample_above = 0;
    int upsample_left = 0;
    if (!disable_edge_filter) {
      const int need_right = p_angle < 90;
      const int need_bottom = p_angle > 180;
      const int filt_type = get_filt_type(xd, plane);
      if (p_angle != 90 && p_angle != 180) {
        const int ab_le = need_above_left ? 1 : 0;
        if (need_above && need_left && (txwpx + txhpx >= 24)) {
          filter_intra_edge_corner(above_row, left_col);
        }
        if (need_above && n_top_px > 0) {
          const int strength =
              intra_edge_filter_strength(txwpx, txhpx, p_angle - 90, filt_type);
          const int n_px = n_top_px + ab_le + (need_right ? txhpx : 0);
          av1_filter_intra_edge(above_row - ab_le, n_px, strength);
        }
        if (need_left && n_left_px > 0) {
          const int strength = intra_edge_filter_strength(
              txhpx, txwpx, p_angle - 180, filt_type);
          const int n_px = n_left_px + ab_le + (need_bottom ? txwpx : 0);
          av1_filter_intra_edge(left_col - ab_le, n_px, strength);
        }
      }
      upsample_above =
          av1_use_intra_edge_upsample(txwpx, txhpx, p_angle - 90, filt_type);
      if (need_above && upsample_above) {
        const int n_px = txwpx + (need_right ? txhpx : 0);
        av1_upsample_intra_edge(above_row, n_px);
      }
      upsample_left =
          av1_use_intra_edge_upsample(txhpx, txwpx, p_angle - 180, filt_type);
      if (need_left && upsample_left) {
        const int n_px = txhpx + (need_bottom ? txwpx : 0);
        av1_upsample_intra_edge(left_col, n_px);
      }
    }
    dr_predictor(dst, dst_stride, tx_size, above_row, left_col, upsample_above,
                 upsample_left, p_angle);
    return;
  }

  // predict
  if (mode == DC_PRED) {
    dc_pred[n_left_px > 0][n_top_px > 0][tx_size](dst, dst_stride, above_row,
                                                  left_col);
  } else {
    pred[mode][tx_size](dst, dst_stride, above_row, left_col);
  }
}

void av1_predict_intra_block(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                             int wpx, int hpx, TX_SIZE tx_size,
                             PREDICTION_MODE mode, int angle_delta,
                             int use_palette,
                             FILTER_INTRA_MODE filter_intra_mode,
#if CONFIG_ADAPT_FILTER_INTRA
                             ADAPT_FILTER_INTRA_MODE adapt_filter_intra_mode,
#endif
                             const uint8_t *ref, int ref_stride, uint8_t *dst,
                             int dst_stride, int col_off, int row_off,
                             int plane) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int txwpx = tx_size_wide[tx_size];
  const int txhpx = tx_size_high[tx_size];
  const int x = col_off << tx_size_wide_log2[0];
  const int y = row_off << tx_size_high_log2[0];

  if (use_palette) {
    int r, c;
    const uint8_t *const map = xd->plane[plane != 0].color_index_map +
                               xd->color_index_map_offset[plane != 0];
    const uint16_t *const palette =
        mbmi->palette_mode_info.palette_colors + plane * PALETTE_MAX_SIZE;
    if (is_cur_buf_hbd(xd)) {
      uint16_t *dst16 = CONVERT_TO_SHORTPTR(dst);
      for (r = 0; r < txhpx; ++r) {
        for (c = 0; c < txwpx; ++c) {
          dst16[r * dst_stride + c] = palette[map[(r + y) * wpx + c + x]];
        }
      }
    } else {
      for (r = 0; r < txhpx; ++r) {
        for (c = 0; c < txwpx; ++c) {
          dst[r * dst_stride + c] =
              (uint8_t)palette[map[(r + y) * wpx + c + x]];
        }
      }
    }
    return;
  }

  BLOCK_SIZE bsize = mbmi->sb_type;
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const int txw = tx_size_wide_unit[tx_size];
  const int txh = tx_size_high_unit[tx_size];
  const int have_top = row_off || (pd->subsampling_y ? xd->chroma_up_available
                                                     : xd->up_available);
  const int have_left =
      col_off ||
      (pd->subsampling_x ? xd->chroma_left_available : xd->left_available);
  const int mi_row = -xd->mb_to_top_edge >> (3 + MI_SIZE_LOG2);
  const int mi_col = -xd->mb_to_left_edge >> (3 + MI_SIZE_LOG2);
  const int xr_chr_offset = 0;
  const int yd_chr_offset = 0;

  // Distance between the right edge of this prediction block to
  // the frame right edge
  const int xr = (xd->mb_to_right_edge >> (3 + pd->subsampling_x)) +
                 (wpx - x - txwpx) - xr_chr_offset;
  // Distance between the bottom edge of this prediction block to
  // the frame bottom edge
  const int yd = (xd->mb_to_bottom_edge >> (3 + pd->subsampling_y)) +
                 (hpx - y - txhpx) - yd_chr_offset;
  const int right_available =
      mi_col + ((col_off + txw) << pd->subsampling_x) < xd->tile.mi_col_end;
  const int bottom_available =
      (yd > 0) &&
      (mi_row + ((row_off + txh) << pd->subsampling_y) < xd->tile.mi_row_end);

  const PARTITION_TYPE partition = mbmi->partition;

  // force 4x4 chroma component block size.
  bsize = scale_chroma_bsize(bsize, pd->subsampling_x, pd->subsampling_y,
                             mi_row, mi_col);

  const int have_top_right = has_top_right(
      cm, bsize, mi_row, mi_col, have_top, right_available, partition, tx_size,
      row_off, col_off, pd->subsampling_x, pd->subsampling_y);
  const int have_bottom_left = has_bottom_left(
      cm, bsize, mi_row, mi_col, bottom_available, have_left, partition,
      tx_size, row_off, col_off, pd->subsampling_x, pd->subsampling_y);

  const int disable_edge_filter = !cm->seq_params.enable_intra_edge_filter;
  if (is_cur_buf_hbd(xd)) {
    build_intra_predictors_high(xd, ref, ref_stride, dst, dst_stride, mode,
                                angle_delta, filter_intra_mode, tx_size,
                                disable_edge_filter,
                                have_top ? AOMMIN(txwpx, xr + txwpx) : 0,
                                have_top_right ? AOMMIN(txwpx, xr) : 0,
                                have_left ? AOMMIN(txhpx, yd + txhpx) : 0,
                                have_bottom_left ? AOMMIN(txhpx, yd) : 0,
#if CONFIG_ADAPT_FILTER_INTRA
                                adapt_filter_intra_mode, col_off, row_off,
#endif
                                plane);
    return;
  }

  build_intra_predictors(xd, ref, ref_stride, dst, dst_stride, mode,
                         angle_delta, filter_intra_mode, tx_size,
                         disable_edge_filter,
                         have_top ? AOMMIN(txwpx, xr + txwpx) : 0,
                         have_top_right ? AOMMIN(txwpx, xr) : 0,
                         have_left ? AOMMIN(txhpx, yd + txhpx) : 0,
                         have_bottom_left ? AOMMIN(txhpx, yd) : 0,
#if CONFIG_ADAPT_FILTER_INTRA
                         adapt_filter_intra_mode, col_off, row_off,
#endif
                         plane);
}

void av1_predict_intra_block_facade(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                    int plane, int blk_col, int blk_row,
                                    TX_SIZE tx_size) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int dst_stride = pd->dst.stride;
  uint8_t *dst =
      &pd->dst.buf[(blk_row * dst_stride + blk_col) << tx_size_wide_log2[0]];
  const PREDICTION_MODE mode =
      (plane == AOM_PLANE_Y) ? mbmi->mode : get_uv_mode(mbmi->uv_mode);
  const int use_palette = mbmi->palette_mode_info.palette_size[plane != 0] > 0;
  const FILTER_INTRA_MODE filter_intra_mode =
      (plane == AOM_PLANE_Y && mbmi->filter_intra_mode_info.use_filter_intra)
          ? mbmi->filter_intra_mode_info.filter_intra_mode
          : FILTER_INTRA_MODES;
#if CONFIG_ADAPT_FILTER_INTRA
  const ADAPT_FILTER_INTRA_MODE adapt_filter_intra_mode =
      (plane == AOM_PLANE_Y &&
       mbmi->adapt_filter_intra_mode_info.use_adapt_filter_intra)
          ? mbmi->adapt_filter_intra_mode_info.adapt_filter_intra_mode
          : ADAPT_FILTER_INTRA_MODES;
#endif  // CONFIG_ADAPT_FILTER_INTRA
  const int angle_delta = mbmi->angle_delta[plane != AOM_PLANE_Y] * ANGLE_STEP;

  if (plane != AOM_PLANE_Y && mbmi->uv_mode == UV_CFL_PRED) {
#if CONFIG_DEBUG
    assert(is_cfl_allowed(xd));
    const int mi_row = -xd->mb_to_top_edge >> (3 + MI_SIZE_LOG2);
    const int mi_col = -xd->mb_to_left_edge >> (3 + MI_SIZE_LOG2);
    const BLOCK_SIZE plane_bsize = get_plane_block_size(
        mi_row, mi_col, mbmi->sb_type, pd->subsampling_x, pd->subsampling_y);
    (void)plane_bsize;
    assert(plane_bsize < BLOCK_SIZES_ALL);
    if (!xd->lossless[mbmi->segment_id]) {
      assert(blk_col == 0);
      assert(blk_row == 0);
      assert(block_size_wide[plane_bsize] == tx_size_wide[tx_size]);
      assert(block_size_high[plane_bsize] == tx_size_high[tx_size]);
    }
#endif
    CFL_CTX *const cfl = &xd->cfl;
    CFL_PRED_TYPE pred_plane = get_cfl_pred_type(plane);
    if (cfl->dc_pred_is_cached[pred_plane] == 0) {
      av1_predict_intra_block(cm, xd, pd->width, pd->height, tx_size, mode,
                              angle_delta, use_palette, filter_intra_mode,
#if CONFIG_ADAPT_FILTER_INTRA
                              adapt_filter_intra_mode,
#endif
                              dst, dst_stride, dst, dst_stride, blk_col,
                              blk_row, plane);
      if (cfl->use_dc_pred_cache) {
        cfl_store_dc_pred(xd, dst, pred_plane, tx_size_wide[tx_size]);
        cfl->dc_pred_is_cached[pred_plane] = 1;
      }
    } else {
      cfl_load_dc_pred(xd, dst, dst_stride, tx_size, pred_plane);
    }
    cfl_predict_block(xd, dst, dst_stride, tx_size, plane);
    return;
  }
  av1_predict_intra_block(cm, xd, pd->width, pd->height, tx_size, mode,
                          angle_delta, use_palette, filter_intra_mode,
#if CONFIG_ADAPT_FILTER_INTRA
                          adapt_filter_intra_mode,
#endif
                          dst, dst_stride, dst, dst_stride, blk_col, blk_row,
                          plane);
}

void av1_init_intra_predictors(void) {
  aom_once(init_intra_predictors_internal);
}
