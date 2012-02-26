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
#include "dmm_buffer.h"
#include "log.h"
#include "util.h"

#include <errno.h>

struct td_context;
struct td_port;
struct td_buffer;

/* mpu <-> dsp communication structure */
struct usn_comm {
	uintptr_t buffer_data;
	uint32_t buffer_size;
	uintptr_t param_data;
	uint32_t param_size;
	uint32_t buffer_len;
	uint32_t silly_eos;
	uint32_t silly_buf_state;
	uint32_t silly_buf_active;
	uint32_t silly_buf_id;
#if SN_API >= 2
	uint32_t nb_available_buf;
	uint32_t donot_flush_buf;
	uint32_t donot_invalidate_buf;
#endif
	uint32_t reserved;
	uintptr_t msg_virt;
	uintptr_t buffer_virt;
	uintptr_t param_virt;
	uint32_t silly_out_buffer_index;
	uint32_t silly_in_buffer_index;
	uintptr_t user_data;
	uint32_t stream_id;
};

typedef struct usn_comm usn_comm_t;

struct td_port *td_port_new(int id, int dir)
{
	struct td_port *p;
	p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;

	p->id = id;
	p->dir = dir;

	return p;
}

void td_port_free(struct td_port *p)
{
	if (!p)
		return;

	free(p->buffers);
	free(p);
}

void td_port_alloc_buffers(struct td_port *p, unsigned nr_buffers)
{
	p->nr_buffers = nr_buffers;
	free(p->buffers);
	p->buffers = calloc(nr_buffers, sizeof(*p->buffers));
	for (unsigned i = 0; i < p->nr_buffers; i++)
		p->buffers[i].port = p;
}

void td_port_flush(struct td_port *p)
{
	unsigned i;
	struct td_buffer *tb = p->buffers;

	for (i = 0; i < p->nr_buffers; i++, tb++) {
		dmm_buffer_t *b = tb->data;
		if (!b)
			continue;
		dmm_buffer_free(b);
		tb->data = NULL;
	}
}

void td_port_setup_params(struct td_context *ctx,
		struct td_port *p,
		size_t size,
		td_setup_params_func func)
{
	unsigned i;
	for (i = 0; i < p->nr_buffers; i++) {
		dmm_buffer_t *b;
		b = dmm_buffer_calloc(ctx->dsp_handle,
				ctx->proc, size, DMA_BIDIRECTIONAL);
		if (func)
			func(ctx, b);
		dmm_buffer_map(b);
		p->buffers[i].params = b;
	}
}

struct td_context *td_new(void *client)
{
	struct td_context *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		pr_err(ctx->client, "failed to allocate");
		return NULL;
	}

	ctx->client = client;

	ctx->ports[0] = td_port_new(0, DMA_TO_DEVICE);
	ctx->ports[1] = td_port_new(1, DMA_FROM_DEVICE);

	return ctx;
}

void td_free(struct td_context *ctx)
{
	if (!ctx)
		return;

	td_port_free(ctx->ports[1]);
	td_port_free(ctx->ports[0]);

	free(ctx);
}

bool td_send_buffer(struct td_context *ctx, struct td_buffer *tb)
{
	usn_comm_t *msg_data;
	struct td_port *port = tb->port;
	int index = port->id;
	dmm_buffer_t *buffer = tb->data;

	pr_debug(ctx->client, "sending %s buffer", index == 0 ? "input" : "output");

	tb->used = true;

	msg_data = tb->comm->data;

	if (port->send_cb)
		port->send_cb(ctx, tb);

	if (tb->params)
		dmm_buffer_begin(tb->params, tb->params->size);

	if (tb->pinned) {
		if (likely(!tb->clean))
			dmm_buffer_begin(buffer, buffer->len);
		else
			tb->clean = false;
	} else {
		dmm_buffer_map(buffer);
	}

	memset(msg_data, 0, sizeof(*msg_data));

	msg_data->buffer_data = (uintptr_t) buffer->map;
	msg_data->buffer_size = buffer->size;
	msg_data->stream_id = port->id;
	msg_data->buffer_len = index == 0 ? buffer->len : 0;

	msg_data->user_data = (uintptr_t) buffer;

	if (tb->params) {
		msg_data->param_data = (uintptr_t) tb->params->map;
		msg_data->param_size = tb->params->len;
		msg_data->param_virt = (uintptr_t) tb->params;
	}

	dmm_buffer_begin(tb->comm, sizeof(*msg_data));

	dsp_send_message(ctx->dsp_handle, ctx->node,
			0x0600 | port->id, (uintptr_t) tb->comm->map, 0);

	return true;
}

