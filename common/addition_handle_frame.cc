/*
 * Copyright (c) 2019, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include "av1/common/addition_handle_frame.h"

uint8_t *getYbuf(uint8_t *yPxl, int height, int width, int stride) {
  if (!yPxl) return nullptr;
  int buflen = height * width;
  unsigned char *buf = new unsigned char[buflen];  //定义一块一帧图像大小的buf
  unsigned char *p = buf;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      unsigned char uctemp = (unsigned char)(*(yPxl + x));
      *p = uctemp;
      p++;
    }
    yPxl += stride;  //一行的一个Y值对应下一行同样位置的增量
  }
  return buf;
}

/*Feed full frame image into the network*/
void addition_handle_frame(AV1_COMMON *cm) {
  YV12_BUFFER_CONFIG *pcPicYuvRec = &cm->cur_frame->buf;

  if (!cm->seq_params.use_highbitdepth) {
    uint8_t *py = pcPicYuvRec->y_buffer;
    uint8_t *bkuPy = py;

    int height = pcPicYuvRec->y_height;
    int width = pcPicYuvRec->y_width;
    int stride = pcPicYuvRec->y_stride;
    uint8_t **buf = new uint8_t *[height];
    for (int i = 0; i < height; i++) {
      buf[i] = new uint8_t[width];
    }
    if (cm->current_frame.order_hint % 16 == 0 &&
        cm->current_frame.order_hint != 0)
      buf = TF_Predict(py, height, width, stride, cm->base_qindex - 80,
                       cm->current_frame.frame_type);
    else
      buf = TF_Predict(py, height, width, stride, cm->base_qindex,
                       cm->current_frame.frame_type);
    /* buf = TF_Predict(py, height, width, stride,
     * cm->current_frame.frame_type==0?170:212);*/
    // buf = TF_Predict(py, height, width, stride, 2);

    // FILE *ff = fopen("D:/AOMedia/aom_org/test_result/cnn1_cnn2.yuv", "wb");
    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        *(bkuPy + j) = buf[i][j];  // Fill in the luma buffer again
                                   // fwrite(bkuPy + j, 1, 1, ff);
      }
      bkuPy += stride;
    }
    // fclose(ff);
  } else {
    uint16_t *py = CONVERT_TO_SHORTPTR(pcPicYuvRec->y_buffer);
    uint16_t *bkuPy = py;

    int height = pcPicYuvRec->y_height;
    int width = pcPicYuvRec->y_width;
    int stride = pcPicYuvRec->y_stride;

    uint16_t **buf = new uint16_t *[height];
    for (int i = 0; i < height; i++) {
      buf[i] = new uint16_t[width];
    }

    buf = TF_Predict_hbd(py, height, width, stride);

    // FILE *ff = fopen("end.yuv", "wb");
    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        *(bkuPy + j) = buf[i][j];  // Fill in the luma buffer again
        // fwrite(bkuPy + j, 1, 1, ff);
      }
      bkuPy += stride;
    }
  }
  finish_python();
}

void addition_handle_frameT(AV1_COMMON *cm, int frame_type) {
  YV12_BUFFER_CONFIG *pcPicYuvRec = &cm->cur_frame->buf;

  if (!cm->seq_params.use_highbitdepth) {
    uint8_t *py = pcPicYuvRec->y_buffer;
    uint8_t *bkuPy = py;

    int height = pcPicYuvRec->y_height;
    int width = pcPicYuvRec->y_width;
    int stride = pcPicYuvRec->y_stride;
    uint8_t **buf = new uint8_t *[height];
    for (int i = 0; i < height; i++) {
      buf[i] = new uint8_t[width];
    }

    buf = TF_Predict(py, height, width, stride, frame_type,
                     cm->current_frame.frame_type);
    /* buf = TF_Predict(py, height, width, stride,
     * cm->current_frame.frame_type==0?170:212);*/
    // buf = TF_Predict(py, height, width, stride, 2);

    // FILE *ff = fopen("D:/AOMedia/aom_org/test_result/cnn1_cnn2.yuv", "wb");
    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        *(bkuPy + j) = buf[i][j];  // Fill in the luma buffer again
                                   // fwrite(bkuPy + j, 1, 1, ff);
      }
      bkuPy += stride;
    }
    // fclose(ff);
  } else {
    uint16_t *py = CONVERT_TO_SHORTPTR(pcPicYuvRec->y_buffer);
    uint16_t *bkuPy = py;

    int height = pcPicYuvRec->y_height;
    int width = pcPicYuvRec->y_width;
    int stride = pcPicYuvRec->y_stride;

    uint16_t **buf = new uint16_t *[height];
    for (int i = 0; i < height; i++) {
      buf[i] = new uint16_t[width];
    }

    buf = TF_Predict_hbd(py, height, width, stride);

    // FILE *ff = fopen("end.yuv", "wb");
    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        *(bkuPy + j) = buf[i][j];  // Fill in the luma buffer again
        // fwrite(bkuPy + j, 1, 1, ff);
      }
      bkuPy += stride;
    }
  }
  finish_python();
}

