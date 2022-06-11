/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2019-2022 The Fluent Bit Authors
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fluent-bit.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_time.h>
#include "flb_tests_runtime.h"

struct test_ctx {
    flb_ctx_t *flb;    /* Fluent Bit library context */
    int i_ffd;         /* Input fd  */
    int f_ffd;         /* Filter fd (unused) */
    int o_ffd;         /* Output fd */
};


pthread_mutex_t result_mutex = PTHREAD_MUTEX_INITIALIZER;
int num_output = 0;
static int get_output_num()
{
    int ret;
    pthread_mutex_lock(&result_mutex);
    ret = num_output;
    pthread_mutex_unlock(&result_mutex);

    return ret;
}

static void set_output_num(int num)
{
    pthread_mutex_lock(&result_mutex);
    num_output = num;
    pthread_mutex_unlock(&result_mutex);
}

static void clear_output_num()
{
    set_output_num(0);
}

struct str_list {
    size_t size;
    char **lists;
};

/* Callback to check expected results */
static void cb_check_str_list(void *ctx, int ffd, int res_ret, 
                              void *res_data, size_t res_size, void *data)
{
    char *p;
    flb_sds_t out_line = res_data;
    int num = get_output_num();
    size_t i;
    struct str_list *l = (struct str_list *)data;

    if (!TEST_CHECK(res_data != NULL)) {
        TEST_MSG("res_data is NULL");
        return;
    }

    if (!TEST_CHECK(l != NULL)) {
        TEST_MSG("l is NULL");
        flb_sds_destroy(out_line);
        return;
    }

    if(!TEST_CHECK(res_ret == 0)) {
        TEST_MSG("callback ret=%d", res_ret);
    }
    if (!TEST_CHECK(res_data != NULL)) {
        TEST_MSG("res_data is NULL");
        flb_sds_destroy(out_line);
        return;
    }

    for (i=0; i<l->size; i++) {
        p = strstr(out_line, l->lists[i]);
        if (!TEST_CHECK(p != NULL)) {
            TEST_MSG("  Got   :%s\n  expect:%s", out_line, l->lists[i]);
        }
    }
    set_output_num(num+1);

    flb_sds_destroy(out_line);
}

static struct test_ctx *test_ctx_create(struct flb_lib_out_cb *data)
{
    int i_ffd;
    int o_ffd;
    struct test_ctx *ctx = NULL;

    ctx = flb_malloc(sizeof(struct test_ctx));
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("malloc failed");
        flb_errno();
        return NULL;
    }

    /* Service config */
    ctx->flb = flb_create();
    flb_service_set(ctx->flb,
                    "Flush", "0.200000000",
                    "Grace", "1",
                    "Log_Level", "error",
                    NULL);

    /* Input */
    i_ffd = flb_input(ctx->flb, (char *) "lib", NULL);
    TEST_CHECK(i_ffd >= 0);
    ctx->i_ffd = i_ffd;

    /* Output */
    o_ffd = flb_output(ctx->flb, (char *) "http", (void *) data);
    ctx->o_ffd = o_ffd;

    return ctx;
}

static void test_ctx_destroy(struct test_ctx *ctx)
{
    TEST_CHECK(ctx != NULL);

    sleep(1);
    flb_stop(ctx->flb);
    flb_destroy(ctx->flb);
    flb_free(ctx);
}

void flb_test_format_json()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\"}]";
    size_t size1 = strlen(buf1);
    char *buf2 = "[2, {\"msg\":\"hello world\"}]";
    size_t size2 = strlen(buf2);

    char *expected_strs[] = {"[{\"date\":1.0,\"msg\":\"hello world\"},{\"date\":2.0,\"msg\":\"hello world\"}]"};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "json",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf2, size2);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

void flb_test_format_json_stream()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\"}]";
    size_t size1 = strlen(buf1);
    char *buf2 = "[2, {\"msg\":\"hello world\"}]";
    size_t size2 = strlen(buf2);

    char *expected_strs[] = {"{\"date\":1.0,\"msg\":\"hello world\"}{\"date\":2.0,\"msg\":\"hello world\"}"};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "json_stream",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf2, size2);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

void flb_test_format_json_lines()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\"}]";
    size_t size1 = strlen(buf1);
    char *buf2 = "[2, {\"msg\":\"hello world\"}]";
    size_t size2 = strlen(buf2);

    char *expected_strs[] = {"{\"date\":1.0,\"msg\":\"hello world\"}\n{\"date\":2.0,\"msg\":\"hello world\"}"};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "json_lines",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf2, size2);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

void flb_test_format_gelf()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\"}]";
    size_t size1 = strlen(buf1);

    char *expected_strs[] = {"\"short_message\":\"hello world\"", "\"timestamp\":1.000"};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "gelf",
                         "gelf_short_message_key", "msg",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}


void flb_test_format_gelf_host_key()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\", \"h_key\":\"localhost\"}]";
    size_t size1 = strlen(buf1);

    char *expected_strs[] = {"\"short_message\":\"hello world\"", "\"timestamp\":1.000", "\"host\":\"localhost\""};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "gelf",
                         "gelf_short_message_key", "msg",
                         "gelf_host_key", "h_key",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

