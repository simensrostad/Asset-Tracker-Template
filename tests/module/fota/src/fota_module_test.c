/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <unity.h>

#include <zephyr/fff.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/logging/log.h>
#include <net/nrf_cloud_fota_poll.h>

#include "message_channel.h"
#include "fota.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, task_wdt_feed, int);
FAKE_VALUE_FUNC(int, task_wdt_add, uint32_t, task_wdt_callback_t, void *);
FAKE_VALUE_FUNC(int, nrf_cloud_fota_poll_init, struct nrf_cloud_fota_poll_ctx *);
FAKE_VALUE_FUNC(int, nrf_cloud_fota_poll_process_pending, struct nrf_cloud_fota_poll_ctx *);
FAKE_VALUE_FUNC(int, nrf_cloud_fota_poll_process, struct nrf_cloud_fota_poll_ctx *);
FAKE_VALUE_FUNC(int, fota_download_cancel);
FAKE_VOID_FUNC1(callback_t, int);

ZBUS_MSG_SUBSCRIBER_DEFINE(fota_subscriber);
ZBUS_CHAN_ADD_OBS(FOTA_CHAN, fota_subscriber, 0);

LOG_MODULE_REGISTER(fota_module_test, 4);

static struct nrf_cloud_fota_poll_ctx test_fota_ctx;

/* Forward declarations */
static void check_for_event_on_fota_channel(enum fota_msg_type expected_fota_type);
static void check_no_fota_events(uint32_t time_in_seconds);
static void send_fota_module_event(enum fota_msg_type msg);

int init_custom_fake(struct nrf_cloud_fota_poll_ctx *ctx)
{
	LOG_DBG("Setup stub for internal nRF Cloud FOTA poll callback handler");

	test_fota_ctx = *ctx;

	return 0;
}

void setUp(void)
{
	RESET_FAKE(task_wdt_feed);
	RESET_FAKE(task_wdt_add);
	RESET_FAKE(nrf_cloud_fota_poll_init);
	RESET_FAKE(nrf_cloud_fota_poll_process_pending);
	RESET_FAKE(nrf_cloud_fota_poll_process);
	RESET_FAKE(fota_download_cancel);

	nrf_cloud_fota_poll_init_fake.custom_fake = init_custom_fake;
}

void tearDown(void)
{
	/* Check that no events are sent on FOTA_CHAN for some elongated amount of time */
	check_no_fota_events(3600);

	/* Reset DuTs internal state between each test case */
	send_fota_module_event(FOTA_CANCEL);
	check_for_event_on_fota_channel(FOTA_CANCEL);
}

void check_for_event_on_fota_channel(enum fota_msg_type expected_fota_type)
{
	int err;
	const struct zbus_channel *chan;
	enum fota_msg_type fota_msg;

	err = zbus_sub_wait_msg(&fota_subscriber, &chan, &fota_msg, K_MSEC(1000));
	if (err == -ENOMSG) {
		LOG_ERR("No fota event received");
		TEST_FAIL();
	} else if (err) {
		LOG_ERR("zbus_sub_wait, error: %d", err);
		SEND_FATAL_ERROR();

		return;
	}

	if (chan != &FOTA_CHAN) {
		LOG_ERR("Received message from wrong channel");
		TEST_FAIL();
	}

	TEST_ASSERT_EQUAL(expected_fota_type, fota_msg);
}

void check_no_fota_events(uint32_t time_in_seconds)
{
	int err;
	const struct zbus_channel *chan;
	enum fota_msg_type fota_msg;

	/* Allow the test thread to sleep so that the DUT's thread is allowed to run. */
	k_sleep(K_SECONDS(time_in_seconds));

	err = zbus_sub_wait_msg(&fota_subscriber, &chan, &fota_msg, K_MSEC(1000));
	if (err == 0) {
		LOG_ERR("Received fota event with type %d", fota_msg);
		TEST_FAIL();
	}
}

static void send_fota_module_event(enum fota_msg_type msg)
{
	int err = zbus_chan_pub(&FOTA_CHAN, &msg, K_SECONDS(1));

	TEST_ASSERT_EQUAL(0, err);
}

static void invoke_nrf_cloud_fota_callback_stub_status(enum nrf_cloud_fota_status status)
{
	test_fota_ctx.status_fn(status, NULL);
}

static void invoke_nrf_cloud_fota_callback_stub_reboot(enum nrf_cloud_fota_reboot_status status)
{
	test_fota_ctx.reboot_fn(status);
}

void test_fota_module_should_return_no_available_job(void)
{
	/* Given */
	nrf_cloud_fota_poll_process_fake.return_val = -EAGAIN;
	send_fota_module_event(FOTA_POLL_REQUEST);

	/* Then */
	check_for_event_on_fota_channel(FOTA_POLL_REQUEST);
	check_for_event_on_fota_channel(FOTA_NO_AVAILABLE_UPDATE);
}

void test_fota_module_should_succeed(void)
{
	/* Given */
	nrf_cloud_fota_poll_process_fake.return_val = 0;
	send_fota_module_event(FOTA_POLL_REQUEST);

	/* When */
	invoke_nrf_cloud_fota_callback_stub_status(NRF_CLOUD_FOTA_DOWNLOADING);
	invoke_nrf_cloud_fota_callback_stub_reboot(FOTA_REBOOT_SUCCESS);

	/* Then */
	check_for_event_on_fota_channel(FOTA_POLL_REQUEST);
	check_for_event_on_fota_channel(FOTA_DOWNLOADING_UPDATE);
	check_for_event_on_fota_channel(FOTA_REBOOT_NEEDED);
}

void test_fota_module_should_fail_on_timeout(void)
{
	/* Given */
	nrf_cloud_fota_poll_process_fake.return_val = 0;
	send_fota_module_event(FOTA_POLL_REQUEST);

	/* When */
	invoke_nrf_cloud_fota_callback_stub_status(NRF_CLOUD_FOTA_DOWNLOADING);
	invoke_nrf_cloud_fota_callback_stub_status(NRF_CLOUD_FOTA_TIMED_OUT);

	/* Then */
	check_for_event_on_fota_channel(FOTA_POLL_REQUEST);
	check_for_event_on_fota_channel(FOTA_DOWNLOADING_UPDATE);
	check_for_event_on_fota_channel(FOTA_DOWNLOAD_TIMED_OUT);
}

/* This is required to be added to each test. That is because unity's
 * main may return nonzero, while zephyr's main currently must
 * return 0 in all cases (other values are reserved).
 */
extern int unity_main(void);

int main(void)
{
	/* Initialize the custom fake */
	// nrf_cloud_fota_poll_init_fake.custom_fake = init_custom_fake;

	/* use the runner from test_runner_generate() */
	(void)unity_main();

	return 0;
}