static void *vdec_create_node(struct td_context *ctx)
{
	struct td_codec *codec;
	int dsp_handle;
	struct dsp_node *node;

	const struct dsp_uuid usn_uuid = { 0x79A3C8B3, 0x95F2, 0x403F, 0x9A, 0x4B,
		{ 0xCF, 0x80, 0x57, 0x73, 0x05, 0x41 } };

	const struct dsp_uuid ringio_uuid = { 0x47698bfb, 0xa7ee, 0x417e, 0xa6, 0x7a,
		{ 0x41, 0xc0, 0x27, 0x9e, 0xb8, 0x05 } };

	const struct dsp_uuid conversions_uuid = { 0x722DD0DA, 0xF532, 0x4238, 0xB8, 0x46,
		{ 0xAB, 0xFF, 0x5D, 0xA4, 0xBA, 0x02 } };

	dsp_handle = ctx->dsp_handle;

	if (!dsp_register(dsp_handle, &ringio_uuid, DSP_DCD_LIBRARYTYPE, DSP_DIR "ringio.dll64P")) {
		pr_err(ctx->client, "failed to register ringio node library");
		return NULL;
	}

	if (!dsp_register(dsp_handle, &usn_uuid, DSP_DCD_LIBRARYTYPE, DSP_DIR "usn.dll64P")) {
		pr_err(ctx->client, "failed to register usn node library");
		return NULL;
	}

	codec = ctx->codec;
	if (!codec) {
		pr_err(ctx->client, "unknown algorithm");
		return NULL;
	}

	pr_info(ctx->client, "algo=%s", codec->filename);

	/* SN_API == 0 doesn't have it, so don't fail */
	(void) dsp_register(dsp_handle, &conversions_uuid, DSP_DCD_LIBRARYTYPE, DSP_DIR "conversions.dll64P");

	if (!dsp_register(dsp_handle, codec->uuid, DSP_DCD_LIBRARYTYPE, codec->filename)) {
		pr_err(ctx->client, "failed to register algo node library");
		return NULL;
	}

	if (!dsp_register(dsp_handle, codec->uuid, DSP_DCD_NODETYPE, codec->filename)) {
		pr_err(ctx->client, "failed to register algo node");
		return NULL;
	}

	{
		struct dsp_node_attr_in attrs = {
			.cb = sizeof(attrs),
			.priority = 5,
			.timeout = 1000,
		};
		void *arg_data;

		codec->create_args(ctx, &attrs.profile_id, &arg_data);

		if (!arg_data)
			return NULL;

		if (!dsp_node_allocate(dsp_handle, ctx->proc, codec->uuid, arg_data, &attrs, &node)) {
			pr_err(ctx->client, "dsp node allocate failed");
			free(arg_data);
			return NULL;
		}
		free(arg_data);
	}

	if (!dsp_node_create(dsp_handle, node)) {
		pr_err(ctx->client, "dsp node create failed");
		dsp_node_free(dsp_handle, node);
		return NULL;
	}

	pr_info(ctx->client, "dsp node created");

	if (codec->setup_params)
		codec->setup_params(ctx);

	if (codec->send_params)
		codec->send_params(ctx, node);

	return node;
}