/*Split into 1000x1000 blocks into the network*/
void addition_handle_blocks(AV1_COMMON *cm) {
  YV12_BUFFER_CONFIG *pcPicYuvRec = &cm->cur_frame->buf;

  int height = pcPicYuvRec->y_height;
  int width = pcPicYuvRec->y_width;
  int buf_width = 1000;
  int buf_height = 1000;
  int bufx_count = width / buf_width;
  int bufy_count = height / buf_height;
  int stride = pcPicYuvRec->y_stride;
  int cur_buf_width;
  int cur_buf_height;

  init_python();

  if (!cm->seq_params.use_highbitdepth) {
    uint8_t *py = pcPicYuvRec->y_buffer;
    uint8_t *bkuPy = py;
    uint8_t *bkuPyTemp = bkuPy;

    for (int y = 0; y < bufy_count + 1; y++) {
      for (int x = 0; x < bufx_count + 1; x++) {
        if (x == bufx_count)
          cur_buf_width = width - buf_width * bufx_count;
        else
          cur_buf_width = buf_width;
        if (y == bufy_count)
          cur_buf_height = height - buf_height * bufy_count;
        else
          cur_buf_height = buf_height;

        if (cur_buf_width != 0 && cur_buf_height != 0) {
          uint8_t **buf = new uint8_t *[cur_buf_height];
          for (int i = 0; i < cur_buf_height; i++) {
            buf[i] = new uint8_t[cur_buf_width];
          }

          TF_Predict_block(buf, py + x * buf_width + stride * buf_height * y,
                           cur_buf_height, cur_buf_width, stride,
                           cm->base_qindex);

          bkuPy = bkuPyTemp;
          // FILE *ff = fopen("end.yuv", "wb");
          for (int i = 0; i < cur_buf_height; i++) {
            for (int j = 0; j < cur_buf_width; j++) {
              *(bkuPy + j + x * buf_width + y * buf_height * stride) =
                  buf[i][j];  // Fill in the luma buffer again
              // fwrite(bkuPy + j, 1, 1, ff);
            }
            bkuPy += stride;
          }
          // fclose(ff);
        }
      }
    }
  } else {
    uint16_t *py = CONVERT_TO_SHORTPTR(pcPicYuvRec->y_buffer);
    uint16_t *bkuPy = py;
    uint16_t *bkuPyTemp = bkuPy;

    for (int y = 0; y < bufy_count + 1; y++) {
      for (int x = 0; x < bufx_count + 1; x++) {
        if (x == bufx_count)
          cur_buf_width = width - buf_width * bufx_count;
        else
          cur_buf_width = buf_width;
        if (y == bufy_count)
          cur_buf_height = height - buf_height * bufy_count;
        else
          cur_buf_height = buf_height;

        if (cur_buf_width != 0 && cur_buf_height != 0) {
          uint16_t **buf = new uint16_t *[cur_buf_height];
          for (int i = 0; i < cur_buf_height; i++) {
            buf[i] = new uint16_t[cur_buf_width];
          }

          buf =
              TF_Predict_block_hbd(py + x * buf_width + stride * buf_height * y,
                                   cur_buf_height, cur_buf_width, stride);

          bkuPy = bkuPyTemp;
          // FILE *ff = fopen("end.yuv", "wb");
          for (int i = 0; i < cur_buf_height; i++) {
            for (int j = 0; j < cur_buf_width; j++) {
              *(bkuPy + j + x * buf_width + y * buf_height * stride) =
                  buf[i][j];
              // fwrite(bkuPy + j, 1, 1, ff);
            }
            bkuPy += stride;
          }
          // fclose(ff);
        }
      }
    }
  }
  finish_python();
}

