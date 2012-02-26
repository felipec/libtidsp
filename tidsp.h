/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef TIDSP_H
#define TIDSP_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

struct td_context;
struct td_buffer;
struct td_port;

struct dmm_buffer;

struct dsp_node;
struct dsp_notification;

struct td_buffer {
	struct td_port *port;
	struct dmm_buffer *data;
	struct dmm_buffer *comm;
	struct dmm_buffer *params;
	void *user_data;
	bool keyframe;
	bool pinned;
	bool clean;
	bool used;
};

typedef void (*td_port_cb_t) (struct td_context *ctx, struct td_buffer *tb);

struct td_port {
	unsigned id;
	int dir;
	struct td_buffer *buffers;
	unsigned nr_buffers;
	td_port_cb_t send_cb;
	td_port_cb_t recv_cb;
};

struct td_codec {
	const struct dsp_uuid *uuid;
	const char *filename;
	void (*setup_params)(struct td_context *ctx);
	void (*create_args)(struct td_context *ctx, unsigned *profile_id, void **arg_data);
	bool (*handle_extra_data)(struct td_context *ctx, void *buf);
	void (*flush_buffer)(struct td_context *ctx);
	void (*send_params)(struct td_context *ctx, struct dsp_node *node);
	void (*update_params)(struct td_context *ctx, struct dsp_node *node, uint32_t msg);
	unsigned (*get_latency)(struct td_context *ctx, unsigned frame_duration);
};

struct td_context {
	void *client;
	int dsp_handle;
	void *proc;
	struct dsp_node *node;
	struct td_codec *codec;
	struct td_port *ports[2];
	struct dsp_notification *events[3];
	struct dmm_buffer *alg_ctrl;

	int width, height;
	int crop_width, crop_height;
	unsigned color_format;
	size_t output_buffer_size;
	unsigned dsp_error;

	void *(*create_node)(struct td_context *ctx);
	bool (*send_play_message)(struct td_context *ctx);
	void (*handle_buffer) (struct td_context *ctx, struct td_buffer *b);
};

struct td_port *td_port_new(int id, int dir);
void td_port_free(struct td_port *p);
void td_port_alloc_buffers(struct td_port *p, unsigned nr_buffers);
void td_port_flush(struct td_port *p);

struct td_context *td_new(void *client);
void td_free(struct td_context *ctx);
bool td_send_buffer(struct td_context *ctx, struct td_buffer *tb);

bool td_init(struct td_context *ctx);
bool td_close(struct td_context *ctx);
bool td_get_event(struct td_context *ctx);

typedef void (*td_setup_params_func)(struct td_context *ctx, struct dmm_buffer *mb);

void td_port_setup_params(struct td_context *ctx, struct td_port *p, size_t size,
		td_setup_params_func func);

extern struct td_codec td_mp4vdec_codec;

#define td_fourcc(a, b, c, d) \
	((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#endif
