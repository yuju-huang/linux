/*
 * Copyright (c) 2016-2020 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <rdma/ib_verbs.h>
//#include <linux/fit_ibapi.h>
#include <linux/completion.h>
#include "fit.h"
#include "fit_internal.h"

MODULE_AUTHOR("yiying");

#define HANDLER_LENGTH 0
#define HANDLER_INTERARRIVAL 0

//#define DEBUG_IBV
#ifdef TEST_PRINTK
#define test_printk(x...)	pr_crit(x)
#else
#define test_printk(x...)	do {} while (0)
#endif

#define SRCADDR INADDR_ANY
#define DSTADDR ((unsigned long int)0xc0a87b01) /* 192.168.123.1 */

int num_parallel_connection = NUM_PARALLEL_CONNECTION;
atomic_t global_reqid;
struct lego_context *FIT_ctx;
int curr_node;
struct ib_device *ibapi_dev;
struct ib_pd *ctx_pd;

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
    if (ibapi_dev != NULL) return;

    if (!check_port_status(device)) return;

	ibapi_dev = device;
	
	if (device == NULL)
		printk(KERN_CRIT "%s device NULL\n", __func__);

	ctx_pd = ib_alloc_pd(device, 0);
	if (!ctx_pd) {
		printk(KERN_ALERT "Couldn't allocate PD\n");
	}

	return;
}

static void ibv_remove_one(struct ib_device *device, void *client_data)
{
	return;
}

inline int ibapi_send_reply_imm(int target_node, void *addr, int size, void *ret_addr, int max_ret_size, int if_use_ret_phys_addr)
{
	struct lego_context *ctx = FIT_ctx;
	int ret;
	ret = fit_send_reply_with_rdma_write_with_imm(ctx, target_node, addr, size, ret_addr, max_ret_size, 0, if_use_ret_phys_addr);
	return ret;
}
EXPORT_SYMBOL(ibapi_send_reply_imm);

#if 0
int ibapi_register_application(unsigned int designed_port, unsigned int max_size_per_message, unsigned int max_user_per_node, char *name, uint64_t name_len)
{
	struct lego_context *ctx = FIT_ctx;
	return fit_register_application(ctx, designed_port, max_size_per_message, max_user_per_node, name, name_len);
}

int ibapi_unregister_application(unsigned int designed_port)
{
	struct lego_context *ctx = FIT_ctx;
	return fit_unregister_application(ctx, designed_port);
}

int ibapi_query_port(int target_node, int designed_port, int requery_flag)
{	
	struct lego_context *ctx = FIT_ctx;
	return fit_query_port(ctx, target_node, designed_port, requery_flag);
}
#endif

inline int ibapi_receive_message(unsigned int designed_port, void *ret_addr, int receive_size, uintptr_t *descriptor)
{
	struct lego_context *ctx = FIT_ctx;
	//printk("Calling ibapi_receive_message\n");
	return fit_receive_message(ctx, designed_port, ret_addr, receive_size, descriptor, 0);
}
EXPORT_SYMBOL(ibapi_receive_message);

inline int ibapi_reply_message(void *addr, int size, uintptr_t descriptor)
{
	struct lego_context *ctx = FIT_ctx;
	//printk("Calling ibapi_reply_message\n");
	return fit_reply_message(ctx, addr, size, descriptor, 0);
}
EXPORT_SYMBOL(ibapi_reply_message);

#if 0
uint64_t ibapi_dist_barrier(unsigned int check_num)
{
	int i;
	struct lego_context *ctx = FIT_ctx;
	int source = ctx->node_id;
	int num_alive_nodes = atomic_read(&ctx->num_alive_nodes);
	uintptr_t tempaddr;
	int priority = LOW_PRIORITY;
	//int connection_id;
	atomic_inc(&ctx->dist_barrier_counter);
	for(i=1;i<=num_alive_nodes;i++)//skip CD
	{
		if(i==ctx->node_id)
			continue;
		tempaddr = fit_ib_reg_mr_addr(ctx, &source, sizeof(int));
		fit_send_message_sge_UD(ctx, i, MSG_DIST_BARRIER, (void *)tempaddr, sizeof(int), 0, 0, priority);
	}
	//while(atomic_read(&ctx->dist_barrier_counter)<num_alive_nodes)
	while(atomic_read(&ctx->dist_barrier_counter)<check_num)
	{
		schedule();
	}
	atomic_sub(check_num, &ctx->dist_barrier_counter);
	return 0;
}
#endif

void ibapi_free_recv_buf(void *input_buf)
{
	//printk(KERN_CRIT "IB freeing post_receive_cache vaddr %p\n", input_buf);
	//kmem_cache_free(post_receive_cache, input_buf);
	//fit_free_recv_buf(input_buf);
	//kmem_cache_free(post_receive_cache, input_buf);
}

#if 0
int ibapi_reg_send_handler(int (*input_funptr)(char *addr, uint32_t size, int sender_id))
{
	struct lego_context *ctx = FIT_ctx;
	ctx->send_handler = input_funptr;
	return 0;
}