static inline void setup_buffers(struct td_context *ctx)
{
	dmm_buffer_t *b;
	struct td_port *p;
	unsigned i;

	p = ctx->ports[0];
	for (i = 0; i < p->nr_buffers; i++) {
		p->buffers[i].data = b = dmm_buffer_new(ctx->dsp_handle, ctx->proc, p->dir);
		dmm_buffer_allocate(b, ctx->output_buffer_size);
	}

	p = ctx->ports[1];
	for (i = 0; i < p->nr_buffers; i++) {
		struct td_buffer *tb = &p->buffers[i];
		tb->data = b = dmm_buffer_new(ctx->dsp_handle, ctx->proc, p->dir);
		dmm_buffer_allocate(b, ctx->output_buffer_size);
		td_send_buffer(ctx, tb);
	}
}

static bool send_play_message(struct td_context *ctx)
{
	return dsp_send_message(ctx->dsp_handle, ctx->node, 0x0100, 0, 0);
};

static bool _dsp_start(struct td_context *ctx)
{
	bool ret = true;
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(ctx->ports); i++) {
		struct td_port *p = ctx->ports[i];
		unsigned j;
		for (j = 0; j < p->nr_buffers; j++) {
			struct td_buffer *tb = &p->buffers[j];
			tb->comm = dmm_buffer_new(ctx->dsp_handle, ctx->proc, DMA_BIDIRECTIONAL);
			dmm_buffer_allocate(tb->comm, sizeof(*tb->comm));
			dmm_buffer_map(tb->comm);
		}
	}

	if (!dsp_node_run(ctx->dsp_handle, ctx->node)) {
		pr_err(ctx->client, "dsp node run failed");
		return false;
	}

	pr_info(ctx->client, "dsp node running");

	ctx->events[0] = calloc(1, sizeof(struct dsp_notification));
	if (!dsp_node_register_notify(ctx->dsp_handle, ctx->node,
				DSP_NODEMESSAGEREADY, 1,
				ctx->events[0]))
	{
		pr_err(ctx->client, "failed to register for notifications");
		return false;
	}

	ctx->events[1] = calloc(1, sizeof(struct dsp_notification));
	if (!dsp_register_notify(ctx->dsp_handle, ctx->proc,
				DSP_MMUFAULT, 1,
				ctx->events[1]))
	{
		pr_err(ctx->client, "failed to register for DSP_MMUFAULT");
		return false;
	}

	ctx->events[2] = calloc(1, sizeof(struct dsp_notification));
	if (!dsp_register_notify(ctx->dsp_handle, ctx->proc,
				DSP_SYSERROR, 1,
				ctx->events[2]))
	{
		pr_err(ctx->client, "failed to register for DSP_SYSERROR");
		return false;
	}

	pr_info(ctx->client, "creating dsp thread");

	ctx->send_play_message(ctx);

	setup_buffers(ctx);

	return ret;
}

static inline bool init_node(struct td_context *ctx)
{
	ctx->output_buffer_size = ctx->width * ctx->height * 3 / 2;
	if (!ctx->output_buffer_size)
		return false;

	ctx->node = ctx->create_node(ctx);
	if (!ctx->node) {
		pr_err(ctx->client, "dsp node creation failed");
		return false;
	}

	if (!_dsp_start(ctx)) {
		pr_err(ctx->client, "dsp start failed");
		return false;
	}

	return true;
}

