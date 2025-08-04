/*
 * kernel/memory/malloc_types.c
 *
 * Memory allocation type tracking implementation
 * Provides type-based statistics and debugging for kmalloc
 */

#include <memory/malloc_types.h>
#include <memory/kmalloc.h>
#include <uart.h>
#include <string.h>
#include <spinlock.h>

// Debug printing
#define MALLOC_TYPE_DEBUG 0

#if MALLOC_TYPE_DEBUG
#define type_debug(msg) uart_puts("[MALLOC_TYPE] " msg)
#else
#define type_debug(msg)
#endif

// Global list of all malloc types
static struct malloc_type *type_list_head = NULL;
static spinlock_t type_list_lock = SPINLOCK_INITIALIZER;
static int malloc_types_initialized = 0;

// Define common kernel malloc types
MALLOC_DEFINE(M_KMALLOC, "kmalloc", "generic kernel malloc");
MALLOC_DEFINE(M_DEVBUF, "devbuf", "device driver buffers");
MALLOC_DEFINE(M_TEMP, "temp", "temporary allocations");
MALLOC_DEFINE(M_FILEDESC, "filedesc", "file descriptors");
MALLOC_DEFINE(M_IOBUF, "iobuf", "I/O buffers");
MALLOC_DEFINE(M_PROC, "proc", "process structures");
MALLOC_DEFINE(M_THREAD, "thread", "thread structures");
MALLOC_DEFINE(M_VM, "vm", "virtual memory structures");
MALLOC_DEFINE(M_VNODE, "vnode", "VFS nodes");
MALLOC_DEFINE(M_CACHE, "cache", "generic cache data");

// Initialize malloc type system
void malloc_type_init(void) {
    if (malloc_types_initialized) {
        type_debug("Already initialized\n");
        return;
    }
    
    type_debug("Initializing malloc type system\n");
    
    // Register common types
    malloc_type_register(&M_KMALLOC);
    malloc_type_register(&M_DEVBUF);
    malloc_type_register(&M_TEMP);
    malloc_type_register(&M_FILEDESC);
    malloc_type_register(&M_IOBUF);
    malloc_type_register(&M_PROC);
    malloc_type_register(&M_THREAD);
    malloc_type_register(&M_VM);
    malloc_type_register(&M_VNODE);
    malloc_type_register(&M_CACHE);
    
    malloc_types_initialized = 1;
    type_debug("Malloc type system initialized\n");
}

// Register a malloc type
void malloc_type_register(struct malloc_type *type) {
    if (!type || !type->name) {
        uart_puts("[MALLOC_TYPE] ERROR: Invalid type for registration\n");
        return;
    }
    
    // Check if already registered
    if (type->flags & MT_FLAGS_INITIALIZED) {
        type_debug("Type already registered: ");
        if (MALLOC_TYPE_DEBUG) {
            uart_puts(type->name);
            uart_puts("\n");
        }
        return;
    }
    
    spin_lock(&type_list_lock);
    
    // Check for duplicate names
    struct malloc_type *current = type_list_head;
    while (current) {
        if (strcmp(current->name, type->name) == 0) {
            spin_unlock(&type_list_lock);
            uart_puts("[MALLOC_TYPE] WARNING: Duplicate type name: ");
            uart_puts(type->name);
            uart_puts("\n");
            return;
        }
        current = current->next;
    }
    
    // Add to list
    type->next = type_list_head;
    type_list_head = type;
    type->flags |= MT_FLAGS_INITIALIZED;
    
    // Initialize statistics if not already done
    if (type->stats.allocs == 0 && type->stats.frees == 0) {
        memset(&type->stats, 0, sizeof(type->stats));
    }
    
    spin_unlock(&type_list_lock);
    
    type_debug("Registered type: ");
    if (MALLOC_TYPE_DEBUG) {
        uart_puts(type->name);
        uart_puts(" - ");
        uart_puts(type->desc);
        uart_puts("\n");
    }
}

// Unregister a malloc type
void malloc_type_unregister(struct malloc_type *type) {
    if (!type) {
        return;
    }
    
    spin_lock(&type_list_lock);
    
    // Remove from list
    struct malloc_type **prev = &type_list_head;
    struct malloc_type *current = type_list_head;
    
    while (current) {
        if (current == type) {
            *prev = current->next;
            type->flags &= ~MT_FLAGS_INITIALIZED;
            break;
        }
        prev = &current->next;
        current = current->next;
    }
    
    spin_unlock(&type_list_lock);
    
    if (current) {
        type_debug("Unregistered type: ");
        if (MALLOC_TYPE_DEBUG) {
            uart_puts(type->name);
            uart_puts("\n");
        }
    }
}

// Update allocation statistics
void malloc_type_update_alloc(struct malloc_type *type, size_t size) {
    if (!type) {
        return;
    }
    
    // Update statistics atomically (in real kernel, use atomic ops)
    type->stats.allocs++;
    type->stats.bytes_allocated += size;
    type->stats.current_allocs++;
    type->stats.current_bytes += size;
    
    // Update peaks
    if (type->stats.current_allocs > type->stats.peak_allocs) {
        type->stats.peak_allocs = type->stats.current_allocs;
    }
    if (type->stats.current_bytes > type->stats.peak_bytes) {
        type->stats.peak_bytes = type->stats.current_bytes;
    }
}

