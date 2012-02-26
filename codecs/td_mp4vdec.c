/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "tidsp.h"
#include "dsp_bridge.h"
#include "dmm_buffer.h"

struct create_args {
	uint32_t size;
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t out_id;
	uint16_t out_type;
	uint16_t out_count;

	uint16_t reserved;

	uint32_t max_width;
	uint32_t max_height;
	uint32_t color_format;
	uint32_t max_framerate;
	uint32_t max_bitrate;
	uint32_t endianness;
	uint32_t profile;
	int32_t max_level;
	uint32_t mode;
	int32_t preroll;
	uint32_t display_width;
};

static void create_args(struct td_context *ctx, unsigned *profile_id, void **arg_data)
{
	struct create_args args = {
		.size = sizeof(args) - 4,
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = ctx->ports[0]->nr_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = ctx->ports[1]->nr_buffers,
		.max_width = ctx->width,
		.max_height = ctx->height,
		.color_format = ctx->color_format == td_fourcc('U', 'Y', 'V', 'Y') ? 4 : 1,
		.max_framerate = 1,
		.max_bitrate = 1,
		.endianness = 1,
		.max_level = -1,
	};

	if (ctx->width * ctx->height > 640 * 480)
		*profile_id = 4;
	else if (ctx->width * ctx->height > 352 * 288)
		*profile_id = 3;
	else if (ctx->width * ctx->height > 176 * 144)
		*profile_id = 2;
	else
		*profile_id = 1;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

struct in_params {
	uint32_t frame_index;
	int32_t buf_count;
	uint32_t ring_io_block_size;
	int32_t performance_mode;
};

struct out_params {
	uint32_t frame_index;
	uint32_t bytes_consumed;
	int32_t error_code;
	uint32_t frame_type;
	uint32_t qp[(720 * 576) / 256];
	int32_t mb_error_buf_flag;
	uint8_t mb_error_buf[(720 * 576) / 256];
};

static void out_recv_cb(struct td_context *ctx, struct td_buffer *tb)
{
	struct out_params *param = tb->params->data;

	tb->keyframe = (param->frame_type == 0);

	pr_debug(ctx->client, "error: 0x%x, frame number: %u, frame type: %u",
			param->error_code, param->frame_index, param->frame_type);
}

static void setup_in_params(struct td_context *ctx, dmm_buffer_t *tmp)
{
	struct in_params *in_param;

	in_param = tmp->data;
	in_param->performance_mode = 0;
}

static void setup_params(struct td_context *ctx)
{
	struct in_params *in_param;
	struct out_params *out_param;
	struct td_port *p;

	p = ctx->ports[0];
	td_port_setup_params(ctx, p, sizeof(*in_param), setup_in_params);

	p = ctx->ports[1];
	td_port_setup_params(ctx, p, sizeof(*out_param), NULL);
	p->recv_cb = out_recv_cb;
}

struct td_codec td_mp4vdec_codec = {
	.uuid = &(const struct dsp_uuid) { 0x7e4b8541, 0x47a1, 0x11d6, 0xb1, 0x56,
		{ 0x00, 0xb0, 0xd0, 0x17, 0x67, 0x4b } },
	.filename = DSP_DIR "mp4vdec_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
};
