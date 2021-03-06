/* Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "fpga/common/pe.h"
#include "common/enforce.h"
#include "common/types.h"
#include "fpga/V2/filter.h"
#include "fpga/V2/image.h"
#include "fpga/common/config.h"
#include "fpga/common/driver.h"
#include "fpga/common/fpga_common.h"
#ifdef COST_TIME_PRINT
#include <sys/time.h>
#include <time.h>
#include <iomanip>
#include <iostream>
#endif

namespace paddle_mobile {
namespace fpga {

using namespace driver;  // NOLINT
using namespace std;     // NOLINT
#define USE_RELU 1
#define USE_BIAS 2

// bypass cmd
#define CMD_FP16_TO_FP16 0
#define CMD_FP16_TO_FP32 1
#define CMD_FP32_TO_FP16 2
#define CMD_FP32_TO_FP32 3
#define CMD_INT8_TO_FP16 4

// bypass macro
#define SIZE_FP16 2
#define SIZE_FP32 4
#define SIZE_INT8 1

#define PE_IRQ_TIMEOUT 1000000

/* Interrupt bit-set offset*/
#define INTERRUPT_RSVD 0x0001
#define INTERRUPT_BYPASS 0x0002
#define INTERRUPT_CONV 0x0004
#define INTERRUPT_POOLING 0x0008
#define INTERRUPT_EW 0x0010

/* Register offset */
#define REG_INTERRUPT 0x000
#define REG_VERSION 0x008
#define REG_TEMPERATURE 0x010
#define REG_FPGA_RESET 0x018
#define REG_TEST_REGISTER 0x048
#define REG_HARDWARE_STATUS 0x050

#define REG_TIMER_COUNTER 0x070

#define REG_SCALE_PARAMETER 0x080
#define REG_ACTIVATION_MODE_AND_LEAKY_RELU_FACTOR 0x090

#define REG_FLASH_CMD 0x200
#define REG_FLASH_DATA 0x208
#define REG_FLASH_CONFIG 0x210
#define REG_FLASH_STATUS 0x218
#define REG_SN 0x220

/*bypass*/
#define REG_CONVERT_CMD 0x400
#define REG_CONVERT_SRC_ADDR 0x408
#define REG_CONVERT_DST_ADDR 0x410
#define REG_CONVERT_RD_LENGTH 0x418
#define REG_CONVERT_WR_LENGTH 0x420

/*resize*/
#define REG_RESIZE_CMD 0x600
#define REG_RESIZE_CHANNEL_NUMBER 0x608
#define REG_RESIZE_INPUT_IMAGE_PIXEL 0x610
#define REG_RESIZE_OUTPUT_IMAGE_PIXEL 0x618
#define REG_RESIZE_INPUT_BASE_ADDR 0x620
#define REG_RESIZE_WEIGHT_BASE_ADDR 0x628
#define REG_RESIZE_SRC_POS_BASE_ADDR 0x630
#define REG_RESIZE_OUTPUT_BASE_ADDR 0x638

/*pooling*/
#define REG_POOLING_CMD 0x800
#define REG_POOLING_IMAGE_BASE_ADDR 0x808
#define REG_POOLING_RESULT_BASE_ADDR 0x810
#define REG_POOLING_IMAGE_PIXEL 0x818
#define REG_POOLING_WINDOW_SIZE 0x820
#define REG_POOLING_RESULT_PIXEL 0x828
#define REG_POOLING_PAD_PIXEL 0x830
#define REG_POOLING_STEP_PIXEL 0x838
#define REG_POOLING_CHANNEL_NUMBER 0x840
#define REG_POOLING_IMAGE_AMOUNT_PER_ROW 0x848
#define REG_POOLING_IMAGE_ONE_PAD_PER_ROW 0x850
#define REG_POOLING_IMAGE_TWO_PAD_PER_ROW 0x858
#define REG_POOLING_IMAGE_ROW_MUL_WINDOW_HEIGHT 0x860
#define REG_POOLING_IMAGE_ROW_MUL_PAD_HEIGHT 0x868
#define REG_POOLING_IMAGE_ROW_MUL_STEP_HEIGHT 0x870
#define REG_POOLING_RESULT_AMOUNT_ALIGN_32 0x878
#define REG_POOLING_RESULT_AMOUNT_ALIGN_16 0x880
#define REG_POOLING_IMAGE_CALCU_HEIGHT 0x888
#define REG_POOLING_IMAGE_PADLEFT_SKIPWINDOW 0x898
#define REG_POOLING_MODE_RECIPROCAL 0x890

/*conv*/
#define REG_CONV_CMD 0xC00
#define REG_CONV_REG0 0xC08
#define REG_CONV_REG1 0xC10
#define REG_CONV_REG2 0xC18
#define REG_CONV_REG3 0xC20
#define REG_CONV_REG4 0xC28
#define REG_CONV_REG5 0xC30
#define REG_CONV_REG6 0xC38
#define REG_CONV_REG7 0xC40
#define REG_CONV_REG8 0xC48
#define REG_CONV_REG9 0xC50
#define REG_CONV_REG10 0xC58
#define REG_CONV_REG11 0xC60

#define REG_CONV_IMAGE_BASE_ADDR 0xC08
#define REG_CONV_FILTER_BASE_ADDR 0xC10
#define REG_CONV_SB_BASE_ADDR 0xC18
#define REG_CONV_RESULT_BASE_ADDR 0xC20
#define REG_CONV_IMAGE_PIXEL 0xC28
#define REG_CONV_FILTER_PIXEL 0xC30
#define REG_CONV_RESULT_PIXEL 0xC38
#define REG_CONV_PAD_PIXEL 0xC40
#define REG_CONV_STEP_PIXEL 0xC48
#define REG_CONV_GROUP_NUMBER 0xC50
#define REG_CONV_FILTER_NUMBER 0xC58
#define REG_CONV_CHANNEL_NUMBER 0xC60
#define REG_CONV_FILTER_PER_GROUP 0xC68
#define REG_CONV_CHANNEL_PER_GROUP 0xC70
#define REG_CONV_IMAGE_AMOUNT_PER_ROW 0xC78
#define REG_CONV_IMAGE_ONE_PAD_PER_ROW 0xC80
#define REG_CONV_IMAGE_TWO_PAD_PER_ROW 0xC88
#define REG_CONV_FILTER_AMOUNT_ALL 0xC90
#define REG_CONV_RESULT_AMOUNT_PER_ROW 0xC98
#define REG_CONV_RESULT_LAST_VALID 0xCA0

