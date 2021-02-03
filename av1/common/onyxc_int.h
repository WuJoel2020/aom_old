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

#ifndef AOM_AV1_COMMON_ONYXC_INT_H_
#define AOM_AV1_COMMON_ONYXC_INT_H_

#include "config/aom_config.h"
#include "config/av1_rtcd.h"

#include "aom/internal/aom_codec_internal.h"
#include "aom_util/aom_thread.h"
#include "av1/common/alloccommon.h"
#include "av1/common/av1_loopfilter.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/entropymv.h"
#include "av1/common/enums.h"
#include "av1/common/frame_buffers.h"
#include "av1/common/mv.h"
#include "av1/common/quant_common.h"
#include "av1/common/restoration.h"
#include "av1/common/tile_common.h"
#include "av1/common/timing.h"
#include "av1/common/odintrin.h"
#include "av1/encoder/hash_motion.h"
#include "aom_dsp/grain_synthesis.h"
#include "aom_dsp/grain_table.h"
#ifdef __cplusplus
extern "C" {
#endif

#if defined(__clang__) && defined(__has_warning)
#if __has_feature(cxx_attributes) && __has_warning("-Wimplicit-fallthrough")
#define AOM_FALLTHROUGH_INTENDED [[clang::fallthrough]]  // NOLINT
#endif
#elif defined(__GNUC__) && __GNUC__ >= 7
#define AOM_FALLTHROUGH_INTENDED __attribute__((fallthrough))  // NOLINT
#endif

#ifndef AOM_FALLTHROUGH_INTENDED
#define AOM_FALLTHROUGH_INTENDED \
  do {                           \
  } while (0)
#endif

#define CDEF_MAX_STRENGTHS 16

/* Constant values while waiting for the sequence header */
#define FRAME_ID_LENGTH 15
#define DELTA_FRAME_ID_LENGTH 14

#define FRAME_CONTEXTS (FRAME_BUFFERS + 1)
// Extra frame context which is always kept at default values
#define FRAME_CONTEXT_DEFAULTS (FRAME_CONTEXTS - 1)
#define PRIMARY_REF_BITS 3
#define PRIMARY_REF_NONE 7

#define NUM_PING_PONG_BUFFERS 2

#define MAX_NUM_TEMPORAL_LAYERS 8
#define MAX_NUM_SPATIAL_LAYERS 4
/* clang-format off */
// clang-format seems to think this is a pointer dereference and not a
// multiplication.
#define MAX_NUM_OPERATING_POINTS \
  MAX_NUM_TEMPORAL_LAYERS * MAX_NUM_SPATIAL_LAYERS
/* clang-format on */

// TODO(jingning): Turning this on to set up transform coefficient
// processing timer.
#define TXCOEFF_TIMER 0
#define TXCOEFF_COST_TIMER 0

enum {
  SINGLE_REFERENCE = 0,
  COMPOUND_REFERENCE = 1,
  REFERENCE_MODE_SELECT = 2,
  REFERENCE_MODES = 3,
} UENUM1BYTE(REFERENCE_MODE);

enum {
  /**
   * Frame context updates are disabled
   */
  REFRESH_FRAME_CONTEXT_DISABLED,
  /**
   * Update frame context to values resulting from backward probability
   * updates based on entropy/counts in the decoded frame
   */
  REFRESH_FRAME_CONTEXT_BACKWARD,
} UENUM1BYTE(REFRESH_FRAME_CONTEXT_MODE);

#define MFMV_STACK_SIZE 3
typedef struct {
  int_mv mfmv0;
  uint8_t ref_frame_offset;
} TPL_MV_REF;

typedef struct {
  int_mv mv;
  MV_REFERENCE_FRAME ref_frame;
} MV_REF;

typedef struct RefCntBuffer {
  // For a RefCntBuffer, the following are reference-holding variables:
  // - cm->ref_frame_map[]
  // - cm->cur_frame
  // - cm->scaled_ref_buf[] (encoder only)
  // - cm->next_ref_frame_map[] (decoder only)
  // - pbi->output_frame_index[] (decoder only)
  // With that definition, 'ref_count' is the number of reference-holding
  // variables that are currently referencing this buffer.
  // For example:
  // - suppose this buffer is at index 'k' in the buffer pool, and
  // - Total 'n' of the variables / array elements above have value 'k' (that
  // is, they are pointing to buffer at index 'k').
  // Then, pool->frame_bufs[k].ref_count = n.
  int ref_count;

  unsigned int order_hint;
  unsigned int ref_order_hints[INTER_REFS_PER_FRAME];

  // These variables are used only in encoder and compare the absolute
  // display order hint to compute the relative distance and overcome
  // the limitation of get_relative_dist() which returns incorrect
  // distance when a very old frame is used as a reference.
  unsigned int display_order_hint;
  unsigned int ref_display_order_hint[INTER_REFS_PER_FRAME];

  MV_REF *mvs;
  uint8_t *seg_map;
  struct segmentation seg;
  int mi_rows;
  int mi_cols;
  // Width and height give the size of the buffer (before any upscaling, unlike
  // the sizes that can be derived from the buf structure)
  int width;
  int height;
  WarpedMotionParams global_motion[REF_FRAMES];
  int showable_frame;  // frame can be used as show existing frame in future
  uint8_t film_grain_params_present;
  aom_film_grain_t film_grain_params;
  aom_codec_frame_buffer_t raw_frame_buffer;
  YV12_BUFFER_CONFIG buf;
  hash_table hash_table;
  FRAME_TYPE frame_type;

  // This is only used in the encoder but needs to be indexed per ref frame
  // so it's extremely convenient to keep it here.
  int interp_filter_selected[SWITCHABLE];

  // Inter frame reference frame delta for loop filter
  int8_t ref_deltas[REF_FRAMES];

  // 0 = ZERO_MV, MV
  int8_t mode_deltas[MAX_MODE_LF_DELTAS];

  FRAME_CONTEXT frame_context;
} RefCntBuffer;

typedef struct BufferPool {
// Protect BufferPool from being accessed by several FrameWorkers at
// the same time during frame parallel decode.
// TODO(hkuang): Try to use atomic variable instead of locking the whole pool.
// TODO(wtc): Remove this. See
// https://chromium-review.googlesource.com/c/webm/libvpx/+/560630.
#if CONFIG_MULTITHREAD
  pthread_mutex_t pool_mutex;
#endif

  // Private data associated with the frame buffer callbacks.
  void *cb_priv;

  aom_get_frame_buffer_cb_fn_t get_fb_cb;
  aom_release_frame_buffer_cb_fn_t release_fb_cb;

  RefCntBuffer frame_bufs[FRAME_BUFFERS];

  // Frame buffers allocated internally by the codec.
  InternalFrameBufferList int_frame_buffers;
} BufferPool;

typedef struct {
  int cdef_damping;
  int nb_cdef_strengths;
  int cdef_strengths[CDEF_MAX_STRENGTHS];
  int cdef_uv_strengths[CDEF_MAX_STRENGTHS];
  int cdef_bits;
} CdefInfo;

typedef struct {
  int delta_q_present_flag;
  // Resolution of delta quant
  int delta_q_res;
  int delta_lf_present_flag;
  // Resolution of delta lf level
  int delta_lf_res;
  // This is a flag for number of deltas of loop filter level
  // 0: use 1 delta, for y_vertical, y_horizontal, u, and v
  // 1: use separate deltas for each filter level
  int delta_lf_multi;
} DeltaQInfo;

typedef struct {
  int enable_order_hint;        // 0 - disable order hint, and related tools
  int order_hint_bits_minus_1;  // dist_wtd_comp, ref_frame_mvs,
                                // frame_sign_bias
                                // if 0, enable_dist_wtd_comp and
                                // enable_ref_frame_mvs must be set as 0.
  int enable_dist_wtd_comp;     // 0 - disable dist-wtd compound modes
                                // 1 - enable it
  int enable_ref_frame_mvs;     // 0 - disable ref frame mvs
                                // 1 - enable it
} OrderHintInfo;

// Sequence header structure.
// Note: All syntax elements of sequence_header_obu that need to be
// bit-identical across multiple sequence headers must be part of this struct,
// so that consistency is checked by are_seq_headers_consistent() function.
typedef struct SequenceHeader {
  int num_bits_width;
  int num_bits_height;
  int max_frame_width;
  int max_frame_height;
  uint8_t frame_id_numbers_present_flag;
  int frame_id_length;
  int delta_frame_id_length;
  BLOCK_SIZE sb_size;  // Size of the superblock used for this frame
  int mib_size;        // Size of the superblock in units of MI blocks
  int mib_size_log2;   // Log 2 of above.

  OrderHintInfo order_hint_info;

  uint8_t force_screen_content_tools;  // 0 - force off
                                       // 1 - force on
                                       // 2 - adaptive
  uint8_t still_picture;               // Video is a single frame still picture
  uint8_t reduced_still_picture_hdr;   // Use reduced header for still picture
  uint8_t force_integer_mv;            // 0 - Don't force. MV can use subpel
                                       // 1 - force to integer
                                       // 2 - adaptive
  uint8_t enable_filter_intra;         // enables/disables filterintra
#if CONFIG_ADAPT_FILTER_INTRA
  int enable_adapt_filter_intra;  // enables/disables adaptive filter intra
#endif
  uint8_t enable_intra_edge_filter;    // enables/disables edge upsampling
  uint8_t enable_interintra_compound;  // enables/disables interintra_compound
  uint8_t enable_masked_compound;      // enables/disables masked compound
  uint8_t enable_dual_filter;          // 0 - disable dual interpolation filter
                                       // 1 - enable vert/horz filter selection
  uint8_t enable_warped_motion;        // 0 - disable warp for the sequence
                                       // 1 - enable warp for the sequence
  uint8_t enable_superres;             // 0 - Disable superres for the sequence
                                       //     and no frame level superres flag
                                       // 1 - Enable superres for the sequence
                                       //     enable per-frame superres flag
  uint8_t enable_cdef;                 // To turn on/off CDEF
  uint8_t enable_restoration;          // To turn on/off loop restoration

  BITSTREAM_PROFILE profile;

  // Operating point info.
  int operating_points_cnt_minus_1;
  int operating_point_idc[MAX_NUM_OPERATING_POINTS];
  uint8_t display_model_info_present_flag;
  uint8_t decoder_model_info_present_flag;
  AV1_LEVEL seq_level_idx[MAX_NUM_OPERATING_POINTS];
  uint8_t tier[MAX_NUM_OPERATING_POINTS];  // seq_tier in the spec. One bit: 0
                                           // or 1.

  // Color config.
  aom_bit_depth_t bit_depth;  // AOM_BITS_8 in profile 0 or 1,
                              // AOM_BITS_10 or AOM_BITS_12 in profile 2 or 3.
  uint8_t use_highbitdepth;   // If true, we need to use 16bit frame buffers.
  uint8_t monochrome;         // Monochorme video
  aom_color_primaries_t color_primaries;
  aom_transfer_characteristics_t transfer_characteristics;
  aom_matrix_coefficients_t matrix_coefficients;
  int color_range;
  int subsampling_x;  // Chroma subsampling for x
  int subsampling_y;  // Chroma subsampling for y
  aom_chroma_sample_position_t chroma_sample_position;
  uint8_t separate_uv_delta_q;
  uint8_t film_grain_params_present;
} SequenceHeader;

typedef struct {
  int skip_mode_allowed;
  int skip_mode_flag;
  int ref_frame_idx_0;
  int ref_frame_idx_1;
} SkipModeInfo;

typedef struct {
  FRAME_TYPE frame_type;
  REFERENCE_MODE reference_mode;

  unsigned int order_hint;
  unsigned int display_order_hint;
  unsigned int frame_number;
  SkipModeInfo skip_mode_info;
  int refresh_frame_flags;  // Which ref frames are overwritten by this frame
  int frame_refs_short_signaling;
} CurrentFrame;

typedef struct AV1Common {
  CurrentFrame current_frame;
  struct aom_internal_error_info error;
  int width;
  int height;
  int render_width;
  int render_height;
  int timing_info_present;
  aom_timing_info_t timing_info;
  int buffer_removal_time_present;
  aom_dec_model_info_t buffer_model;
  aom_dec_model_op_parameters_t op_params[MAX_NUM_OPERATING_POINTS + 1];
  aom_op_timing_info_t op_frame_timing[MAX_NUM_OPERATING_POINTS + 1];
  uint32_t frame_presentation_time;

  int context_update_tile_id;

  // Scale of the current frame with respect to itself.
  struct scale_factors sf_identity;

  RefCntBuffer *prev_frame;

  // TODO(hkuang): Combine this with cur_buf in macroblockd.
  RefCntBuffer *cur_frame;

  // For encoder, we have a two-level mapping from reference frame type to the
  // corresponding buffer in the buffer pool:
  // * 'remapped_ref_idx[i - 1]' maps reference type 'i' (range: LAST_FRAME ...
  // EXTREF_FRAME) to a remapped index 'j' (in range: 0 ... REF_FRAMES - 1)
  // * Later, 'cm->ref_frame_map[j]' maps the remapped index 'j' to a pointer to
  // the reference counted buffer structure RefCntBuffer, taken from the buffer
  // pool cm->buffer_pool->frame_bufs.
  //
  // LAST_FRAME,                        ...,      EXTREF_FRAME
  //      |                                           |
  //      v                                           v
  // remapped_ref_idx[LAST_FRAME - 1],  ...,  remapped_ref_idx[EXTREF_FRAME - 1]
  //      |                                           |
  //      v                                           v
  // ref_frame_map[],                   ...,     ref_frame_map[]
  //
  // Note: INTRA_FRAME always refers to the current frame, so there's no need to
  // have a remapped index for the same.
  int remapped_ref_idx[REF_FRAMES];

  struct scale_factors ref_scale_factors[REF_FRAMES];

  // For decoder, ref_frame_map[i] maps reference type 'i' to a pointer to
  // the buffer in the buffer pool 'cm->buffer_pool.frame_bufs'.
  // For encoder, ref_frame_map[j] (where j = remapped_ref_idx[i]) maps
  // remapped reference index 'j' (that is, original reference type 'i') to
  // a pointer to the buffer in the buffer pool 'cm->buffer_pool.frame_bufs'.
  RefCntBuffer *ref_frame_map[REF_FRAMES];

  // Prepare ref_frame_map for the next frame.
  // Only used in frame parallel decode.
  RefCntBuffer *next_ref_frame_map[REF_FRAMES];
  FRAME_TYPE last_frame_type; /* last frame's frame type for motion search.*/

  int show_frame;
  int showable_frame;  // frame can be used as show existing frame in future
  int show_existing_frame;

  uint8_t disable_cdf_update;
  MvSubpelPrecision mv_precision;
  uint8_t cur_frame_force_integer_mv;  // 0 the default in AOM, 1 only integer
#if CONFIG_FLEX_MVRES
  uint8_t use_flex_mv_precision;
#endif  // CONFIG_FLEX_MVRES

  uint8_t allow_screen_content_tools;
  int allow_intrabc;
  int allow_warped_motion;

  // MBs, mb_rows/cols is in 16-pixel units; mi_rows/cols is in
  // MB_MODE_INFO (4-pixel) units.
  int MBs;
  int mb_rows, mi_rows;
  int mb_cols, mi_cols;
  int mi_stride;

  /* profile settings */
  TX_MODE tx_mode;

#if CONFIG_ENTROPY_STATS
  int coef_cdf_category;
#endif

  int base_qindex;
  int y_dc_delta_q;
  int u_dc_delta_q;
  int v_dc_delta_q;
  int u_ac_delta_q;
  int v_ac_delta_q;

  // The dequantizers below are true dequantizers used only in the
  // dequantization process.  They have the same coefficient
  // shift/scale as TX.
  int16_t y_dequant_QTX[MAX_SEGMENTS][2];
  int16_t u_dequant_QTX[MAX_SEGMENTS][2];
  int16_t v_dequant_QTX[MAX_SEGMENTS][2];

  // Global quant matrix tables
  const qm_val_t *giqmatrix[NUM_QM_LEVELS][3][TX_SIZES_ALL];
  const qm_val_t *gqmatrix[NUM_QM_LEVELS][3][TX_SIZES_ALL];

  // Local quant matrix tables for each frame
  const qm_val_t *y_iqmatrix[MAX_SEGMENTS][TX_SIZES_ALL];
  const qm_val_t *u_iqmatrix[MAX_SEGMENTS][TX_SIZES_ALL];
  const qm_val_t *v_iqmatrix[MAX_SEGMENTS][TX_SIZES_ALL];

  // Encoder
  int using_qmatrix;
  int qm_y;
  int qm_u;
  int qm_v;
  int min_qmlevel;
  int max_qmlevel;
  int use_quant_b_adapt;

  /* We allocate a MB_MODE_INFO struct for each macroblock, together with
     an extra row on top and column on the left to simplify prediction. */
  int mi_alloc_size;
  MB_MODE_INFO *mi; /* Corresponds to upper left visible macroblock */

  // TODO(agrange): Move prev_mi into encoder structure.
  // prev_mi will only be allocated in encoder.
  MB_MODE_INFO *prev_mi; /* 'mi' from last frame */

  // Separate mi functions between encoder and decoder.
  int (*alloc_mi)(struct AV1Common *cm, int mi_size);
  void (*free_mi)(struct AV1Common *cm);
  void (*setup_mi)(struct AV1Common *cm);

  // Grid of pointers to 4x4 MB_MODE_INFO structs. Any 4x4 not in the visible
  // area will be NULL.
  MB_MODE_INFO **mi_grid_base;
  MB_MODE_INFO **prev_mi_grid_base;

  // Whether to use previous frames' motion vectors for prediction.
  int allow_ref_frame_mvs;

  uint8_t *last_frame_seg_map;

  InterpFilter interp_filter;

  int switchable_motion_mode;

  loop_filter_info_n lf_info;
  // The denominator of the superres scale; the numerator is fixed.
  uint8_t superres_scale_denominator;
  int superres_upscaled_width;
  int superres_upscaled_height;
  RestorationInfo rst_info[MAX_MB_PLANE];

  // Pointer to a scratch buffer used by self-guided restoration
  int32_t *rst_tmpbuf;
  RestorationLineBuffers *rlbs;

  // Output of loop restoration
  YV12_BUFFER_CONFIG rst_frame;

  // Flag signaling how frame contexts should be updated at the end of
  // a frame decode
  REFRESH_FRAME_CONTEXT_MODE refresh_frame_context;

  int ref_frame_sign_bias[REF_FRAMES]; /* Two state 0, 1 */

  struct loopfilter lf;
  struct segmentation seg;
  int coded_lossless;  // frame is fully lossless at the coded resolution.
  int all_lossless;    // frame is fully lossless at the upscaled resolution.

  int reduced_tx_set_used;

  // Context probabilities for reference frame prediction
  MV_REFERENCE_FRAME comp_fwd_ref[FWD_REFS];
  MV_REFERENCE_FRAME comp_bwd_ref[BWD_REFS];

  FRAME_CONTEXT *fc; /* this frame entropy */
  FRAME_CONTEXT *default_frame_context;
  int primary_ref_frame;

  int error_resilient_mode;

  int tile_cols, tile_rows;

  int max_tile_width_sb;
  int min_log2_tile_cols;
  int max_log2_tile_cols;
  int max_log2_tile_rows;
  int min_log2_tile_rows;
  int min_log2_tiles;
  int max_tile_height_sb;
  int uniform_tile_spacing_flag;
  int log2_tile_cols;                        // only valid for uniform tiles
  int log2_tile_rows;                        // only valid for uniform tiles
  int tile_col_start_sb[MAX_TILE_COLS + 1];  // valid for 0 <= i <= tile_cols
  int tile_row_start_sb[MAX_TILE_ROWS + 1];  // valid for 0 <= i <= tile_rows
  int tile_width, tile_height;               // In MI units
  int min_inner_tile_width;                  // min width of non-rightmost tile

  unsigned int large_scale_tile;
  unsigned int single_tile_decoding;

  int byte_alignment;
  int skip_loop_filter;
  int skip_film_grain;

  // External BufferPool passed from outside.
  BufferPool *buffer_pool;

  PARTITION_CONTEXT **above_seg_context;
  ENTROPY_CONTEXT **above_context[MAX_MB_PLANE];
  TXFM_CONTEXT **above_txfm_context;
  WarpedMotionParams global_motion[REF_FRAMES];
  aom_film_grain_t film_grain_params;

  CdefInfo cdef_info;
  DeltaQInfo delta_q_info;  // Delta Q and Delta LF parameters

  int num_tg;
  SequenceHeader seq_params;
  int current_frame_id;
  int ref_frame_id[REF_FRAMES];
  int valid_for_referencing[REF_FRAMES];
  TPL_MV_REF *tpl_mvs;
  int tpl_mvs_mem_size;
  // TODO(jingning): This can be combined with sign_bias later.
  int8_t ref_frame_side[REF_FRAMES];

  int is_annexb;

  int temporal_layer_id;
  int spatial_layer_id;
  unsigned int number_temporal_layers;
  unsigned int number_spatial_layers;
  int num_allocated_above_context_mi_col;
  int num_allocated_above_contexts;
  int num_allocated_above_context_planes;

#if TXCOEFF_TIMER
  int64_t cum_txcoeff_timer;
  int64_t txcoeff_timer;
  int txb_count;
#endif

#if TXCOEFF_COST_TIMER
  int64_t cum_txcoeff_cost_timer;
  int64_t txcoeff_cost_timer;
  int64_t txcoeff_cost_count;
#endif
  const cfg_options_t *options;
  int is_decoding;
#if CONFIG_CNN_RESTORATION
  int use_cnn;
#endif  // CONFIG_CNN_RESTORATION

#if CRLC_LF
  int use_cnn;
  CRLCInfo crlc_info[MAX_MB_PLANE];
#endif
} AV1_COMMON;

// TODO(hkuang): Don't need to lock the whole pool after implementing atomic
// frame reference count.
static void lock_buffer_pool(BufferPool *const pool) {
#if CONFIG_MULTITHREAD
  pthread_mutex_lock(&pool->pool_mutex);
#else
  (void)pool;
#endif
}

static void unlock_buffer_pool(BufferPool *const pool) {
#if CONFIG_MULTITHREAD
  pthread_mutex_unlock(&pool->pool_mutex);
#else
  (void)pool;
#endif
}

static INLINE YV12_BUFFER_CONFIG *get_ref_frame(AV1_COMMON *cm, int index) {
  if (index < 0 || index >= REF_FRAMES) return NULL;
  if (cm->ref_frame_map[index] == NULL) return NULL;
  return &cm->ref_frame_map[index]->buf;
}

static INLINE int get_free_fb(AV1_COMMON *cm) {
  RefCntBuffer *const frame_bufs = cm->buffer_pool->frame_bufs;
  int i;

  lock_buffer_pool(cm->buffer_pool);
  for (i = 0; i < FRAME_BUFFERS; ++i)
    if (frame_bufs[i].ref_count == 0) break;

  if (i != FRAME_BUFFERS) {
    if (frame_bufs[i].buf.use_external_reference_buffers) {
      // If this frame buffer's y_buffer, u_buffer, and v_buffer point to the
      // external reference buffers. Restore the buffer pointers to point to the
      // internally allocated memory.
      YV12_BUFFER_CONFIG *ybf = &frame_bufs[i].buf;
      ybf->y_buffer = ybf->store_buf_adr[0];
      ybf->u_buffer = ybf->store_buf_adr[1];
      ybf->v_buffer = ybf->store_buf_adr[2];
      ybf->use_external_reference_buffers = 0;
    }

    frame_bufs[i].ref_count = 1;
  } else {
    // We should never run out of free buffers. If this assertion fails, there
    // is a reference leak.
    assert(0 && "Ran out of free frame buffers. Likely a reference leak.");
    // Reset i to be INVALID_IDX to indicate no free buffer found.
    i = INVALID_IDX;
  }

  unlock_buffer_pool(cm->buffer_pool);
  return i;
}

static INLINE RefCntBuffer *assign_cur_frame_new_fb(AV1_COMMON *const cm) {
  // Release the previously-used frame-buffer
  if (cm->cur_frame != NULL) {
    --cm->cur_frame->ref_count;
    cm->cur_frame = NULL;
  }

  // Assign a new framebuffer
  const int new_fb_idx = get_free_fb(cm);
  if (new_fb_idx == INVALID_IDX) return NULL;

  cm->cur_frame = &cm->buffer_pool->frame_bufs[new_fb_idx];
  cm->cur_frame->buf.buf_8bit_valid = 0;
  av1_zero(cm->cur_frame->interp_filter_selected);
  return cm->cur_frame;
}

// Modify 'lhs_ptr' to reference the buffer at 'rhs_ptr', and update the ref
// counts accordingly.
static INLINE void assign_frame_buffer_p(RefCntBuffer **lhs_ptr,
                                         RefCntBuffer *rhs_ptr) {
  RefCntBuffer *const old_ptr = *lhs_ptr;
  if (old_ptr != NULL) {
    assert(old_ptr->ref_count > 0);
    // One less reference to the buffer at 'old_ptr', so decrease ref count.
    --old_ptr->ref_count;
  }

  *lhs_ptr = rhs_ptr;
  // One more reference to the buffer at 'rhs_ptr', so increase ref count.
  ++rhs_ptr->ref_count;
}

static INLINE int frame_is_intra_only(const AV1_COMMON *const cm) {
  return cm->current_frame.frame_type == KEY_FRAME ||
         cm->current_frame.frame_type == INTRA_ONLY_FRAME;
}

static INLINE int frame_is_sframe(const AV1_COMMON *cm) {
  return cm->current_frame.frame_type == S_FRAME;
}

// These functions take a reference frame label between LAST_FRAME and
// EXTREF_FRAME inclusive.  Note that this is different to the indexing
// previously used by the frame_refs[] array.
static INLINE int get_ref_frame_map_idx(const AV1_COMMON *const cm,
                                        const MV_REFERENCE_FRAME ref_frame) {
  return (ref_frame >= LAST_FRAME && ref_frame <= EXTREF_FRAME)
             ? cm->remapped_ref_idx[ref_frame - LAST_FRAME]
             : INVALID_IDX;
}

static INLINE RefCntBuffer *get_ref_frame_buf(
    const AV1_COMMON *const cm, const MV_REFERENCE_FRAME ref_frame) {
  const int map_idx = get_ref_frame_map_idx(cm, ref_frame);
  return (map_idx != INVALID_IDX) ? cm->ref_frame_map[map_idx] : NULL;
}

// Both const and non-const versions of this function are provided so that it
// can be used with a const AV1_COMMON if needed.
static INLINE const struct scale_factors *get_ref_scale_factors_const(
    const AV1_COMMON *const cm, const MV_REFERENCE_FRAME ref_frame) {
  const int map_idx = get_ref_frame_map_idx(cm, ref_frame);
  return (map_idx != INVALID_IDX) ? &cm->ref_scale_factors[map_idx] : NULL;
}

static INLINE struct scale_factors *get_ref_scale_factors(
    AV1_COMMON *const cm, const MV_REFERENCE_FRAME ref_frame) {
  const int map_idx = get_ref_frame_map_idx(cm, ref_frame);
  return (map_idx != INVALID_IDX) ? &cm->ref_scale_factors[map_idx] : NULL;
}

static INLINE RefCntBuffer *get_primary_ref_frame_buf(
    const AV1_COMMON *const cm) {
  if (cm->primary_ref_frame == PRIMARY_REF_NONE) return NULL;
  const int map_idx = get_ref_frame_map_idx(cm, cm->primary_ref_frame + 1);
  return (map_idx != INVALID_IDX) ? cm->ref_frame_map[map_idx] : NULL;
}

// Returns 1 if this frame might allow mvs from some reference frame.
static INLINE int frame_might_allow_ref_frame_mvs(const AV1_COMMON *cm) {
  return !cm->error_resilient_mode &&
         cm->seq_params.order_hint_info.enable_ref_frame_mvs &&
         cm->seq_params.order_hint_info.enable_order_hint &&
         !frame_is_intra_only(cm);
}

// Returns 1 if this frame might use warped_motion
static INLINE int frame_might_allow_warped_motion(const AV1_COMMON *cm) {
  return !cm->error_resilient_mode && !frame_is_intra_only(cm) &&
         cm->seq_params.enable_warped_motion;
}

static INLINE void ensure_mv_buffer(RefCntBuffer *buf, AV1_COMMON *cm) {
  const int buf_rows = buf->mi_rows;
  const int buf_cols = buf->mi_cols;

  if (buf->mvs == NULL || buf_rows != cm->mi_rows || buf_cols != cm->mi_cols) {
    aom_free(buf->mvs);
    buf->mi_rows = cm->mi_rows;
    buf->mi_cols = cm->mi_cols;
    CHECK_MEM_ERROR(cm, buf->mvs,
                    (MV_REF *)aom_calloc(
                        ((cm->mi_rows + 1) >> 1) * ((cm->mi_cols + 1) >> 1),
                        sizeof(*buf->mvs)));
    aom_free(buf->seg_map);
    CHECK_MEM_ERROR(cm, buf->seg_map,
                    (uint8_t *)aom_calloc(cm->mi_rows * cm->mi_cols,
                                          sizeof(*buf->seg_map)));
  }

  const int mem_size =
      ((cm->mi_rows + MAX_MIB_SIZE) >> 1) * (cm->mi_stride >> 1);
  int realloc = cm->tpl_mvs == NULL;
  if (cm->tpl_mvs) realloc |= cm->tpl_mvs_mem_size < mem_size;

  if (realloc) {
    aom_free(cm->tpl_mvs);
    CHECK_MEM_ERROR(cm, cm->tpl_mvs,
                    (TPL_MV_REF *)aom_calloc(mem_size, sizeof(*cm->tpl_mvs)));
    cm->tpl_mvs_mem_size = mem_size;
  }
}

void cfl_init(CFL_CTX *cfl, const SequenceHeader *seq_params);

static INLINE int av1_num_planes(const AV1_COMMON *cm) {
  return cm->seq_params.monochrome ? 1 : MAX_MB_PLANE;
}

static INLINE void av1_init_above_context(AV1_COMMON *cm, MACROBLOCKD *xd,
                                          const int tile_row) {
  const int num_planes = av1_num_planes(cm);
  for (int i = 0; i < num_planes; ++i) {
    xd->above_context[i] = cm->above_context[i][tile_row];
  }
  xd->above_seg_context = cm->above_seg_context[tile_row];
  xd->above_txfm_context = cm->above_txfm_context[tile_row];
}

static INLINE void av1_init_macroblockd(AV1_COMMON *cm, MACROBLOCKD *xd,
                                        tran_low_t *dqcoeff) {
  const int num_planes = av1_num_planes(cm);
  for (int i = 0; i < num_planes; ++i) {
    xd->plane[i].dqcoeff = dqcoeff;

    if (xd->plane[i].plane_type == PLANE_TYPE_Y) {
      memcpy(xd->plane[i].seg_dequant_QTX, cm->y_dequant_QTX,
             sizeof(cm->y_dequant_QTX));
      memcpy(xd->plane[i].seg_iqmatrix, cm->y_iqmatrix, sizeof(cm->y_iqmatrix));

    } else {
      if (i == AOM_PLANE_U) {
        memcpy(xd->plane[i].seg_dequant_QTX, cm->u_dequant_QTX,
               sizeof(cm->u_dequant_QTX));
        memcpy(xd->plane[i].seg_iqmatrix, cm->u_iqmatrix,
               sizeof(cm->u_iqmatrix));
      } else {
        memcpy(xd->plane[i].seg_dequant_QTX, cm->v_dequant_QTX,
               sizeof(cm->v_dequant_QTX));
        memcpy(xd->plane[i].seg_iqmatrix, cm->v_iqmatrix,
               sizeof(cm->v_iqmatrix));
      }
    }
  }
  xd->mi_stride = cm->mi_stride;
  xd->error_info = &cm->error;
  cfl_init(&xd->cfl, &cm->seq_params);
}

static INLINE void set_skip_context(MACROBLOCKD *xd, int mi_row, int mi_col,
                                    BLOCK_SIZE bsize, int num_planes) {
  int i;
  int row_offset = mi_row;
  int col_offset = mi_col;
  for (i = 0; i < num_planes; ++i) {
    struct macroblockd_plane *const pd = &xd->plane[i];
    // Offset the buffer pointer
    // TODO(urvang): Refactor the spread-out logic for offsetting into a common
    // function.
#if CONFIG_3WAY_PARTITIONS
    if (pd->subsampling_y && (mi_row & 0x01)) {
      if (mi_size_wide[bsize] == 4 && mi_size_high[bsize] == 1) {
        // Special case: 3rd horizontal sub-block of a 16x16 horz3 partition.
        row_offset = mi_row - 3;
      } else if (mi_size_high[bsize] == 1) {
        row_offset = mi_row - 1;
      } else if (mi_size_wide[bsize] == 4 && mi_size_high[bsize] == 2) {
        // Special case: 2rd horizontal sub-block of a 16x16 horz3 partition.
        row_offset = mi_row - 1;
      }
    }
    if (pd->subsampling_x && (mi_col & 0x01)) {
      if (mi_size_wide[bsize] == 1 && mi_size_high[bsize] == 4) {
        // Special case: 3rd vertical sub-block of a 16x16 vert3 partition.
        col_offset = mi_col - 3;
      } else if (mi_size_wide[bsize] == 1) {
        col_offset = mi_col - 1;
      } else if (mi_size_wide[bsize] == 2 && mi_size_high[bsize] == 4) {
        // Special case: 2nd vertical sub-block of a 16x16 vert3 partition.
        col_offset = mi_col - 1;
      }
    }
#else
    if (pd->subsampling_y && (mi_row & 0x01) && (mi_size_high[bsize] == 1))
      row_offset = mi_row - 1;
    if (pd->subsampling_x && (mi_col & 0x01) && (mi_size_wide[bsize] == 1))
      col_offset = mi_col - 1;
#endif  // CONFIG_3WAY_PARTITIONS
    assert(row_offset >= 0);
    assert(col_offset >= 0);
    int above_idx = col_offset;
    int left_idx = row_offset & MAX_MIB_MASK;
    assert(IMPLIES(pd->subsampling_x, above_idx % 2 == 0));
    assert(IMPLIES(pd->subsampling_y, left_idx % 2 == 0));
    pd->above_context = &xd->above_context[i][above_idx >> pd->subsampling_x];
    pd->left_context = &xd->left_context[i][left_idx >> pd->subsampling_y];
  }
}

static INLINE int calc_mi_size(int len) {
  // len is in mi units. Align to a multiple of SBs.
  return ALIGN_POWER_OF_TWO(len, MAX_MIB_SIZE_LOG2);
}

static INLINE void set_plane_n4(MACROBLOCKD *const xd, int mi_row, int mi_col,
                                BLOCK_SIZE bsize, int num_planes) {
  for (int i = 0; i < num_planes; i++) {
    struct macroblockd_plane *const pd = &xd->plane[i];
    bsize = scale_chroma_bsize(bsize, pd->subsampling_x, pd->subsampling_y,
                               mi_row, mi_col);
    pd->width = block_size_wide[bsize] >> pd->subsampling_x;
    pd->height = block_size_high[bsize] >> pd->subsampling_y;
    assert(pd->width >= 4);
    assert(pd->height >= 4);
  }
}

static INLINE int is_chroma_reference_helper(int mi_row, int mi_col, int bw,
                                             int bh, int subsampling_x,
                                             int subsampling_y) {
#if CONFIG_3WAY_PARTITIONS
  const int is_2nd_16x16_horz3_subpartition =
      (mi_row & 0x01) && (bw == 4) && (bh == 2) && subsampling_y;
  const int is_2nd_16x16_vert3_subpartition =
      (mi_col & 0x01) && (bw == 2) && (bh == 4) && subsampling_x;
  if (is_2nd_16x16_horz3_subpartition || is_2nd_16x16_vert3_subpartition) {
    return 0;
  }
#endif  // CONFIG_3WAY_PARTITIONS

  return ((mi_row & 0x01) || !(bh & 0x01) || !subsampling_y) &&
         ((mi_col & 0x01) || !(bw & 0x01) || !subsampling_x);
}

static INLINE void set_mi_row_col(MACROBLOCKD *xd, const TileInfo *const tile,
                                  int mi_row, int bh, int mi_col, int bw,
                                  int mi_rows, int mi_cols) {
  xd->mb_to_top_edge = -((mi_row * MI_SIZE) * 8);
  xd->mb_to_bottom_edge = ((mi_rows - bh - mi_row) * MI_SIZE) * 8;
  xd->mb_to_left_edge = -((mi_col * MI_SIZE) * 8);
  xd->mb_to_right_edge = ((mi_cols - bw - mi_col) * MI_SIZE) * 8;

  // Are edges available for intra prediction?
  xd->up_available = (mi_row > tile->mi_row_start);

  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;

  xd->left_available = (mi_col > tile->mi_col_start);
  xd->chroma_up_available = xd->up_available;
  xd->chroma_left_available = xd->left_available;

#if CONFIG_3WAY_PARTITIONS
  if (ss_x && (mi_col & 0x01)) {
    if (bw == 1 && bh == 4) {
      // Special case: 3rd vertical sub-block of a 16x16 vert3 partition.
      xd->chroma_left_available = (mi_col - 3) > tile->mi_col_start;
    } else if (bw == 1) {
      xd->chroma_left_available = (mi_col - 1) > tile->mi_col_start;
    } else if (bw == 2 && bh == 4) {
      // Special case: 2nd vertical sub-block of a 16x16 vert3 partition.
      xd->chroma_left_available = (mi_col - 1) > tile->mi_col_start;
    }
  }
  if (ss_y && (mi_row & 0x01)) {
    if (bw == 4 && bh == 1) {
      // Special case: 3rd horizontal sub-block of a 16x16 horz3 partition.
      xd->chroma_up_available = (mi_row - 3) > tile->mi_row_start;
    } else if (bh == 1) {
      xd->chroma_up_available = (mi_row - 1) > tile->mi_row_start;
    } else if (bw == 4 && bh == 2) {
      // Special case: 2rd horizontal sub-block of a 16x16 horz3 partition.
      xd->chroma_up_available = (mi_row - 1) > tile->mi_row_start;
    }
  }
#else
  if (ss_x && bw < mi_size_wide[BLOCK_8X8])
    xd->chroma_left_available = (mi_col - 1) > tile->mi_col_start;
  if (ss_y && bh < mi_size_high[BLOCK_8X8])
    xd->chroma_up_available = (mi_row - 1) > tile->mi_row_start;
#endif  // CONFIG_3WAY_PARTITIONS

  if (xd->up_available) {
    xd->above_mbmi = xd->mi[-xd->mi_stride];
  } else {
    xd->above_mbmi = NULL;
  }

  if (xd->left_available) {
    xd->left_mbmi = xd->mi[-1];
  } else {
    xd->left_mbmi = NULL;
  }
#if CONFIG_INTRA_ENTROPY
  if (xd->left_available && xd->up_available) {
    xd->aboveleft_mbmi = xd->mi[-xd->mi_stride - 1];
  } else {
    xd->aboveleft_mbmi = NULL;
  }
#endif  // CONFIG_INTRA_ENTROPY

  const int chroma_ref =
      is_chroma_reference_helper(mi_row, mi_col, bw, bh, ss_x, ss_y);
  if (chroma_ref) {
    // To help calculate the "above" and "left" chroma blocks, note that the
    // current block may cover multiple luma blocks (eg, if partitioned into
    // 4x4 luma blocks).
    // First, find the top-left-most luma block covered by this chroma block
#if CONFIG_3WAY_PARTITIONS
    const int row_offset = (bw == 4 && bh == 1) ? 3 : 1;
    const int col_offset = (bw == 1 && bh == 4) ? 3 : 1;
#else
    const int row_offset = 1;
    const int col_offset = 1;
#endif  // CONFIG_3WAY_PARTITIONS
    const int row_offset_chroma = row_offset * (mi_row & ss_y);
    const int col_offset_chroma = col_offset * (mi_col & ss_x);
    MB_MODE_INFO **base_mi =
        &xd->mi[-row_offset_chroma * xd->mi_stride - col_offset_chroma];

    // Then, we consider the luma region covered by the left or above 4x4 chroma
    // prediction. We want to point to the chroma reference block in that
    // region, which is the bottom-right-most mi unit.
    // This leads to the following offsets:
    MB_MODE_INFO *chroma_above_mi =
        xd->chroma_up_available ? base_mi[-xd->mi_stride + ss_x] : NULL;
    xd->chroma_above_mbmi = chroma_above_mi;

    MB_MODE_INFO *chroma_left_mi =
        xd->chroma_left_available ? base_mi[ss_y * xd->mi_stride - 1] : NULL;
    xd->chroma_left_mbmi = chroma_left_mi;
  }

  xd->n4_h = bh;
  xd->n4_w = bw;
  xd->is_sec_rect = 0;
  if (xd->n4_w < xd->n4_h) {
#if CONFIG_3WAY_PARTITIONS
    // For PARTITION_VERT_3, it would be (0, 1, 1), because 2nd subpartition has
    // ratio 1:2, so not enough top-right pixels are available.
    // For other partitions, it would be (0, 1).
    if (mi_col & (xd->n4_h - 1)) xd->is_sec_rect = 1;
#else
    // Only mark is_sec_rect as 1 for the last block.
    // For PARTITION_VERT_4, it would be (0, 0, 0, 1);
    // For other partitions, it would be (0, 1).
    if (!((mi_col + xd->n4_w) & (xd->n4_h - 1))) xd->is_sec_rect = 1;
#endif  // CONFIG_3WAY_PARTITIONS
  }

  if (xd->n4_w > xd->n4_h)
    if (mi_row & (xd->n4_w - 1)) xd->is_sec_rect = 1;
}

#if !CONFIG_INTRA_ENTROPY
static INLINE aom_cdf_prob *get_y_mode_cdf(FRAME_CONTEXT *tile_ctx,
                                           const MB_MODE_INFO *above_mi,
                                           const MB_MODE_INFO *left_mi) {
  const PREDICTION_MODE above = av1_above_block_mode(above_mi);
  const PREDICTION_MODE left = av1_left_block_mode(left_mi);
  const int above_ctx = intra_mode_context[above];
  const int left_ctx = intra_mode_context[left];
  return tile_ctx->kf_y_cdf[above_ctx][left_ctx];
}
#endif  // !CONFIG_INTRA_ENTROPY

static INLINE void update_partition_context(MACROBLOCKD *xd, int mi_row,
                                            int mi_col, BLOCK_SIZE subsize,
                                            BLOCK_SIZE bsize) {
  PARTITION_CONTEXT *const above_ctx = xd->above_seg_context + mi_col;
  PARTITION_CONTEXT *const left_ctx =
      xd->left_seg_context + (mi_row & MAX_MIB_MASK);

  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  memset(above_ctx, partition_context_lookup[subsize].above, bw);
  memset(left_ctx, partition_context_lookup[subsize].left, bh);
}

static INLINE int is_chroma_reference(int mi_row, int mi_col, BLOCK_SIZE bsize,
                                      int subsampling_x, int subsampling_y) {
  assert(bsize < BLOCK_SIZES_ALL);
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  return is_chroma_reference_helper(mi_row, mi_col, bw, bh, subsampling_x,
                                    subsampling_y);
}

static INLINE aom_cdf_prob cdf_element_prob(const aom_cdf_prob *cdf,
                                            size_t element) {
  assert(cdf != NULL);
  return (element > 0 ? cdf[element - 1] : CDF_PROB_TOP) - cdf[element];
}

static INLINE void partition_gather_horz_alike(aom_cdf_prob *out,
                                               const aom_cdf_prob *const in,
                                               BLOCK_SIZE bsize) {
  (void)bsize;
  out[0] = CDF_PROB_TOP;
  out[0] -= cdf_element_prob(in, PARTITION_HORZ);
  out[0] -= cdf_element_prob(in, PARTITION_SPLIT);
  out[0] -= cdf_element_prob(in, PARTITION_HORZ_A);
  out[0] -= cdf_element_prob(in, PARTITION_HORZ_B);
  out[0] -= cdf_element_prob(in, PARTITION_VERT_A);
#if CONFIG_3WAY_PARTITIONS
  if (bsize != BLOCK_128X128) out[0] -= cdf_element_prob(in, PARTITION_HORZ_3);
#else
  if (bsize != BLOCK_128X128) out[0] -= cdf_element_prob(in, PARTITION_HORZ_4);
#endif  // CONFIG_3WAY_PARTITIONS
  out[0] = AOM_ICDF(out[0]);
  out[1] = AOM_ICDF(CDF_PROB_TOP);
}

static INLINE void partition_gather_vert_alike(aom_cdf_prob *out,
                                               const aom_cdf_prob *const in,
                                               BLOCK_SIZE bsize) {
  (void)bsize;
  out[0] = CDF_PROB_TOP;
  out[0] -= cdf_element_prob(in, PARTITION_VERT);
  out[0] -= cdf_element_prob(in, PARTITION_SPLIT);
  out[0] -= cdf_element_prob(in, PARTITION_HORZ_A);
  out[0] -= cdf_element_prob(in, PARTITION_VERT_A);
  out[0] -= cdf_element_prob(in, PARTITION_VERT_B);
#if CONFIG_3WAY_PARTITIONS
  if (bsize != BLOCK_128X128) out[0] -= cdf_element_prob(in, PARTITION_VERT_3);
#else
  if (bsize != BLOCK_128X128) out[0] -= cdf_element_prob(in, PARTITION_VERT_4);
#endif  // CONFIG_3WAY_PARTITIONS
  out[0] = AOM_ICDF(out[0]);
  out[1] = AOM_ICDF(CDF_PROB_TOP);
}

static INLINE void update_ext_partition_context(MACROBLOCKD *xd, int mi_row,
                                                int mi_col, BLOCK_SIZE subsize,
                                                BLOCK_SIZE bsize,
                                                PARTITION_TYPE partition) {
  if (bsize >= BLOCK_8X8) {
    const int hbs = mi_size_wide[bsize] / 2;
#if CONFIG_3WAY_PARTITIONS
    const int quarter_step = hbs / 2;
#endif  // CONFIG_3WAY_PARTITIONS
    const BLOCK_SIZE bsize2 = get_partition_subsize(bsize, PARTITION_SPLIT);
    switch (partition) {
      case PARTITION_SPLIT:
        if (bsize != BLOCK_8X8) break;
        AOM_FALLTHROUGH_INTENDED;
      case PARTITION_NONE:
      case PARTITION_HORZ:
      case PARTITION_VERT:
        update_partition_context(xd, mi_row, mi_col, subsize, bsize);
        break;
#if CONFIG_3WAY_PARTITIONS
      case PARTITION_HORZ_3: {
        const BLOCK_SIZE bsize3 = get_partition_subsize(bsize, PARTITION_HORZ);
        update_partition_context(xd, mi_row, mi_col, subsize, subsize);
        update_partition_context(xd, mi_row + quarter_step, mi_col, bsize3,
                                 bsize3);
        update_partition_context(xd, mi_row + 3 * quarter_step, mi_col, subsize,
                                 subsize);
        break;
      }
      case PARTITION_VERT_3: {
        const BLOCK_SIZE bsize3 = get_partition_subsize(bsize, PARTITION_VERT);
        update_partition_context(xd, mi_row, mi_col, subsize, subsize);
        update_partition_context(xd, mi_row, mi_col + quarter_step, bsize3,
                                 bsize3);
        update_partition_context(xd, mi_row, mi_col + 3 * quarter_step, subsize,
                                 subsize);
        break;
      }
#else
      case PARTITION_HORZ_4:
      case PARTITION_VERT_4:
        update_partition_context(xd, mi_row, mi_col, subsize, bsize);
        break;
#endif  // CONFIG_3WAY_PARTITIONS
      case PARTITION_HORZ_A:
        update_partition_context(xd, mi_row, mi_col, bsize2, subsize);
        update_partition_context(xd, mi_row + hbs, mi_col, subsize, subsize);
        break;
      case PARTITION_HORZ_B:
        update_partition_context(xd, mi_row, mi_col, subsize, subsize);
        update_partition_context(xd, mi_row + hbs, mi_col, bsize2, subsize);
        break;
      case PARTITION_VERT_A:
        update_partition_context(xd, mi_row, mi_col, bsize2, subsize);
        update_partition_context(xd, mi_row, mi_col + hbs, subsize, subsize);
        break;
      case PARTITION_VERT_B:
        update_partition_context(xd, mi_row, mi_col, subsize, subsize);
        update_partition_context(xd, mi_row, mi_col + hbs, bsize2, subsize);
        break;
      default: assert(0 && "Invalid partition type");
    }
  }
}

static INLINE int partition_plane_context(const MACROBLOCKD *xd, int mi_row,
                                          int mi_col, BLOCK_SIZE bsize) {
  const PARTITION_CONTEXT *above_ctx = xd->above_seg_context + mi_col;
  const PARTITION_CONTEXT *left_ctx =
      xd->left_seg_context + (mi_row & MAX_MIB_MASK);
  // Minimum partition point is 8x8. Offset the bsl accordingly.
  const int bsl = mi_size_wide_log2[bsize] - mi_size_wide_log2[BLOCK_8X8];
  int above = (*above_ctx >> bsl) & 1, left = (*left_ctx >> bsl) & 1;

  assert(mi_size_wide_log2[bsize] == mi_size_high_log2[bsize]);
  assert(bsl >= 0);

  return (left * 2 + above) + bsl * PARTITION_PLOFFSET;
}

// Return the number of elements in the partition CDF when
// partitioning the (square) block with luma block size of bsize.
static INLINE int partition_cdf_length(BLOCK_SIZE bsize) {
  if (bsize <= BLOCK_8X8)
    return PARTITION_TYPES;
  else if (bsize == BLOCK_128X128)
    return EXT_PARTITION_TYPES - 2;
  else
    return EXT_PARTITION_TYPES;
}

static INLINE int max_block_wide(const MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                 int plane) {
  assert(bsize < BLOCK_SIZES_ALL);
  int max_blocks_wide = block_size_wide[bsize];
  const struct macroblockd_plane *const pd = &xd->plane[plane];

  if (xd->mb_to_right_edge < 0)
    max_blocks_wide += xd->mb_to_right_edge >> (3 + pd->subsampling_x);

  // Scale the width in the transform block unit.
  return max_blocks_wide >> tx_size_wide_log2[0];
}

static INLINE int max_block_high(const MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                 int plane) {
  assert(bsize < BLOCK_SIZES_ALL);
  int max_blocks_high = block_size_high[bsize];
  const struct macroblockd_plane *const pd = &xd->plane[plane];

  if (xd->mb_to_bottom_edge < 0)
    max_blocks_high += xd->mb_to_bottom_edge >> (3 + pd->subsampling_y);

  // Scale the height in the transform block unit.
  return max_blocks_high >> tx_size_high_log2[0];
}

static INLINE int max_intra_block_width(const MACROBLOCKD *xd,
                                        BLOCK_SIZE plane_bsize, int plane,
                                        TX_SIZE tx_size) {
  const int max_blocks_wide = max_block_wide(xd, plane_bsize, plane)
                              << tx_size_wide_log2[0];
  return ALIGN_POWER_OF_TWO(max_blocks_wide, tx_size_wide_log2[tx_size]);
}

static INLINE int max_intra_block_height(const MACROBLOCKD *xd,
                                         BLOCK_SIZE plane_bsize, int plane,
                                         TX_SIZE tx_size) {
  const int max_blocks_high = max_block_high(xd, plane_bsize, plane)
                              << tx_size_high_log2[0];
  return ALIGN_POWER_OF_TWO(max_blocks_high, tx_size_high_log2[tx_size]);
}

static INLINE void av1_zero_above_context(AV1_COMMON *const cm,
                                          const MACROBLOCKD *xd,
                                          int mi_col_start, int mi_col_end,
                                          const int tile_row) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  const int num_planes = av1_num_planes(cm);
  const int width = mi_col_end - mi_col_start;
  const int aligned_width =
      ALIGN_POWER_OF_TWO(width, seq_params->mib_size_log2);

  const int offset_y = mi_col_start;
  const int width_y = aligned_width;
  const int offset_uv = offset_y >> seq_params->subsampling_x;
  const int width_uv = width_y >> seq_params->subsampling_x;

  av1_zero_array(cm->above_context[0][tile_row] + offset_y, width_y);
  if (num_planes > 1) {
    if (cm->above_context[1][tile_row] && cm->above_context[2][tile_row]) {
      av1_zero_array(cm->above_context[1][tile_row] + offset_uv, width_uv);
      av1_zero_array(cm->above_context[2][tile_row] + offset_uv, width_uv);
    } else {
      aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Invalid value of planes");
    }
  }

  av1_zero_array(cm->above_seg_context[tile_row] + mi_col_start, aligned_width);

  memset(cm->above_txfm_context[tile_row] + mi_col_start,
         tx_size_wide[TX_SIZES_LARGEST], aligned_width * sizeof(TXFM_CONTEXT));
}