void addition_handle_blocks_rdo(YV12_BUFFER_CONFIG *source_frame,
                                AV1_COMMON *cm) {
  YV12_BUFFER_CONFIG *recon_buf = &cm->cur_frame->buf;
  YV12_BUFFER_CONFIG *source_buf = source_frame;
  int height = recon_buf->y_height;
  int width = recon_buf->y_width;
  int stride = recon_buf->y_stride;
  uint8_t *y_source_buf =
      getYbuf(source_buf->y_buffer, source_buf->y_crop_height,
              source_buf->y_width, source_buf->y_stride);
  // printf("%d,", y_source_buf[0]);
  uint8_t *y_unfilter_buf = getYbuf(recon_buf->y_buffer, height, width, stride);
  //char unfilter_file[256] =
  //    "D:\\konglingyi\\work\\av1\\AV1\\example\\guangyao_20181127\\test_"
  //    "result\\E19080601/unfilter.yuv";
  //char filter_file[256] =
  //    "D:\\konglingyi\\work\\av1\\AV1\\example\\guangyao_20181127\\test_"
  //    "result\\E19080601/filter.yuv";
  //save_frame_yuv(unfilter_file, recon_buf, cm->current_frame.order_hint);
  if (height != 0 && width != 0) {
    uint8_t **y_filter_buf = new uint8_t *[height];
    for (int i = 0; i < height; i++) {
      y_filter_buf[i] = new uint8_t[width];
    }

    int buf_width = 64;
    int buf_height = 64;
    int bufx_count = width / buf_width;
    int bufy_count = height / buf_height;

    int padding_size = 8;

    int cur_buf_width;
    int cur_buf_height;

    int cur_block_width;
    int cur_block_height;

    int start_read_x = 0, start_read_y = 0;
    int start_write_x = 0, start_write_y = 0;

    // init_python();

    // if (!cm->seq_params.use_highbitdepth) {
    uint8_t *py = recon_buf->y_buffer;
    uint8_t *bkuPy = py;
    uint8_t *bkuPyTemp = bkuPy;
    uint8_t *source = source_buf->y_buffer;
    for (int y = 0; y < bufy_count + 1; y++) {
      for (int x = 0; x < bufx_count + 1; x++) {
        if (x == bufx_count) {
          cur_block_width = width - buf_width * bufx_count;
          cur_buf_width = width - buf_width * bufx_count + padding_size;
          start_read_x = buf_width * bufx_count - padding_size;
          start_write_x = padding_size;
        } else if (x * buf_width < padding_size) {
          cur_block_width = buf_width;
          cur_buf_width = buf_width + padding_size;
          start_read_x = x * buf_width;
          start_write_x = x * buf_width;
        } else {
          cur_block_width = buf_width;
          cur_buf_width = buf_width + 2 * padding_size;
          start_read_x = x * buf_width - padding_size;
          start_write_x = padding_size;
        }

        if (y == bufy_count) {
          cur_block_height = height - buf_height * bufy_count;
          cur_buf_height = height - buf_height * bufy_count + padding_size;
          start_read_y = buf_height * bufy_count - padding_size;
          start_write_y = padding_size;
        } else if (y * buf_height < padding_size) {
          cur_block_height = buf_height;
          cur_buf_height = buf_height + padding_size;
          start_read_y = y * buf_height;
          start_write_y = y * buf_height;
        } else {
          cur_block_height = buf_height;
          cur_buf_height = buf_height + 2 * padding_size;
          start_read_y = y * buf_height - padding_size;
          start_write_y = padding_size;
        }

        if (cur_buf_width != 0 && cur_buf_height != 0) {
          uint8_t **buf = new uint8_t *[cur_buf_height];
          for (int i = 0; i < cur_buf_height; i++) {
            buf[i] = new uint8_t[cur_buf_width];
          }
          uint8_t **source_block_buf = new uint8_t *[cur_block_height];
          for (int i = 0; i < cur_block_height; i++) {
            source_block_buf[i] = new uint8_t[cur_block_width];
          }
          uint8_t **unfilter_block_buf = new uint8_t *[cur_block_height];
          for (int i = 0; i < cur_block_height; i++) {
            unfilter_block_buf[i] = new uint8_t[cur_block_width];
          }
          uint8_t **filter_block_buf = new uint8_t *[cur_block_height];
          for (int i = 0; i < cur_block_height; i++) {
            filter_block_buf[i] = new uint8_t[cur_block_width];
          }
          for (int i = 0; i < cur_block_height; i++) {
            for (int j = 0; j < cur_block_width; j++) {
              source_block_buf[i][j] =
                  y_source_buf[(i + y * buf_height) * width + j +
                               x * buf_width];
              // *(source + (i + y * buf_height) * stride + j + x * buf_width);
            }
          }
          for (int i = 0; i < cur_block_height; i++) {
            for (int j = 0; j < cur_block_width; j++) {
              unfilter_block_buf[i][j] =
                  y_unfilter_buf[(i + y * buf_height) * width + j +
                                 x * buf_width];
            }
          }
          if (cm->current_frame.order_hint % 16 == 0 &&
              cm->current_frame.order_hint != 0 && cm->base_qindex == 212)
            buf =
                TF_Predict(py + start_read_x + stride * start_read_y,
                           cur_buf_height, cur_buf_width, stride,
                           cm->base_qindex - 80, cm->current_frame.frame_type);
          else {
            buf = TF_Predict(py + start_read_x + stride * start_read_y,
                             cur_buf_height, cur_buf_width, stride,
                             cm->base_qindex, cm->current_frame.frame_type);
          }
          for (int i = 0; i < cur_block_height; i++) {
            for (int j = 0; j < cur_block_width; j++) {
              filter_block_buf[i][j] =
                  buf[i + start_write_y][j + start_write_x];
            }
          }
          /*  char source_block_file[128];
          sprintf(source_block_file,
                  "D:\\AOMedia\\aom_org\\test_result\\E19072501\\block_"
                  "yuv\\source_block_%d_%d_%dx%d.yuv",
                  x, y, cur_block_width, cur_block_height);
          save_buf_to_yuv(source_block_buf, cur_block_height, cur_block_width,
                          source_block_file);
          char unfilter_block_file[128];
          sprintf(unfilter_block_file,
                  "D:\\AOMedia\\aom_org\\test_result\\E19072501\\block_"
                  "yuv\\unfilter_block_%"
                  "d_%d_%dx%d.yuv",
                  x, y, cur_block_width, cur_block_height);
          save_buf_to_yuv(unfilter_block_buf, cur_block_height, cur_block_width,
                          unfilter_block_file);
          char filter_block_file[128];
          sprintf(filter_block_file,
                  "D:\\AOMedia\\aom_org\\test_result\\E19072501\\block_"
                  "yuv\\filter_block_%"
                  "d_%d_%dx%d.yuv",
                  x, y, cur_block_width, cur_block_height);
          save_buf_to_yuv(filter_block_buf, cur_block_height, cur_block_width,
                          filter_block_file);*/
          double unfilter_MSE = 0, filter_MSE = 0;
          unfilter_MSE = computeMSE(source_block_buf, unfilter_block_buf,
                                    cur_block_height, cur_block_width);
          filter_MSE = computeMSE(source_block_buf, filter_block_buf,
                                  cur_block_height, cur_block_width);
          for (int i = 0; i < cur_block_height; i++) {
            for (int j = 0; j < cur_block_width; j++) {
              y_filter_buf[y * buf_height + i][x * buf_width + j] =
                  // filter_block_buf[i][j];
                  (unfilter_MSE <= filter_MSE) ? unfilter_block_buf[i][j]
                                               : filter_block_buf[i][j];
            }
          }
        }
      }
    }
    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        *(bkuPy + j) = y_filter_buf[i][j];  // fill in the luma buffer again
      }
      bkuPy += stride;
    }
  }
  //save_frame_yuv(filter_file, recon_buf, cm->current_frame.order_hint);
}

