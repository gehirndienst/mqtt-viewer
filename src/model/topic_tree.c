// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <string.h>

#include "model/topic_tree.h"

void topic_tree_init(TopicTree* tree, uint32_t initial_pool_size) {
    pool_alloc_init(&tree->node_pool, sizeof(TopicNode), initial_pool_size);
    tree->root_count = 0;
    tree->total_count = 0;
    memset(tree->roots, 0, sizeof(tree->roots));
}

void topic_tree_destroy(TopicTree* tree) {
    pool_alloc_destroy(&tree->node_pool);
    tree->root_count = 0;
    tree->total_count = 0;
}

static TopicNode* node_new(TopicTree* tree, const char* segment, TopicNode* parent) {
    TopicNode* node = pool_alloc_get(&tree->node_pool);
    strncpy(node->segment, segment, sizeof(node->segment) - 1);
    node->segment[sizeof(node->segment) - 1] = '\0';
    node->parent = parent;
    node->child_count = 0;
    node->message_count = 0;
    node->has_retained = false;
    node->expanded = false;
    node->last_message_ts = 0;
    node->last_payload_preview[0] = '\0';
    node->throughput = 0.0f;
    tree->total_count++;
    return node;
}

static TopicNode* find_child(TopicNode* const* children, uint32_t count, const char* segment) {
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(children[i]->segment, segment) == 0) {
            return children[i];
        }
    }
    return NULL;
}

static void add_child(TopicNode** children, uint32_t* count, TopicNode* child) {
    if (*count < TOPIC_MAX_CHILDREN) {
        children[*count] = child;
        (*count)++;
    }
}

TopicNode* topic_tree_insert(TopicTree* tree, const char* topic) {
    char buf[512];
    strncpy(buf, topic, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    TopicNode** children = tree->roots;
    uint32_t* child_count = &tree->root_count;
    TopicNode* parent = NULL;
    TopicNode* current = NULL;

    char* saveptr;
    char* seg = strtok_r(buf, "/", &saveptr);
    while (seg) {
        current = find_child(children, *child_count, seg);
        if (!current) {
            if (*child_count >= TOPIC_MAX_CHILDREN) {
                return NULL; // capacity limit reached
            }
            current = node_new(tree, seg, parent);
            add_child(children, child_count, current);
        }
        parent = current;
        children = current->children;
        child_count = &current->child_count;
        seg = strtok_r(NULL, "/", &saveptr);
    }

    return current;
}

TopicNode* topic_tree_find(const TopicTree* tree, const char* topic) {
    char buf[512];
    strncpy(buf, topic, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    TopicNode* const* children = tree->roots;
    uint32_t child_count = tree->root_count;
    TopicNode* current = NULL;

    char* saveptr;
    char* seg = strtok_r(buf, "/", &saveptr);
    while (seg) {
        current = find_child(children, child_count, seg);
        if (!current) {
            return NULL;
        }
        children = current->children;
        child_count = current->child_count;
        seg = strtok_r(NULL, "/", &saveptr);
    }

    return current;
}

uint32_t topic_tree_count(const TopicTree* tree) {
    return tree->total_count;
}

void topic_node_full_path(const TopicNode* node, char* buf, size_t buf_size) {
    if (buf_size == 0) {
        return;
    }
    const TopicNode* stack[64];
    int depth = 0;
    for (const TopicNode* n = node; n && depth < 64; n = n->parent) {
        stack[depth++] = n;
    }

    buf[0] = '\0';
    size_t pos = 0;
    for (int i = depth - 1; i >= 0; i--) {
        size_t seg_len = strlen(stack[i]->segment);
        if (pos + seg_len + (i < depth - 1 ? 1 : 0) >= buf_size) {
            break;
        }
        if (i < depth - 1) {
            buf[pos++] = '/';
        }
        memcpy(buf + pos, stack[i]->segment, seg_len);
        pos += seg_len;
    }
    buf[pos] = '\0';
}
