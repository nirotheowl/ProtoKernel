#ifndef _LIB_RADIX_TREE_H
#define _LIB_RADIX_TREE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <spinlock.h>

#define RADIX_TREE_MAP_SHIFT    6
#define RADIX_TREE_MAP_SIZE     (1UL << RADIX_TREE_MAP_SHIFT)
#define RADIX_TREE_MAP_MASK     (RADIX_TREE_MAP_SIZE - 1)
#define RADIX_TREE_MAX_HEIGHT   6

#define RADIX_TREE_TAG_MAX      2
enum radix_tree_tags {
    RADIX_TREE_TAG_ALLOCATED = 0,
    RADIX_TREE_TAG_MSI       = 1,
};

struct radix_tree_node;

struct radix_tree_root {
    struct radix_tree_node *rnode;
    unsigned int height;
    uint32_t max_key;
    spinlock_t lock;
};

struct radix_tree_node {
    unsigned int height;
    unsigned int count;
    struct radix_tree_node *parent;
    void *slots[RADIX_TREE_MAP_SIZE];
    unsigned long tags[RADIX_TREE_TAG_MAX][(RADIX_TREE_MAP_SIZE + 63) / 64];
};

struct radix_tree_iter {
    uint32_t index;
    uint32_t next_index;
    struct radix_tree_node *node;
    unsigned int height;
};

#define RADIX_TREE_INIT { \
    .rnode = NULL, \
    .height = 0, \
    .max_key = 0, \
    .lock = SPINLOCK_INITIALIZER \
}

void radix_tree_init(struct radix_tree_root *root);
int radix_tree_insert(struct radix_tree_root *root, uint32_t index, void *item);
void *radix_tree_delete(struct radix_tree_root *root, uint32_t index);
void *radix_tree_lookup(struct radix_tree_root *root, uint32_t index);
void *radix_tree_replace(struct radix_tree_root *root, uint32_t index, void *item);

int radix_tree_tag_set(struct radix_tree_root *root, uint32_t index, unsigned int tag);
void radix_tree_tag_clear(struct radix_tree_root *root, uint32_t index, unsigned int tag);
int radix_tree_tag_get(struct radix_tree_root *root, uint32_t index, unsigned int tag);

void *radix_tree_next_slot(struct radix_tree_root *root, 
                          struct radix_tree_iter *iter,
                          uint32_t index);
void *radix_tree_next_tagged(struct radix_tree_root *root,
                            struct radix_tree_iter *iter,
                            uint32_t index, unsigned int tag);

unsigned int radix_tree_gang_lookup(struct radix_tree_root *root,
                                   void **results, uint32_t first_index,
                                   unsigned int max_items);

int radix_tree_preload_range(struct radix_tree_root *root,
                            uint32_t start, uint32_t end);

bool radix_tree_empty(struct radix_tree_root *root);
void radix_tree_shrink(struct radix_tree_root *root);

struct radix_tree_stats {
    unsigned int nodes;
    unsigned int height;
    unsigned int entries;
    size_t memory_usage;
};
void radix_tree_get_stats(struct radix_tree_root *root, 
                         struct radix_tree_stats *stats);

struct radix_tree_node *radix_tree_node_alloc(void);
void radix_tree_node_free(struct radix_tree_node *node);
void radix_tree_node_cache_init(void);

#endif /* _LIB_RADIX_TREE_H */