static INLINE void av1_zero_left_context(MACROBLOCKD *const xd) {
  av1_zero(xd->left_context);
  av1_zero(xd->left_seg_context);

  memset(xd->left_txfm_context_buffer, tx_size_high[TX_SIZES_LARGEST],
         sizeof(xd->left_txfm_context_buffer));
}

// Disable array-bounds checks as the TX_SIZE enum contains values larger than
// TX_SIZES_ALL (TX_INVALID) which make extending the array as a workaround
// infeasible. The assert is enough for static analysis and this or other tools
// asan, valgrind would catch oob access at runtime.
#if defined(__GNUC__) && __GNUC__ >= 4
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
#pragma GCC diagnostic warning "-Warray-bounds"
#endif

static INLINE void set_txfm_ctx(TXFM_CONTEXT *txfm_ctx, uint8_t txs, int len) {
  int i;
  for (i = 0; i < len; ++i) txfm_ctx[i] = txs;
}

static INLINE void set_txfm_ctxs(TX_SIZE tx_size, int n4_w, int n4_h, int skip,
                                 const MACROBLOCKD *xd) {
  uint8_t bw = tx_size_wide[tx_size];
  uint8_t bh = tx_size_high[tx_size];

  if (skip) {
    bw = n4_w * MI_SIZE;
    bh = n4_h * MI_SIZE;
  }

  set_txfm_ctx(xd->above_txfm_context, bw, n4_w);
  set_txfm_ctx(xd->left_txfm_context, bh, n4_h);
}