#define REG_CONV_BLOCK_AMOUNT_PER_ROW 0xCA8
#define REG_CONV_FILTER_PAD_WIDTH_MUL_CH 0xCB0
#define REG_CONV_IMAGE_AMOUNT_PER_ROW_MUL_WIN_F 0xCB8
#define REG_CONV_IMAGE_AMOUNT_PER_ROW_MUL_WIN 0xCC0
#define REG_CONV_IMAGE_BLOCK_NUM 0xCC8
#define REG_CONV_IMAGE_BLOCK_LEN 0xCD0
#define REG_CONV_IMAGE_BLOCK_LEN_LAST 0xCD8
#define REG_CONV_IMAGE_WIN_CNT 0xCE0
#define REG_CONV_IMAGE_WIN_CNT_LAST 0xCE8
#define REG_CONV_RES_ROW_DATA_ALIGN4_PAD 0xCF8
#define REG_CONV_PROG_FULL_CNT 0xD08
#define REG_CONV_POST_PROG_FULL_CNT 0xD10
#define REG_CONV_FPGA_BIAS_SCALE_LEN 0xD20

#define REG_CONV_IMAGE_SCALE 0xD28
#define REG_CONV_FILTER_SCALE 0xD30

/*ew*/
#define REG_EW_CMD 0x0F00
#define REG_EW_IMAGE0_BASE_ADDR 0x0F08
#define REG_EW_IMAGE1_BASE_ADDR 0x0F10
#define REG_EW_RESULT_BASE_ADDR 0x0F18
#define REG_EW_DATA_LEN 0x0F20
#define REG_EW_COEFFICIENT 0x0F28
#define REG_EW_IMAGE_PIXEL 0x0F30
#define REG_EW_IMAGE_AMOUNT_PER_ROW 0x0F38

/*dwconv*/
#define REG_DWCONV_FILTER_BASE_ADDR 0xe08
#define REG_DWCONV_FILTER_SHAPE 0xe10
#define REG_DWCONV_FILTER_N_ALIGN 0xe18
#define REG_DWCONV_FILTER_SUBNUMBER 0xe20
#define REG_DWCONV_CMD 0xe00

int ComputeFpgaConv(const struct SplitConvArgs &args) {
//  ComputeBasicConv(args.conv_arg[0]);
#ifdef FPGA_PRINT_MODE
  DLOG << "=============ComputeFPGAConv===========";
  DLOG << "   filter_num:" << args.filter_num
       << "   group_num:" << args.group_num
       << "   split_num:" << args.split_num;
#endif
  int ret = 0;
  int split_num = args.split_num;
  for (int i = 0; i < split_num; i++) {
    ret |= ComputeBasicConv(args.conv_arg[i]);
  }

  if (split_num > 1) {
    ComputeFPGAConcat(args.concat_arg);
  }

  return ret;
}

int ComputeBasicConv(const struct ConvArgs &args) {
#ifdef FPGA_PRINT_MODE
  DLOG << "======Compute Basic Conv======";
  DLOG << "   relu_enabled:" << args.relu_enabled;
  DLOG << "   sb_address:" << args.sb_address
       << "   filter_address:" << args.filter_address
       << "   filter_num:" << args.filter_num
       << "   group_num:" << args.group_num;
  DLOG << "   image_address:" << args.image.address
       << "   image_scale_address:" << args.image.scale_address
       << "   image_channels:" << args.image.channels
       << "   image_height:" << args.image.height
       << "   image_width:" << args.image.width
       << "   pad_height:" << args.image.pad_height
       << "   pad_width:" << args.image.pad_width;
  DLOG << "   kernel_height:" << args.kernel.height
       << "   kernel_width:" << args.kernel.width
       << "   stride_h:" << args.kernel.stride_h
       << "   stride_w:" << args.kernel.stride_w;
  DLOG << "   out_address:" << args.output.address
       << "   out_scale_address:" << args.output.scale_address;
#endif

#ifdef PADDLE_MOBILE_ZU5
  int ret = 0;
  uint64_t output_scale = 0;

  // uint64_t reg_ActivationArgs = 0;
  // active function:{none,leakeyrelu,sigmoid,tanh}
  // ActivationArgs active_args;
  // active_args.activation_type = LEAKYRELU;

  // active_args.activation_type = args.output.activation.activation_type;

  // active_args.leaky_relu_negative_slope =
  //    args.output.activation.leaky_relu_negative_slope;

  // reg_ActivationArgs = (uint64_t(active_args.activation_type) << 32) |
  //                     active_args.leaky_relu_negative_slope;

  // DLOG << "   activation_type:" << active_args.activation_type
  //     << "   leaky_relu_negative_slope:"
  //     << active_args.leaky_relu_negative_slope;
  DLOG << "   reg_ActivationArgs:";
  uint64_t bypass_interrupt = reg_readq(REG_INTERRUPT);
  pthread_mutex_lock(&g_fpgainfo.pe_data->mutex);
  if (ERROR == g_fpgainfo.pe_data->pes[PE_IDX_CONV]->status) {
    ret = -EIO;
    DLOG << "Conv Status Error!";
    pthread_mutex_unlock(&g_fpgainfo.pe_data->mutex);
    return ret;
  }
  // reg_writeq(reg_ActivationArgs,
  // REG_ACTIVATION_MODE_AND_LEAKY_RELU_FACTOR);  // active functoion

  reg_writeq(output_scale, REG_SCALE_PARAMETER);
  // new
  reg_writeq((args.driver.row_padding_down << 45) |
                 (args.driver.row_padding_up << 34) |
                 (args.driver.col_padding_down << 17) |
                 args.driver.col_padding_up,
             REG_CONV_REG0);

  reg_writeq((args.driver.image_win_cnt_last << 50) |
                 (args.driver.image_win_cnt << 39) |
                 (args.driver.image_block_amount_per_row << 20) |
                 args.driver.filter_pad_width_mul_channel,
             REG_CONV_REG1);

  reg_writeq((args.driver.stride_h << 50) | (args.driver.skip_window << 30) |
                 (args.driver.filter_row << 10) |
                 (args.driver.filter_height << 5) | args.driver.filter_width,
             REG_CONV_REG2);

  reg_writeq((args.driver.filter_num << 42) | (args.driver.filter_align << 26) |
                 (args.driver.prog_full_cnt << 16) |
                 args.driver.filter_amount_all,
             REG_CONV_REG3);

  reg_writeq((args.driver.post_prog_full_cnt << 54) |
                 (args.driver.last_cal_res_row_num << 50) |
                 (args.driver.cal_res_num << 39) |
                 (args.driver.res_row_data_align4_pad << 35) |
                 (args.driver.output_amount_per_row << 16) |
                 args.driver.output_width,
             REG_CONV_REG4);

  reg_writeq((args.driver.deconv_dump << 40) | (args.driver.deconv_ena << 39) |
                 (args.driver.deconv_res_skip_row << 7) |
                 args.driver.deconv_skip_row,
             REG_CONV_REG5);

  reg_writeq((args.driver.result_amount_per_row_multi_para << 43) |
                 (args.driver.output_height << 32) |
                 args.driver.output_address_phy,
             REG_CONV_REG6);

  reg_writeq((args.driver.filter_amount_whole << 48) |
                 (args.driver.fpga_bias_scale_len << 32) |
                 args.driver.sb_address_phy,
             REG_CONV_REG7);

  reg_writeq(
      (args.driver.filters_amount_whole << 32) | args.driver.filter_address_phy,
      REG_CONV_REG8);

  reg_writeq((args.driver.image_amount_per_row << 43) |
                 (args.driver.image_hight << 32) |
                 args.driver.image_address_phy,
             REG_CONV_REG9);

  reg_writeq((args.driver.filter_pad_hight << 46) |
                 (args.driver.image_amount_per_row_multi_win << 23) |
                 args.driver.image_amount_per_row_multi_win_first,
             REG_CONV_REG10);

  reg_writeq((args.driver.image_block_num << 48) |
                 (args.driver.image_block_len << 24) |
                 args.driver.image_block_len_last,
             REG_CONV_REG11);

  reg_writeq(args.driver.cmd, REG_CONV_CMD);
  if (0 != fpga_regpoll(REG_INTERRUPT, INTERRUPT_CONV, PE_IRQ_TIMEOUT)) {
    g_fpgainfo.pe_data->pes[PE_IDX_CONV]->status = ERROR;
    ret = -EIO;
    DLOG << "Conv Wait Irq Timeout!";
    PADDLE_MOBILE_ENFORCE(0, "Conv Wait Irq Timeout");
  }
  DLOG << "after reg poll";

  pthread_mutex_unlock(&g_fpgainfo.pe_data->mutex);

  return ret;
#endif
  return 0;
}  // ComputeBasicConv

