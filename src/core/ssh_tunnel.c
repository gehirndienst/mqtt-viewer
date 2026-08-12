// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "core/ssh_tunnel.h"
#include "platform/log.h"

extern char** environ;

int ssh_tunnel_build_argv(const SshTunnelOpts* opts, uint16_t local_port, const char* argv[], int argv_cap) {
    static char jump_port_buf[8];
    static char local_spec_buf[600];
    static char user_host_buf[400];

    bool use_password = opts->jump_password && opts->jump_password[0];

    int argc = 0;
#define PUSH(s) \
    do { \
        if (argc >= argv_cap - 1) return -1; \
        argv[argc++] = (s); \
    } while (0)

    if (use_password) {
        PUSH("sshpass");
        PUSH("-e"); // read the password from the SSHPASS env var
    }
    PUSH("ssh");
    PUSH("-N"); // no remote command - forwarding only
    if (!use_password) {
        PUSH("-o");
        PUSH("BatchMode=yes"); // never prompt for a password/passphrase
    }
    PUSH("-o");
    PUSH("ExitOnForwardFailure=yes"); // fail fast if the forward can't be set up
    PUSH("-o");
    PUSH("StrictHostKeyChecking=accept-new"); // no interactive host-key prompt

    snprintf(jump_port_buf, sizeof(jump_port_buf), "%u", opts->jump_port ? opts->jump_port : 22);
    PUSH("-p");
    PUSH(jump_port_buf);

    if (opts->jump_key_path && opts->jump_key_path[0]) {
        PUSH("-i");
        PUSH(opts->jump_key_path);
    }

    snprintf(local_spec_buf, sizeof(local_spec_buf), "127.0.0.1:%u:%s:%u", local_port, opts->target_host,
             opts->target_port);
    PUSH("-L");
    PUSH(local_spec_buf);

    if (opts->jump_user && opts->jump_user[0]) {
        snprintf(user_host_buf, sizeof(user_host_buf), "%s@%s", opts->jump_user, opts->jump_host);
        PUSH(user_host_buf);
    } else {
        PUSH(opts->jump_host);
    }
#undef PUSH

    argv[argc] = NULL;
    return argc;
}

static bool reserve_free_port(uint16_t* out_port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return false;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr*)&addr, &len) != 0) {
        close(fd);
        return false;
    }
    *out_port = ntohs(addr.sin_port);
    close(fd);
    return true;
}

static bool port_connectable(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    bool ok = connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    close(fd);
    return ok;
}

bool ssh_tunnel_start(SshTunnel* tunnel, const SshTunnelOpts* opts, ConnectionLog* log) {
    memset(tunnel, 0, sizeof(*tunnel));

    uint16_t local_port;
    if (!reserve_free_port(&local_port)) {
        connection_log_add(log, CONN_LOG_ERROR, "SSH tunnel: failed to reserve a local port");
        return false;
    }

    const char* argv[SSH_TUNNEL_MAX_ARGS];
    int argc = ssh_tunnel_build_argv(opts, local_port, argv, SSH_TUNNEL_MAX_ARGS);
    if (argc < 0) {
        connection_log_add(log, CONN_LOG_ERROR, "SSH tunnel: command too long to build");
        return false;
    }

    // pwd auth passes the secret via the SSHPASS env var,never argv, which `ps` can read
    char** envp = environ;
    char** owned_envp = NULL;
    char sshpass_var[192];
    if (opts->jump_password && opts->jump_password[0]) {
        int n = 0;
        while (environ[n]) n++;
        owned_envp = malloc(sizeof(char*) * (size_t)(n + 2));
        if (owned_envp) {
            for (int i = 0; i < n; i++) owned_envp[i] = environ[i];
            snprintf(sshpass_var, sizeof(sshpass_var), "SSHPASS=%s", opts->jump_password);
            owned_envp[n] = sshpass_var;
            owned_envp[n + 1] = NULL;
            envp = owned_envp;
        }
    }

    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], NULL, NULL, (char* const*)argv, envp);
    free(owned_envp);
    if (rc != 0) {
        char buf[256];
        if (rc == ENOENT && strcmp(argv[0], "sshpass") == 0) {
            snprintf(buf, sizeof(buf),
                     "SSH tunnel: 'sshpass' not found on PATH - install it (e.g. `brew install "
                     "hudochenkov/sshpass/sshpass` or `apt install sshpass`) or use key-based auth instead");
        } else {
            snprintf(buf, sizeof(buf), "SSH tunnel: failed to launch %s: %s", argv[0], strerror(rc));
        }
        connection_log_add(log, CONN_LOG_ERROR, buf);
        return false;
    }

    const int timeout_ms = 8000;
    const int step_ms = 100;
    for (int waited = 0; waited < timeout_ms; waited += step_ms) {
        int status;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            connection_log_add(log, CONN_LOG_ERROR,
                               "SSH tunnel: ssh exited before the tunnel came up (check jump host/user/key)");
            return false;
        }

        if (port_connectable(local_port)) {
            tunnel->pid = pid;
            tunnel->local_port = local_port;
            char msg[128];
            snprintf(msg, sizeof(msg), "SSH tunnel established on 127.0.0.1:%u", local_port);
            connection_log_add(log, CONN_LOG_INFO, msg);
            return true;
        }

        struct timespec ts = {.tv_sec = 0, .tv_nsec = (long)step_ms * 1000000L};
        nanosleep(&ts, NULL);
    }

    connection_log_add(log, CONN_LOG_ERROR, "SSH tunnel: timed out waiting for the tunnel to come up");
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    return false;
}

bool ssh_tunnel_poll_alive(SshTunnel* tunnel) {
    if (tunnel->pid <= 0) return false;
    int status;
    pid_t r = waitpid(tunnel->pid, &status, WNOHANG);
    if (r == 0) return true; // still running
    LOG_WARN("ssh_tunnel_poll_alive: tunnel process %d exited", (int)tunnel->pid);
    memset(tunnel, 0, sizeof(*tunnel));
    return false;
}

void ssh_tunnel_stop(SshTunnel* tunnel) {
    if (tunnel->pid > 0) {
        kill(tunnel->pid, SIGTERM);
        waitpid(tunnel->pid, NULL, 0);
    }
    memset(tunnel, 0, sizeof(*tunnel));
}
