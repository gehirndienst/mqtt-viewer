// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/topic_tree.h"
#include "test_helpers.h"

TEST(create_and_destroy) {
    TopicTree tree;
    topic_tree_init(&tree, 256);
    ASSERT_EQ(topic_tree_count(&tree), 0);
    topic_tree_destroy(&tree);
}

TEST(insert_single_level) {
    TopicTree tree;
    topic_tree_init(&tree, 256);
    TopicNode* node = topic_tree_insert(&tree, "test");
    ASSERT_NOT_NULL(node);
    ASSERT_STR_EQ(node->segment, "test");
    ASSERT_NULL(node->parent);
    ASSERT_EQ(topic_tree_count(&tree), 1);
    topic_tree_destroy(&tree);
}

TEST(insert_multi_level) {
    TopicTree tree;
    topic_tree_init(&tree, 256);
    TopicNode* node = topic_tree_insert(&tree, "sensors/temperature/living-room");
    ASSERT_NOT_NULL(node);
    ASSERT_STR_EQ(node->segment, "living-room");
    ASSERT_NOT_NULL(node->parent);
    ASSERT_STR_EQ(node->parent->segment, "temperature");
    ASSERT_NOT_NULL(node->parent->parent);
    ASSERT_STR_EQ(node->parent->parent->segment, "sensors");
    ASSERT_EQ(topic_tree_count(&tree), 3);
    topic_tree_destroy(&tree);
}

TEST(insert_shared_prefix) {
    TopicTree tree;
    topic_tree_init(&tree, 256);
    topic_tree_insert(&tree, "sensors/temperature");
    topic_tree_insert(&tree, "sensors/humidity");
    ASSERT_EQ(topic_tree_count(&tree), 3);
    topic_tree_destroy(&tree);
}

TEST(find_existing) {
    TopicTree tree;
    topic_tree_init(&tree, 256);
    topic_tree_insert(&tree, "a/b/c");
    TopicNode* found = topic_tree_find(&tree, "a/b/c");
    ASSERT_NOT_NULL(found);
    ASSERT_STR_EQ(found->segment, "c");
    topic_tree_destroy(&tree);
}

TEST(find_missing) {
    TopicTree tree;
    topic_tree_init(&tree, 256);
    topic_tree_insert(&tree, "a/b/c");
    TopicNode* found = topic_tree_find(&tree, "a/b/d");
    ASSERT_NULL(found);
    topic_tree_destroy(&tree);
}

TEST(find_intermediate) {
    TopicTree tree;
    topic_tree_init(&tree, 256);
    topic_tree_insert(&tree, "a/b/c");
    TopicNode* found = topic_tree_find(&tree, "a/b");
    ASSERT_NOT_NULL(found);
    ASSERT_STR_EQ(found->segment, "b");
    topic_tree_destroy(&tree);
}

TEST(get_full_topic) {
    TopicTree tree;
    topic_tree_init(&tree, 256);
    TopicNode* node = topic_tree_insert(&tree, "sensors/temperature/kitchen");
    char buf[256];
    topic_node_full_path(node, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "sensors/temperature/kitchen");
    topic_tree_destroy(&tree);
}

TEST(insert_multiple_roots) {
    TopicTree tree;
    topic_tree_init(&tree, 256);
    topic_tree_insert(&tree, "b/child");
    topic_tree_insert(&tree, "a/child");
    topic_tree_insert(&tree, "c/child");
    ASSERT_EQ(tree.root_count, 3);
    ASSERT_STR_EQ(tree.roots[0]->segment, "b");
    ASSERT_STR_EQ(tree.roots[1]->segment, "a");
    ASSERT_STR_EQ(tree.roots[2]->segment, "c");
    topic_tree_destroy(&tree);
}

int main(void) {
    printf("topic_tree tests:\n");
    RUN(create_and_destroy);
    RUN(insert_single_level);
    RUN(insert_multi_level);
    RUN(insert_shared_prefix);
    RUN(find_existing);
    RUN(find_missing);
    RUN(find_intermediate);
    RUN(get_full_topic);
    RUN(insert_multiple_roots);
    printf("All topic_tree tests passed\n");
    return 0;
}