int ComputeFpgaPool(const struct PoolingArgs &args) {
#ifdef FPGA_PRINT_MODE
  DLOG << "=============ComputeFpgaPool===========";
  DLOG << "   mode:" << args.mode
       << "   kernel_reciprocal:" << fp16_2_fp32(args.kernel_reciprocal);
  DLOG << "   image_address:" << args.image.address
       << "   image_scale_address:" << args.image.scale_address
       << "   image_channels:" << args.image.channels
       << "   image_height:" << args.image.height
       << "   image_width:" << args.image.width
       << "   pad_height:" << args.image.pad_height
       << "   pad_width:" << args.image.pad_width;
  DLOG << "   kernel_height:" << args.kernel.height
       << "   kernel_width:" << args.kernel.width
       << "   stride_h:" << args.kernel.stride_h
       << "   stride_w:" << args.kernel.stride_w;
  DLOG << "   out_address:" << args.output.address
       << "   out_scale_address:" << args.output.scale_address;
#endif
#ifdef PADDLE_MOBILE_ZU5
  // return 0;
  uint64_t output_scale = 0;
  uint64_t timer_cnt = 0;
  int ret = 0;
  uint64_t cmd = 0;
  uint64_t image_physical_address = 0;
  uint64_t output_physical_address = 0;
  uint64_t bypass_interrupt = reg_readq(REG_INTERRUPT);
  image_physical_address = vaddr_to_paddr(args.image.address);
  output_physical_address = vaddr_to_paddr(args.output.address);
  uint64_t C_paral_64 = align_to_x((uint64_t)args.image.channels, 64);
  uint64_t C_align_32 = align_to_x((uint64_t)args.image.channels, 32);
  uint64_t output_height = (uint64_t)(
      (args.image.height + args.image.pad_height * 2 - args.kernel.height) /
          args.kernel.stride_h +
      1);
  uint64_t output_width = (uint64_t)(
      (args.image.width + args.image.pad_width * 2 - args.kernel.width) /
          args.kernel.stride_w +
      1);

  uint64_t image_amount_per_row =
      align_to_x((uint64_t)args.image.width * (uint64_t)args.image.channels,
                 IMAGE_ALIGNMENT);
  uint64_t image_one_pad_per_row =
      (uint64_t)args.image.width * (uint64_t)args.image.channels +
      (uint64_t)args.image.pad_width * (uint64_t)args.image.channels;

  uint64_t result_amount_align_32 =
      align_to_x((uint64_t)output_width * (uint64_t)args.image.channels, 32);
  uint64_t result_addr_row =
      (result_amount_align_32 << 32) | output_physical_address;
  uint64_t row_padding_down =
      (uint64_t)args.image.height + (uint64_t)args.image.pad_height;
  uint64_t kernel_width_sub1 = (uint64_t)args.kernel.width - 1;
  uint64_t kernel_padding_step = row_padding_down |
                                 ((uint64_t)args.image.pad_height << 16) |
                                 ((uint64_t)args.kernel.stride_h << 24) |
                                 ((uint64_t)kernel_width_sub1 << 32) |
                                 ((uint64_t)args.kernel.height << 40) |
                                 ((uint64_t)(args.kernel.height - 1) << 48);
  uint64_t image_calcu_height =
      (uint64_t)args.kernel.height +
      (output_height - 1) * (uint64_t)args.kernel.stride_h;
  uint64_t result_size_calcu_height = (output_height - 1) |
                                      ((output_width - 1) << 16) |
                                      (image_calcu_height << 32);
  uint64_t col_padding_down =
      ((uint64_t)args.image.width + (uint64_t)args.image.pad_width) *
      (uint64_t)args.image.channels;

  uint64_t image_row_col_padding_down =
      image_amount_per_row | (col_padding_down << 32);
  uint64_t image_rowXpadding_h =
      image_amount_per_row * (uint64_t)args.image.pad_height;
  uint64_t image_rowXstep_h =
      image_amount_per_row * (uint64_t)args.kernel.stride_h;
  uint64_t image_rowXpad_h_rowXstep_h =
      image_rowXpadding_h | (image_rowXstep_h << 32);
  uint64_t channelXpad_w =
      (uint64_t)args.image.channels * (uint64_t)args.image.pad_width;
  uint64_t channelXstep_w =
      (uint64_t)args.image.channels * (uint64_t)args.kernel.stride_w;
  uint64_t channelXpad_w_channelXstep_w =
      channelXpad_w | (channelXstep_w << 32);
  uint64_t filter_row_align = C_align_32 * (uint64_t)args.kernel.width;
  uint64_t sub_filter_amount_align =
      C_align_32 * (uint64_t)args.kernel.width * (uint64_t)args.kernel.height;
  uint64_t mult_factor = 0;
  float average_reciprocal = args.kernel_reciprocal;
  uint32_t *kernel_reciprocal;
  kernel_reciprocal = (reinterpret_cast<uint32_t *>(&average_reciprocal));
  if (args.mode == 1)
    mult_factor = (uint64_t)(*kernel_reciprocal) | ((uint64_t)1 << 32) |
                  ((uint64_t)1 << 40);
  else
    mult_factor =
        (uint64_t)0x3f800000 | ((uint64_t)1 << 32) | ((uint64_t)1 << 40);
  pthread_mutex_lock(&g_fpgainfo.pe_data->mutex);
  if (ERROR == g_fpgainfo.pe_data->pes[PE_IDX_POOLING]->status) {
    ret = -EIO;
    DLOG << "Conv Status Error!";
    pthread_mutex_unlock(&g_fpgainfo.pe_data->mutex);
    return ret;
  }
  reg_writeq(output_scale, REG_SCALE_PARAMETER);
  reg_writeq(image_physical_address, 0x808);
  reg_writeq(result_addr_row, 0x810);
  reg_writeq(kernel_padding_step, 0x818);
  reg_writeq(result_size_calcu_height, 0x820);
  reg_writeq((uint64_t)args.image.channels, 0x828);
  reg_writeq(image_row_col_padding_down, 0x830);
  reg_writeq(image_rowXpad_h_rowXstep_h, 0x838);
  reg_writeq(mult_factor, 0x840);  // dw donot care
  reg_writeq(channelXpad_w_channelXstep_w, 0x848);
  if (args.mode == 1)
    cmd = (uint64_t)4;
  else
    cmd = (uint64_t)8;
  reg_writeq(cmd, 0x800);

  DLOG << "before reg poll";
  if (0 != fpga_regpoll(REG_INTERRUPT, INTERRUPT_POOLING, PE_IRQ_TIMEOUT)) {
    g_fpgainfo.pe_data->pes[PE_IDX_POOLING]->status = ERROR;
    ret = -EIO;
    DLOG << "Pooling Wait Irq Timeout!";
    PADDLE_MOBILE_ENFORCE(0, "Pooling Wait Irq Timeout!");
  }
  DLOG << "after reg poll";

  pthread_mutex_unlock(&g_fpgainfo.pe_data->mutex);

  return ret;
#endif
  return 0;
}  // ComputeFpgaPool

