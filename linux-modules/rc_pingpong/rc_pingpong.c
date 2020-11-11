/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define _GNU_SOURCE

#include <rdma/ib_verbs.h>

#define CURRENT_NODE 0
#define COMPUTE21_LID 18
#define COMPUTE22_LID 19
#define IB_PORT 1
#define PSN 1
#define REMOTE_QPN 806

#define LID COMPUTE22_LID
#define PORT 1234
#define MTU 2048
#define RX_DEPTH 256
#define ITERS 1000
#define SIZE 4096

enum {
	PINGPONG_RECV_WRID = 1,
	PINGPONG_SEND_WRID = 2,
};

static int page_size;
static int validate_buf;

struct ib_device *ibdev;

struct pingpong_context {
	struct ib_context	*context;
	struct ib_comp_channel *channel;
	struct ib_pd		*pd;
	// struct ib_mr		*mr;
	union {
		struct ib_cq		*cq;
		struct ib_cq_ex	*cq_ex;
	} cq_s;
	struct ib_qp		*qp;
	char			*buf;
	int			 size;
	int			 send_flags;
	int			 rx_depth;
	int			 pending;
	uint64_t		 completion_timestamp_mask;
};

enum ib_mtu pp_mtu_to_enum(int mtu)
{
	switch (mtu) {
	case 256:  return IB_MTU_256;
	case 512:  return IB_MTU_512;
	case 1024: return IB_MTU_1024;
	case 2048: return IB_MTU_2048;
	case 4096: return IB_MTU_4096;
	default:   return 0;
	}
}

static bool check_port_status(struct ib_device *dev)
{
    const int start_port = rdma_start_port(dev);
    const int end_port = rdma_end_port(dev);
    struct ib_port_attr port_attr;
    int port;

    for (port = start_port; port <= end_port; ++port) {
        if (ib_query_port(dev, port, &port_attr) < 0) continue;
        if ((port_attr.lid) != 0 && (port_attr.state == IB_PORT_ACTIVE))
            return true;
    }
    return false;
}

static void ibv_add_one(struct ib_device *device)
{
    // TODO: the global variable here is not good.
    if (ibdev != NULL) return;

    if (!check_port_status(device)) return;

	ibdev = device;
    printk(KERN_DEBUG "ibdev=%p\n", (void*)ibdev);
	
	if (device == NULL)
		printk(KERN_CRIT "%s device NULL\n", __func__);

	return;
}

static void ibv_remove_one(struct ib_device *device, void *client_data)
{
	return;
}

static struct ib_cq *pp_cq(struct pingpong_context *ctx)
{
	return ctx->cq_s.cq;
}

struct pingpong_dest {
	int lid;
	int qpn;
	int psn;
	union ib_gid gid;
};

static int get_remote_lid(int my_lid)
{
    return (my_lid == COMPUTE21_LID) ? COMPUTE22_LID : COMPUTE21_LID;
}

static int pp_connect_ctx(struct pingpong_context *ctx, int port, int my_psn,
			  enum ib_mtu mtu, int sl,
			  struct pingpong_dest *dest, int sgid_idx)
{
    struct rdma_ah_attr* ah_attr;
	struct ib_qp_attr attr = {
		.qp_state		= IB_QPS_RTR,
		.path_mtu		= mtu,
		.dest_qp_num		= dest->qpn,
		.rq_psn			= dest->psn,
		.max_dest_rd_atomic	= 1,
		.min_rnr_timer		= 12,
	};
    ah_attr = &attr.ah_attr;
    ah_attr->type = rdma_ah_find_type((struct ib_device*)ctx->context, port);
    rdma_ah_set_sl(ah_attr, sl);
    rdma_ah_set_port_num(ah_attr, port);
    rdma_ah_set_dlid(ah_attr, dest->lid);
    rdma_ah_set_path_bits(ah_attr, 0);

	if (ib_modify_qp(ctx->qp, &attr,
			  IB_QP_STATE              |
			  IB_QP_AV                 |
			  IB_QP_PATH_MTU           |
			  IB_QP_DEST_QPN           |
			  IB_QP_RQ_PSN             |
			  IB_QP_MAX_DEST_RD_ATOMIC |
			  IB_QP_MIN_RNR_TIMER)) {
		printk(KERN_CRIT "Failed to modify QP to RTR\n");
		return 1;
	}

	attr.qp_state	    = IB_QPS_RTS;
	attr.timeout	    = 14;
	attr.retry_cnt	    = 7;
	attr.rnr_retry	    = 7;
	attr.sq_psn	    = my_psn;
	attr.max_rd_atomic  = 1;
	if (ib_modify_qp(ctx->qp, &attr,
			  IB_QP_STATE              |
			  IB_QP_TIMEOUT            |
			  IB_QP_RETRY_CNT          |
			  IB_QP_RNR_RETRY          |
			  IB_QP_SQ_PSN             |
			  IB_QP_MAX_QP_RD_ATOMIC)) {
		printk(KERN_CRIT "Failed to modify QP to RTS\n");
		return 1;
	}
    printk(KERN_DEBUG "port=%d, mtu=%d, sl=%d, qpn=%d\n", port, mtu, sl, dest->qpn);

