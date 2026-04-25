/*
 * Copyright (c) 2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2024-01-15     Srikanth Patchava Initial version - message queue tests
 */

#include <rtthread.h>
#include <rthw.h>
#include "utest.h"

/* Test configuration */
#define MQ_MSG_SIZE     32
#define MQ_MAX_MSGS     8
#define MQ_TEST_PRIO    20
#define MQ_STACK_SIZE   2048
#define MQ_TIMEOUT      100

static struct rt_messagequeue static_mq;
static rt_uint8_t mq_pool[MQ_MAX_MSGS * (MQ_MSG_SIZE + sizeof(struct rt_mq_message))];

static rt_mq_t test_mq = RT_NULL;

/* Helper thread for async receive tests */
static rt_thread_t recv_thread = RT_NULL;
static rt_uint8_t recv_buf[MQ_MSG_SIZE];
static volatile rt_bool_t recv_done = RT_FALSE;
static volatile rt_err_t recv_result = -RT_ERROR;

static void recv_thread_entry(void *param)
{
    rt_mq_t mq = (rt_mq_t)param;
    recv_result = rt_mq_recv(mq, recv_buf, MQ_MSG_SIZE, MQ_TIMEOUT);
    recv_done = RT_TRUE;
}

/* TC1: Create and delete dynamic message queue */
static void test_mq_create_delete(void)
{
    rt_mq_t mq;

    mq = rt_mq_create("test_mq", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    uassert_not_null(mq);

    rt_err_t ret = rt_mq_delete(mq);
    uassert_int_equal(ret, RT_EOK);
}

/* TC2: Init and detach static message queue */
static void test_mq_init_detach(void)
{
    rt_err_t ret;

    ret = rt_mq_init(&static_mq, "s_mq", mq_pool, MQ_MSG_SIZE,
                     sizeof(mq_pool), RT_IPC_FLAG_FIFO);
    uassert_int_equal(ret, RT_EOK);

    ret = rt_mq_detach(&static_mq);
    uassert_int_equal(ret, RT_EOK);
}

/* TC3: Send and receive basic message */
static void test_mq_send_recv(void)
{
    rt_err_t ret;
    char send_buf[] = "hello mq test";
    char local_recv[MQ_MSG_SIZE];

    test_mq = rt_mq_create("t_sr", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    uassert_not_null(test_mq);

    ret = rt_mq_send(test_mq, send_buf, sizeof(send_buf));
    uassert_int_equal(ret, RT_EOK);

    rt_memset(local_recv, 0, sizeof(local_recv));
    ret = rt_mq_recv(test_mq, local_recv, MQ_MSG_SIZE, MQ_TIMEOUT);
    uassert_int_equal(ret, RT_EOK);
    uassert_str_equal(local_recv, "hello mq test");

    rt_mq_delete(test_mq);
    test_mq = RT_NULL;
}

/* TC4: Urgent message goes to front of queue */
static void test_mq_urgent(void)
{
    rt_err_t ret;
    char msg1[] = "normal";
    char msg2[] = "urgent";
    char local_recv[MQ_MSG_SIZE];

    test_mq = rt_mq_create("t_urg", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    uassert_not_null(test_mq);

    ret = rt_mq_send(test_mq, msg1, sizeof(msg1));
    uassert_int_equal(ret, RT_EOK);

    ret = rt_mq_urgent(test_mq, msg2, sizeof(msg2));
    uassert_int_equal(ret, RT_EOK);

    /* Urgent message should be received first */
    rt_memset(local_recv, 0, sizeof(local_recv));
    ret = rt_mq_recv(test_mq, local_recv, MQ_MSG_SIZE, MQ_TIMEOUT);
    uassert_int_equal(ret, RT_EOK);
    uassert_str_equal(local_recv, "urgent");

    /* Normal message received second */
    rt_memset(local_recv, 0, sizeof(local_recv));
    ret = rt_mq_recv(test_mq, local_recv, MQ_MSG_SIZE, MQ_TIMEOUT);
    uassert_int_equal(ret, RT_EOK);
    uassert_str_equal(local_recv, "normal");

    rt_mq_delete(test_mq);
    test_mq = RT_NULL;
}

/* TC5: Receive timeout on empty queue */
static void test_mq_recv_timeout(void)
{
    rt_err_t ret;
    char local_recv[MQ_MSG_SIZE];
    rt_tick_t start, elapsed;

    test_mq = rt_mq_create("t_to", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    uassert_not_null(test_mq);

    start = rt_tick_get();
    ret = rt_mq_recv(test_mq, local_recv, MQ_MSG_SIZE, 10);
    elapsed = rt_tick_get() - start;

    uassert_int_equal(ret, -RT_ETIMEOUT);
    uassert_true(elapsed >= 10);

    rt_mq_delete(test_mq);
    test_mq = RT_NULL;
}

/* TC6: Non-blocking receive on empty queue */
static void test_mq_recv_noblock(void)
{
    rt_err_t ret;
    char local_recv[MQ_MSG_SIZE];

    test_mq = rt_mq_create("t_nb", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    uassert_not_null(test_mq);

    ret = rt_mq_recv(test_mq, local_recv, MQ_MSG_SIZE, RT_WAITING_NO);
    uassert_int_equal(ret, -RT_ETIMEOUT);

    rt_mq_delete(test_mq);
    test_mq = RT_NULL;
}

/* TC7: Fill queue to capacity (overflow test) */
static void test_mq_full_queue(void)
{
    rt_err_t ret;
    char msg[] = "fill";
    int i;

    test_mq = rt_mq_create("t_full", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    uassert_not_null(test_mq);

    /* Fill the queue */
    for (i = 0; i < MQ_MAX_MSGS; i++)
    {
        ret = rt_mq_send(test_mq, msg, sizeof(msg));
        uassert_int_equal(ret, RT_EOK);
    }

    /* Next send should fail (non-blocking) */
    ret = rt_mq_send_wait(test_mq, msg, sizeof(msg), RT_WAITING_NO);
    uassert_int_not_equal(ret, RT_EOK);

    rt_mq_delete(test_mq);
    test_mq = RT_NULL;
}

/* TC8: Message size boundary - max size */
static void test_mq_max_msg_size(void)
{
    rt_err_t ret;
    char send_buf[MQ_MSG_SIZE];
    char local_recv[MQ_MSG_SIZE];

    test_mq = rt_mq_create("t_max", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    uassert_not_null(test_mq);

    rt_memset(send_buf, 0xAB, MQ_MSG_SIZE);
    ret = rt_mq_send(test_mq, send_buf, MQ_MSG_SIZE);
    uassert_int_equal(ret, RT_EOK);

    rt_memset(local_recv, 0, MQ_MSG_SIZE);
    ret = rt_mq_recv(test_mq, local_recv, MQ_MSG_SIZE, MQ_TIMEOUT);
    uassert_int_equal(ret, RT_EOK);
    uassert_buf_equal(local_recv, send_buf, MQ_MSG_SIZE);

    rt_mq_delete(test_mq);
    test_mq = RT_NULL;
}

/* TC9: Message too large should fail */
static void test_mq_oversize_msg(void)
{
    rt_err_t ret;
    char big_buf[MQ_MSG_SIZE + 1];

    test_mq = rt_mq_create("t_big", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    uassert_not_null(test_mq);

    rt_memset(big_buf, 0, sizeof(big_buf));
    ret = rt_mq_send(test_mq, big_buf, sizeof(big_buf));
    uassert_int_not_equal(ret, RT_EOK);

    rt_mq_delete(test_mq);
    test_mq = RT_NULL;
}

/* TC10: FIFO ordering of messages */
static void test_mq_fifo_order(void)
{
    rt_err_t ret;
    int i;
    rt_uint32_t send_val, recv_val;

    test_mq = rt_mq_create("t_fifo", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    uassert_not_null(test_mq);

    for (i = 0; i < MQ_MAX_MSGS; i++)
    {
        send_val = (rt_uint32_t)i;
        ret = rt_mq_send(test_mq, &send_val, sizeof(send_val));
        uassert_int_equal(ret, RT_EOK);
    }

    for (i = 0; i < MQ_MAX_MSGS; i++)
    {
        recv_val = 0xFFFFFFFF;
        ret = rt_mq_recv(test_mq, &recv_val, MQ_MSG_SIZE, MQ_TIMEOUT);
        uassert_int_equal(ret, RT_EOK);
        uassert_int_equal(recv_val, (rt_uint32_t)i);
    }

    rt_mq_delete(test_mq);
    test_mq = RT_NULL;
}

/* TC11: Cross-thread send and receive */
static void test_mq_cross_thread(void)
{
    rt_err_t ret;
    char msg[] = "xthread";

    test_mq = rt_mq_create("t_xt", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    uassert_not_null(test_mq);

    recv_done = RT_FALSE;
    recv_result = -RT_ERROR;
    rt_memset(recv_buf, 0, sizeof(recv_buf));

    recv_thread = rt_thread_create("t_recv", recv_thread_entry, test_mq,
                                   MQ_STACK_SIZE, MQ_TEST_PRIO - 1, 10);
    uassert_not_null(recv_thread);
    rt_thread_startup(recv_thread);

    /* Give receiver time to start waiting */
    rt_thread_mdelay(10);

    ret = rt_mq_send(test_mq, msg, sizeof(msg));
    uassert_int_equal(ret, RT_EOK);

    /* Wait for receiver to complete */
    rt_thread_mdelay(50);
    uassert_true(recv_done);
    uassert_int_equal(recv_result, RT_EOK);
    uassert_str_equal((char *)recv_buf, "xthread");

    rt_mq_delete(test_mq);
    test_mq = RT_NULL;
}

/* TC12: Multiple send-recv cycles */
static void test_mq_multiple_cycles(void)
{
    rt_err_t ret;
    int i;
    rt_uint32_t val;

    test_mq = rt_mq_create("t_cyc", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    uassert_not_null(test_mq);

    for (i = 0; i < 32; i++)
    {
        val = (rt_uint32_t)(i * 7 + 3);
        ret = rt_mq_send(test_mq, &val, sizeof(val));
        uassert_int_equal(ret, RT_EOK);

        val = 0;
        ret = rt_mq_recv(test_mq, &val, MQ_MSG_SIZE, MQ_TIMEOUT);
        uassert_int_equal(ret, RT_EOK);
        uassert_int_equal(val, (rt_uint32_t)(i * 7 + 3));
    }

    rt_mq_delete(test_mq);
    test_mq = RT_NULL;
}

static rt_err_t utest_tc_init(void)
{
    return RT_EOK;
}

static rt_err_t utest_tc_cleanup(void)
{
    if (test_mq != RT_NULL)
    {
        rt_mq_delete(test_mq);
        test_mq = RT_NULL;
    }
    return RT_EOK;
}

static void testcase(void)
{
    UTEST_UNIT_RUN(test_mq_create_delete);
    UTEST_UNIT_RUN(test_mq_init_detach);
    UTEST_UNIT_RUN(test_mq_send_recv);
    UTEST_UNIT_RUN(test_mq_urgent);
    UTEST_UNIT_RUN(test_mq_recv_timeout);
    UTEST_UNIT_RUN(test_mq_recv_noblock);
    UTEST_UNIT_RUN(test_mq_full_queue);
    UTEST_UNIT_RUN(test_mq_max_msg_size);
    UTEST_UNIT_RUN(test_mq_oversize_msg);
    UTEST_UNIT_RUN(test_mq_fifo_order);
    UTEST_UNIT_RUN(test_mq_cross_thread);
    UTEST_UNIT_RUN(test_mq_multiple_cycles);
}
UTEST_TC_EXPORT(testcase, "testcases.kernel.mq", utest_tc_init, utest_tc_cleanup, 20);