int ComputeFpgaEWAdd(const struct EWAddArgs &args) {
#ifdef FPGA_PRINT_MODE
  DLOG << "=============ComputeFpgaEWAdd===========";
  DLOG << "   relu_enabled:" << args.relu_enabled;
  DLOG << "   const0:" << fp16_2_fp32(int16_t(args.const0))
       << "   const1:" << fp16_2_fp32(int16_t(args.const1));
  DLOG << "   image0_address:" << args.image0.address
       << "   image0_scale_address:" << args.image0.scale_address
       << "   image0_channels:" << args.image0.channels
       << "   image0_height:" << args.image0.height
       << "   image0_width:" << args.image0.width
       << "   pad0_height:" << args.image0.pad_height
       << "   pad0_width:" << args.image0.pad_width;
  DLOG << "   image1_address:" << args.image1.address
       << "   image1_scale_address:" << args.image1.scale_address
       << "   image1_channels:" << args.image1.channels
       << "   image1_height:" << args.image1.height
       << "   image1_width:" << args.image1.width
       << "   pad1_height:" << args.image1.pad_height
       << "   pad_width:" << args.image1.pad_width;
  DLOG << "   out_address:" << args.output.address
       << "   out_scale_address:" << args.output.scale_address;
#endif
#ifdef PADDLE_MOBILE_ZU5
  int ret = 0;
  uint64_t bypass_interrupt = reg_readq(REG_INTERRUPT);

  pthread_mutex_lock(&g_fpgainfo.pe_data->mutex);
  if (ERROR == g_fpgainfo.pe_data->pes[PE_IDX_EW]->status) {
    ret = -EIO;
    DLOG << "EW Status Error!";
    pthread_mutex_unlock(&g_fpgainfo.pe_data->mutex);
    return ret;
  }

  uint64_t image0_physical_address = 0;
  uint64_t image1_physical_address = 0;
  uint64_t image_physical_address = 0;
  uint64_t output_physical_address = 0;
  image0_physical_address = vaddr_to_paddr(args.image0.address);
  image1_physical_address = vaddr_to_paddr(args.image1.address);
  image_physical_address =
      image0_physical_address | (image1_physical_address << 32);
  output_physical_address = vaddr_to_paddr(args.output.address);
  uint64_t image_amount_per_row =
      align_to_x((uint64_t)args.image0.width * (uint64_t)args.image0.channels,
                 IMAGE_ALIGNMENT);
  uint64_t result_addr_row =
      output_physical_address | (image_amount_per_row << 32);
  uint64_t kernel_padding_step = 0;
  kernel_padding_step = ((uint64_t)args.image0.height * 2) |
                        ((uint64_t)2 << 24) | ((uint64_t)2 << 40) |
                        ((uint64_t)1 << 48);
  uint64_t result_size_calcu_height =
      ((uint64_t)args.image0.height - 1) |
      ((image_amount_per_row / 32 - 1) << 16) |
      (((uint64_t)args.image0.height * 2) << 32);
  uint64_t image_row_col_padding_down =
      image_amount_per_row | (image_amount_per_row << 32);
  float quantParam =
      ((args.image0.scale_address)[0]) / ((args.output.scale_address)[0]);
  uint32_t *ew_scale = reinterpret_cast<uint32_t *>(&quantParam);
  uint64_t ew_scale_mult_factor = (*ew_scale) | ((uint64_t)args.const0 << 32) |
                                  ((uint64_t)args.const1 << 40);
  reg_writeq(0ul, REG_SCALE_PARAMETER);
  reg_writeq(image_physical_address, 0x808);
  reg_writeq(result_addr_row, 0x810);
  reg_writeq(kernel_padding_step, 0x818);
  reg_writeq(result_size_calcu_height, 0x820);
  reg_writeq(32, 0x828);
  reg_writeq(image_row_col_padding_down, 0x830);
  reg_writeq(((image_amount_per_row * 2) << 32), 0x838);
  reg_writeq(ew_scale_mult_factor, 0x840);  // dw donot care
  reg_writeq(((uint64_t)32 << 32), 0x848);
  reg_writeq(0, 0x858);
  uint64_t cmd = 0;
  cmd = (uint64_t)2 | (((uint64_t)args.relu_enabled) << 8);
  reg_writeq(cmd, 0x800);

  if (0 != fpga_regpoll(REG_INTERRUPT, INTERRUPT_POOLING, PE_IRQ_TIMEOUT)) {
    g_fpgainfo.pe_data->pes[PE_IDX_EW]->status = ERROR;
    ret = -EIO;
    DLOG << "EW Wait Irq Timeout!";
    PADDLE_MOBILE_ENFORCE(0, "EW Wait Irq Timeout!");
  }

  pthread_mutex_unlock(&g_fpgainfo.pe_data->mutex);
  return ret;
#endif
  return 0;
}  // ComputeFpgaEWAdd

