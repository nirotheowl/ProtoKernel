/*
 * kernel/lib/radix_tree.c
 *
 * Generic radix tree implementation for sparse mappings
 */

#include <lib/radix_tree.h>
#include <memory/kmalloc.h>
#include <string.h>
#include <panic.h>

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