bool td_init(struct td_context *ctx)
{
	int dsp_handle;

	ctx->create_node = vdec_create_node;
	ctx->send_play_message = send_play_message;
	ctx->color_format = td_fourcc('I', '4', '2', '0');

	ctx->dsp_handle = dsp_handle = dsp_open();

	if (dsp_handle < 0) {
		pr_err(ctx->client, "dsp open failed");
		return false;
	}

	if (!dsp_attach(dsp_handle, 0, NULL, &ctx->proc)) {
		pr_err(ctx->client, "dsp attach failed");
		goto fail;
	}

	td_port_alloc_buffers(ctx->ports[0], 2);
	td_port_alloc_buffers(ctx->ports[1], 2);

	if (!init_node(ctx)) {
		pr_err(ctx->client, "dsp node init failed");
		goto fail;
	}

	return true;

fail:
	if (ctx->proc) {
		if (!dsp_detach(dsp_handle, ctx->proc))
			pr_err(ctx->client, "dsp detach failed");
		ctx->proc = NULL;
	}

	if (ctx->dsp_handle >= 0) {
		if (dsp_close(dsp_handle) < 0)
			pr_err(ctx->client, "dsp close failed");
		ctx->dsp_handle = -1;
	}

	return false;
}

static inline bool destroy_node(struct td_context *ctx)
{
	if (ctx->node) {
		if (!dsp_node_free(ctx->dsp_handle, ctx->node)) {
			pr_err(ctx->client, "dsp node free failed");
			return false;
		}

		pr_info(ctx->client, "dsp node deleted");
	}

	return true;
}

static bool _dsp_stop(struct td_context *ctx)
{
	unsigned long exit_status;
	unsigned i;

	if (!ctx->node)
		return true;

	for (i = 0; i < ARRAY_SIZE(ctx->ports); i++)
		td_port_flush(ctx->ports[i]);

	dsp_send_message(ctx->dsp_handle, ctx->node, 0x0200, 0, 0);

	for (i = 0; i < ARRAY_SIZE(ctx->ports); i++) {
		unsigned j;
		struct td_port *port = ctx->ports[i];
		for (j = 0; j < port->nr_buffers; j++) {
			dmm_buffer_free(port->buffers[j].params);
			port->buffers[j].params = NULL;
		}
	}

	for (i = 0; i < ARRAY_SIZE(ctx->events); i++) {
		free(ctx->events[i]);
		ctx->events[i] = NULL;
	}

	if (ctx->alg_ctrl) {
		dmm_buffer_free(ctx->alg_ctrl);
		ctx->alg_ctrl = NULL;
	}

	if (ctx->dsp_error)
		goto leave;

	if (!dsp_node_terminate(ctx->dsp_handle, ctx->node, &exit_status))
		pr_err(ctx->client, "dsp node terminate failed: 0x%lx", exit_status);

leave:
	if (!destroy_node(ctx))
		pr_err(ctx->client, "dsp node destroy failed");

	ctx->node = NULL;

	for (i = 0; i < ARRAY_SIZE(ctx->ports); i++) {
		struct td_port *p = ctx->ports[i];
		unsigned j;
		for (j = 0; j < p->nr_buffers; j++) {
			dmm_buffer_free(p->buffers[j].comm);
			p->buffers[j].comm = NULL;
		}
		td_port_alloc_buffers(p, 0);
	}

	pr_info(ctx->client, "dsp node terminated");

	return true;
}

bool td_close(struct td_context *ctx)
{
	bool ret = true;

	_dsp_stop(ctx);

	if (ctx->dsp_error)
		goto leave;

	if (ctx->proc) {
		if (!dsp_detach(ctx->dsp_handle, ctx->proc)) {
			pr_err(ctx->client, "dsp detach failed");
			ret = false;
		}
		ctx->proc = NULL;
	}

leave:

	if (ctx->dsp_handle >= 0) {
		if (dsp_close(ctx->dsp_handle) < 0) {
			pr_err(ctx->client, "dsp close failed");
			ret = false;
		}
		ctx->dsp_handle = -1;
	}

	return ret;
}

static void td_got_error(struct td_context *ctx, unsigned id, const char *message)
{
	pr_err(ctx->client, "%s", message);
	ctx->dsp_error = id;
}