	return 0;
}

static struct pingpong_dest *pp_client_exch_dest(const char *servername, int port,
						 const struct pingpong_dest *my_dest)
{
    int qpn = REMOTE_QPN;
	struct pingpong_dest *rem_dest = NULL;
	rem_dest = kmalloc(sizeof *rem_dest, GFP_KERNEL);
	if (!rem_dest)
		return NULL;

    printk(KERN_DEBUG "remote_qpn=%d\n", qpn);
    rem_dest->qpn = qpn;
    rem_dest->lid = get_remote_lid(my_dest->lid);
    rem_dest->psn = 1;
    return rem_dest;
}

static struct pingpong_dest *pp_server_exch_dest(struct pingpong_context *ctx,
						 int ib_port, enum ib_mtu mtu,
						 int port, int sl,
						 const struct pingpong_dest *my_dest,
						 int sgid_idx)
{
	struct pingpong_dest *rem_dest = NULL;
    int qpn = REMOTE_QPN;
    printk(KERN_DEBUG "remote_qpn=%d\n", qpn);

	rem_dest = kmalloc(sizeof *rem_dest, GFP_KERNEL);
	if (!rem_dest)
		return NULL;
    rem_dest->lid = get_remote_lid(my_dest->lid);
    rem_dest->psn = PSN;
    rem_dest->qpn = qpn;
	if (pp_connect_ctx(ctx, IB_PORT, my_dest->psn, mtu, sl, rem_dest,
								sgid_idx)) {
		printk(KERN_CRIT "Couldn't connect to remote QP\n");
        return NULL;
	}
    return rem_dest;
}

