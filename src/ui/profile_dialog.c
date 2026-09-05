// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"
#include "raylib.h"

#include "core/mqtt_client.h"
#include "model/app_state.h"
#include "model/broker_profile.h"
#include "model/util.h"
#include "platform/db.h"
#include "ui/profile_dialog.h"
#include "ui/text_input.h"
#include "ui/theme.h"
#include "ui/ui_util.h"


#define FIELD_BUF_COUNT 128
#define FIELD_BUF_SIZE 512

#define FIDX_NAME 0
#define FIDX_HOST 1
#define FIDX_PORT 2
#define FIDX_CLIENTID 3
#define FIDX_KA 4
#define FIDX_USER 5
#define FIDX_PASS 6
#define FIDX_CA 7
#define FIDX_CERT 8
#define FIDX_KEY 9
#define FIDX_SSH_HOST 10
#define FIDX_SSH_PORT 11
#define FIDX_SSH_USER 12
#define FIDX_SSH_PASS 13
#define FIDX_SSH_KEY 14
#define FIDX_SUB_BASE 15
#define FIDX_MAX (FIDX_SUB_BASE + MAX_PROFILE_SUBS)

static char s_field_bufs[FIELD_BUF_COUNT][FIELD_BUF_SIZE];
static int s_field_buf_idx = FIELD_BUF_COUNT - 1;
// Numeric fields stored as strings; parsed to integers on Save / Connect
static char s_port_str[16];
static char s_ka_str[16];
static char s_ssh_port_str[16];
static bool s_active_field_selected = false;
static bool s_ssh_section_expanded = false;

static char* next_field_buf(void) {
    s_field_buf_idx = (s_field_buf_idx + 1) % FIELD_BUF_COUNT;
    return s_field_bufs[s_field_buf_idx];
}

typedef struct {
    char* buf;
    size_t max;
} FieldTarget;

static FieldTarget field_target(BrokerProfile* prof, int idx) {
    switch (idx) {
        case FIDX_NAME:
            return (FieldTarget){prof->name, sizeof(prof->name)};
        case FIDX_HOST:
            return (FieldTarget){prof->host, sizeof(prof->host)};
        case FIDX_PORT:
            return (FieldTarget){s_port_str, sizeof(s_port_str)};
        case FIDX_CLIENTID:
            return (FieldTarget){prof->client_id, sizeof(prof->client_id)};
        case FIDX_KA:
            return (FieldTarget){s_ka_str, sizeof(s_ka_str)};
        case FIDX_USER:
            return (FieldTarget){prof->username, sizeof(prof->username)};
        case FIDX_PASS:
            return (FieldTarget){prof->password, sizeof(prof->password)};
        case FIDX_CA:
            return (FieldTarget){prof->tls_ca_cert, sizeof(prof->tls_ca_cert)};
        case FIDX_CERT:
            return (FieldTarget){prof->tls_client_cert, sizeof(prof->tls_client_cert)};
        case FIDX_KEY:
            return (FieldTarget){prof->tls_client_key, sizeof(prof->tls_client_key)};
        case FIDX_SSH_HOST:
            return (FieldTarget){prof->ssh_jump_host, sizeof(prof->ssh_jump_host)};
        case FIDX_SSH_PORT:
            return (FieldTarget){s_ssh_port_str, sizeof(s_ssh_port_str)};
        case FIDX_SSH_USER:
            return (FieldTarget){prof->ssh_jump_user, sizeof(prof->ssh_jump_user)};
        case FIDX_SSH_PASS:
            return (FieldTarget){prof->ssh_jump_password, sizeof(prof->ssh_jump_password)};
        case FIDX_SSH_KEY:
            return (FieldTarget){prof->ssh_jump_key_path, sizeof(prof->ssh_jump_key_path)};
        default:
            if (idx >= FIDX_SUB_BASE && idx < FIDX_SUB_BASE + MAX_PROFILE_SUBS) {
                int si = idx - FIDX_SUB_BASE;
                return (FieldTarget){prof->subscriptions[si].topic, sizeof(prof->subscriptions[si].topic)};
            }
            return (FieldTarget){NULL, 0};
    }
}

static void sync_numeric_bufs(const BrokerProfile* prof) {
    snprintf(s_port_str, sizeof(s_port_str), "%d", (int)prof->port);
    snprintf(s_ka_str, sizeof(s_ka_str), "%d", (int)prof->keepalive_secs);
    snprintf(s_ssh_port_str, sizeof(s_ssh_port_str), "%d", (int)prof->ssh_jump_port);
}