static inline void got_message(struct td_context *ctx, struct dsp_msg *msg)
{
	uint32_t id;
	uint32_t command_id;

	id = msg->cmd & 0x000000ff;
	command_id = msg->cmd & 0xffffff00;

	switch (command_id) {
	case 0x0600: {
		dmm_buffer_t *b;
		struct td_port *p;
		usn_comm_t *msg_data;
		dmm_buffer_t *param;
		unsigned i;
		struct td_buffer *tb = NULL;

		for (i = 0; i < ARRAY_SIZE(ctx->ports); i++)
			if (ctx->ports[i]->id == id) {
				p = ctx->ports[i];
				break;
			}

		BUG_ON(i >= ARRAY_SIZE(ctx->ports), ctx->client, "bad port index: %i", id);

		pr_debug(ctx->client, "got %s buffer", id == 0 ? "input" : "output");

		for (i = 0; i < p->nr_buffers; i++) {
			if (msg->arg_1 == (uintptr_t) p->buffers[i].comm->map) {
				tb = &p->buffers[i];
				break;
			}
		}

		BUG_ON(!tb, ctx->client, "buffer mismatch");

		dmm_buffer_end(tb->comm, tb->comm->size);

		msg_data = tb->comm->data;
		b = (void *) msg_data->user_data;
		b->len = msg_data->buffer_len;

		BUG_ON(b->len > b->size, ctx->client, "wrong buffer size");

		if (tb->pinned)
			dmm_buffer_end(b, b->len);
		else
			dmm_buffer_unmap(b);

		param = (void *) msg_data->param_virt;
		if (param)
			dmm_buffer_end(param, param->size);

		if (p->recv_cb)
			p->recv_cb(ctx, tb);

		tb->used = false;

		if (ctx->handle_buffer)
			ctx->handle_buffer(ctx, tb);
		break;
	}
	case 0x0500:
		pr_debug(ctx->client, "got flush");
		break;
	case 0x0200:
		pr_debug(ctx->client, "got stop");
		break;
	case 0x0400:
		pr_debug(ctx->client, "got alg ctrl");
		dmm_buffer_free(ctx->alg_ctrl);
		ctx->alg_ctrl = NULL;
		break;
	case 0x0e00:
		if (msg->arg_1 == 1 && msg->arg_2 == 0x0500) {
			pr_debug(ctx->client, "playback completed");
			break;
		}

		if (msg->arg_1 == 1 && (msg->arg_2 & 0x0600) == 0x0600) {
			struct td_codec *codec = ctx->codec;
			if (codec->update_params)
				codec->update_params(ctx, ctx->node, msg->arg_2);
			break;
		}

		pr_warning(ctx->client, "DSP event: cmd=0x%04X, arg1=%u, arg2=0x%04X",
			   msg->cmd, msg->arg_1, msg->arg_2);
		if ((msg->arg_2 & 0x0F00) == 0x0F00)
			td_got_error(ctx, 0, "algo error");
		break;
	default:
		pr_warning(ctx->client, "unhandled command: %u", command_id);
	}
}

bool td_get_event(struct td_context *ctx)
{
	unsigned index = 0;

	pr_debug(ctx->client, "waiting for events");

	if (!dsp_wait_for_events(ctx->dsp_handle, ctx->events, 3, &index, 100)) {
		if (errno == ETIME) {
			pr_warning(ctx->client, "timed out waiting for events\n");
			return false;
		}
		pr_err(ctx->client, "failed waiting for events: %i", errno);
		return true;
	}

	if (index == 0) {
		struct dsp_msg msg;
		while (true) {
			if (!dsp_node_get_message(ctx->dsp_handle, ctx->node, &msg, 0))
				break;
			pr_debug(ctx->client, "got dsp message: 0x%0x 0x%0x 0x%0x",
					msg.cmd, msg.arg_1, msg.arg_2);
			got_message(ctx, &msg);
		}
	}

	return true;
}