int PerformBypass(const struct BypassArgs &args) {
#ifdef FPGA_PRINT_MODE
  DLOG << "=============ComputeFpgaBypass===========";
  DLOG << "   input_type:" << args.input_data_type
       << "   output_type:" << args.output_data_type
       << "   input_layout_type:" << args.input_layout_type
       << "   output_layout_type:" << args.output_layout_type;
  DLOG << "   image_address:" << args.image.address
       << "   image_scale_address:" << args.image.scale_address
       << "   image_channels:" << args.image.channels
       << "   image_height:" << args.image.height
       << "   image_width:" << args.image.width
       << "   pad_height:" << args.image.pad_height
       << "   pad_width:" << args.image.pad_width;
  DLOG << "   out_address:" << args.output.address
       << "   out_scale_address:" << args.output.scale_address;
#endif
#ifdef PADDLE_MOBILE_ZU5
  uint64_t bypass_interrupt = reg_readq(REG_INTERRUPT);
  uint64_t output_scale = 0;
  uint64_t timer_cnt = 0;
  uint64_t cmd = 0;
  uint64_t datalen = 0;
  uint64_t input_address_phy = 0;
  uint64_t output_address_phy = 0;
  uint8_t data_cell_in = 0;
  uint8_t data_cell_out = 0;
  int ret = 0;

  uint64_t reg_ActivationArgs = 0;
  ActivationArgs active_args;
  active_args.activation_type = args.output.activation.activation_type;

  active_args.leaky_relu_negative_slope =
      args.output.activation.leaky_relu_negative_slope;

  reg_ActivationArgs = (uint64_t(active_args.activation_type) << 32) |
                       active_args.leaky_relu_negative_slope;

  datalen = (uint64_t)args.image.width * (uint64_t)args.image.height *
            (uint64_t)args.image.channels;
  datalen = align_to_x(datalen, 16);
  input_address_phy = vaddr_to_paddr_driver(args.image.address);
  output_address_phy = vaddr_to_paddr_driver(args.output.address);
  DLOG << "input_phy:" << input_address_phy;
  DLOG << "output_phy:" << output_address_phy;

  switch (args.input_data_type) {
    case DATA_TYPE_FP16: {
      switch (args.output_data_type) {
        case DATA_TYPE_FP16:
          data_cell_in = SIZE_FP16;
          data_cell_out = SIZE_FP16;
          cmd = CMD_FP16_TO_FP16;
          break;

        case DATA_TYPE_FP32:
          data_cell_in = SIZE_FP16;
          data_cell_out = SIZE_FP32;
          cmd = CMD_FP16_TO_FP32;
          break;

        default:
          break;
      }
    } break;

    case DATA_TYPE_INT8: {
      if (args.output_data_type != DATA_TYPE_FP16) {
        DLOG << "error:Output Datetype error,not DATA_TYPE_FP16: "
             << args.output_data_type;
      }
      data_cell_in = SIZE_INT8;
      data_cell_out = SIZE_FP16;
      cmd = CMD_INT8_TO_FP16;
    } break;

    case DATA_TYPE_FP32: {
      switch (args.output_data_type) {
        case DATA_TYPE_FP16:
          data_cell_in = SIZE_FP32;
          data_cell_out = SIZE_FP16;
          cmd = CMD_FP32_TO_FP16;
          break;

        case DATA_TYPE_FP32:
          data_cell_in = SIZE_FP32;
          data_cell_out = SIZE_FP32;
          cmd = CMD_FP32_TO_FP32;
          break;

        default:
          break;
      }
    } break;

    default:
      break;
  }
  if (cmd != CMD_FP16_TO_FP16 && cmd != CMD_FP16_TO_FP32 &&
      cmd != CMD_FP32_TO_FP16 && cmd != CMD_FP32_TO_FP32 &&
      cmd != CMD_INT8_TO_FP16) {
    //   std::cout<< " err back Error1!" <<std::endl;
    return -EFAULT;
  }
  if ((data_cell_in != SIZE_FP16 && data_cell_in != SIZE_FP32 &&
       data_cell_in != SIZE_INT8) ||
      (data_cell_out != SIZE_FP16 && data_cell_out != SIZE_FP32)) {
    return -EFAULT;
  }
  pthread_mutex_lock(&g_fpgainfo.pe_data->mutex);
  if (ERROR == g_fpgainfo.pe_data->pes[PE_IDX_BYPASS]->status) {
    ret = -EIO;
    DLOG << "Bypass Status Error!";
    pthread_mutex_unlock(&g_fpgainfo.pe_data->mutex);
    return ret;
  }
  reg_writeq(reg_ActivationArgs,
             REG_ACTIVATION_MODE_AND_LEAKY_RELU_FACTOR);  // active functoion
  reg_writeq(output_scale, REG_SCALE_PARAMETER);
  reg_writeq(input_address_phy, REG_CONVERT_SRC_ADDR);
  reg_writeq(output_address_phy, REG_CONVERT_DST_ADDR);
  reg_writeq(datalen, REG_CONVERT_RD_LENGTH);
  reg_writeq(datalen, REG_CONVERT_WR_LENGTH);
  reg_writeq(cmd, REG_CONVERT_CMD);
  DLOG << "before reg poll";
  if (0 != fpga_regpoll(REG_INTERRUPT, INTERRUPT_BYPASS, PE_IRQ_TIMEOUT)) {
    g_fpgainfo.pe_data->pes[PE_IDX_BYPASS]->status = ERROR;
    ret = -EIO;
    DLOG << "BYPASS Wait Irq Timeout!";
    PADDLE_MOBILE_ENFORCE(0, "BYPASS Wait Irq Timeout!");
  }
  DLOG << "after reg poll";

  output_scale = reg_readq(REG_SCALE_PARAMETER);
  output_scale = (output_scale << 32) | (output_scale >> 32);
  fpga_copy(args.output.scale_address, &output_scale, sizeof(float) * 2);
  reg_writeq(reg_ActivationArgs, REG_ACTIVATION_MODE_AND_LEAKY_RELU_FACTOR);
  pthread_mutex_unlock(&g_fpgainfo.pe_data->mutex);
  return ret;
#endif
  return 0;
}  // PerformBypass