// Update free statistics
void malloc_type_update_free(struct malloc_type *type, size_t size) {
    if (!type) {
        return;
    }
    
    // Update statistics atomically
    type->stats.frees++;
    type->stats.bytes_freed += size;
    
    // Sanity check
    if (type->stats.current_allocs > 0) {
        type->stats.current_allocs--;
    } else {
        uart_puts("[MALLOC_TYPE] WARNING: Free without alloc for type ");
        uart_puts(type->name);
        uart_puts("\n");
    }
    
    if (type->stats.current_bytes >= size) {
        type->stats.current_bytes -= size;
    } else {
        uart_puts("[MALLOC_TYPE] WARNING: Free size mismatch for type ");
        uart_puts(type->name);
        uart_puts("\n");
        type->stats.current_bytes = 0;
    }
}

// Find type by name
struct malloc_type *malloc_type_find(const char *name) {
    if (!name) {
        return NULL;
    }
    
    spin_lock(&type_list_lock);
    
    struct malloc_type *current = type_list_head;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            spin_unlock(&type_list_lock);
            return current;
        }
        current = current->next;
    }
    
    spin_unlock(&type_list_lock);
    return NULL;
}

// Iterate through all types
struct malloc_type *malloc_type_iterate(struct malloc_type *current) {
    struct malloc_type *next;
    
    spin_lock(&type_list_lock);
    
    if (current == NULL) {
        next = type_list_head;
    } else {
        next = current->next;
    }
    
    spin_unlock(&type_list_lock);
    
    return next;
}

// Get type statistics
void malloc_type_get_stats(struct malloc_type *type, struct malloc_type_stats *stats) {
    if (!type || !stats) {
        return;
    }
    
    // Copy statistics atomically
    *stats = type->stats;
}

// Helper function for formatted decimal output (if not available in uart)
static void uart_putdec_width(uint64_t value, int width) {
    char buffer[32];
    int pos = 0;
    
    // Convert to string
    if (value == 0) {
        buffer[pos++] = '0';
    } else {
        uint64_t temp = value;
        while (temp > 0) {
            buffer[pos++] = '0' + (temp % 10);
            temp /= 10;
        }
    }
    
    // Print padding
    for (int i = pos; i < width; i++) {
        uart_putc(' ');
    }
    
    // Print number in reverse
    for (int i = pos - 1; i >= 0; i--) {
        uart_putc(buffer[i]);
    }
}

// Dump all type statistics
void malloc_type_dump_stats(void) {
    uart_puts("\n=== Malloc Type Statistics ===\n");
    uart_puts("Type           Active  Bytes    Total   Peak    Freed   Failed\n");
    uart_puts("------------- ------- -------- ------- ------- ------- -------\n");
    
    struct malloc_type *type = NULL;
    while ((type = malloc_type_iterate(type)) != NULL) {
        // Print type name (left-aligned, max 13 chars)
        uart_puts(type->name);
        int name_len = strlen(type->name);
        for (int i = name_len; i < 14; i++) {
            uart_putc(' ');
        }
        
        // Print active allocations
        uart_putdec_width(type->stats.current_allocs, 7);
        uart_putc(' ');
        
        // Print current bytes
        uart_putdec_width(type->stats.current_bytes, 8);
        uart_putc(' ');
        
        // Print total allocations
        uart_putdec_width(type->stats.allocs, 7);
        uart_putc(' ');
        
        // Print peak allocations
        uart_putdec_width(type->stats.peak_allocs, 7);
        uart_putc(' ');
        
        // Print total frees
        uart_putdec_width(type->stats.frees, 7);
        uart_putc(' ');
        
        // Print failed allocations
        uart_putdec_width(type->stats.failed_allocs, 7);
        uart_puts("\n");
    }
    
    uart_puts("\nType Details:\n");
    type = NULL;
    while ((type = malloc_type_iterate(type)) != NULL) {
        if (type->stats.current_allocs > 0 || type->stats.allocs > 0) {
            uart_puts("  ");
            uart_puts(type->name);
            uart_puts(": ");
            uart_puts(type->desc);
            uart_puts("\n");
            
            uart_puts("    Current: ");
            uart_putdec(type->stats.current_allocs);
            uart_puts(" allocations, ");
            uart_putdec(type->stats.current_bytes);
            uart_puts(" bytes\n");
            
            uart_puts("    Peak:    ");
            uart_putdec(type->stats.peak_allocs);
            uart_puts(" allocations, ");
            uart_putdec(type->stats.peak_bytes);
            uart_puts(" bytes\n");
            
            uart_puts("    Total:   ");
            uart_putdec(type->stats.bytes_allocated);
            uart_puts(" bytes allocated, ");
            uart_putdec(type->stats.bytes_freed);
            uart_puts(" bytes freed\n");
            
            // Check for leaks
            if (type->stats.current_allocs > 0) {
                uart_puts("    WARNING: Possible memory leak - ");
                uart_putdec(type->stats.current_allocs);
                uart_puts(" active allocations\n");
            }
            
            uart_puts("\n");
        }
    }
}