static void render_field(const char* label, const char* value, const char* field_id, bool is_active) {
    char lbl_id[64], val_id[64];
    snprintf(lbl_id, sizeof(lbl_id), "%s_Lbl", field_id);
    snprintf(val_id, sizeof(val_id), "%s_Val", field_id);
    Clay_String row_id_cs = ui_utils_clay_string(field_id);
    Clay_String lbl_id_cs = ui_utils_clay_string(lbl_id);
    Clay_String val_id_cs = ui_utils_clay_string(val_id);

    CLAY(CLAY_SID(row_id_cs),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                     .padding = {0, 0, 3, 3},
                     .childGap = 8,
                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                 },
         }) {
        CLAY(CLAY_SID(lbl_id_cs),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_PERCENT(0.30f), CLAY_SIZING_FIT(0)},
                     },
             }) {
            Clay_String ls = ui_utils_clay_string(label);
            CLAY_TEXT(ls, THEME_TEXT_SMALL);
        }

        bool is_selected = is_active && s_active_field_selected;
        char* buf = next_field_buf();
        if (is_active && !is_selected) {
            snprintf(buf, FIELD_BUF_SIZE, "%s|", value ? value : "");
        } else {
            util_str_copy(buf, FIELD_BUF_SIZE, value);
        }
        Clay_String vs = ui_utils_clay_string(buf);

        CLAY(CLAY_SID(val_id_cs),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(24, 0)},
                         .padding = {8, 8, 4, 4},
                     },
                 .backgroundColor =
                     is_selected ? THEME_BG_INPUT_SELECTED : (is_active ? THEME_BG_INPUT_ACTIVE : THEME_BG_INPUT),
                 .cornerRadius = CLAY_CORNER_RADIUS(3),
                 .border =
                     {
                         .width = CLAY_BORDER_OUTSIDE(1),
                         .color = is_active ? THEME_ACCENT_BLUE : THEME_BORDER,
                     },
             }) {
            CLAY_TEXT(vs,
                      CLAY_TEXT_CONFIG({
                          .fontSize = 13,
                          .fontId = FONT_MONO,
                          .textColor = THEME_TEXT_SECONDARY,
                      }));
        }
    }
}

static void render_toggle(const char* label, bool value, const char* toggle_id) {
    char lbl_id[64], btn_id[64];
    snprintf(lbl_id, sizeof(lbl_id), "%s_Lbl", toggle_id);
    snprintf(btn_id, sizeof(btn_id), "%s_Btn", toggle_id);
    Clay_String row_cs = ui_utils_clay_string(toggle_id);
    Clay_String lbl_cs = ui_utils_clay_string(lbl_id);
    Clay_String btn_cs = ui_utils_clay_string(btn_id);

    CLAY(CLAY_SID(row_cs),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                     .padding = {0, 0, 3, 3},
                     .childGap = 8,
                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                 },
         }) {
        CLAY(CLAY_SID(lbl_cs),
             {
                 .layout = {.sizing = {CLAY_SIZING_PERCENT(0.30f), CLAY_SIZING_FIT(0)}},
             }) {
            Clay_String ls = ui_utils_clay_string(label);
            CLAY_TEXT(ls, THEME_TEXT_SMALL);
        }
        CLAY(CLAY_SID(btn_cs),
             {
                 .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .padding = {10, 10, 4, 4}},
                 .backgroundColor = value ? THEME_BG_TOGGLE_ON : THEME_BG_BUTTON,
                 .cornerRadius = CLAY_CORNER_RADIUS(3),
                 .border = {.width = CLAY_BORDER_OUTSIDE(1), .color = value ? THEME_GREEN : THEME_BORDER},
             }) {
            if (value) {
                CLAY_TEXT(CLAY_STRING("\xe2\x9c\x93 Enabled"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 12,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_GREEN,
                          }));
            } else {
                CLAY_TEXT(CLAY_STRING("\xe2\x97\x8b Disabled"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 12,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_TEXT_MUTED,
                          }));
            }
        }
    }
}

// click target id is "{id}_Hdr"
static void render_accordion_header(const char* label, bool expanded, const char* id, const char* suffix) {
    char row_id[64];
    snprintf(row_id, sizeof(row_id), "%s_Hdr", id);
    Clay_String row_cs = ui_utils_clay_string(row_id);

    CLAY(CLAY_SID(row_cs),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                     .padding = {0, 0, 6, 4},
                     .childGap = 6,
                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                 },
         }) {
        CLAY_TEXT(expanded ? CLAY_STRING("v") : CLAY_STRING(">"),
                  CLAY_TEXT_CONFIG({
                      .fontSize = 10,
                      .fontId = FONT_DEFAULT,
                      .textColor = THEME_TEXT_DIM,
                  }));

        char* text = next_field_buf();
        snprintf(text, FIELD_BUF_SIZE, "%s%s", label, suffix ? suffix : "");
        Clay_String ls = ui_utils_clay_string(text);
        CLAY_TEXT(ls,
                  CLAY_TEXT_CONFIG({
                      .fontSize = 10,
                      .fontId = FONT_DEFAULT,
                      .textColor = THEME_TEXT_DIM,
                  }));
    }
}

// base_id generates button IDs: {base_id}_0, {base_id}_1, ...
// sel_idx: which button is currently selected (highlighted)
static void render_selector(const char* label, const char** opt_labels, int count, int sel_idx, const char* base_id) {
    static char s_sel_row_id[64];
    static char s_sel_lbl_id[64];
    static char s_sel_content_id[64];
    snprintf(s_sel_row_id, sizeof(s_sel_row_id), "%s_Row", base_id);
    snprintf(s_sel_lbl_id, sizeof(s_sel_lbl_id), "%s_Lbl", base_id);
    snprintf(s_sel_content_id, sizeof(s_sel_content_id), "%s_Content", base_id);
    Clay_String row_cs = ui_utils_clay_string(s_sel_row_id);
    Clay_String lbl_cs = ui_utils_clay_string(s_sel_lbl_id);
    Clay_String content_cs = ui_utils_clay_string(s_sel_content_id);

    CLAY(CLAY_SID(row_cs),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                     .padding = {0, 0, 3, 3},
                     .childGap = 8,
                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                 },
         }) {
        CLAY(CLAY_SID(lbl_cs),
             {
                 .layout = {.sizing = {CLAY_SIZING_PERCENT(0.30f), CLAY_SIZING_FIT(0)}},
             }) {
            Clay_String ls = ui_utils_clay_string(label);
            CLAY_TEXT(ls, THEME_TEXT_SMALL);
        }
        CLAY(CLAY_SID(content_cs),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .childGap = 8,
                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                     },
             }) {
            for (int i = 0; i < count && i < 8; i++) {
                bool is_sel = (i == sel_idx);
                CLAY(CLAY_SIDI(ui_utils_clay_string(base_id), (uint32_t)i),
                     {
                         .layout =
                             {
                                 .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)},
                                 .padding = {8, 8, 4, 4},
                             },
                         .backgroundColor = is_sel ? THEME_BG_SELECTED : THEME_BG_BUTTON,
                         .cornerRadius = CLAY_CORNER_RADIUS(3),
                         .border =
                             {
                                 .width = CLAY_BORDER_OUTSIDE(1),
                                 .color = is_sel ? THEME_ACCENT_BLUE : THEME_BORDER,
                             },
                     }) {
                    Clay_String os = ui_utils_clay_string(opt_labels[i]);
                    CLAY_TEXT(os,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 12,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = is_sel ? THEME_TEXT_PRIMARY : THEME_TEXT_SECONDARY,
                              }));
                }
            }
        }
    }
}

