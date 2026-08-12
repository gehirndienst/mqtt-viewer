// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "core/ssh_tunnel.h"
#include "test_helpers.h"

TEST(full_opts_with_user_and_key) {
    SshTunnelOpts opts = {
        .target_host = "10.0.0.5",
        .target_port = 8883,
        .jump_host = "bastion.example.com",
        .jump_port = 2222,
        .jump_user = "alice",
        .jump_key_path = "/home/alice/.ssh/id_ed25519",
    };
    const char* argv[SSH_TUNNEL_MAX_ARGS];
    int argc = ssh_tunnel_build_argv(&opts, 54321, argv, SSH_TUNNEL_MAX_ARGS);

    ASSERT_EQ(argc, 15);
    ASSERT_NULL(argv[argc]);
    ASSERT_STR_EQ(argv[0], "ssh");
    ASSERT_STR_EQ(argv[1], "-N");
    ASSERT_STR_EQ(argv[2], "-o");
    ASSERT_STR_EQ(argv[3], "BatchMode=yes");
    ASSERT_STR_EQ(argv[4], "-o");
    ASSERT_STR_EQ(argv[5], "ExitOnForwardFailure=yes");
    ASSERT_STR_EQ(argv[6], "-o");
    ASSERT_STR_EQ(argv[7], "StrictHostKeyChecking=accept-new");
    ASSERT_STR_EQ(argv[8], "-p");
    ASSERT_STR_EQ(argv[9], "2222");
    ASSERT_STR_EQ(argv[10], "-i");
    ASSERT_STR_EQ(argv[11], "/home/alice/.ssh/id_ed25519");
    ASSERT_STR_EQ(argv[12], "-L");
    ASSERT_STR_EQ(argv[13], "127.0.0.1:54321:10.0.0.5:8883");
    ASSERT_STR_EQ(argv[14], "alice@bastion.example.com");
}

TEST(no_key_path_omits_dash_i) {
    SshTunnelOpts opts = {
        .target_host = "broker.internal",
        .target_port = 1883,
        .jump_host = "jump.example.com",
        .jump_port = 22,
        .jump_user = "bob",
        .jump_key_path = NULL,
    };
    const char* argv[SSH_TUNNEL_MAX_ARGS];
    int argc = ssh_tunnel_build_argv(&opts, 40000, argv, SSH_TUNNEL_MAX_ARGS);

    ASSERT_EQ(argc, 13);
    for (int i = 0; i < argc; i++) {
        ASSERT_NE(strcmp(argv[i], "-i"), 0);
    }
    ASSERT_STR_EQ(argv[8], "-p");
    ASSERT_STR_EQ(argv[9], "22");
    ASSERT_STR_EQ(argv[10], "-L");
    ASSERT_STR_EQ(argv[11], "127.0.0.1:40000:broker.internal:1883");
    ASSERT_STR_EQ(argv[12], "bob@jump.example.com");
}

TEST(empty_user_omits_at_sign) {
    SshTunnelOpts opts = {
        .target_host = "broker.internal",
        .target_port = 1883,
        .jump_host = "jump.example.com",
        .jump_port = 22,
        .jump_user = "",
        .jump_key_path = "",
    };
    const char* argv[SSH_TUNNEL_MAX_ARGS];
    int argc = ssh_tunnel_build_argv(&opts, 40000, argv, SSH_TUNNEL_MAX_ARGS);

    ASSERT_STR_EQ(argv[argc - 1], "jump.example.com");
}

TEST(zero_jump_port_defaults_to_22) {
    SshTunnelOpts opts = {
        .target_host = "broker.internal",
        .target_port = 1883,
        .jump_host = "jump.example.com",
        .jump_port = 0,
        .jump_user = NULL,
        .jump_key_path = NULL,
    };
    const char* argv[SSH_TUNNEL_MAX_ARGS];
    ssh_tunnel_build_argv(&opts, 40000, argv, SSH_TUNNEL_MAX_ARGS);

    ASSERT_STR_EQ(argv[8], "-p");
    ASSERT_STR_EQ(argv[9], "22");
}

TEST(local_forward_spec_uses_loopback_and_target) {
    SshTunnelOpts opts = {
        .target_host = "mqtt.captn.example.io",
        .target_port = 8883,
        .jump_host = "jump.example.com",
        .jump_port = 22,
        .jump_user = NULL,
        .jump_key_path = NULL,
    };
    const char* argv[SSH_TUNNEL_MAX_ARGS];
    int argc = ssh_tunnel_build_argv(&opts, 51515, argv, SSH_TUNNEL_MAX_ARGS);

    bool found = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "127.0.0.1:51515:mqtt.captn.example.io:8883") == 0) found = true;
    }
    ASSERT_TRUE(found);
}

TEST(password_switches_to_sshpass_and_omits_batchmode) {
    SshTunnelOpts opts = {
        .target_host = "broker.internal",
        .target_port = 1883,
        .jump_host = "jump.example.com",
        .jump_port = 22,
        .jump_user = "carol",
        .jump_key_path = NULL,
        .jump_password = "hunter2",
    };
    const char* argv[SSH_TUNNEL_MAX_ARGS];
    int argc = ssh_tunnel_build_argv(&opts, 40000, argv, SSH_TUNNEL_MAX_ARGS);

    ASSERT_STR_EQ(argv[0], "sshpass");
    ASSERT_STR_EQ(argv[1], "-e");
    ASSERT_STR_EQ(argv[2], "ssh");
    for (int i = 0; i < argc; i++) {
        ASSERT_NE(strcmp(argv[i], "BatchMode=yes"), 0); // conflicts with sshpass's prompt-and-answer approach
        ASSERT_NULL(strstr(argv[i], "hunter2")); // password must never appear in argv (visible via `ps`)
    }
    ASSERT_STR_EQ(argv[argc - 1], "carol@jump.example.com");
}

TEST(no_password_keeps_plain_ssh_and_batchmode) {
    SshTunnelOpts opts = {
        .target_host = "broker.internal",
        .target_port = 1883,
        .jump_host = "jump.example.com",
        .jump_port = 22,
        .jump_user = NULL,
        .jump_key_path = NULL,
        .jump_password = NULL,
    };
    const char* argv[SSH_TUNNEL_MAX_ARGS];
    int argc = ssh_tunnel_build_argv(&opts, 40000, argv, SSH_TUNNEL_MAX_ARGS);

    ASSERT_STR_EQ(argv[0], "ssh");
    bool has_batchmode = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "BatchMode=yes") == 0) has_batchmode = true;
    }
    ASSERT_TRUE(has_batchmode);
}

TEST(too_small_argv_cap_fails) {
    SshTunnelOpts opts = {
        .target_host = "h",
        .target_port = 1,
        .jump_host = "j",
        .jump_port = 22,
        .jump_user = NULL,
        .jump_key_path = NULL,
    };
    const char* argv[8];
    int argc = ssh_tunnel_build_argv(&opts, 1, argv, 3);
    ASSERT_EQ(argc, -1);
}

int main(void) {
    printf("test_ssh_tunnel:\n");
    RUN(full_opts_with_user_and_key);
    RUN(no_key_path_omits_dash_i);
    RUN(empty_user_omits_at_sign);
    RUN(zero_jump_port_defaults_to_22);
    RUN(local_forward_spec_uses_loopback_and_target);
    RUN(password_switches_to_sshpass_and_omits_batchmode);
    RUN(no_password_keeps_plain_ssh_and_batchmode);
    RUN(too_small_argv_cap_fails);
    printf("all tests passed\n");
    return 0;
}