void flb_test_format_gelf_timestamp_key()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\", \"t_key\":\"2018-05-30T09:39:52.000681Z\"}]";
    size_t size1 = strlen(buf1);

    char *expected_strs[] = {"\"short_message\":\"hello world\"", "\"timestamp\":\"2018-05-30T09:39:52.000681Z\""};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "gelf",
                         "gelf_short_message_key", "msg",
                         "gelf_timestamp_key", "t_key",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

void flb_test_format_gelf_full_message_key()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\", \"f_msg\":\"full message\"}]";
    size_t size1 = strlen(buf1);

    char *expected_strs[] = {"\"short_message\":\"hello world\"", "\"timestamp\":1.000","\"full_message\":\"full message\""};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "gelf",
                         "gelf_short_message_key", "msg",
                         "gelf_full_message_key", "f_msg",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

void flb_test_format_gelf_level_key()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\", \"l_msg\":6\"}]";
    size_t size1 = strlen(buf1);

    char *expected_strs[] = {"\"short_message\":\"hello world\"", "\"timestamp\":1.000","\"level\":6"};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "gelf",
                         "gelf_short_message_key", "msg",
                         "gelf_level_key", "l_msg",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

void flb_test_set_json_date_key()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\"}]";
    size_t size1 = strlen(buf1);

    char *expected_strs[] = {"{\"timestamp\":1.0,\"msg\":\"hello world\"}"};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "json",
                         "json_date_key", "timestamp",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

void flb_test_disable_json_date_key()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\"}]";
    size_t size1 = strlen(buf1);

    char *expected_strs[] = {"{\"msg\":\"hello world\"}"};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "json",
                         "json_date_key", "false",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

void flb_test_json_date_format_epoch()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\"}]";
    size_t size1 = strlen(buf1);

    char *expected_strs[] = {"{\"date\":1,\"msg\":\"hello world\"}"};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "json",
                         "json_date_format", "epoch",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

void flb_test_json_date_format_iso8601()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\"}]";
    size_t size1 = strlen(buf1);

    char *expected_strs[] = {"\"msg\":\"hello world\"", "\"date\":\"1970-01-01T00:00:01.000000Z\""};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "json",
                         "json_date_format", "iso8601",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

void flb_test_json_date_format_java_sql_timestamp()
{
    struct flb_lib_out_cb cb_data;
    struct test_ctx *ctx;
    int ret;
    int num;

    char *buf1 = "[1, {\"msg\":\"hello world\"}]";
    size_t size1 = strlen(buf1);

    char *expected_strs[] = {"\"msg\":\"hello world\"", "\"date\":\"1970-01-01 00:00:01.000000\""};
    struct str_list expected = {
                                .size = sizeof(expected_strs)/sizeof(char*),
                                .lists = &expected_strs[0],
    };

    clear_output_num();

    ctx = test_ctx_create(&cb_data);
    if (!TEST_CHECK(ctx != NULL)) {
        TEST_MSG("test_ctx_create failed");
        exit(EXIT_FAILURE);
    }

    ret = flb_output_set(ctx->flb, ctx->o_ffd,
                         "match", "*",
                         "format", "json",
                         "json_date_format", "java_sql_timestamp",
                         NULL);
    TEST_CHECK(ret == 0);

    ret = flb_output_set_test(ctx->flb, ctx->o_ffd,
                         "formatter", cb_check_str_list,
                          &expected, NULL);
    TEST_CHECK(ret == 0);

    /* Start the engine */
    ret = flb_start(ctx->flb);
    TEST_CHECK(ret == 0);

    /* Ingest data sample */
    ret = flb_lib_push(ctx->flb, ctx->i_ffd, (char *) buf1, size1);
    TEST_CHECK(ret >= 0);

    /* waiting to flush */
    flb_time_msleep(500);

    num = get_output_num();
    if (!TEST_CHECK(num > 0))  {
        TEST_MSG("no outputs");
    }

    test_ctx_destroy(ctx);
}

/* Test list */
TEST_LIST = {
    {"format_json" , flb_test_format_json},
    {"format_json_stream" , flb_test_format_json_stream},
    {"format_json_lines" , flb_test_format_json_lines},
    {"format_gelf" , flb_test_format_gelf},
    {"format_gelf_host_key" , flb_test_format_gelf_host_key},
    {"format_gelf_timestamp_key" , flb_test_format_gelf_timestamp_key},
    {"format_gelf_full_message_key" , flb_test_format_gelf_full_message_key},
    {"format_gelf_level_key" , flb_test_format_gelf_level_key},
    {"set_json_date_key" , flb_test_set_json_date_key},
    {"disable_json_date_key" , flb_test_disable_json_date_key},
    {"json_date_format_epoch" , flb_test_json_date_format_epoch},
    {"json_date_format_iso8601" , flb_test_json_date_format_iso8601},
    {"json_date_format_java_sql_timestamp" , flb_test_json_date_format_java_sql_timestamp},
    {NULL, NULL}
};