void addition_handle_blocks_adp(AV1_COMMON *cm) {
  YV12_BUFFER_CONFIG *recon_buf = &cm->cur_frame->buf;
  YV12_BUFFER_CONFIG *pred_buf = &cm->cur_frame->pred_buf;//onyxc_int.h
  int height = recon_buf->y_height;
  int width = recon_buf->y_width;
  int stride = recon_buf->y_stride;


  if (height != 0 && width != 0) {
    int16_t **y_resi_buf = new int16_t *[height];
    for (int i = 0; i < height; i++) {
      y_resi_buf[i] = new int16_t[width];
      for (int j = 0; j < width; j++) {
        y_resi_buf[i][j] = *(recon_buf->y_buffer + j + i * stride) -
                           *(pred_buf->y_buffer + j + i * stride);
        // printf("\ nresi[%d][%d]:%d",i,j, y_resi_buf[i][j]);
      }
    }

    uint8_t **y_filter_buf = new uint8_t *[height];
    for (int i = 0; i < height; i++) {
      y_filter_buf[i] = new uint8_t[width];
    }

    //int buf_width = 64;
    //int buf_height = 64;
	int buf_width = 128;
	int buf_height = 128;
    int bufx_count = width / buf_width;
    int bufy_count = height / buf_height;

    int padding_size = 8;

    int cur_buf_width;
    int cur_buf_height;

    int cur_block_width;
    int cur_block_height;

    int start_read_x = 0, start_read_y = 0;
    int start_write_x = 0, start_write_y = 0;

    // if (!cm->seq_params.use_highbitdepth) {
    uint8_t *py = recon_buf->y_buffer;
    uint8_t *bkuPy = py;
    uint8_t *bkuPyTemp = bkuPy;
    for (int y = 0; y < bufy_count + 1; y++) {
      for (int x = 0; x < bufx_count + 1; x++) {
        if (x == bufx_count) {
          cur_block_width = width - buf_width * bufx_count;
          cur_buf_width = width - buf_width * bufx_count + padding_size;
          start_read_x = buf_width * bufx_count - padding_size;
          start_write_x = padding_size;
        } else if (x * buf_width < padding_size) {
          cur_block_width = buf_width;
          cur_buf_width = buf_width + padding_size;
          start_read_x = x * buf_width;
          start_write_x = x * buf_width;
        } else {
          cur_block_width = buf_width;
          cur_buf_width = buf_width + 2 * padding_size;
          start_read_x = x * buf_width - padding_size;
          start_write_x = padding_size;
        }

        if (y == bufy_count) {
          cur_block_height = height - buf_height * bufy_count;
          cur_buf_height = height - buf_height * bufy_count + padding_size;
          start_read_y = buf_height * bufy_count - padding_size;
          start_write_y = padding_size;
        } else if (y * buf_height < padding_size) {
          cur_block_height = buf_height;
          cur_buf_height = buf_height + padding_size;
          start_read_y = y * buf_height;
          start_write_y = y * buf_height;
        } else {
          cur_block_height = buf_height;
          cur_buf_height = buf_height + 2 * padding_size;
          start_read_y = y * buf_height - padding_size;
          start_write_y = padding_size;
        }

        if (cur_buf_width != 0 && cur_buf_height != 0) {
          uint8_t **buf = new uint8_t *[cur_buf_height];
          for (int i = 0; i < cur_buf_height; i++) {
            buf[i] = new uint8_t[cur_buf_width];
          }
          double cur_block_SAD = 0, cur_block_MAD = 0;
          for (int i = 0; i < cur_block_height; i++) {
            for (int j = 0; j < cur_block_width; j++) {
               //printf("\ nresi[%d][%d]:%d", i + y * buf_height,
               //       j + x * buf_width,
               //       y_resi_buf[i + y * buf_height][j + x * buf_width]);
              cur_block_SAD +=
                  abs(y_resi_buf[i + y * buf_height][j + x * buf_width]);
            }
          }
          cur_block_MAD = cur_block_SAD / cur_block_height / cur_block_width;
            //printf("\n cur_block_SAD = %lf, cur_block_MAD = %lf",
            // cur_block_SAD, cur_block_MAD);

          uint8_t **filter_block_buf = new uint8_t *[cur_block_height];
          for (int i = 0; i < cur_block_height; i++) {
            filter_block_buf[i] = new uint8_t[cur_block_width];
          }
          if (cur_block_MAD > 1) {
            printf("\n x:%d,y:%d", x, y);
            if (cm->current_frame.order_hint % 16 == 0 &&
                cm->current_frame.order_hint != 0 && cm->base_qindex == 212)
              buf = TF_Predict(py + start_read_x + stride * start_read_y,
                               cur_buf_height, cur_buf_width, stride,
                               cm->base_qindex - 80,
                               cm->current_frame.frame_type);
            else {
              buf = TF_Predict(py + start_read_x + stride * start_read_y,
                               cur_buf_height, cur_buf_width, stride,
                               cm->base_qindex, cm->current_frame.frame_type);
            }
            for (int i = 0; i < cur_block_height; i++) {
              for (int j = 0; j < cur_block_width; j++) {
                *(recon_buf->y_buffer + (i + y * buf_height) * stride + j +
                  x * buf_width) = buf[i + start_write_y][j + start_write_x];
              }
            }
          }

         
        }
      }
    }
    // for (int i = 0; i < height; i++) {
    //  for (int j = 0; j < width; j++) {
    //    *(bkuPy + j) = y_filter_buf[i][j];  // fill in the luma buffer again
    //  }

    //  bkuPy += stride;
    //}
  }
  //save_frame_yuv(filter_file, recon_buf, cm->current_frame.order_hint);
}

