/*
 * kernel/include/memory/malloc_types.h
 *
 * Memory allocation type system for debugging and statistics
 * Inspired by FreeBSD's malloc(9) type system
 */

#ifndef _MALLOC_TYPES_H_
#define _MALLOC_TYPES_H_

#include <stdint.h>
#include <stddef.h>

// Maximum length for type names
#define MALLOC_TYPE_NAME_MAX    16
#define MALLOC_TYPE_DESC_MAX    32

// Per-type statistics structure
struct malloc_type_stats {
    uint64_t allocs;              // Total allocations of this type
    uint64_t frees;               // Total frees of this type
    uint64_t bytes_allocated;     // Total bytes ever allocated
    uint64_t bytes_freed;         // Total bytes freed
    uint64_t current_allocs;      // Currently active allocations
    uint64_t current_bytes;       // Currently allocated bytes
    uint64_t peak_allocs;         // Peak number of allocations
    uint64_t peak_bytes;          // Peak bytes allocated
    uint64_t failed_allocs;       // Failed allocation attempts
};

// Memory allocation type structure
struct malloc_type {
    // Type identification
    const char *name;             // Short name (e.g., "devbuf")
    const char *desc;             // Longer description
    
    // Statistics
    struct malloc_type_stats stats;
    
    // Linked list of all types
    struct malloc_type *next;
    
    // Flags
    uint32_t flags;
    
    // Private data for future use
    void *private;
};

// Flags for malloc types
#define MT_FLAGS_INITIALIZED    0x0001    // Type has been initialized
#define MT_FLAGS_STATIC         0x0002    // Statically allocated type

// Macro to define a malloc type
#define MALLOC_DEFINE(type, shortname, longname) \
    struct malloc_type type = { \
        .name = shortname, \
        .desc = longname, \
        .stats = {0}, \
        .next = NULL, \
        .flags = MT_FLAGS_STATIC, \
        .private = NULL \
    }

// Macro to declare an external malloc type
#define MALLOC_DECLARE(type) \
    extern struct malloc_type type

// Common kernel malloc types
MALLOC_DECLARE(M_KMALLOC);      // Generic kmalloc (default)
MALLOC_DECLARE(M_DEVBUF);       // Device driver buffers
MALLOC_DECLARE(M_TEMP);         // Temporary allocations
MALLOC_DECLARE(M_FILEDESC);     // File descriptors
MALLOC_DECLARE(M_IOBUF);        // I/O buffers
MALLOC_DECLARE(M_PROC);         // Process structures
MALLOC_DECLARE(M_THREAD);       // Thread structures
MALLOC_DECLARE(M_VM);           // Virtual memory structures
MALLOC_DECLARE(M_VNODE);        // VFS nodes
MALLOC_DECLARE(M_CACHE);        // Generic cache data

// Type management functions
void malloc_type_init(void);
void malloc_type_register(struct malloc_type *type);
void malloc_type_unregister(struct malloc_type *type);

// Statistics functions
void malloc_type_update_alloc(struct malloc_type *type, size_t size);
void malloc_type_update_free(struct malloc_type *type, size_t size);
void malloc_type_dump_stats(void);
void malloc_type_get_stats(struct malloc_type *type, struct malloc_type_stats *stats);

// Find type by name
struct malloc_type *malloc_type_find(const char *name);

// Iterate through all types (returns next type or NULL)
struct malloc_type *malloc_type_iterate(struct malloc_type *current);

#endif /* _MALLOC_TYPES_H_ */