int ibapi_reg_send_reply_handler(int (*input_funptr)(char *input_addr, uint32_t input_size, char *output_addr, uint32_t *output_size, int sender_id))
{
	struct lego_context *ctx = FIT_ctx;
	ctx->send_reply_handler = input_funptr;
	return 0;
}

int ibapi_reg_send_reply_opt_handler(int (*input_funptr)(char *input_addr, uint32_t input_size, void **output_addr, uint32_t *output_size, int sender_id))
{
	struct lego_context *ctx = FIT_ctx;
	ctx->send_reply_opt_handler = input_funptr;
	return 0;
}

int ibapi_reg_send_reply_rdma_imm_handler(int (*input_funptr)(int sender_id, void *msg, uint32_t size, uint32_t inbox_addr, uint32_t inbox_rkey, uint32_t inbox_semaphore))
{
	struct lego_context *ctx = FIT_ctx;
	ctx->send_reply_rdma_imm_handler = input_funptr;
	return 0;
}
#endif

int ibapi_num_connected_nodes(void)
{
	if(!FIT_ctx)
	{	
		printk(KERN_CRIT "%s: using FIT ctx directly since ctx is NULL\n", __func__);
		return atomic_read(&FIT_ctx->num_alive_nodes);
	}
	return atomic_read(&FIT_ctx->num_alive_nodes);
}

int ibapi_get_node_id(void)
{
	struct lego_context *ctx;
	if(FIT_ctx)
	{
		ctx = FIT_ctx;
		return ctx->node_id;
	}
	return 0;
}

int ibapi_establish_conn(int ib_port, int mynodeid)
{
	struct lego_context *ctx;
	
	//printk(KERN_CRIT "Start calling rc_internal to create FIT based on %p\n", ibapi_dev);

	if (!ibapi_dev) {
		printk(KERN_CRIT "ERROR: %s uninitilized ibapi_dev\n)", __func__);
		return -1;
	}

	ctx = fit_establish_conn(ibapi_dev, ib_port, mynodeid);
	
	if(!ctx)
	{
		printk(KERN_ALERT "%s: ctx %p fail to init_interface \n", __func__, (void *)ctx);
		return -1;
	}

	FIT_ctx = ctx;

	printk(KERN_CRIT "FIT layer done with all initialization on node %d. Ready to go!\n", ctx->node_id);

	return ctx->node_id;
}

static struct ib_client ibv_client = {
	.name   = "ibv_server",
	.add    = ibv_add_one,
	.remove = ibv_remove_one
};

static void lego_ib_test(void)
{
	int ret, i;
	char *buf = kmalloc(4096, GFP_KERNEL);
	char *retb = kmalloc(4096, GFP_KERNEL);
	uintptr_t desc;

    printk(KERN_CRIT "lego_ib_test\n");
	if (CONFIG_FIT_LOCAL_ID == 0) {
		for (i = 0; i < 10; i++) {
			ret = ibapi_receive_message(0, buf, 4096, &desc);
			pr_info("received message ret %d msg [%s]\n", ret, buf);
			if (ret == SEND_REPLY_SIZE_TOO_BIG) {
				printk(KERN_CRIT "received msg wrong size %d\n", ret);
				return;
			}
			retb[0] = '1';
			retb[1] = '2';
			retb[2] = '\0';
			ret = ibapi_reply_message(retb, 10, desc);
		}
	} else {
		buf[0] = 'a';
		buf[1] = 'b';
		buf[2] = '\0';
		for (i = 0; i < 10; i++) {
			ret = ibapi_send_reply_imm(0, buf, 4096, retb, 4096, 0);
			pr_info("%s(%2d) retbuffer: %s\n", __func__, i, retb);
		}
	}
}