void addition_handle_blocks_rdo_frame(YV12_BUFFER_CONFIG *source_frame,
                                      AV1_COMMON *cm) {
  YV12_BUFFER_CONFIG *recon_buf = &cm->cur_frame->buf;
  YV12_BUFFER_CONFIG *source_buf = source_frame;
  int height = recon_buf->y_height;
  int width = recon_buf->y_width;
  int stride = recon_buf->y_stride;
  uint8_t *y_source_buf =
      getYbuf(source_buf->y_buffer, source_buf->y_crop_height,
              source_buf->y_width, source_buf->y_stride);
  // printf("%d,", y_source_buf[0]);
  uint8_t *y_unfilter_buf = getYbuf(recon_buf->y_buffer, height, width, stride);
  //char unfilter_file[256] =
  //    "D:/AOMedia/aom_org/test_result/E19072501/unfilter.yuv";
  //char filter_file[256] = "D:/AOMedia/aom_org/test_result/E19072501/filter.yuv";
  //save_frame_yuv(unfilter_file, recon_buf, cm->current_frame.order_hint);
  if (height != 0 && width != 0) {
    uint8_t **y_filter_buf = new uint8_t *[height];
    for (int i = 0; i < height; i++) {
      y_filter_buf[i] = new uint8_t[width];
    }
    y_filter_buf = TF_Predict(recon_buf->y_buffer, height, width, stride, 1,
                              cm->current_frame.frame_type);
    //int buf_width = 64;
    //int buf_height = 64;
	int buf_width = 128;
	int buf_height = 128;
    int bufx_count = width / buf_width;
    int bufy_count = height / buf_height;

    int padding_size = 8;

    int cur_buf_width;
    int cur_buf_height;

    int cur_block_width;
    int cur_block_height;

    int start_read_x = 0, start_read_y = 0;
    int start_write_x = 0, start_write_y = 0;

    // init_python();

    // if (!cm->seq_params.use_highbitdepth) {
    uint8_t *py = recon_buf->y_buffer;
    uint8_t *bkuPy = py;
    uint8_t *bkuPyTemp = bkuPy;
    uint8_t *source = source_buf->y_buffer;
    for (int y = 0; y < bufy_count + 1; y++) {
      for (int x = 0; x < bufx_count + 1; x++) {
        if (x == bufx_count) {
          cur_block_width = width - buf_width * bufx_count;
          cur_buf_width = width - buf_width * bufx_count + padding_size;
          start_read_x = buf_width * bufx_count - padding_size;
          start_write_x = padding_size;
        } else if (x * buf_width < padding_size) {
          cur_block_width = buf_width;
          cur_buf_width = buf_width + padding_size;
          start_read_x = x * buf_width;
          start_write_x = x * buf_width;
        } else {
          cur_block_width = buf_width;
          cur_buf_width = buf_width + 2 * padding_size;
          start_read_x = x * buf_width - padding_size;
          start_write_x = padding_size;
        }

        if (y == bufy_count) {
          cur_block_height = height - buf_height * bufy_count;
          cur_buf_height = height - buf_height * bufy_count + padding_size;
          start_read_y = buf_height * bufy_count - padding_size;
          start_write_y = padding_size;
        } else if (y * buf_height < padding_size) {
          cur_block_height = buf_height;
          cur_buf_height = buf_height + padding_size;
          start_read_y = y * buf_height;
          start_write_y = y * buf_height;
        } else {
          cur_block_height = buf_height;
          cur_buf_height = buf_height + 2 * padding_size;
          start_read_y = y * buf_height - padding_size;
          start_write_y = padding_size;
        }

        if (cur_buf_width != 0 && cur_buf_height != 0) {
          uint8_t **buf = new uint8_t *[cur_buf_height];
          for (int i = 0; i < cur_buf_height; i++) {
            buf[i] = new uint8_t[cur_buf_width];
          }
          uint8_t **source_block_buf = new uint8_t *[cur_block_height];
          for (int i = 0; i < cur_block_height; i++) {
            source_block_buf[i] = new uint8_t[cur_block_width];
          }
          uint8_t **unfilter_block_buf = new uint8_t *[cur_block_height];
          for (int i = 0; i < cur_block_height; i++) {
            unfilter_block_buf[i] = new uint8_t[cur_block_width];
          }
          uint8_t **filter_block_buf = new uint8_t *[cur_block_height];
          for (int i = 0; i < cur_block_height; i++) {
            filter_block_buf[i] = new uint8_t[cur_block_width];
          }
          for (int i = 0; i < cur_block_height; i++) {
            for (int j = 0; j < cur_block_width; j++) {
              source_block_buf[i][j] =
                  y_source_buf[(i + y * buf_height) * width + j +
                               x * buf_width];
              // *(source + (i + y * buf_height) * stride + j + x * buf_width);
            }
          }
          for (int i = 0; i < cur_block_height; i++) {
            for (int j = 0; j < cur_block_width; j++) {
              unfilter_block_buf[i][j] =
                  y_unfilter_buf[(i + y * buf_height) * width + j +
                                 x * buf_width];
            }
          }

          /*  buf = TF_Predict(py + start_read_x + stride * start_read_y,
                             cur_buf_height, cur_buf_width, stride, 2);*/
          for (int i = 0; i < cur_block_height; i++) {
            for (int j = 0; j < cur_block_width; j++) {
              filter_block_buf[i][j] =
                  y_filter_buf[i + y * buf_height][j + x * buf_width];
            }
          }
          char source_block_file[128];
          sprintf(source_block_file,
                  "D:\\AOMedia\\aom_org\\test_result\\E19072501\\block_"
                  "yuv\\source_block_%d_%d_%dx%d.yuv",
                  x, y, cur_block_width, cur_block_height);
          //save_buf_to_yuv(source_block_buf, cur_block_height, cur_block_width,
          //                source_block_file);
          char unfilter_block_file[128];
          sprintf(unfilter_block_file,
                  "D:\\AOMedia\\aom_org\\test_result\\E19072501\\block_"
                  "yuv\\unfilter_block_%"
                  "d_%d_%dx%d.yuv",
                  x, y, cur_block_width, cur_block_height);
          //save_buf_to_yuv(unfilter_block_buf, cur_block_height, cur_block_width,
          //                unfilter_block_file);
          char filter_block_file[128];
          sprintf(filter_block_file,
                  "D:\\AOMedia\\aom_org\\test_result\\E19072501\\block_"
                  "yuv\\filter_block_%"
                  "d_%d_%dx%d.yuv",
                  x, y, cur_block_width, cur_block_height);
          //save_buf_to_yuv(filter_block_buf, cur_block_height, cur_block_width,
          //                filter_block_file);
          double unfilter_MSE = 0, filter_MSE = 0;
          unfilter_MSE = computeMSE(source_block_buf, unfilter_block_buf,
                                    cur_block_height, cur_block_width);
          filter_MSE = computeMSE(source_block_buf, filter_block_buf,
                                  cur_block_height, cur_block_width);
          for (int i = 0; i < cur_block_height; i++) {
            for (int j = 0; j < cur_block_width; j++) {
              y_filter_buf[y * buf_height + i][x * buf_width + j] =
                  // filter_block_buf[i][j];
                  (unfilter_MSE <= filter_MSE) ? unfilter_block_buf[i][j]
                                               : filter_block_buf[i][j];
            }
          }
        }
      }
    }
    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        *(bkuPy + j) = y_filter_buf[i][j];  // fill in the luma buffer again
      }

      bkuPy += stride;
    }
  }
  //save_frame_yuv(filter_file, recon_buf, cm->current_frame.order_hint);
}

