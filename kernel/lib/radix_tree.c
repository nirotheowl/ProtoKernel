/*
 * kernel/lib/radix_tree.c
 *
 * Generic radix tree implementation for sparse mappings
 */

#include <lib/radix_tree.h>
#include <memory/kmalloc.h>
#include <string.h>
#include <panic.h>
#include <uart.h>

static inline int radix_tree_get_shift(int height) {
    return height * RADIX_TREE_MAP_SHIFT;
}

static inline int radix_tree_get_slot(uint32_t index, int height) {
    int shift = radix_tree_get_shift(height);
    return (index >> shift) & RADIX_TREE_MAP_MASK;
}

static inline uint32_t radix_tree_maxindex(unsigned int height) {
    if (height == 0)
        return 0;
    if (height >= RADIX_TREE_MAX_HEIGHT)
        return ~0U;
    return (1UL << (height * RADIX_TREE_MAP_SHIFT)) - 1;
}

void radix_tree_init(struct radix_tree_root *root) {
    root->rnode = NULL;
    root->height = 0;
    root->max_key = 0;
    spin_lock_init(&root->lock);
}

static struct radix_tree_node *radix_tree_node_create(void) {
    struct radix_tree_node *node = radix_tree_node_alloc();
    if (node) {
        memset(node, 0, sizeof(*node));
    }
    return node;
}

static inline void tag_set(struct radix_tree_node *node, unsigned int tag,
                          int offset) {
    unsigned long *addr = &node->tags[tag][offset / 64];
    unsigned long mask = 1UL << (offset % 64);
    *addr |= mask;
}

static inline void tag_clear(struct radix_tree_node *node, unsigned int tag,
                            int offset) {
    unsigned long *addr = &node->tags[tag][offset / 64];
    unsigned long mask = 1UL << (offset % 64);
    *addr &= ~mask;
}

static inline int tag_get(struct radix_tree_node *node, unsigned int tag,
                         int offset) {
    unsigned long *addr = &node->tags[tag][offset / 64];
    unsigned long mask = 1UL << (offset % 64);
    return (*addr & mask) != 0;
}

static int any_tag_set(struct radix_tree_node *node, unsigned int tag) {
    int i;
    for (i = 0; i < (RADIX_TREE_MAP_SIZE + 63) / 64; i++) {
        if (node->tags[tag][i])
            return 1;
    }
    return 0;
}

static int radix_tree_extend(struct radix_tree_root *root, uint32_t index) {
    struct radix_tree_node *node;
    unsigned int height;

    height = 1;
    while (index > radix_tree_maxindex(height))
        height++;

    if (root->height == 0) {
        node = radix_tree_node_create();
        if (!node)
            return -1;
        node->height = 0;
        root->rnode = node;
        root->height = 1;
    }

    while (height > root->height) {
        node = radix_tree_node_create();
        if (!node)
            return -1;

        node->height = root->height;
        node->count = 1;
        node->slots[0] = root->rnode;
        
        if (root->rnode) {
            struct radix_tree_node *child = root->rnode;
            child->parent = node;
            
            // Propagate tags from the old root to the new root
            // If the old root has any tags set, they should be set at slot 0 of the new root
            for (unsigned int tag = 0; tag < RADIX_TREE_TAG_MAX; tag++) {
                if (any_tag_set(child, tag)) {
                    tag_set(node, tag, 0);
                }
            }
        }

        root->rnode = node;
        root->height++;
    }

    return 0;
}

int radix_tree_insert(struct radix_tree_root *root, uint32_t index, void *item) {
    struct radix_tree_node *node, *parent;
    unsigned int height;
    int offset;

    if (!item)
        return -1;

    spin_lock(&root->lock);

    if (radix_tree_extend(root, index) < 0) {
        spin_unlock(&root->lock);
        return -1;
    }

    node = root->rnode;
    height = root->height - 1;

    while (height > 0) {
        parent = node;
        offset = radix_tree_get_slot(index, height);
        
        node = parent->slots[offset];
        if (!node) {
            node = radix_tree_node_create();
            if (!node) {
                spin_unlock(&root->lock);
                return -1;
            }
            node->height = height - 1;
            node->parent = parent;
            parent->slots[offset] = node;
            parent->count++;
        }
        
        height--;
    }

    offset = radix_tree_get_slot(index, 0);
    if (node->slots[offset]) {
        spin_unlock(&root->lock);
        return -1;
    }

    node->slots[offset] = item;
    node->count++;
    
    if (index > root->max_key)
        root->max_key = index;

    spin_unlock(&root->lock);
    return 0;
}

