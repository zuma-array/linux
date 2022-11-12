/*
 * sound/soc/amlogic/auge/tdm_hw.h
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef __AML_TDM_HW_H__
#define __AML_TDM_HW_H__

#include "audio_io.h"
#include "regs.h"

struct pcm_setting {
	unsigned int pcm_mode;
	unsigned int sysclk;
	unsigned int sysclk_bclk_ratio;
	unsigned int bclk;
	unsigned int bclk_lrclk_ratio;
	unsigned int lrclk;
	unsigned int tx_mask;
	unsigned int rx_mask;
	unsigned int slots;
	unsigned int slot_width;
	unsigned int pcm_width;
	unsigned int lane_mask_out;
	unsigned int lane_mask_in;
};

extern void aml_tdm_enable(
	struct aml_audio_controller *actrl,
	int stream, int index,
	bool is_enable);

extern void aml_tdm_arb_config(
	struct aml_audio_controller *actrl);

extern void aml_tdm_fifo_reset(
	struct aml_audio_controller *actrl,
	int stream, int index);

extern void aml_tdm_fifo_ctrl(
	struct aml_audio_controller *actrl,
	int bitwidth, int stream,
	int index);

extern void aml_tdm_set_format(
	struct aml_audio_controller *actrl,
	struct pcm_setting *p_config,
	unsigned int clk_sel,
	unsigned int index,
	unsigned int fmt);

extern void aml_tdm_set_slot(
	struct aml_audio_controller *actrl,
	int slots, int slot_width, int index);

extern void aml_tdm_set_channel_mask(
	struct aml_audio_controller *actrl,
	int stream, int index, int lanes, int mask);

extern void aml_tdm_set_lane_channel_swap(
	struct aml_audio_controller *actrl,
	int stream, int index, int swap);

extern void aml_tdm_set_bclk_ratio(
	struct aml_audio_controller *actrl,
	int clk_sel, int lrclk_hi, int bclk_ratio);

extern void aml_tdm_set_lrclkdiv(
	struct aml_audio_controller *actrl,
	int clk_sel, int ratio);

#endif