void addition_handle_frame_rdo(YV12_BUFFER_CONFIG *source_frame,
                               AV1_COMMON *cm) {
  YV12_BUFFER_CONFIG *recon_buf = &cm->cur_frame->buf;
  YV12_BUFFER_CONFIG *source_buf = source_frame;
  int height = recon_buf->y_height;
  int width = recon_buf->y_width;
  int stride = recon_buf->y_stride;

  // printf("%d,", y_source_buf[0]);
  uint8_t **y_unfilter_buf = new uint8_t *[height];
  for (int i = 0; i < height; i++) {
    y_unfilter_buf[i] = new uint8_t[width];
  }
  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      y_unfilter_buf[i][j] = *(recon_buf->y_buffer + i * stride + j);
    }
  }
  uint8_t **y_source_buf = new uint8_t *[height];
  for (int i = 0; i < height; i++) {
    y_source_buf[i] = new uint8_t[width];
  }
  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      y_source_buf[i][j] =
          *(source_buf->y_buffer + i * source_buf->y_stride + j);
    }
  }
  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      y_unfilter_buf[i][j] = *(recon_buf->y_buffer + i * stride + j);
    }
  }
  /* char unfilter_block_file[128];
   sprintf(unfilter_block_file,
           "D:\\AOMedia\\aom_org\\test_result\\E19072501\\unfilter_block.yuv");
   save_buf_to_yuv(y_unfilter_buf, height, width, unfilter_block_file);
   char source_block_file[128];
   sprintf(source_block_file,
           "D:\\AOMedia\\aom_org\\test_result\\E19072501\\source_block.yuv");
   save_buf_to_yuv(y_source_buf, height, width, source_block_file);
   char unfilter_file[256] =
       "D:/AOMedia/aom_org/test_result/E19072501/unfilter.yuv";
   char filter_file[256] =
   "D:/AOMedia/aom_org/test_result/E19072501/filter.yuv";
   save_frame_yuv(unfilter_file, recon_buf, cm->current_frame.order_hint);*/
  if (height != 0 && width != 0) {
    double unfilter_MSE = 0, filter_MSE = 0;
    uint8_t **y_filter_buf = new uint8_t *[height];
    for (int i = 0; i < height; i++) {
      y_filter_buf[i] = new uint8_t[width];
    }
    y_filter_buf = TF_Predict(recon_buf->y_buffer, height, width, stride, cm->base_qindex,
                              cm->current_frame.frame_type);
    unfilter_MSE = computeMSE(y_source_buf, y_unfilter_buf, height, width);
    filter_MSE = computeMSE(y_source_buf, y_filter_buf, height, width);

    printf("\nnorder_hint:%d,  unfilter_PSNR:%f,filter_PSNR:%f,cnn:%d\n",
           cm->current_frame.order_hint, unfilter_MSE, filter_MSE,
           unfilter_MSE > filter_MSE);
    // printf("\norder_hint:%d,   unfilter_PSNR:%f",
    // cm->current_frame.order_hint,unfilter_MSE );
    if (unfilter_MSE > filter_MSE) {
      for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
          *(recon_buf->y_buffer + i * stride + j) = y_filter_buf[i][j];
        }
      }
    }
  }
  // save_frame_yuv(filter_file, recon_buf, cm->current_frame.order_hint);
}
/*frame_type determines what kind of network blocks need to be fed into, not
 * exactly equivalent to what frame the current frame is.*/