static struct pingpong_context *pp_init_ctx(struct ib_device *ib_dev, int size,
					    int rx_depth, int port)
{
	struct pingpong_context *ctx;
	int access_flags = IB_ACCESS_LOCAL_WRITE;

	ctx = kcalloc(1, sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->size       = size;
	ctx->send_flags = IB_SEND_SIGNALED;
	ctx->rx_depth   = rx_depth;

    // TODO: align?
	ctx->buf = kmalloc(size, GFP_KERNEL);
	if (!ctx->buf) {
		printk(KERN_CRIT "Couldn't allocate work buf.\n");
		goto clean_ctx;
	}

	/* FIXME memset(ctx->buf, 0, size); */
	memset(ctx->buf, 0x7b, size);

#if 1
    ctx->context = (struct ib_context *)ib_dev;
#else
	ctx->context = ib_open_device(ib_dev);
	if (!ctx->context) {
		printk(KERN_CRIT "Couldn't get context for %s\n",
			ib_get_device_name(ib_dev));
		goto clean_buffer;
	}
#endif

    // TODO: is NULL ok? (ref FIT)
#if 1
    ctx->channel = NULL;
#else
	ctx->channel = ib_create_comp_channel(ctx->context);
	if (!ctx->channel) {
		printk(KERN_CRIT "Couldn't create completion channel\n");
		goto clean_device;
	}
#endif

	ctx->pd = ib_alloc_pd(ibdev, 0);
	if (!ctx->pd) {
		printk(KERN_CRIT "Couldn't allocate PD\n");
		goto clean_buffer;
	}

#if 0
    ctx->mr = ib_reg_mr(ctx->pd, ctx->buf, size, access_flags);

	if (!ctx->mr) {
		printk(KERN_CRIT "Couldn't register MR\n");
		goto clean_pd;
	}
#endif

    struct ib_cq_init_attr cq_attr = {};
    cq_attr.cqe = rx_depth + 1;
    cq_attr.comp_vector = 0;
	ctx->cq_s.cq = ib_create_cq(ibdev, NULL, NULL, NULL, &cq_attr);

	if (!pp_cq(ctx)) {
		printk(KERN_CRIT "Couldn't create CQ\n");
		goto clean_pd;
	}

	{
		struct ib_qp_attr attr;
		struct ib_qp_init_attr init_attr = {
			.send_cq = pp_cq(ctx),
			.recv_cq = pp_cq(ctx),
			.cap     = {
				.max_send_wr  = 1,
				.max_recv_wr  = 256,//rx_depth,
				.max_send_sge = 8,//1,
				.max_recv_sge = 16//1
			},
			.qp_type = IB_QPT_RC
		};

        ctx->qp = ib_create_qp(ctx->pd, &init_attr);

		if (!ctx->qp)  {
			printk(KERN_CRIT "Couldn't create QP\n");
			goto clean_cq;
		}

		ib_query_qp(ctx->qp, &attr, IB_QP_CAP, &init_attr);
		if (init_attr.cap.max_inline_data >= size)
			ctx->send_flags |= IB_SEND_INLINE;
	}

	{
		struct ib_qp_attr attr = {
			.qp_state        = IB_QPS_INIT,
			.pkey_index      = 0,
			.port_num        = port,
			.qp_access_flags = 0
		};

		if (ib_modify_qp(ctx->qp, &attr,
				  IB_QP_STATE              |
				  IB_QP_PKEY_INDEX         |
				  IB_QP_PORT               |
				  IB_QP_ACCESS_FLAGS)) {
			printk(KERN_CRIT "Failed to modify QP to INIT\n");
			goto clean_qp;
		}
	}

	return ctx;

clean_qp:
	ib_destroy_qp(ctx->qp);

clean_cq:
	ib_destroy_cq(pp_cq(ctx));

clean_pd:
	ib_dealloc_pd(ctx->pd);

clean_buffer:
	kfree(ctx->buf);

clean_ctx:
	kfree(ctx);

	return NULL;
}

static int pp_close_ctx(struct pingpong_context *ctx)
{
	if (ib_destroy_qp(ctx->qp)) {
		printk(KERN_CRIT "Couldn't destroy QP\n");
		return 1;
	}

	if (ib_destroy_cq(pp_cq(ctx))) {
		printk(KERN_CRIT "Couldn't destroy CQ\n");
		return 1;
	}

	ib_dealloc_pd(ctx->pd);

	kfree(ctx->buf);
	kfree(ctx);

	return 0;
}

static int pp_post_recv(struct pingpong_context *ctx, int n)
{
    void* dma = ib_dma_map_single(ibdev, ctx->buf, ctx->size, DMA_FROM_DEVICE);
	struct ib_sge list = {
		.addr	= dma,
		.length = ctx->size,
		.lkey	= ctx->pd->local_dma_lkey,
	};
	struct ib_recv_wr wr = {
		.wr_id	    = PINGPONG_RECV_WRID,
		.sg_list    = &list,
		.num_sge    = 1,
	};
	struct ib_recv_wr *bad_wr;
	int i;

	for (i = 0; i < n; ++i)
        if (ib_post_recv(ctx->qp, &wr, (const struct ib_recv_wr **)&bad_wr))
			break;

	return i;
}

static int pp_post_send(struct pingpong_context *ctx)
{
    void* dma = ib_dma_map_single(ibdev, ctx->buf, ctx->size, DMA_FROM_DEVICE);
	struct ib_sge list = {
		.addr	= dma,
		.length = ctx->size,
		.lkey	= ctx->pd->local_dma_lkey,
	};
	struct ib_send_wr wr = {
		.wr_id	    = PINGPONG_SEND_WRID,
		.sg_list    = &list,
		.num_sge    = 1,
		.opcode     = IB_WR_SEND,
		.send_flags = ctx->send_flags,
	};
	struct ib_send_wr *bad_wr;

    printk(KERN_DEBUG "calling ib_post_send\n");
	return ib_post_send(ctx->qp, &wr, (const struct ib_send_wr **)&bad_wr);
}

struct ts_params {
	uint64_t		 comp_recv_max_time_delta;
	uint64_t		 comp_recv_min_time_delta;
	uint64_t		 comp_recv_total_time_delta;
	uint64_t		 comp_recv_prev_time;
	int			 last_comp_with_ts;
	unsigned int		 comp_with_time_iters;
};

static inline int parse_single_wc(struct pingpong_context *ctx, int *scnt,
				  int *rcnt, int *routs, int iters,
				  uint64_t wr_id, enum ib_wc_status status,
				  uint64_t completion_timestamp)
{
	if (status != IB_WC_SUCCESS) {
		printk(KERN_CRIT "Failed status %s (%d) for wr_id %d\n",
			ib_wc_status_msg(status),
			status, (int)wr_id);
		return 1;
	}

	switch ((int)wr_id) {
	case PINGPONG_SEND_WRID:
		++(*scnt);
		break;

	case PINGPONG_RECV_WRID:
		if (--(*routs) <= 1) {
			*routs += pp_post_recv(ctx, ctx->rx_depth - *routs);
			if (*routs < ctx->rx_depth) {
				printk(KERN_DEBUG "Couldn't post receive (%d)\n",
					*routs);
				return 1;
			}
		}

		++(*rcnt);

		break;

	default:
		printk(KERN_CRIT "Completion for unknown wr_id %d\n",
			(int)wr_id);
		return 1;
	}

	ctx->pending &= ~(int)wr_id;
	if (*scnt < iters && !ctx->pending) {
		if (pp_post_send(ctx)) {
			printk(KERN_CRIT "Couldn't post send\n");
			return 1;
		}
		ctx->pending = PINGPONG_RECV_WRID |
			PINGPONG_SEND_WRID;
	}

	return 0;
}

int main(void)
{
	struct pingpong_context *ctx;
	struct pingpong_dest     my_dest;
	struct pingpong_dest    *rem_dest;
	char                    *servername = "compute21";
	unsigned int             port = 18515;
	int                      ib_port = 1;
	unsigned int             size = 4096;
	enum ib_mtu		 mtu = IB_MTU_1024;
	unsigned int             rx_depth = 500;
	unsigned int             iters = 1000;
	int                      routs;
	int                      rcnt, scnt;
    int          gidx = -1;
    char             gid[33];
	int                      sl = 0;
    int i;

    port = PORT;
    mtu = pp_mtu_to_enum(MTU);
    rx_depth = RX_DEPTH;
    iters = ITERS;
    size = SIZE;

	page_size = PAGE_SIZE;
    
	ctx = pp_init_ctx(ibdev, size, rx_depth, ib_port);
	if (!ctx)
		return 1;

	routs = pp_post_recv(ctx, ctx->rx_depth);
	if (routs < ctx->rx_depth) {
		printk(KERN_CRIT "Couldn't post receive (%d)\n", routs);
		return 1;
	}

	my_dest.lid = LID;

    memset(&my_dest.gid, 0, sizeof my_dest.gid);

	my_dest.qpn = ctx->qp->qp_num;
	my_dest.psn = PSN;
	printk(KERN_DEBUG "  local address:  LID %d, QPN %d, PSN %d, GID %s, servername=%s\n",
	       my_dest.lid, my_dest.qpn, my_dest.psn, gid, servername);

	if (servername)
		rem_dest = pp_client_exch_dest(servername, port, &my_dest);
	else
		rem_dest = pp_server_exch_dest(ctx, ib_port, mtu, port, sl,
								&my_dest, gidx);

	if (!rem_dest)
		return 1;

	printk(KERN_DEBUG "  remote address: LID %d, QPN %d, PSN %d, GID %s, servername=%s\n",
	       rem_dest->lid, rem_dest->qpn, rem_dest->psn, gid, servername);

	if (servername)
		if (pp_connect_ctx(ctx, ib_port, my_dest.psn, mtu, sl, rem_dest,
					gidx))
			return 1;

	ctx->pending = PINGPONG_RECV_WRID;

	if (servername) {
		if (validate_buf)
			for (i = 0; i < size; i += page_size)
				ctx->buf[i] = i / page_size % sizeof(char);

		if (pp_post_send(ctx)) {
			printk(KERN_CRIT "Couldn't post send\n");
			return 1;
		}
		ctx->pending |= PINGPONG_SEND_WRID;
	}

	rcnt = scnt = 0;
	while (rcnt < iters || scnt < iters) {
		int ret;

		int ne, i;
		struct ib_wc wc[2];

		do {
			ne = ib_poll_cq(pp_cq(ctx), 2, wc);
			if (ne < 0) {
				printk(KERN_CRIT "poll CQ failed %d\n", ne);
				return 1;
			}
		} while (ne < 1);

		for (i = 0; i < ne; ++i) {
			ret = parse_single_wc(ctx, &scnt, &rcnt, &routs,
					      iters,
					      wc[i].wr_id,
					      wc[i].status,
					      0);
			if (ret) {
				printk(KERN_CRIT "parse WC failed %d\n", ne);
				return 1;
			}
		}
	}

	{
		if ((!servername) && (validate_buf)) {
			for (i = 0; i < size; i += page_size)
				if (ctx->buf[i] != i / page_size % sizeof(char))
					printk(KERN_DEBUG "invalid data in page %d\n",
					       i / page_size);
		}
	}

//	ib_ack_cq_events(pp_cq(ctx), num_cq_events);

	if (pp_close_ctx(ctx))
		return 1;

	kfree(rem_dest);

	return 0;
}

static struct ib_client ibv_client = {
	.name   = "ibv_server",
	.add    = ibv_add_one,
	.remove = ibv_remove_one,
};

static int rc_pingpong_init(void)
{
	int ret;

	printk(KERN_CRIT "%s\n", __func__);

	ret = ib_register_client(&ibv_client);
	if (ret) {
		pr_err("couldn't register IB client\n");
		return ret;
	}

    main();
	return 0;
}

static void rc_pingpong_cleanup(void)
{
	pr_info("Removing rc_pingpong module\n");
	ib_unregister_client(&ibv_client);
    return;
}

module_init(rc_pingpong_init);
module_exit(rc_pingpong_cleanup);
MODULE_LICENSE("GPL");