uint64_t FPGAVersion() {
#ifdef FPGA_PRINT_MODE
  DLOG << "=============ComputeFpgaBypass===========";
#endif
#ifdef PADDLE_MOBILE_ZU5
  uint64_t fpga_ver = 0;
  pthread_mutex_lock(&g_fpgainfo.pe_data->mutex);
  fpga_ver = reg_readq(REG_HARDWARE_STATUS);
  pthread_mutex_unlock(&g_fpgainfo.pe_data->mutex);
  return fpga_ver;
#endif
  return 0;
}  // FPGAVersion

int ComputeFPGAConcat(const struct ConcatArgs &args) {
#ifdef FPGA_PRINT_MODE
  DLOG << "=============ComputeFpgaConcat===========";
  DLOG << "   Image_num: " << args.image_num
       << "   out_address:" << args.image_out
       << "   out_scale_address:" << args.scale_out
       << "   out_channel:" << args.out_channel;
  DLOG << "   image_height:" << args.height << "   image_width:" << args.width;
  for (int i = 0; i < args.image_num; i++) {
    DLOG << "   " << i << "th:        ";
    DLOG << "   channel_num:"
         << args.channel_num[i]
         //<< "   aligned_channel_num:" << args.aligned_channel_num[i]
         << "   image_address:" << args.images_in[i]
         << "   image_scale_address:" << args.scales_in[i];
  }
#endif

  image::concat_images(args.images_in, args.scales_in, args.image_out,
                       args.scale_out, args.image_num, args.channel_num,
                       args.height, args.width);
  return 0;
}  // ComputeFPGAConcat

void deconv_post_process(const struct DeconvArgs &args) {
  int sub_conv_n = args.sub_conv_num;
  int sub_height = args.sub_output_height;
  int sub_width = args.sub_output_width;
  int omit_size = args.omit_size;
  int channel = args.filter_num;
  int num = 1;
  int origin_h = sub_height * sub_conv_n;
  int origin_w = sub_width * sub_conv_n;
  int align_origin_w = align_to_x(origin_w * channel, 16);
  int deconv_h = origin_h - 2 * omit_size;
  int deconv_w = origin_w - 2 * omit_size;
  int deconv_row_len = deconv_w * channel;
  int align_deconv_row_len = align_to_x(deconv_row_len, 16);

  for (int idx = 0; idx < sub_conv_n; ++idx) {
    paddle_mobile::fpga::fpga_invalidate(
        args.split_conv_args[idx]->output.address,
        align_origin_w * origin_h * sizeof(int16_t));
  }

  int deconv_idx = 0;
  for (int nn = 0; nn < num; ++nn) {
    for (int hh = 0; hh < origin_h; ++hh) {
      int hx = (hh % sub_conv_n);
      auto sub_t =
          (int16_t *)(args.split_conv_args[sub_conv_n - hx - 1]  // NOLINT
                          ->output.address);
      int hi = (hh / sub_conv_n);
      if ((hh < omit_size) || (hh >= (origin_h - omit_size))) continue;
      int sidx = (nn * origin_h * align_origin_w + hi * align_origin_w +
                  omit_size * channel);
      fpga_copy((int16_t *)(args.output.address) + deconv_idx,    // NOLINT
                sub_t + sidx, sizeof(int16_t) * deconv_row_len);  // NOLINT
      deconv_idx += align_deconv_row_len;
    }
  }
  fpga_flush(args.output.address,
             num * align_deconv_row_len * deconv_h * sizeof(int16_t));
}
void DWDeconv_post_process(const struct DWDeconvArgs &args) {
  int sub_conv_n = args.sub_conv_num;
  int sub_height = args.sub_output_height;
  int sub_width = args.sub_output_width;
  int omit_size = args.omit_size;
  int channel = args.filter_num;
  int num = 1;
  int origin_h = sub_height * sub_conv_n;
  int origin_w = sub_width * sub_conv_n;
  int align_origin_w = align_to_x(origin_w * channel, IMAGE_ALIGNMENT);
  int deconv_h = origin_h - 2 * omit_size;
  int deconv_w = origin_w - 2 * omit_size;
  int deconv_row_len = deconv_w * channel;
  int align_deconv_row_len = align_to_x(deconv_row_len, IMAGE_ALIGNMENT);

  for (int idx = 0; idx < sub_conv_n; ++idx) {
    paddle_mobile::fpga::fpga_invalidate(
        args.dw_conv_args[idx]->output.address,
        align_origin_w * origin_h * sizeof(int16_t));
  }

  int deconv_idx = 0;
  for (int nn = 0; nn < num; ++nn) {
    for (int hh = 0; hh < origin_h; ++hh) {
      int hx = (hh % sub_conv_n);
      auto sub_t = (int16_t *)(args.dw_conv_args[sub_conv_n - hx - 1]  // NOLINT
                                   ->output.address);
      int hi = (hh / sub_conv_n);
      if ((hh < omit_size) || (hh >= (origin_h - omit_size))) continue;
      int sidx = (nn * origin_h * align_origin_w + hi * align_origin_w +
                  omit_size * channel);
      fpga_copy((int16_t *)(args.output.address) + deconv_idx,    // NOLINT
                sub_t + sidx, sizeof(int16_t) * deconv_row_len);  // NOLINT
      deconv_idx += align_deconv_row_len;
    }
  }
  fpga_flush(args.output.address,
             num * align_deconv_row_len * deconv_h * sizeof(int16_t));
}