/*Low bitdepth*/
uint8_t **blocks_to_cnn_secondly(uint8_t *pBuffer_y, int height, int width,
                                 int stride, FRAME_TYPE frame_type) {
  uint8_t **dst = new uint8_t *[height];
  for (int i = 0; i < height; i++) {
    dst[i] = new uint8_t[width];
  }

  if (frame_type == FRAME_TYPES) {
    for (int r = 0; r < height; ++r) {
      for (int c = 0; c < width; ++c) {
        dst[r][c] = (uint8_t)(*(pBuffer_y + c));
      }
      pBuffer_y += stride;
    }
    return dst;
  } else if (frame_type != FRAME_TYPES) {
    int buf_width = 1000;
    int buf_height = 1000;
    int bufx_count = width / buf_width;
    int bufy_count = height / buf_height; /**/
    int cur_buf_width;
    int cur_buf_height;

    for (int y = 0; y < bufy_count + 1; y++) {
      for (int x = 0; x < bufx_count + 1; x++) {
        if (x == bufx_count)
          cur_buf_width = width - buf_width * bufx_count;
        else
          cur_buf_width = buf_width;
        if (y == bufy_count)
          cur_buf_height = height - buf_height * bufy_count;
        else
          cur_buf_height = buf_height;

        if (cur_buf_width != 0 && cur_buf_height != 0) {
          uint8_t **buf = new uint8_t *[cur_buf_height];
          for (int i = 0; i < cur_buf_height; i++) {
            buf[i] = new uint8_t[cur_buf_width];
          }
          TF_Predict_block(buf,
                           pBuffer_y + x * buf_width + stride * buf_height * y,
                           cur_buf_height, cur_buf_width, stride, frame_type);

          for (int i = 0; i < cur_buf_height; i++) {
            for (int j = 0; j < cur_buf_width; j++) {
              dst[y * buf_height + i][x * buf_width + j] = buf[i][j];
            }
          }

          for (int i = 0; i < cur_buf_height; i++) {
            delete[] buf[i];
          }
          delete[] buf;
        }
      }
    }
  }
  /*
  FILE *ff = fopen("end.yuv", "wb");
  for (int i = 0; i < height; i++) {
          for (int j = 0; j < width; j++) {
                  fwrite(*(dst+i)+j, 1, 1, ff);
          }
  }
  fclose(ff);
  */
  return dst;
}