static int my_test(void)
{
    int i;
    const size_t page_size = 4096;
    const size_t size = 4096;
    const int connection_id = 0;
	char *buf = kmalloc(size, GFP_KERNEL);
    if (!buf) {
        printk(KERN_CRIT "alloc buf fails\n");
        return 1;
    }
	for (i = 0; i < size; i += page_size)
		buf[i] = i / page_size % sizeof(char);

    void* dma = ib_dma_map_single(ibapi_dev, buf, size, DMA_FROM_DEVICE);
	struct ib_sge list = {
		.addr	= dma,
		.length = size,
		.lkey	= FIT_ctx->pd->local_dma_lkey,
	};
	struct ib_send_wr wr = {
		.wr_id	    = 2,
		.sg_list    = &list,
		.num_sge    = 1,
		.opcode     = IB_WR_SEND,
		.send_flags = IB_SEND_SIGNALED,
	};
	struct ib_send_wr *bad_wr;

    printk(KERN_DEBUG "calling ib_post_send\n");
	return ib_post_send(FIT_ctx->qp[connection_id], &wr, (const struct ib_send_wr **)&bad_wr);

#if 0
    void* dma = ib_dma_map_single(ibapi_dev, buf, size, DMA_FROM_DEVICE);

	struct ib_sge sg;
    memset(&sg, 0, sizeof(sg));
    sg.addr	= dma;
	sg.length = size;
	sg.lkey	= FIT_ctx->pd->local_dma_lkey;

	struct ib_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id	  = 2;
	wr.sg_list    = &sg;
	wr.num_sge    = 1;
	wr.opcode     = IB_WR_SEND;
	wr.send_flags = IB_SEND_SIGNALED;

    int myflag;
    uint16_t pkey;
    struct ib_qp_attr attr2;
    struct ib_qp_init_attr init_attr;
    struct rdma_ah_attr* ah;

    myflag = IB_QP_STATE | IB_QP_CUR_STATE | IB_QP_EN_SQD_ASYNC_NOTIFY | IB_QP_ACCESS_FLAGS | IB_QP_PKEY_INDEX |
             IB_QP_PORT | IB_QP_QKEY | IB_QP_AV | IB_QP_PATH_MTU | IB_QP_TIMEOUT |
             IB_QP_RETRY_CNT | IB_QP_RNR_RETRY | IB_QP_RQ_PSN | IB_QP_MAX_QP_RD_ATOMIC |
             IB_QP_ALT_PATH | IB_QP_MIN_RNR_TIMER | IB_QP_SQ_PSN | IB_QP_MAX_DEST_RD_ATOMIC |
             IB_QP_MAX_DEST_RD_ATOMIC | IB_QP_CAP | IB_QP_DEST_QPN;
    ib_query_qp(FIT_ctx->qp[connection_id], &attr2, myflag, &init_attr);
    ib_query_pkey(ibapi_dev, attr2.port_num, attr2.pkey_index, &pkey);
    ah = &attr2.ah_attr;
                                                            
    printk(KERN_CRIT "qpn=%d, dest_qp_num=%d, qp_state=%d, cur_state=%d, en_sqd_async_notify=%d, access_flags=%d, pkey_idx=%d, pkey=%d, port=%d, qkey=%d, mtu=%d, timeout=%d, retry_cnt=%d, rnr_retry=%d, rq_psn=%d, max_qp_rd_atomic=%d, min_rnr_timer=%d, sq_psn=%d, max_dest_rd_atomic=%d, path_mig_state=%d\n", FIT_ctx->qp[connection_id]->qp_num, attr2.dest_qp_num, attr2.qp_state, attr2.cur_qp_state, attr2.en_sqd_async_notify, attr2.qp_access_flags, attr2.pkey_index, pkey, attr2.port_num, attr2.qkey, attr2.path_mtu, attr2.timeout, attr2.retry_cnt, attr2.rnr_retry, attr2.rq_psn, attr2.max_rd_atomic, attr2.sq_psn, attr2.max_dest_rd_atomic, attr2.path_mig_state);
    printk(KERN_CRIT "sl=%d, static_rate=%d, port_num=%d, ah_flags=%d, ah_type=%d, dlid=%d, src_path_bits=%d\n", ah->sl, ah->static_rate, ah->port_num, ah->ah_flags, ah->type, ah->ib.dlid, ah->ib.src_path_bits);
    printk(KERN_CRIT "max_send_wr=%d, max_recv_wr=%d, max_send_sge=%d, max_recv_sge=%d, max_inline_data=%d, max_rdma_ctxs=%d, sq_sig_type=%d, qp_type=%d, create_flags=%d, port_num=%d, source_qpn=%d\n", init_attr.cap.max_send_wr, init_attr.cap.max_recv_wr, init_attr.cap.max_send_sge, init_attr.cap.max_recv_sge, init_attr.cap.max_inline_data, init_attr.cap.max_rdma_ctxs, init_attr.sq_sig_type, init_attr.qp_type, init_attr.create_flags, init_attr.port_num, init_attr.source_qpn);

	return ib_post_send(FIT_ctx->qp[connection_id], &wr, NULL);
#endif
}

int fit_state = FIT_MODULE_DOWN;
EXPORT_SYMBOL(fit_state);

// static int __init lego_ib_init(void)
static int lego_ib_init(void)
{
	int ret;

	printk(KERN_CRIT "%s\n", __func__);

	fit_internal_init();

	ret = ib_register_client(&ibv_client);
	if (ret) {
		pr_err("couldn't register IB client\n");
		return ret;
	}

	atomic_set(&global_reqid, 0);

	ret = ibapi_establish_conn(/* ib_port */1, CONFIG_FIT_LOCAL_ID);
    if (ret < 0) {
        printk(KERN_CRIT "ibapi_establish_conn return %d\n", ret); 
        return -1;
    }

//    lego_ib_test();
    ret = my_test();
    printk(KERN_CRIT "my_test return %d\n");

	fit_state = FIT_MODULE_UP;
	return 0;
}

// static void __exit lego_ib_cleanup(void)
static void lego_ib_cleanup(void)
{
	fit_state = FIT_MODULE_DOWN;

	pr_info("Removing LegoOS FIT Module...");
	fit_cleanup_module();
	ib_unregister_client(&ibv_client);
	fit_internal_cleanup();
}

module_init(lego_ib_init);
module_exit(lego_ib_cleanup);
MODULE_LICENSE("GPL");
