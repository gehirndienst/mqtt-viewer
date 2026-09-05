// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "clay.h"
#include "raylib.h"

#include "model/topic_tree.h"
#include "ui/context_menu.h"
#include "ui/theme.h"
#include "ui/ui_util.h"

static void set_expanded_recursive(TopicNode* node, bool expanded) {
    node->expanded = expanded;
    for (uint32_t i = 0; i < node->child_count; i++) {
        if (node->children[i]) set_expanded_recursive(node->children[i], expanded);
    }
}

void context_menu_render(AppState* state) {
    if (!state->context_menu_open) return;
    if (!state->context_menu_target) return;

    // Deferred action flags
    bool do_copy = false;
    bool do_publish = false;
    bool do_expand = false;
    bool do_collapse = false;
    bool do_clear = false;

    // Static string IDs for menu items - hashed immediately by CLAY_SID
    static const char* id_copy = "CMCopy";
    static const char* id_publish = "CMPublish";
    static const char* id_expand = "CMExpand";
    static const char* id_collapse = "CMCollapse";
    static const char* id_clear = "CMClear";

    // Menu item label strings - must be in static storage for CLAY_TEXT
    static const char* label_copy = "Copy Topic Path";
    static const char* label_publish = "Publish to This Topic";
    static const char* label_expand = "Expand All";
    static const char* label_collapse = "Collapse All";
    static const char* label_clear = "Clear History";

    CLAY(CLAY_ID("ContextMenu"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .padding = {4, 4, 4, 4},
                     .childGap = 2,
                 },
             .backgroundColor = THEME_BG_PANEL,
             .cornerRadius = CLAY_CORNER_RADIUS(4),
             .border = {.width = CLAY_BORDER_OUTSIDE(1), .color = THEME_BORDER},
             .floating =
                 {
                     .attachTo = CLAY_ATTACH_TO_ROOT,
                     .zIndex = 200,
                     .offset = {.x = state->context_menu_x, .y = state->context_menu_y},
                 },
         }) {
        // Copy Topic Path
        Clay_String copy_id_cs = ui_utils_clay_string(id_copy);
        CLAY(CLAY_SID(copy_id_cs),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {12, 12, 6, 6},
                     },
                 .cornerRadius = CLAY_CORNER_RADIUS(3),
             }) {
            Clay_String lbl = ui_utils_clay_string(label_copy);
            CLAY_TEXT(lbl,
                      CLAY_TEXT_CONFIG({
                          .fontSize = 13,
                          .fontId = FONT_DEFAULT,
                          .textColor = THEME_TEXT_PRIMARY,
                      }));
        }

        // Publish Here
        Clay_String publish_id_cs = ui_utils_clay_string(id_publish);
        CLAY(CLAY_SID(publish_id_cs),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {12, 12, 6, 6},
                     },
                 .cornerRadius = CLAY_CORNER_RADIUS(3),
             }) {
            Clay_String lbl = ui_utils_clay_string(label_publish);
            CLAY_TEXT(lbl,
                      CLAY_TEXT_CONFIG({
                          .fontSize = 13,
                          .fontId = FONT_DEFAULT,
                          .textColor = THEME_TEXT_PRIMARY,
                      }));
        }

        // Separator 1
        CLAY(CLAY_ID("CMSep1"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {12, 12, 3, 3},
                     },
             }) {
            CLAY(CLAY_ID("CMSep1Line"),
                 {
                     .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1)}},
                     .backgroundColor = THEME_BORDER,
                 }) {}
        }

        // Expand All
        Clay_String expand_id_cs = ui_utils_clay_string(id_expand);
        CLAY(CLAY_SID(expand_id_cs),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {12, 12, 6, 6},
                     },
                 .cornerRadius = CLAY_CORNER_RADIUS(3),
             }) {
            Clay_String lbl = ui_utils_clay_string(label_expand);
            CLAY_TEXT(lbl,
                      CLAY_TEXT_CONFIG({
                          .fontSize = 13,
                          .fontId = FONT_DEFAULT,
                          .textColor = THEME_TEXT_PRIMARY,
                      }));
        }

        // Collapse All
        Clay_String collapse_id_cs = ui_utils_clay_string(id_collapse);
        CLAY(CLAY_SID(collapse_id_cs),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {12, 12, 6, 6},
                     },
                 .cornerRadius = CLAY_CORNER_RADIUS(3),
             }) {
            Clay_String lbl = ui_utils_clay_string(label_collapse);
            CLAY_TEXT(lbl,
                      CLAY_TEXT_CONFIG({
                          .fontSize = 13,
                          .fontId = FONT_DEFAULT,
                          .textColor = THEME_TEXT_PRIMARY,
                      }));
        }

        // Separator 2
        CLAY(CLAY_ID("CMSep2"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {12, 12, 3, 3},
                     },
             }) {
            CLAY(CLAY_ID("CMSep2Line"),
                 {
                     .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1)}},
                     .backgroundColor = THEME_BORDER,
                 }) {}
        }

        // Clear History
        Clay_String clear_id_cs = ui_utils_clay_string(id_clear);
        CLAY(CLAY_SID(clear_id_cs),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {12, 12, 6, 6},
                     },
                 .cornerRadius = CLAY_CORNER_RADIUS(3),
             }) {
            Clay_String lbl = ui_utils_clay_string(label_clear);
            CLAY_TEXT(lbl,
                      CLAY_TEXT_CONFIG({
                          .fontSize = 13,
                          .fontId = FONT_DEFAULT,
                          .textColor = THEME_RED,
                      }));
        }
    }

    // Build Clay_String IDs for detection
    Clay_String copy_id_cs = ui_utils_clay_string(id_copy);
    Clay_String publish_id_cs = ui_utils_clay_string(id_publish);
    Clay_String expand_id_cs = ui_utils_clay_string(id_expand);
    Clay_String collapse_id_cs = ui_utils_clay_string(id_collapse);
    Clay_String clear_id_cs = ui_utils_clay_string(id_clear);

    if (Clay_PointerOver(Clay_GetElementId(copy_id_cs)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        do_copy = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(publish_id_cs)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        do_publish = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(expand_id_cs)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        do_expand = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(collapse_id_cs)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        do_collapse = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(clear_id_cs)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        do_clear = true;
    }

    // Dismiss on Escape or click-outside (checked AFTER item click detection)
    if (state->context_menu_open) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            state->context_menu_open = false;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            !Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ContextMenu")))) {
            state->context_menu_open = false;
        }
    }

    // deferred actions
    if (do_copy) {
        static char full_path[512];
        topic_node_full_path(state->context_menu_target, full_path, sizeof(full_path));
        SetClipboardText(full_path);
        state->context_menu_open = false;
    }
    if (do_publish) {
        topic_node_full_path(state->context_menu_target, state->publish_topic, sizeof(state->publish_topic));
        state->publish_panel_open = true;
        state->context_menu_open = false;
    }
    if (do_expand) {
        set_expanded_recursive(state->context_menu_target, true);
        state->context_menu_open = false;
    }
    if (do_collapse) {
        set_expanded_recursive(state->context_menu_target, false);
        state->context_menu_open = false;
    }
    if (do_clear) {
        TopicNode* n = state->context_menu_target;
        uint32_t cleared = n->message_count;
        for (TopicNode* p = n; p; p = p->parent) {
            p->subtree_message_count -= cleared;
            if (p->subtree_message_count > 0) {
                snprintf(p->subtree_count_str, sizeof(p->subtree_count_str), "\xce\xa3 %u", p->subtree_message_count);
            } else {
                p->subtree_count_str[0] = '\0';
            }
        }
        n->message_count = 0;
        n->msg_count_str[0] = '\0';
        n->last_payload_preview[0] = '\0';
        n->has_retained = false;
        state->context_menu_open = false;
    }
}
