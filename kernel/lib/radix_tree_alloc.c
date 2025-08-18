/*
 * kernel/lib/radix_tree_alloc.c
 *
 * Node allocation for radix trees
 */

#include <lib/radix_tree.h>
#include <memory/kmalloc.h>
#include <memory/slab.h>
#include <string.h>
#include <spinlock.h>

struct radix_tree_node_cache {
    struct radix_tree_node *free_list;
    unsigned int free_count;
    unsigned int total_allocated;
    spinlock_t lock;
};

static struct radix_tree_node_cache node_cache = {
    .free_list = NULL,
    .free_count = 0,
    .total_allocated = 0,
    .lock = SPINLOCK_INITIALIZER
};

#define MAX_FREE_NODES 32

void radix_tree_node_cache_init(void) {
    spin_lock_init(&node_cache.lock);
    node_cache.free_list = NULL;
    node_cache.free_count = 0;
    node_cache.total_allocated = 0;
}

struct radix_tree_node *radix_tree_node_alloc(void) {
    struct radix_tree_node *node;
    
    spin_lock(&node_cache.lock);
    
    if (node_cache.free_list) {
        node = node_cache.free_list;
        node_cache.free_list = (struct radix_tree_node *)node->slots[0];
        node_cache.free_count--;
        spin_unlock(&node_cache.lock);
        
        memset(node, 0, sizeof(*node));
        return node;
    }
    
    spin_unlock(&node_cache.lock);
    
    node = kmalloc(sizeof(struct radix_tree_node), KM_NOSLEEP);
    if (node) {
        memset(node, 0, sizeof(*node));
        
        spin_lock(&node_cache.lock);
        node_cache.total_allocated++;
        spin_unlock(&node_cache.lock);
    }
    
    return node;
}

void radix_tree_node_free(struct radix_tree_node *node) {
    if (!node)
        return;
    
    spin_lock(&node_cache.lock);
    
    if (node_cache.free_count < MAX_FREE_NODES) {
        memset(node, 0, sizeof(*node));
        node->slots[0] = node_cache.free_list;
        node_cache.free_list = node;
        node_cache.free_count++;
        spin_unlock(&node_cache.lock);
    } else {
        node_cache.total_allocated--;
        spin_unlock(&node_cache.lock);
        kfree(node);
    }
}