void save_frame_yuv(char *file_name, YV12_BUFFER_CONFIG *buf,
                    int frame_number) {
  FILE *f_out = NULL;
  if (frame_number == 0) {
    if ((f_out = fopen(file_name, "wb")) == NULL) {
      printf("Unable to open file %s to write.\n", file_name);
      return;
    }
  } else {
    if ((f_out = fopen(file_name, "ab")) == NULL) {
      printf("Unable to open file %s to append.\n", file_name);
      return;
    }
  }
  int h;
  for (h = 0; h < buf->y_height; ++h) {
    fwrite(&buf->y_buffer[h * buf->y_stride], 1, buf->y_width, f_out);
  }
  // --- U ---
  for (h = 0; h < buf->uv_height; ++h) {
    fwrite(&buf->u_buffer[h * buf->uv_stride], 1, buf->uv_width, f_out);
  }
  // --- V ---
  for (h = 0; h < buf->uv_height; ++h) {
    fwrite(&buf->v_buffer[h * buf->uv_stride], 1, buf->uv_width, f_out);
  }
  fclose(f_out);
}

double computeMSE(uint8_t **data1, uint8_t **data2, int height, int width) {
  double MSE = 0;
  // printf("data1:%d,data2:%d", data1[0][0], data2[0][0]);
  for (int i = 0; i < height; i++)
    for (int j = 0; j < width; j++) {
      MSE += pow((double)(data1[i][j] - data2[i][j]), 2);
    }
  // return (10 * log10(255.0 * 255.0 / (MSE / (height * width))));
  return MSE / (height * width);
}

void save_buf_to_yuv(uint8_t **buf, int height, int width, char *file_name) {
  FILE *f_out = NULL;
  if ((f_out = fopen(file_name, "wb")) == NULL) {
    printf("Unable to open file %s to write.\n", file_name);
    return;
  }
  for (int h = 0; h < height; ++h) {
    for (int w = 0; w < width; ++w)
      fwrite(&buf[h][w], sizeof(uint8_t), 1, f_out);
  }
  fclose(f_out);
}