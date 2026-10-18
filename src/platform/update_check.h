// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
/**
 * @file
 * @brief Manual "check for updates" against the GitHub latest-release API (shells out to curl on a worker thread)
 */
#ifndef UPDATE_CHECK_H
#define UPDATE_CHECK_H

typedef enum {
    UPDATE_IDLE, /**< No check has been started */
    UPDATE_CHECKING, /**< Worker thread is running */
    UPDATE_UP_TO_DATE, /**< Running version is the latest release */
    UPDATE_AVAILABLE, /**< A newer release exists; see UpdateResult::latest */
    UPDATE_FAILED, /**< curl missing, offline, or unexpected reply; see UpdateResult::error */
} UpdateStatus;

typedef struct {
    UpdateStatus status;
    char latest[64]; /**< Release tag, valid when status is UPDATE_AVAILABLE or UPDATE_UP_TO_DATE */
    char error[96]; /**< Human-readable reason, valid when status is UPDATE_FAILED */
} UpdateResult;

/**
 * @brief Start a background check comparing @p current_version with the latest GitHub release.
 *
 * No-op while a check is already running. Never blocks the caller.
 */
void update_check_start(const char* current_version);

/** @brief Thread-safe snapshot of the current check state; call every frame. */
UpdateResult update_check_poll(void);

#endif
