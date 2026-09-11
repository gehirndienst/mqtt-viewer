// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef TOPIC_TREE_H
#define TOPIC_TREE_H

#include "model/pool_alloc.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TOPIC_MAX_CHILDREN 64
#define TOPIC_PREVIEW_LEN 16384

// 256 x 16 KB = 4 MB per slab, calloc'd so pages stay lazy until touched
#define TOPIC_NODE_PAYLOAD_PREVIEW_SLAB_CAPACITY 256

typedef struct TopicNode {
    struct TopicNode* parent;
    struct TopicNode* children[TOPIC_MAX_CHILDREN];
    char* last_payload_preview; // TOPIC_PREVIEW_LEN bytes or NULL until the first message
    uint64_t last_message_ts; // microseconds
    uint64_t last_subtree_message_ts; // microseconds; last message anywhere in this node's subtree
    uint64_t last_display_update_us;
    uint64_t last_subtree_display_update_us;
    uint32_t child_count;
    uint32_t message_count;
    uint32_t subtree_message_count;
    uint32_t last_payload_len; // bytes
    float throughput; // computed by main.c for selected topic
    uint8_t last_qos;
    bool has_retained;
    bool expanded; // UI state: tree node expanded
    bool filter_match; // UI state: this node or a descendant matches the current topic filter
    char segment[128];
    char msg_count_str[16];
    char subtree_count_str[16];
} TopicNode;

typedef struct {
    PoolAlloc node_pool;
    PoolAlloc preview_pool; // TOPIC_PREVIEW_LEN blocks, handed out lazily by topic_node_preview_buf()
    TopicNode* roots[TOPIC_MAX_CHILDREN];
    uint32_t root_count;
    uint32_t total_count;
} TopicTree;

static_assert(TOPIC_MAX_CHILDREN <= 256, "TOPIC_MAX_CHILDREN must fit a uint8_t scan loop");
static_assert(TOPIC_PREVIEW_LEN >= 16, "TOPIC_PREVIEW_LEN must hold at least a short preview string");

/**
 * @brief Initialize an empty topic tree backed by a pool allocator.
 * @param tree              Tree to initialize (caller-allocated).
 * @param initial_pool_size Initial slab capacity (nodes per first slab).
 */
void topic_tree_init(TopicTree* tree, uint32_t initial_pool_size);

/** @brief Destroy the tree and free all nodes. */
void topic_tree_destroy(TopicTree* tree);

/**
 * @brief Insert or find the node for @p topic, creating intermediate nodes
 *        as needed. The full topic path is tokenised on '/'.
 * @param tree   Tree handle.
 * @param topic  NUL-terminated MQTT topic string (e.g. "a/b/c").
 * @return Pointer to the leaf TopicNode, or NULL on OOM.
 */
TopicNode* topic_tree_insert(TopicTree* tree, const char* topic);

/**
 * @brief Find the node for @p topic without creating missing nodes.
 * @return Pointer to the leaf TopicNode, or NULL if not found.
 */
TopicNode* topic_tree_find(const TopicTree* tree, const char* topic);

/**
 * @brief Record one received message on @p node.
 *
 * Increments the node's own message_count and the subtree_message_count
 * of the node and every ancestor up to the root.
 */
void topic_node_count_message(TopicNode* node);

/**
 * @brief Forget every message counted on @p node itself: own count, badge strings, preview, retained flag, and the
 *        matching share of the subtree counts up to the root. Children are untouched.
 * @return The node's message_count before the reset.
 */
uint32_t topic_node_clear_messages(TopicNode* node);

/** @brief Total number of leaf and intermediate nodes. */
uint32_t topic_tree_count(const TopicTree* tree);

/**
 * @brief Read-only view of the node's latest payload preview.
 * @return NUL-terminated string; "" when the node has never carried a payload.
 */
const char* topic_node_preview(const TopicNode* node);

/**
 * @brief Writable preview buffer of TOPIC_PREVIEW_LEN bytes, allocated from the tree's
 *        preview pool on first use. Only called for nodes that actually receive messages.
 */
char* topic_node_preview_buf(TopicTree* tree, TopicNode* node);

/** @brief Empty the preview without releasing its block (the node may receive messages again). */
void topic_node_preview_clear(TopicNode* node);

/**
 * @brief Write the full slash-separated topic path of @p node into @p buf.
 * @param node      Node whose path to reconstruct (walks parent pointers).
 * @param buf       Destination buffer.
 * @param buf_size  Size of @p buf in bytes.
 */
void topic_node_full_path(const TopicNode* node, char* buf, size_t buf_size);

#endif