static INLINE void txfm_partition_update(TXFM_CONTEXT *above_ctx,
                                         TXFM_CONTEXT *left_ctx,
                                         TX_SIZE tx_size, TX_SIZE txb_size) {
  BLOCK_SIZE bsize = txsize_to_bsize[txb_size];
  int bh = mi_size_high[bsize];
  int bw = mi_size_wide[bsize];
  uint8_t txw = tx_size_wide[tx_size];
  uint8_t txh = tx_size_high[tx_size];
  int i;
  for (i = 0; i < bh; ++i) left_ctx[i] = txh;
  for (i = 0; i < bw; ++i) above_ctx[i] = txw;
}

static INLINE TX_SIZE get_sqr_tx_size(int tx_dim) {
  switch (tx_dim) {
    case 128:
    case 64: return TX_64X64; break;
    case 32: return TX_32X32; break;
    case 16: return TX_16X16; break;
    case 8: return TX_8X8; break;
    default: return TX_4X4;
  }
}

static INLINE TX_SIZE get_tx_size(int width, int height) {
  if (width == height) {
    return get_sqr_tx_size(width);
  }
  if (width < height) {
    if (width + width == height) {
      switch (width) {
        case 4: return TX_4X8; break;
        case 8: return TX_8X16; break;
        case 16: return TX_16X32; break;
        case 32: return TX_32X64; break;
      }
    } else {
      switch (width) {
        case 4: return TX_4X16; break;
        case 8: return TX_8X32; break;
        case 16: return TX_16X64; break;
      }
    }
  } else {
    if (height + height == width) {
      switch (height) {
        case 4: return TX_8X4; break;
        case 8: return TX_16X8; break;
        case 16: return TX_32X16; break;
        case 32: return TX_64X32; break;
      }
    } else {
      switch (height) {
        case 4: return TX_16X4; break;
        case 8: return TX_32X8; break;
        case 16: return TX_64X16; break;
      }
    }
  }
  assert(0);
  return TX_4X4;
}