void *radix_tree_lookup(struct radix_tree_root *root, uint32_t index) {
    struct radix_tree_node *node;
    unsigned int height;
    int offset;

    spin_lock(&root->lock);

    if (!root->rnode || root->height == 0) {
        spin_unlock(&root->lock);
        return NULL;
    }

    if (index > radix_tree_maxindex(root->height)) {
        spin_unlock(&root->lock);
        return NULL;
    }

    node = root->rnode;
    height = root->height - 1;

    while (height > 0) {
        offset = radix_tree_get_slot(index, height);
        node = node->slots[offset];
        if (!node) {
            spin_unlock(&root->lock);
            return NULL;
        }
        height--;
    }

    offset = radix_tree_get_slot(index, 0);
    void *ret = node->slots[offset];
    
    spin_unlock(&root->lock);
    return ret;
}

void radix_tree_shrink(struct radix_tree_root *root) {
    struct radix_tree_node *node;

    while (root->height > 1) {
        node = root->rnode;
        if (!node)
            break;
        
        if (node->count != 1)
            break;

        if (!node->slots[0])
            break;

        root->rnode = node->slots[0];
        root->height--;
        
        if (root->rnode) {
            struct radix_tree_node *child = root->rnode;
            child->parent = NULL;
        }
        
        radix_tree_node_free(node);
    }

    if (root->height == 1 && root->rnode) {
        node = root->rnode;
        if (node->count == 0) {
            root->rnode = NULL;
            root->height = 0;
            root->max_key = 0;
            radix_tree_node_free(node);
        }
    }
}

void *radix_tree_delete(struct radix_tree_root *root, uint32_t index) {
    struct radix_tree_node *node, *parent;
    unsigned int height;
    int offset;
    void *ret;

    spin_lock(&root->lock);

    if (!root->rnode || root->height == 0) {
        spin_unlock(&root->lock);
        return NULL;
    }

    if (index > radix_tree_maxindex(root->height)) {
        spin_unlock(&root->lock);
        return NULL;
    }

    node = root->rnode;
    height = root->height - 1;
    parent = NULL;

    while (height > 0) {
        offset = radix_tree_get_slot(index, height);
        parent = node;
        node = node->slots[offset];
        if (!node) {
            spin_unlock(&root->lock);
            return NULL;
        }
        height--;
    }

    offset = radix_tree_get_slot(index, 0);
    ret = node->slots[offset];
    if (!ret) {
        spin_unlock(&root->lock);
        return NULL;
    }

    node->slots[offset] = NULL;
    node->count--;

    while (node && node->count == 0) {
        parent = node->parent;
        if (!parent) {
            root->rnode = NULL;
            root->height = 0;
            root->max_key = 0;
            radix_tree_node_free(node);
            break;
        }

        for (offset = 0; offset < RADIX_TREE_MAP_SIZE; offset++) {
            if (parent->slots[offset] == node) {
                parent->slots[offset] = NULL;
                parent->count--;
                break;
            }
        }
        
        radix_tree_node_free(node);
        node = parent;
    }

    radix_tree_shrink(root);

    if (index == root->max_key) {
        root->max_key = 0;
    }

    spin_unlock(&root->lock);
    return ret;
}

void *radix_tree_replace(struct radix_tree_root *root, uint32_t index, void *item) {
    struct radix_tree_node *node;
    unsigned int height;
    int offset;
    void *old;

    if (!item)
        return radix_tree_delete(root, index);

    spin_lock(&root->lock);

    if (!root->rnode || root->height == 0) {
        spin_unlock(&root->lock);
        return NULL;
    }

    if (index > radix_tree_maxindex(root->height)) {
        spin_unlock(&root->lock);
        return NULL;
    }

    node = root->rnode;
    height = root->height - 1;

    while (height > 0) {
        offset = radix_tree_get_slot(index, height);
        node = node->slots[offset];
        if (!node) {
            spin_unlock(&root->lock);
            return NULL;
        }
        height--;
    }

    offset = radix_tree_get_slot(index, 0);
    old = node->slots[offset];
    node->slots[offset] = item;
    
    spin_unlock(&root->lock);
    return old;
}

bool radix_tree_empty(struct radix_tree_root *root) {
    bool empty;
    spin_lock(&root->lock);
    empty = (root->rnode == NULL) || 
            (root->height == 1 && root->rnode->count == 0);
    spin_unlock(&root->lock);
    return empty;
}

static void radix_tree_count_nodes(struct radix_tree_node *node, 
                                  struct radix_tree_stats *stats) {
    int i;
    
    if (!node)
        return;
    
    stats->nodes++;
    stats->memory_usage += sizeof(struct radix_tree_node);
    
    if (node->height == 0) {
        for (i = 0; i < RADIX_TREE_MAP_SIZE; i++) {
            if (node->slots[i])
                stats->entries++;
        }
    } else {
        for (i = 0; i < RADIX_TREE_MAP_SIZE; i++) {
            if (node->slots[i])
                radix_tree_count_nodes(node->slots[i], stats);
        }
    }
}

