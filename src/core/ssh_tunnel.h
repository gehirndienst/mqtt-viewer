// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef SSH_TUNNEL_H
#define SSH_TUNNEL_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "model/connection_log.h"

// [sshpass -e] + ssh + -N + 3x(-o VALUE) + -p PORT + -i KEY + -L SPEC + user@host + NULL terminator
#define SSH_TUNNEL_MAX_ARGS 20

typedef struct {
    pid_t pid;
    uint16_t local_port;
} SshTunnel;

typedef struct {
    const char* target_host;
    uint16_t target_port;
    const char* jump_host;
    uint16_t jump_port;
    const char* jump_user;
    const char* jump_key_path;
    const char* jump_password;
} SshTunnelOpts;

/**
 * @brief Build the argv for the `ssh` local port-forward command.
 *
 * Forwards 127.0.0.1:@p local_port on this machine to opts->target_host:target_port.
 * @param opts        Jump host and forward target.
 * @param local_port  Local port already reserved for the tunnel.
 * @param argv        Output array of borrowed string pointers, NULL-terminated on success.
 * @param argv_cap    Capacity of @p argv, including the trailing NULL slot.
 * @return Number of argv entries written (excluding the trailing NULL), or -1 if @p argv_cap
 *         is too small.
 */
int ssh_tunnel_build_argv(const SshTunnelOpts* opts, uint16_t local_port, const char* argv[], int argv_cap);

/**
 * @brief Reserve a free local TCP port and spawn `ssh -N -L` to forward it to the broker.
 *
 * Blocks (up to a few seconds) until the forwarded port accepts a connection, the ssh
 * process exits early (bad host/user/key), or a timeout elapses. Errors are reported to
 * @p log per the project's ConnectionLog convention.
 * @param tunnel  Zeroed/uninitialized tunnel state to fill in.
 * @param opts    Jump host and forward target.
 * @param log     Connection log for user-visible success/failure messages.
 * @return true once the tunnel is up; @p tunnel->local_port is then the port to dial on
 *         127.0.0.1 in place of the broker's real host/port.
 */
bool ssh_tunnel_start(SshTunnel* tunnel, const SshTunnelOpts* opts, ConnectionLog* log);

/**
 * @brief Check whether the tunnel process is still running.
 * @return true if still alive; reaps and zeroes @p tunnel and returns false otherwise.
 */
bool ssh_tunnel_poll_alive(SshTunnel* tunnel);

/** @brief Terminate the tunnel process (if any) and zero @p tunnel. */
void ssh_tunnel_stop(SshTunnel* tunnel);

#endif
