/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _FOTA_H_
#define _FOTA_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Channels provided by this module */
ZBUS_CHAN_DECLARE(FOTA_CHAN);

enum fota_msg_type {
	/* Output message types */

	/* Event received when downloading the FOTA update failed. */
	FOTA_DOWNLOAD_FAILED = 0x1,

	/* Event received when downloading the FOTA update timed out. */
	FOTA_DOWNLOAD_TIMED_OUT,

	/* Event received when a FOTA update is being downloaded. */
	FOTA_DOWNLOADING_UPDATE,

	/* Event received if there is no available update. */
	FOTA_NO_AVAILABLE_UPDATE,

	/* Event received when a FOTA update has succeeded, reboot is needed */
	FOTA_REBOOT_NEEDED,

	/* Input message types */

	/* Request to poll cloud for any available firmware updates. */
	FOTA_POLL_REQUEST,

	/* Cancel the FOTA process. */
	FOTA_CANCEL,
};

#define MSG_TO_FOTA_TYPE(_msg) (*(const enum fota_msg_type *)_msg)

#ifdef __cplusplus
}
#endif

#endif /* _FOTA_H_ */