void radix_tree_get_stats(struct radix_tree_root *root, 
                         struct radix_tree_stats *stats) {
    spin_lock(&root->lock);
    
    memset(stats, 0, sizeof(*stats));
    stats->height = root->height;
    
    if (root->rnode) {
        radix_tree_count_nodes(root->rnode, stats);
    }
    
    spin_unlock(&root->lock);
}

int radix_tree_tag_set(struct radix_tree_root *root, uint32_t index, 
                      unsigned int tag) {
    struct radix_tree_node *node, *parent;
    unsigned int height;
    int offset;

    if (tag >= RADIX_TREE_TAG_MAX)
        return -1;

    spin_lock(&root->lock);

    if (!root->rnode || root->height == 0) {
        spin_unlock(&root->lock);
        return -1;
    }

    if (index > radix_tree_maxindex(root->height)) {
        spin_unlock(&root->lock);
        return -1;
    }

    node = root->rnode;
    height = root->height - 1;
    parent = NULL;

    while (height > 0) {
        offset = radix_tree_get_slot(index, height);
        parent = node;
        node = node->slots[offset];
        if (!node) {
            spin_unlock(&root->lock);
            return -1;
        }
        height--;
    }

    offset = radix_tree_get_slot(index, 0);
    if (!node->slots[offset]) {
        spin_unlock(&root->lock);
        return -1;
    }

    tag_set(node, tag, offset);

    while (parent) {
        offset = radix_tree_get_slot(index, parent->height);
        if (tag_get(parent, tag, offset))
            break;
        tag_set(parent, tag, offset);
        parent = parent->parent;
    }

    spin_unlock(&root->lock);
    return 0;
}

void radix_tree_tag_clear(struct radix_tree_root *root, uint32_t index,
                         unsigned int tag) {
    struct radix_tree_node *node, *parent;
    unsigned int height;
    int offset;
    int tagged;

    if (tag >= RADIX_TREE_TAG_MAX)
        return;

    spin_lock(&root->lock);

    if (!root->rnode || root->height == 0) {
        spin_unlock(&root->lock);
        return;
    }

    if (index > radix_tree_maxindex(root->height)) {
        spin_unlock(&root->lock);
        return;
    }

    node = root->rnode;
    height = root->height - 1;

    while (height > 0) {
        offset = radix_tree_get_slot(index, height);
        node = node->slots[offset];
        if (!node) {
            spin_unlock(&root->lock);
            return;
        }
        height--;
    }

    offset = radix_tree_get_slot(index, 0);
    if (!tag_get(node, tag, offset)) {
        spin_unlock(&root->lock);
        return;
    }

    tag_clear(node, tag, offset);

    while (node->parent) {
        parent = node->parent;
        offset = radix_tree_get_slot(index, parent->height - 1);

        tagged = 0;
        struct radix_tree_node *child = parent->slots[offset];
        if (child && any_tag_set(child, tag)) {
            tagged = 1;
        }

        if (tagged) {
            break;
        }

        tag_clear(parent, tag, offset);
        node = parent;
    }

    spin_unlock(&root->lock);
}

int radix_tree_tag_get(struct radix_tree_root *root, uint32_t index,
                      unsigned int tag) {
    struct radix_tree_node *node;
    unsigned int height;
    int offset;
    int ret;

    if (tag >= RADIX_TREE_TAG_MAX)
        return 0;

    spin_lock(&root->lock);

    if (!root->rnode || root->height == 0) {
        spin_unlock(&root->lock);
        return 0;
    }

    if (index > radix_tree_maxindex(root->height)) {
        spin_unlock(&root->lock);
        return 0;
    }

    node = root->rnode;
    height = root->height - 1;

    while (height > 0) {
        offset = radix_tree_get_slot(index, height);
        node = node->slots[offset];
        if (!node) {
            spin_unlock(&root->lock);
            return 0;
        }
        height--;
    }

    offset = radix_tree_get_slot(index, 0);
    ret = tag_get(node, tag, offset);

    spin_unlock(&root->lock);
    return ret;
}