void profile_dialog_render(AppState* state, Db* db, MqttClient* mqtt) {
    if (!state->profile_dialog_open) return;

    s_field_buf_idx = FIELD_BUF_COUNT - 1;
    s_active_field_selected = state->profile_field_all_selected;
    state->profile_save_flash_timer -= GetFrameTime();
    bool save_flashing = (state->profile_save_flash_timer > 0.0f);

    BrokerProfile* prof = (state->active_profile_idx >= 0 && state->active_profile_idx < 32)
        ? &state->profiles[state->active_profile_idx]
        : NULL;

    // sync numeric string buffers when the active profile changes
    if (state->active_profile_idx != state->profile_last_idx) {
        state->profile_last_idx = state->active_profile_idx;
        state->profile_active_field = -1;
        if (prof) sync_numeric_bufs(prof);
    }

    // any change of focused field drops the selection
    static int s_prev_active_field = -2;
    if (state->profile_active_field != s_prev_active_field) {
        s_prev_active_field = state->profile_active_field;
        state->profile_field_all_selected = false;
    }

    int sub_count = prof ? prof->subscription_count : 0;
    int total_fields = FIDX_SUB_BASE + sub_count;

    if (state->profile_active_field >= 0 && prof) {
        FieldTarget ft = field_target(prof, state->profile_active_field);
        if (ft.buf) {
            if (text_input_select_all_pressed()) {
                state->profile_field_all_selected = true;
            }
            if (!text_input_handle_copy(ft.buf)) {
                int ch;
                while ((ch = GetCharPressed()) != 0) {
                    if (state->profile_field_all_selected) {
                        ft.buf[0] = '\0';
                        state->profile_field_all_selected = false;
                    }
                    size_t len = strlen(ft.buf);
                    if (len + 1 < ft.max) {
                        ft.buf[len] = (char)ch;
                        ft.buf[len + 1] = '\0';
                    }
                }

                if (state->profile_field_all_selected && text_input_paste_pressed()) {
                    ft.buf[0] = '\0'; // paste replaces the selection instead of appending to it
                }

                if (text_input_handle_paste(ft.buf, ft.max, false)) {
                    state->profile_field_all_selected = false;
                }

                bool ctrl_down = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
                if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
                    if (ctrl_down || state->profile_field_all_selected) {
                        ft.buf[0] = '\0';
                    } else {
                        size_t len = strlen(ft.buf);
                        if (len > 0) ft.buf[len - 1] = '\0';
                    }
                    state->profile_field_all_selected = false;
                }
            }
        }
        if (IsKeyPressed(KEY_TAB)) {
            int next = (state->profile_active_field + 1) % total_fields;
            if (!s_ssh_section_expanded && next >= FIDX_SSH_HOST && next <= FIDX_SSH_PASS) {
                next = FIDX_SUB_BASE % total_fields;
            }
            state->profile_active_field = next;
            state->profile_field_all_selected = false;
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            state->profile_active_field = -1;
            state->profile_field_all_selected = false;
        }
    }

    bool close_dialog = false;
    bool save_profile = false;
    bool connect_profile = false;
    bool delete_profile = false;
    bool new_profile = false;
    bool add_sub = false;
    bool toggle_clean_session = false;
    bool toggle_tls_verify = false;
    bool toggle_ssh_tunnel = false;
    int set_protocol = 0; // 0 = no change
    int set_tls_ver = -1; // -1 = no change
    int set_transport = -1; // -1 = no change
    int remove_sub = -1;
    int set_sub_qos_idx = -1;
    int set_sub_qos_val = -1;
    int select_profile = -1;

    // adapt sizing: when no profile is selected the modal fits its content
    bool has_prof = (prof != NULL);
    float screen_h = (float)GetScreenHeight();
    float body_fixed_h = screen_h * 0.70f;
    Clay_SizingAxis modal_vert = CLAY_SIZING_FIT(0); // modal shrinks to content
    Clay_SizingAxis body_vert = has_prof ? CLAY_SIZING_FIXED(body_fixed_h) : CLAY_SIZING_FIT(0);

    CLAY(CLAY_ID("ProfileOverlay"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .childAlignment =
                         {
                             .x = CLAY_ALIGN_X_CENTER,
                             .y = CLAY_ALIGN_Y_CENTER,
                         },
                 },
             .floating =
                 {
                     .attachTo = CLAY_ATTACH_TO_ROOT,
                     .zIndex = 100,
                 },
             .backgroundColor = THEME_BG_OVERLAY,
         }) {
        CLAY(CLAY_ID("ProfileModal"),
             {
                 .layout =
                     {
                         .sizing =
                             {
                                 CLAY_SIZING_FIXED(720),
                                 modal_vert,
                             },
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     },
                 .backgroundColor = THEME_BG_PANEL,
                 .cornerRadius = CLAY_CORNER_RADIUS(6),
                 .border = {.width = CLAY_BORDER_OUTSIDE(1), .color = THEME_BORDER},
             }) {
            // Title bar
            CLAY(CLAY_ID("ProfileTitleBar"),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             .padding = {16, 16, 12, 12},
                             .childGap = 8,
                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                         },
                     .border = {.width = {.bottom = 1}, .color = THEME_BORDER},
                 }) {
                CLAY_TEXT(CLAY_STRING("Broker Profiles"), THEME_TEXT_HEADING);
                CLAY(CLAY_ID("TitleSpacer"),
                     {
                         .layout =
                             {
                                 .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             },
                     }) {}
                CLAY(CLAY_ID("ProfileCloseBtn"),
                     {
                         .layout = {.padding = {8, 8, 4, 4}},
                         .backgroundColor = THEME_BG_BUTTON,
                         .cornerRadius = CLAY_CORNER_RADIUS(3),
                     }) {
                    CLAY_TEXT(CLAY_STRING("\xc3\x97"),
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 14,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = THEME_TEXT_MUTED,
                              }));
                }
            }

            // body: list on left, form on right
            CLAY(CLAY_ID("ProfileBody"),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), body_vert},
                         },
                 }) {
                // Left: profile list
                CLAY(CLAY_ID("ProfileList"),
                     {
                         .layout =
                             {
                                 .sizing = {CLAY_SIZING_PERCENT(0.28f), body_vert},
                                 .layoutDirection = CLAY_TOP_TO_BOTTOM,
                             },
                         .border = {.width = {.right = 1}, .color = THEME_BORDER},
                     }) {
                    CLAY(CLAY_ID("ProfileListScroll"),
                         {
                             .layout =
                                 {
                                     .sizing = {CLAY_SIZING_GROW(0), body_vert},
                                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                     .padding = {8, 8, 8, 8},
                                     .childGap = 4,
                                 },
                             .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()},
                         }) {
                        for (int i = 0; i < state->profile_count; i++) {
                            bool is_active = (i == state->active_profile_idx);
                            CLAY(CLAY_IDI("ProfItem", (uint32_t)i),
                                 {
                                     .layout =
                                         {
                                             .sizing =
                                                 {
                                                     CLAY_SIZING_GROW(0),
                                                     CLAY_SIZING_FIT(0),
                                                 },
                                             .padding = {10, 10, 6, 6},
                                         },
                                     .backgroundColor = is_active ? THEME_BG_SELECTED : (Clay_Color){0, 0, 0, 0},
                                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                                 }) {
                                char* name_buf = next_field_buf();
                                util_str_copy(name_buf, FIELD_BUF_SIZE, state->profiles[i].name);
                                Clay_String ns = ui_utils_clay_string(name_buf);
                                CLAY_TEXT(ns, THEME_TEXT_BODY);
                            }
                        }

                        CLAY(CLAY_ID("NewProfileBtn"),
                             {
                                 .layout =
                                     {
                                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                                         .padding = {10, 10, 6, 6},
                                     },
                                 .backgroundColor = THEME_BG_BUTTON,
                                 .cornerRadius = CLAY_CORNER_RADIUS(3),
                             }) {
                            CLAY_TEXT(CLAY_STRING("+ New Profile"), THEME_TEXT_SMALL);
                        }
                    } // ProfileListScroll
                } // ProfileList

                // right: profile form (only if a profile is selected)
                CLAY(CLAY_ID("ProfileForm"),
                     {
                         .layout =
                             {
                                 .sizing = {CLAY_SIZING_GROW(0), body_vert},
                                 .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                 .padding = {16, 16, 12, 12},
                                 .childGap = 6,
                             },
                         .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()},
                     }) {
                    if (prof) {
                        CLAY_TEXT(CLAY_STRING("CONNECTION"),
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 10,
                                      .fontId = FONT_DEFAULT,
                                      .textColor = THEME_TEXT_DIM,
                                  }));
                        render_field("Name", prof->name, "FldName", state->profile_active_field == FIDX_NAME);
                        render_field(prof->ssh_tunnel_enabled ? "Broker Host (via jump)" : "Host", prof->host,
                                     "FldHost", state->profile_active_field == FIDX_HOST);
                        render_field(prof->ssh_tunnel_enabled ? "Broker Port (via jump)" : "Port", s_port_str,
                                     "FldPort", state->profile_active_field == FIDX_PORT);
                        render_field("Client ID", prof->client_id, "FldClientId",
                                     state->profile_active_field == FIDX_CLIENTID);
                        {
                            static const char* proto_labels[] = {"3.1", "3.1.1", "5.0"};
                            int proto_idx = (prof->protocol_version == 31) ? 0 : (prof->protocol_version == 5) ? 2 : 1;
                            render_selector("Protocol", proto_labels, 3, proto_idx, "SelProto");
                        }
                        render_field("Keepalive (s)", s_ka_str, "FldKeepalive", state->profile_active_field == FIDX_KA);
                        render_toggle("Clean Session", prof->clean_session, "ToggleCleanSession");

                        CLAY_TEXT(CLAY_STRING("AUTHENTICATION"),
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 10,
                                      .fontId = FONT_DEFAULT,
                                      .textColor = THEME_TEXT_DIM,
                                  }));
                        render_field("Username", prof->username, "FldUser", state->profile_active_field == FIDX_USER);
                        {
                            const char* pass_val = (state->profile_active_field == FIDX_PASS)
                                ? prof->password
                                : "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2";
                            render_field("Password", pass_val, "FldPass", state->profile_active_field == FIDX_PASS);
                        }

                        CLAY_TEXT(CLAY_STRING("TRANSPORT"),
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 10,
                                      .fontId = FONT_DEFAULT,
                                      .textColor = THEME_TEXT_DIM,
                                  }));
                        {
                            static const char* tr_labels[] = {"TCP", "WS"};
                            render_selector("Protocol", tr_labels, 2, prof->transport, "SelTransport");
                        }

                        CLAY_TEXT(CLAY_STRING("TLS"),
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 10,
                                      .fontId = FONT_DEFAULT,
                                      .textColor = THEME_TEXT_DIM,
                                  }));
                        {
                            static const char* tls_labels[] = {"Off", "1.2", "1.3"};
                            int tls_idx = (prof->tls_version == 12) ? 1 : (prof->tls_version == 13) ? 2 : 0;
                            render_selector("Version", tls_labels, 3, tls_idx, "SelTlsVer");
                        }
                        render_field("CA Certificate", prof->tls_ca_cert, "FldCA",
                                     state->profile_active_field == FIDX_CA);
                        render_field("Client Cert", prof->tls_client_cert, "FldCert",
                                     state->profile_active_field == FIDX_CERT);
                        render_field("Client Key", prof->tls_client_key, "FldKey",
                                     state->profile_active_field == FIDX_KEY);
                        render_toggle("Verify Host", prof->tls_verify, "ToggleTlsVerify");

                        render_accordion_header("SSH TUNNEL", s_ssh_section_expanded, "SshSection",
                                                prof->ssh_tunnel_enabled ? " (enabled)" : NULL);
                        if (s_ssh_section_expanded) {
                            render_toggle("Enable SSH Tunnel", prof->ssh_tunnel_enabled, "ToggleSshTunnel");
                            render_field("Jump Host", prof->ssh_jump_host, "FldSshHost",
                                         state->profile_active_field == FIDX_SSH_HOST);
                            render_field("Jump Port", s_ssh_port_str, "FldSshPort",
                                         state->profile_active_field == FIDX_SSH_PORT);
                            render_field("Jump User", prof->ssh_jump_user, "FldSshUser",
                                         state->profile_active_field == FIDX_SSH_USER);
                            {
                                const char* ssh_pass_val = (state->profile_active_field == FIDX_SSH_PASS)
                                    ? prof->ssh_jump_password
                                    : "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2";
                                render_field("Jump Password", ssh_pass_val, "FldSshPass",
                                             state->profile_active_field == FIDX_SSH_PASS);
                            }
                            render_field("Private Key", prof->ssh_jump_key_path, "FldSshKey",
                                         state->profile_active_field == FIDX_SSH_KEY);
                        }

                        CLAY_TEXT(CLAY_STRING("SUBSCRIPTIONS"),
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 10,
                                      .fontId = FONT_DEFAULT,
                                      .textColor = THEME_TEXT_DIM,
                                  }));
                        for (int s = 0; s < prof->subscription_count; s++) {
                            bool sub_active = (state->profile_active_field == FIDX_SUB_BASE + s);

                            // Build topic display (cursor when active)
                            char* sub_buf = next_field_buf();
                            if (sub_active) {
                                snprintf(sub_buf, FIELD_BUF_SIZE, "%s|", prof->subscriptions[s].topic);
                            } else {
                                util_str_copy(sub_buf, FIELD_BUF_SIZE, prof->subscriptions[s].topic);
                            }

                            Clay_String topic_cs = ui_utils_clay_string(sub_buf);

                            CLAY(CLAY_IDI("Sub", (uint32_t)s),
                                 {
                                     .layout =
                                         {
                                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                                             .padding = {0, 0, 3, 3},
                                             .childGap = 8,
                                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                                         },
                                 }) {
                                CLAY(CLAY_IDI("SubLbl", (uint32_t)s),
                                     {
                                         .layout = {.sizing = {CLAY_SIZING_PERCENT(0.30f), CLAY_SIZING_FIT(0)}},
                                     }) {
                                    CLAY_TEXT(CLAY_STRING("Topic"), THEME_TEXT_SMALL);
                                }
                                CLAY(CLAY_IDI("SubVal", (uint32_t)s),
                                     {
                                         .layout =
                                             {
                                                 .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                                                 .childGap = 8,
                                                 .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                                             },
                                     }) {
                                    CLAY(CLAY_IDI("SubInp", (uint32_t)s),
                                         {
                                             .layout =
                                                 {
                                                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(24, 0)},
                                                     .padding = {8, 8, 4, 4},
                                                 },
                                             .backgroundColor = sub_active ? THEME_BG_INPUT_ACTIVE : THEME_BG_INPUT,
                                             .cornerRadius = CLAY_CORNER_RADIUS(3),
                                             .border =
                                                 {
                                                     .width = CLAY_BORDER_OUTSIDE(1),
                                                     .color = sub_active ? THEME_ACCENT_BLUE : THEME_BORDER,
                                                 },
                                         }) {
                                        CLAY_TEXT(topic_cs,
                                                  CLAY_TEXT_CONFIG({
                                                      .fontSize = 13,
                                                      .fontId = FONT_MONO,
                                                      .textColor = THEME_TEXT_SECONDARY,
                                                  }));
                                    }
                                    // QoS toggle buttons (0 / 1 / 2)
                                    static const char* s_qlbls[] = {"0", "1", "2"};
                                    for (int q = 0; q < 3; q++) {
                                        bool qos_sel = ((int)prof->subscriptions[s].qos == q);
                                        Clay_String ql = {.length = 1, .chars = s_qlbls[q]};
                                        CLAY(CLAY_IDI("SubQos", (uint32_t)(s * 3 + q)),
                                             {
                                                 .layout = {.padding = {8, 8, 4, 4}},
                                                 .backgroundColor = qos_sel ? THEME_BG_SELECTED : THEME_BG_BUTTON,
                                                 .cornerRadius = CLAY_CORNER_RADIUS(3),
                                             }) {
                                            CLAY_TEXT(ql, THEME_TEXT_SMALL);
                                        }
                                    }
                                }
                            }
                        }

                        // Add / Remove subscription buttons
                        CLAY(CLAY_ID("SubButtons"),
                             {
                                 .layout =
                                     {
                                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                                         .childGap = 6,
                                         .padding = {0, 0, 4, 4},
                                     },
                             }) {
                            if (prof->subscription_count < MAX_PROFILE_SUBS) {
                                CLAY(CLAY_ID("AddSubBtn"),
                                     {
                                         .layout = {.padding = {10, 10, 4, 4}},
                                         .backgroundColor = THEME_BG_BUTTON,
                                         .cornerRadius = CLAY_CORNER_RADIUS(3),
                                     }) {
                                    CLAY_TEXT(CLAY_STRING("+ Add Topic"), THEME_TEXT_SMALL);
                                }
                            }
                            if (prof->subscription_count > 0) {
                                CLAY(CLAY_ID("RemSubBtn"),
                                     {
                                         .layout = {.padding = {10, 10, 4, 4}},
                                         .backgroundColor = THEME_BG_DANGER,
                                         .cornerRadius = CLAY_CORNER_RADIUS(3),
                                     }) {
                                    CLAY_TEXT(CLAY_STRING("- Remove Last"), THEME_TEXT_SMALL);
                                }
                            }
                        }
                    } else {
                        CLAY_TEXT(CLAY_STRING("Select a profile or create a new one."), THEME_TEXT_SMALL);
                    }
                }
            }

            // Hover states from previous frame's bounding boxes
            bool del_hover = prof && Clay_PointerOver(Clay_GetElementId(CLAY_STRING("BtnDelete")));
            bool save_hover = prof && Clay_PointerOver(Clay_GetElementId(CLAY_STRING("BtnSave")));
            bool conn_hover = prof && Clay_PointerOver(Clay_GetElementId(CLAY_STRING("BtnConnect")));

            CLAY(CLAY_ID("ProfileActions"),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             .padding = {16, 16, 10, 10},
                             .childGap = 8,
                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                         },
                     .border = {.width = {.top = 1}, .color = THEME_BORDER},
                 }) {
                CLAY(CLAY_ID("ActionSpacer"),
                     {
                         .layout =
                             {
                                 .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             },
                     }) {}

                if (prof) {
                    CLAY(CLAY_ID("BtnDelete"), {
                        .layout = { .padding = { 12, 12, 6, 6 } },
.backgroundColor = del_hover ? THEME_BG_DANGER_HOVER : THEME_BG_DANGER,
                        .cornerRadius = CLAY_CORNER_RADIUS(4),
                        .border = del_hover
                        ? (Clay_BorderElementConfig) {
                            .width = CLAY_BORDER_OUTSIDE(1), .color = THEME_BORDER_DANGER,
                        }
: (Clay_BorderElementConfig) { 0 },
                    }) {
                        CLAY_TEXT(CLAY_STRING("Delete"),
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 12,
                                      .fontId = FONT_DEFAULT,
                                      .textColor = del_hover ? THEME_TEXT_DANGER : THEME_TEXT_MUTED,
                                  }));
                    }
                    CLAY(CLAY_ID("BtnSave"), {
                        .layout = { .padding = { 12, 12, 6, 6 } },
                        .backgroundColor = save_flashing ? THEME_BG_FLASH_OK
                                           : save_hover  ? THEME_BG_BTN_HOVER
                                                         : THEME_BG_BUTTON,
                        .cornerRadius = CLAY_CORNER_RADIUS(4),
                        .border = save_flashing
                        ? (Clay_BorderElementConfig) {
                            .width = CLAY_BORDER_OUTSIDE(1), .color = THEME_GREEN,
                        }
: save_hover ? (Clay_BorderElementConfig) {
                            .width = CLAY_BORDER_OUTSIDE(1), .color = THEME_BORDER,
                        }
: (Clay_BorderElementConfig) { 0 },
                    }) {
                        CLAY_TEXT(save_flashing ? CLAY_STRING("Saved!") : CLAY_STRING("Save"),
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 12,
                                      .fontId = FONT_DEFAULT,
                                      .textColor = save_flashing ? THEME_GREEN
                                          : save_hover           ? THEME_TEXT_PRIMARY
                                                                 : THEME_TEXT_MUTED,
                                  }));
                    }
                    CLAY(CLAY_ID("BtnConnect"), {
                        .layout = { .padding = { 12, 12, 6, 6 } },
.backgroundColor = conn_hover ? (Clay_Color) { 68, 120, 68, 255 } : THEME_BG_BTN_PRI,
                        .cornerRadius = CLAY_CORNER_RADIUS(4),
                        .border = conn_hover
                        ? (Clay_BorderElementConfig) {
                            .width = CLAY_BORDER_OUTSIDE(1), .color = THEME_GREEN,
                        }
: (Clay_BorderElementConfig) { 0 },
                    }) {
                        CLAY_TEXT(CLAY_STRING("Connect"),
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 13,
                                      .fontId = FONT_DEFAULT,
                                      .textColor = THEME_TEXT_PRIMARY,
                                  }));
                    }
                }
            }
        }
    }

    // Click detection (outside all CLAY blocks)
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ProfileOverlay"))) &&
        !Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ProfileModal"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        close_dialog = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ProfileCloseBtn"))) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        close_dialog = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("NewProfileBtn"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        new_profile = true;
    }
    if (prof) {
        if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("BtnDelete"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            delete_profile = true;
        }
        if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("BtnSave"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            save_profile = true;
        }
        if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("BtnConnect"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            connect_profile = true;
        }
        if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("AddSubBtn"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            add_sub = true;
        }
        if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("RemSubBtn"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            remove_sub = prof->subscription_count - 1;
        }

        // Toggle click detection
        if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ToggleCleanSession_Btn"))) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            toggle_clean_session = true;
        }
        if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ToggleTlsVerify_Btn"))) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            toggle_tls_verify = true;
        }
        if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ToggleSshTunnel_Btn"))) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            toggle_ssh_tunnel = true;
        }
        if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("SshSection_Hdr"))) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            s_ssh_section_expanded = !s_ssh_section_expanded;
            if (!s_ssh_section_expanded && state->profile_active_field >= FIDX_SSH_HOST &&
                state->profile_active_field <= FIDX_SSH_PASS) {
                state->profile_active_field = -1;
                state->profile_field_all_selected = false;
            }
        }

        // Protocol selector clicks
        {
            static const int proto_values[] = {31, 311, 5};
            for (int i = 0; i < 3; i++) {
                if (Clay_PointerOver(CLAY_IDI("SelProto", (uint32_t)i)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    set_protocol = proto_values[i];
                }
            }
        }
        // TLS version selector clicks
        {
            static const int tls_values[] = {0, 12, 13};
            for (int i = 0; i < 3; i++) {
                if (Clay_PointerOver(CLAY_IDI("SelTlsVer", (uint32_t)i)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    set_tls_ver = tls_values[i];
                }
            }
        }
        // Transport selector clicks
        {
            for (int i = 0; i < 3; i++) {
                if (Clay_PointerOver(CLAY_IDI("SelTransport", (uint32_t)i)) &&
                    IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    set_transport = i;
                }
            }
        }

        // Field click to set focus
        static const struct {
            const char* val_id;
            int fidx;
        } kFieldClicks[] = {
            {"FldName_Val", FIDX_NAME},        {"FldHost_Val", FIDX_HOST},
            {"FldPort_Val", FIDX_PORT},        {"FldClientId_Val", FIDX_CLIENTID},
            {"FldKeepalive_Val", FIDX_KA},     {"FldUser_Val", FIDX_USER},
            {"FldPass_Val", FIDX_PASS},        {"FldCA_Val", FIDX_CA},
            {"FldCert_Val", FIDX_CERT},        {"FldKey_Val", FIDX_KEY},
            {"FldSshHost_Val", FIDX_SSH_HOST}, {"FldSshPort_Val", FIDX_SSH_PORT},
            {"FldSshUser_Val", FIDX_SSH_USER}, {"FldSshPass_Val", FIDX_SSH_PASS},
            {"FldSshKey_Val", FIDX_SSH_KEY},
        };
        for (int fi = 0; fi < (int)(sizeof(kFieldClicks) / sizeof(kFieldClicks[0])); fi++) {
            if (Clay_PointerOver(Clay_GetElementId(ui_utils_clay_string(kFieldClicks[fi].val_id))) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                state->profile_active_field = kFieldClicks[fi].fidx;
                state->profile_field_all_selected = false;
            }
        }
        // Subscription field clicks
        for (int s = 0; s < prof->subscription_count; s++) {
            if (Clay_PointerOver(CLAY_IDI("SubInp", (uint32_t)s)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                state->profile_active_field = FIDX_SUB_BASE + s;
                state->profile_field_all_selected = false;
            }
        }
        // Subscription QoS button clicks
        for (int s = 0; s < prof->subscription_count; s++) {
            for (int q = 0; q < 3; q++) {
                if (Clay_PointerOver(CLAY_IDI("SubQos", (uint32_t)(s * 3 + q))) &&
                    IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    set_sub_qos_idx = s;
                    set_sub_qos_val = q;
                }
            }
        }
    }

    // Profile list item clicks
    for (int i = 0; i < state->profile_count; i++) {
        if (Clay_PointerOver(CLAY_IDI("ProfItem", (uint32_t)i)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            select_profile = i;
        }
    }

    if (select_profile >= 0) {
        state->active_profile_idx = select_profile;
    }
    if (close_dialog) {
        state->profile_dialog_open = false;
        state->profile_active_field = -1;
    }
    if (new_profile) {
        if (state->profile_count < 32) {
            broker_profile_init_default(&state->profiles[state->profile_count]);
            util_str_copy(state->profiles[state->profile_count].name, sizeof(state->profiles[0].name), "New Profile");
            state->active_profile_idx = state->profile_count;
            state->profile_count++;
            prof = &state->profiles[state->active_profile_idx];
            sync_numeric_bufs(prof);
            state->profile_last_idx = state->active_profile_idx;
            state->profile_active_field = FIDX_NAME; // auto-focus Name for quick rename
        }
    }
    if (add_sub && prof && prof->subscription_count < MAX_PROFILE_SUBS) {
        int si = prof->subscription_count;
        prof->subscriptions[si].topic[0] = '#';
        prof->subscriptions[si].topic[1] = '\0';
        prof->subscriptions[si].qos = 1;
        prof->subscription_count++;
        state->profile_active_field = FIDX_SUB_BASE + si; // auto-focus new sub topic
    }
    if (remove_sub >= 0 && prof && remove_sub < prof->subscription_count) {
        prof->subscription_count--;
        if (state->profile_active_field >= FIDX_SUB_BASE + prof->subscription_count) {
            state->profile_active_field = -1;
        }
    }
    if (set_sub_qos_idx >= 0 && prof && set_sub_qos_idx < prof->subscription_count) {
        prof->subscriptions[set_sub_qos_idx].qos = (uint8_t)set_sub_qos_val;
    }
    if (toggle_clean_session && prof) {
        prof->clean_session = !prof->clean_session;
    }
    if (toggle_tls_verify && prof) {
        prof->tls_verify = !prof->tls_verify;
    }
    if (toggle_ssh_tunnel && prof) {
        prof->ssh_tunnel_enabled = !prof->ssh_tunnel_enabled;
    }
    if (set_protocol != 0 && prof) {
        prof->protocol_version = set_protocol;
    }
    if (set_tls_ver >= 0 && prof) {
        prof->tls_version = set_tls_ver;
    }
    if (set_transport >= 0 && prof) {
        prof->transport = set_transport;
    }
    if (save_profile && prof && db) {
        prof->port = (uint16_t)atoi(s_port_str);
        prof->keepalive_secs = (uint16_t)atoi(s_ka_str);
        prof->ssh_jump_port = (uint16_t)atoi(s_ssh_port_str);
        if (db_save_profile(db, prof)) {
            state->profile_save_flash_timer = 1.0f;
        }
    }
    if (delete_profile && prof && db) {
        db_delete_profile(db, prof->id);
        int del_idx = state->active_profile_idx;
        for (int i = del_idx; i < state->profile_count - 1; i++) {
            state->profiles[i] = state->profiles[i + 1];
        }
        memset(&state->profiles[state->profile_count - 1], 0, sizeof(BrokerProfile));
        state->profile_count--;
        if (state->profile_count == 0) {
            state->active_profile_idx = -1;
        } else if (del_idx >= state->profile_count) {
            state->active_profile_idx = state->profile_count - 1;
        } else {
            state->active_profile_idx = del_idx;
        }
        state->profile_active_field = -1;
        state->profile_last_idx = -2;
    }
    if (connect_profile && prof) {
        prof->port = (uint16_t)atoi(s_port_str);
        prof->keepalive_secs = (uint16_t)atoi(s_ka_str);
        prof->ssh_jump_port = (uint16_t)atoi(s_ssh_port_str);
        if (db) db_save_profile(db, prof);
        mqtt_client_disconnect(mqtt);
        MqttConnectOpts opts = {
            .host = prof->host,
            .port = prof->port,
            .client_id = prof->client_id[0] ? prof->client_id : NULL,
            .username = prof->username[0] ? prof->username : NULL,
            .password = prof->password[0] ? prof->password : NULL,
            .clean_session = prof->clean_session,
            .keepalive_secs = prof->keepalive_secs,
            .protocol_version = prof->protocol_version,
            .transport = prof->transport,
            .tls_ca_cert = prof->tls_ca_cert,
            .tls_client_cert = prof->tls_client_cert,
            .tls_client_key = prof->tls_client_key,
            .tls_version = prof->tls_version,
            .tls_verify = prof->tls_verify,
            .ssh_tunnel_enabled = prof->ssh_tunnel_enabled,
            .ssh_jump_host = prof->ssh_jump_host,
            .ssh_jump_port = prof->ssh_jump_port,
            .ssh_jump_user = prof->ssh_jump_user[0] ? prof->ssh_jump_user : NULL,
            .ssh_jump_key_path = prof->ssh_jump_key_path[0] ? prof->ssh_jump_key_path : NULL,
            .ssh_jump_password = prof->ssh_jump_password[0] ? prof->ssh_jump_password : NULL,
        };
        if (mqtt_client_connect(mqtt, &opts)) {
            util_str_copy(state->broker_host, sizeof(state->broker_host), prof->host);
            state->broker_port = prof->port;
            state->subscription_count = 0;
            for (int i = 0; i < prof->subscription_count; i++) {
                MqttSubscribeOpts sub = {
                    .topic_filter = prof->subscriptions[i].topic,
                    .qos = prof->subscriptions[i].qos,
                };
                mqtt_client_subscribe(mqtt, &sub);
                if (state->subscription_count < MAX_SUBSCRIPTIONS) {
                    Subscription* ss = &state->subscriptions[state->subscription_count++];
                    memset(ss, 0, sizeof(*ss));
                    util_str_copy(ss->topic_filter, sizeof(ss->topic_filter), prof->subscriptions[i].topic);
                    ss->qos = prof->subscriptions[i].qos;
                    ss->state = SUB_STATE_ACTIVE;
                }
            }
        }
        state->profile_dialog_open = false;
        state->profile_active_field = -1;
    }
}