static INLINE int txfm_partition_context(const TXFM_CONTEXT *const above_ctx,
                                         const TXFM_CONTEXT *const left_ctx,
                                         BLOCK_SIZE bsize, TX_SIZE tx_size) {
  const uint8_t txw = tx_size_wide[tx_size];
  const uint8_t txh = tx_size_high[tx_size];
  const int above = *above_ctx < txw;
  const int left = *left_ctx < txh;
  int category = TXFM_PARTITION_CONTEXTS;

  // dummy return, not used by others.
  if (tx_size <= TX_4X4) return 0;

  TX_SIZE max_tx_size =
      get_sqr_tx_size(AOMMAX(block_size_wide[bsize], block_size_high[bsize]));

  if (max_tx_size >= TX_8X8) {
    category =
        (txsize_sqr_up_map[tx_size] != max_tx_size && max_tx_size > TX_8X8) +
        (TX_SIZES - 1 - max_tx_size) * 2;
  }
  assert(category != TXFM_PARTITION_CONTEXTS);
  return category * 3 + above + left;
}

// Compute the next partition in the direction of the sb_type stored in the mi
// array, starting with bsize.
static INLINE PARTITION_TYPE get_partition(const AV1_COMMON *const cm,
                                           int mi_row, int mi_col,
                                           BLOCK_SIZE bsize) {
  if (mi_row >= cm->mi_rows || mi_col >= cm->mi_cols) return PARTITION_INVALID;

  const int offset = mi_row * cm->mi_stride + mi_col;
  MB_MODE_INFO **mi = cm->mi_grid_base + offset;
  const BLOCK_SIZE subsize = mi[0]->sb_type;

  if (subsize == bsize) return PARTITION_NONE;

  const int bhigh = mi_size_high[bsize];
  const int bwide = mi_size_wide[bsize];
  const int sshigh = mi_size_high[subsize];
  const int sswide = mi_size_wide[subsize];

  if (bsize > BLOCK_8X8 && mi_row + bwide / 2 < cm->mi_rows &&
      mi_col + bhigh / 2 < cm->mi_cols) {
    // In this case, the block might be using an extended partition
    // type.
    const MB_MODE_INFO *const mbmi_right = mi[bwide / 2];
    const MB_MODE_INFO *const mbmi_below = mi[bhigh / 2 * cm->mi_stride];

    if (sswide == bwide) {
      // Smaller height but same width. Is PARTITION_HORZ_3 / PARTITION_HORZ_4,
      // PARTITION_HORZ or PARTITION_HORZ_B. To distinguish the latter two,
      // check if the lower half was split.
      if (sshigh * 4 == bhigh) {
#if CONFIG_3WAY_PARTITIONS
        return PARTITION_HORZ_3;
#else
        return PARTITION_HORZ_4;
#endif  // CONFIG_3WAY_PARTITIONS
      }
      assert(sshigh * 2 == bhigh);

      if (mbmi_below->sb_type == subsize)
        return PARTITION_HORZ;
      else
        return PARTITION_HORZ_B;
    } else if (sshigh == bhigh) {
      // Smaller width but same height. Is PARTITION_VERT_3 / PARTITION_VERT_4,
      // PARTITION_VERT or PARTITION_VERT_B. To distinguish the latter two,
      // check if the right half was split.
      if (sswide * 4 == bwide) {
#if CONFIG_3WAY_PARTITIONS
        return PARTITION_VERT_3;
#else
        return PARTITION_VERT_4;
#endif  // CONFIG_3WAY_PARTITIONS
      }
      assert(sswide * 2 == bhigh);

      if (mbmi_right->sb_type == subsize)
        return PARTITION_VERT;
      else
        return PARTITION_VERT_B;
    } else {
      // Smaller width and smaller height. Might be PARTITION_SPLIT or could be
      // PARTITION_HORZ_A or PARTITION_VERT_A. If subsize isn't halved in both
      // dimensions, we immediately know this is a split (which will recurse to
      // get to subsize). Otherwise look down and to the right. With
      // PARTITION_VERT_A, the right block will have height bhigh; with
      // PARTITION_HORZ_A, the lower block with have width bwide. Otherwise
      // it's PARTITION_SPLIT.
      if (sswide * 2 != bwide || sshigh * 2 != bhigh) return PARTITION_SPLIT;

      if (mi_size_wide[mbmi_below->sb_type] == bwide) return PARTITION_HORZ_A;
      if (mi_size_high[mbmi_right->sb_type] == bhigh) return PARTITION_VERT_A;

      return PARTITION_SPLIT;
    }
  }
  const int vert_split = sswide < bwide;
  const int horz_split = sshigh < bhigh;
  const int split_idx = (vert_split << 1) | horz_split;
  assert(split_idx != 0);

  static const PARTITION_TYPE base_partitions[4] = {
    PARTITION_INVALID, PARTITION_HORZ, PARTITION_VERT, PARTITION_SPLIT
  };

  return base_partitions[split_idx];
}

static INLINE void set_sb_size(SequenceHeader *const seq_params,
                               BLOCK_SIZE sb_size) {
  seq_params->sb_size = sb_size;
  seq_params->mib_size = mi_size_wide[seq_params->sb_size];
  seq_params->mib_size_log2 = mi_size_wide_log2[seq_params->sb_size];
}

// Returns true if the frame is fully lossless at the coded resolution.
// Note: If super-resolution is used, such a frame will still NOT be lossless at
// the upscaled resolution.
static INLINE int is_coded_lossless(const AV1_COMMON *cm,
                                    const MACROBLOCKD *xd) {
  int coded_lossless = 1;
  if (cm->seg.enabled) {
    for (int i = 0; i < MAX_SEGMENTS; ++i) {
      if (!xd->lossless[i]) {
        coded_lossless = 0;
        break;
      }
    }
  } else {
    coded_lossless = xd->lossless[0];
  }
  return coded_lossless;
}

static INLINE int is_valid_seq_level_idx(AV1_LEVEL seq_level_idx) {
  return seq_level_idx < SEQ_LEVELS || seq_level_idx == SEQ_LEVEL_MAX;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_ONYXC_INT_H_