void *radix_tree_next_slot(struct radix_tree_root *root,
                          struct radix_tree_iter *iter,
                          uint32_t index) {
    struct radix_tree_node *node, *child;
    unsigned int height;
    int offset;
    void *ret = NULL;
    uint32_t orig_index = index;

    spin_lock(&root->lock);

    if (!root->rnode || root->height == 0) {
        spin_unlock(&root->lock);
        return NULL;
    }

    iter->index = index;
    iter->next_index = index + 1;

    while (index <= root->max_key) {
        if (index > radix_tree_maxindex(root->height))
            break;

        node = root->rnode;
        height = root->height - 1;
        
        // Traverse down to leaf level
        int skip = 0;
        while (height > 0 && !skip) {
            offset = radix_tree_get_slot(index, height);
            child = node->slots[offset];
            if (!child) {
                // No child at this slot, skip to next subtree
                index = (index | ((1UL << radix_tree_get_shift(height)) - 1)) + 1;
                if (index == 0) { // Wrapped around
                    spin_unlock(&root->lock);
                    return NULL;
                }
                skip = 1; // Skip to next iteration of outer loop
            } else {
                node = child;
                height--;
            }
        }
        
        if (skip)
            continue;

        // Check leaf level
        offset = radix_tree_get_slot(index, 0);
        if (node->slots[offset]) {
            ret = node->slots[offset];
            iter->index = index;
            // Handle wraparound when index is 0xFFFFFFFF
            if (index == 0xFFFFFFFF) {
                iter->next_index = 0; // This will end iteration
            } else {
                iter->next_index = index + 1;
            }
            iter->node = node;
            iter->height = 0;
            spin_unlock(&root->lock);
            return ret;
        }

        index++;
        if (index == 0) // Wrapped around from 0xFFFFFFFF
            break;
    }

    spin_unlock(&root->lock);
    return NULL;
}

void *radix_tree_next_tagged(struct radix_tree_root *root,
                            struct radix_tree_iter *iter,
                            uint32_t index, unsigned int tag) {
    struct radix_tree_node *node, *child;
    unsigned int height;
    int offset;
    uint32_t orig_index = index;

    if (tag >= RADIX_TREE_TAG_MAX)
        return NULL;

    spin_lock(&root->lock);

    if (!root->rnode || root->height == 0) {
        spin_unlock(&root->lock);
        return NULL;
    }

    iter->index = index;
    iter->next_index = index + 1;

    while (index <= root->max_key) {
        if (index > radix_tree_maxindex(root->height))
            break;
            
        // Prevent infinite loop from wraparound
        if (orig_index > 0 && index < orig_index)
            break;

        node = root->rnode;
        height = root->height - 1;
        
        // Traverse down the tree looking for tagged entries
        int need_restart = 0;
        while (height > 0 && !need_restart) {
            offset = radix_tree_get_slot(index, height);
            
            // Look for a tagged child starting from offset
            int found = 0;
            int i;
            for (i = offset; i < RADIX_TREE_MAP_SIZE; i++) {
                if (!tag_get(node, tag, i))
                    continue;
                
                child = node->slots[i];
                if (child) {
                    if (i > offset) {
                        // Found a tagged slot after our current position
                        // Update index to point to the start of this subtree
                        uint32_t mask = ~((1UL << radix_tree_get_shift(height + 1)) - 1);
                        index = (index & mask) | (i << radix_tree_get_shift(height));
                    }
                    node = child;
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                // No tagged entries in this subtree, skip to next subtree at higher level
                uint32_t new_index = (index | ((1UL << radix_tree_get_shift(height + 1)) - 1)) + 1;
                if (new_index == 0 || new_index < index) { // Check for wraparound
                    spin_unlock(&root->lock);
                    return NULL;
                }
                index = new_index;
                need_restart = 1;
            } else {
                height--;
            }
        }
        
        if (need_restart)
            continue;

        // At leaf level, find tagged entry
        for (offset = radix_tree_get_slot(index, 0); 
             offset < RADIX_TREE_MAP_SIZE; offset++) {
            if (tag_get(node, tag, offset) && node->slots[offset]) {
                iter->index = (index & ~RADIX_TREE_MAP_MASK) | offset;
                // Handle wraparound when index is 0xFFFFFFFF
                if (iter->index == 0xFFFFFFFF) {
                    iter->next_index = 0; // This will end iteration
                } else {
                    iter->next_index = iter->index + 1;
                }
                iter->node = node;
                iter->height = 0;
                void *ret = node->slots[offset];
                spin_unlock(&root->lock);
                return ret;
            }
        }

        // No tagged entry found in this leaf, move to next
        index = (index | RADIX_TREE_MAP_MASK) + 1;
        if (index == 0) // Wrapped around
            break;
    }

    spin_unlock(&root->lock);
    return NULL;
}

unsigned int radix_tree_gang_lookup(struct radix_tree_root *root,
                                   void **results, uint32_t first_index,
                                   unsigned int max_items) {
    struct radix_tree_iter iter;
    unsigned int count = 0;
    void *entry;
    uint32_t index = first_index;

    if (!results || !max_items)
        return 0;

    while (count < max_items) {
        entry = radix_tree_next_slot(root, &iter, index);
        if (!entry)
            break;
        results[count++] = entry;
        index = iter.next_index;
    }

    return count;
}

int radix_tree_preload_range(struct radix_tree_root *root,
                            uint32_t start, uint32_t end) {
    return 0;
}