int ComputeFpgaDeconv(const struct DeconvArgs &args) {
#ifdef FPGA_PRINT_MODE
  DLOG << "=============ComputeFPGADeConv===========";
  DLOG << "   filter_num:" << args.filter_num
       << "   group_num:" << args.group_num << "omit_size:" << args.omit_size
       << "sub_output_width: " << args.sub_output_width
       << "sub_output_height: " << args.sub_output_height
       << "   sub_conv_num:" << args.sub_conv_num;
  DLOG << "args.output.address: " << args.output.address
       << "args.output.scale_address: " << args.output.scale_address;

#endif

  int sub_conv_num = args.sub_conv_num;

#ifdef COST_TIME_PRINT
  timeval start, end;
  long dif_sec, dif_usec;  // NOLINT
#endif

  for (int i = 0; i < sub_conv_num; i++) {
#ifdef COST_TIME_PRINT
    gettimeofday(&start, NULL);
#endif

    ComputeFpgaConv(*args.split_conv_args[i]);
#ifdef COST_TIME_PRINT
    gettimeofday(&end, NULL);
    dif_sec = end.tv_sec - start.tv_sec;
    dif_usec = end.tv_usec - start.tv_usec;
    std::cout << "deconv basic_conv: " << i << " times:  "
              << "    cost time: " << (dif_sec * 1000000 + dif_usec) << "us"
              << std::endl;
#endif
  }

  /*if (sub_conv_num > 1) {
    float max_scale = -1.0f;
#ifdef COST_TIME_PRINT
    gettimeofday(&start, NULL);
#endif
    for (int i = 0; i < sub_conv_num; i++) {
      paddle_mobile::fpga::fpga_invalidate(
          args.split_conv_args[i]->output.scale_address, 2 * sizeof(float));
      float ptr_scale = (args.split_conv_args[i]->output.scale_address)[0];
      if (ptr_scale > max_scale) {
        args.output.scale_address[0] = ptr_scale;
        args.output.scale_address[1] =
            (args.split_conv_args[i]->output.scale_address)[1];
      }
    }

#ifdef COST_TIME_PRINT
    gettimeofday(&end, NULL);
    dif_sec = end.tv_sec - start.tv_sec;
    dif_usec = end.tv_usec - start.tv_usec;
    std::cout << "deconv scale  "
              << "    cost time: " << (dif_sec * 1000000 + dif_usec) << "us"
              << std::endl;
#endif
  }*/

  return 0;
}  // ComputeFpgaDeconv

int ComputeFPGASplit(const struct SplitArgs &args) {
#ifdef FPGA_PRINT_MODE
  DLOG << "=============ComputeFpgaSplit===========";
  DLOG << "   Image_num: " << args.image_num
       << "   in_address:" << args.image_in
       << "   in_scale_address:" << args.scale_in;
  DLOG << "   image_height:" << args.height << "   image_width:" << args.width;
  for (int i = 0; i < args.image_num; i++) {
    DLOG << "   " << i << "th:        ";
    DLOG << "   channel_num:" << args.out_channel_nums[i]
         << "   image_address:" << args.images_out[i]
         << "   image_scale_address:" << args.scales_out[i];
  }
#endif
  image::split_image(args.image_in, args.images_out, args.image_num,
                     args.out_channel_nums, args.height, args.width);
  return 0;
}  // ComputeFPGASplit
int ComputeDWConv(const struct DWconvArgs &args) {
#ifdef FPGA_PRINT_MODE
  DLOG << "=============ComputeDWConv===========";
  // DLOG << "   mode:" << args.relu_enabled;
  DLOG << "   image_address:" << args.image.address
       << "   image_scale_address:" << args.image.scale_address
       << "   image_channels:" << args.image.channels
       << "   image_height:" << args.image.height
       << "   image_width:" << args.image.width
       << "   pad_height:" << args.image.pad_height
       << "   pad_width:" << args.image.pad_width;
  DLOG << "   filter_address:" << args.filter_address;
  //<< "   bias_address:" << args.bias_address;
  DLOG << "   kernel_height:" << args.kernel.height
       << "   kernel_width:" << args.kernel.width
       << "   stride_h:" << args.kernel.stride_h
       << "   stride_w:" << args.kernel.stride_w;
  DLOG << "   out_address:" << args.output.address
       << "   out_scale_address:" << args.output.scale_address;
#endif
#ifdef PADDLE_MOBILE_ZU5
  DLOG << "DWConv";
  uint64_t bypass_interrupt = reg_readq(REG_INTERRUPT);
  // return 0;
  uint64_t timer_cnt = 0;
  int ret = 0;
  uint64_t cmd = 0;
  uint64_t image_physical_address = 0;
  uint64_t output_physical_address = 0;
  uint64_t filter_physical_address = 0;
  uint64_t bias_physical_address = 0;

  image_physical_address = vaddr_to_paddr(args.image.address);
  output_physical_address = vaddr_to_paddr(args.output.address);
  filter_physical_address = vaddr_to_paddr(args.filter_address);
  bias_physical_address = vaddr_to_paddr(args.bias_address);
  uint64_t C_align_64 = align_to_x((uint64_t)args.image.channels, 64);
  uint64_t C_align_32 = align_to_x((uint64_t)args.image.channels, 32);
  uint64_t output_height = (uint64_t)(
      (args.image.height + args.image.pad_height * 2 - args.kernel.height) /
          args.kernel.stride_h +
      1);
  uint64_t output_width = (uint64_t)(
      ((args.image.width + args.image.pad_width * 2 - args.kernel.width) /
           args.kernel.stride_w +
       1) *
      args.sub_conv_num);

  uint64_t image_amount_per_row =
      align_to_x((uint64_t)args.image.width * (uint64_t)args.image.channels,
                 IMAGE_ALIGNMENT);
  uint64_t image_one_pad_per_row =
      (uint64_t)args.image.width * (uint64_t)args.image.channels +
      (uint64_t)args.image.pad_width * (uint64_t)args.image.channels;

  uint64_t result_amount_align_32 =
      align_to_x((uint64_t)output_width * (uint64_t)args.image.channels, 32);
  uint64_t result_addr_row =
      (result_amount_align_32 << 32) | output_physical_address;
  uint64_t row_padding_down =
      (uint64_t)args.image.height + (uint64_t)args.image.pad_height;
  uint64_t kernel_width_sub1 = (uint64_t)args.kernel.width - 1;
  uint64_t kernel_padding_step = row_padding_down |
                                 ((uint64_t)args.image.pad_height << 16) |
                                 ((uint64_t)args.kernel.stride_h << 24) |
                                 ((uint64_t)kernel_width_sub1 << 32) |
                                 ((uint64_t)args.kernel.height << 40) |
                                 ((uint64_t)(args.kernel.height - 1) << 48);
  uint64_t image_calcu_height =
      (uint64_t)args.kernel.height +
      (output_height - 1) * (uint64_t)args.kernel.stride_h;
  uint64_t result_size_calcu_height = (output_height - 1) |
                                      ((output_width - 1) << 16) |
                                      (image_calcu_height << 32);
  uint64_t col_padding_down =
      ((uint64_t)args.image.width + (uint64_t)args.image.pad_width) *
      (uint64_t)args.image.channels;

  uint64_t image_row_col_padding_down =
      image_amount_per_row | (col_padding_down << 32);
  uint64_t image_rowXpadding_h =
      image_amount_per_row * (uint64_t)args.image.pad_height;
  uint64_t image_rowXstep_h =
      image_amount_per_row * (uint64_t)args.kernel.stride_h;
  uint64_t image_rowXpad_h_rowXstep_h =
      image_rowXpadding_h | (image_rowXstep_h << 32);
  uint64_t channelXpad_w =
      (uint64_t)args.image.channels * (uint64_t)args.image.pad_width;
  uint64_t channelXstep_w =
      (uint64_t)args.image.channels * (uint64_t)args.kernel.stride_w;
  uint64_t channelXpad_w_channelXstep_w =
      channelXpad_w | (channelXstep_w << 32);

  uint64_t filter_row_align = C_align_64 * (uint64_t)args.kernel.width;
  uint64_t sub_filter_amount_align =
      C_align_64 * (uint64_t)args.kernel.width * (uint64_t)args.kernel.height;
  uint64_t filter_amount_align =
      sub_filter_amount_align * (uint64_t)args.sub_conv_num;
  uint64_t filter_param = filter_row_align | (filter_amount_align << 16) |
                          (sub_filter_amount_align << 32) |
                          (((uint64_t)args.sub_conv_num - 1) << 48);
  uint64_t channel_parameter =
      (uint64_t)args.image.channels | (C_align_64 << 16);
  pthread_mutex_lock(&g_fpgainfo.pe_data->mutex);
  if (ERROR == g_fpgainfo.pe_data->pes[PE_IDX_POOLING]->status) {
    ret = -EIO;
    DLOG << "Conv Status Error!";
    pthread_mutex_unlock(&g_fpgainfo.pe_data->mutex);
    return ret;
  }
  reg_writeq(0ul, REG_SCALE_PARAMETER);
  reg_writeq(image_physical_address, 0x808);
  reg_writeq(result_addr_row, 0x810);
  reg_writeq(kernel_padding_step, 0x818);
  reg_writeq(result_size_calcu_height, 0x820);
  reg_writeq(channel_parameter, 0x828);
  reg_writeq(image_row_col_padding_down, 0x830);
  reg_writeq(image_rowXpad_h_rowXstep_h, 0x838);
  reg_writeq(0, 0x840);
  reg_writeq(channelXpad_w_channelXstep_w, 0x848);
  reg_writeq(filter_physical_address, 0x850);
  reg_writeq(filter_param, 0x858);
  reg_writeq(((bias_physical_address + C_align_64 * 4) |
              (bias_physical_address << 32)),
             0x860);
  cmd = (uint64_t)1 | (((uint64_t)args.relu_enabled) << 8);
  reg_writeq(cmd, 0x800);

  DLOG << "before reg poll";
  if (0 != fpga_regpoll(REG_INTERRUPT, INTERRUPT_POOLING, PE_IRQ_TIMEOUT)) {
    g_fpgainfo.pe_data->pes[PE_IDX_POOLING]->status = ERROR;
    ret = -EIO;
    DLOG << "DWconv Wait Irq Timeout!";
    PADDLE_MOBILE_ENFORCE(0, "DWConv Wait Irq Timeout");
  }
  DLOG << "after reg poll";
  pthread_mutex_unlock(&g_fpgainfo.pe_data->mutex);
  return ret;
#endif
  return 0;
}
int ComputeDWDeconv(const struct DWDeconvArgs &args) {
#ifdef FPGA_PRINT_MODE
  DLOG << "=============ComputeFPGADeConv===========";
  DLOG << "   filter_num:" << args.filter_num
       << "   group_num:" << args.group_num << "omit_size:" << args.omit_size
       << "sub_output_width: " << args.sub_output_width
       << "sub_output_height: " << args.sub_output_height
       << "   sub_conv_num:" << args.sub_conv_num;
  DLOG << "args.output.address: " << args.output.address
       << "args.output.scale_address: " << args.output.scale_address;

#endif

  int sub_conv_num = args.sub_conv_num;

#ifdef COST_TIME_PRINT
  timeval start, end;
  long dif_sec, dif_usec;  // NOLINT
#endif

  for (int i = 0; i < sub_conv_num; i++) {
#ifdef COST_TIME_PRINT
    gettimeofday(&start, NULL);
#endif

    ComputeDWConv(*args.dw_conv_args[i]);
#ifdef COST_TIME_PRINT
    gettimeofday(&end, NULL);
    dif_sec = end.tv_sec - start.tv_sec;
    dif_usec = end.tv_usec - start.tv_usec;
    std::cout << "deconv basic_conv: " << i << " times:  "
              << "    cost time: " << (dif_sec * 1000000 + dif_usec) << "us"
              << std::endl;
#endif
  }

  if (sub_conv_num > 1) {
    float max_scale = -1.0f;
#ifdef COST_TIME_PRINT
    gettimeofday(&start, NULL);
#endif
    for (int i = 0; i < sub_conv_num; i++) {
      paddle_mobile::fpga::fpga_invalidate(
          args.dw_conv_args[i]->output.scale_address, 2 * sizeof(float));
      float ptr_scale = (args.dw_conv_args[i]->output.scale_address)[0];
      if (ptr_scale > max_scale) {
        args.output.scale_address[0] = ptr_scale;
        args.output.scale_address[1] =
            (args.dw_conv_args[i]->output.scale_address)[1];
      }
    }

#ifdef COST_TIME_PRINT
    gettimeofday(&end, NULL);
    dif_sec = end.tv_sec - start.tv_sec;
    dif_usec = end.tv_usec - start.tv_usec;
    std::cout << "deconv scale  "
              << "    cost time: " << (dif_sec * 1000000 + dif_usec) << "us"
              << std::endl;
#endif
  }

#ifdef COST_TIME_PRINT
  gettimeofday(&start, NULL);
#endif
  DWDeconv_post_process(args);
#ifdef COST_TIME_PRINT
  gettimeofday(&end, NULL);
  dif_sec = end.tv_sec - start.tv_sec;
  dif_usec = end.tv_usec - start.tv_usec;
  std::cout << "deconv_post_process  "
            << "    cost time: " << (dif_sec * 1000000 + dif_usec) << "us"
            << std::endl;
#endif
  return 0;
}  // ComputeFpgaDeconv

}  // namespace fpga
}  // namespace paddle_mobile
