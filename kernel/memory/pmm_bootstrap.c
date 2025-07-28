#include <memory/pmm_bootstrap.h>
#include <uart.h>

/* PMM bootstrap allocator state */
static struct {
    uint64_t start;
    uint64_t current;
    uint64_t end;
    size_t used;
} pmm_bootstrap_state = {0};

void pmm_bootstrap_init(uint64_t start_addr, uint64_t end_addr) {
    /* Align start to page boundary */
    start_addr = (start_addr + 0xFFF) & ~0xFFF;
    
    pmm_bootstrap_state.start = start_addr;
    pmm_bootstrap_state.current = start_addr;
    pmm_bootstrap_state.end = end_addr;
    pmm_bootstrap_state.used = 0;
    
    uart_puts("PMM bootstrap allocator initialized:\n");
    uart_puts("  Start: ");
    uart_puthex(start_addr);
    uart_puts("\n  End: ");
    uart_puthex(end_addr);
    uart_puts("\n  Available: ");
    uart_puthex((end_addr - start_addr) / 1024);
    uart_puts(" KB\n");
}

uint64_t pmm_bootstrap_alloc(size_t size, size_t alignment) {
    if (size == 0) return 0;
    
    /* Default alignment is 8 bytes */
    if (alignment == 0) alignment = 8;
    
    /* Align current pointer */
    uint64_t aligned = (pmm_bootstrap_state.current + alignment - 1) & ~(alignment - 1);
    
    /* Check if we have enough space */
    if (aligned + size > pmm_bootstrap_state.end) {
        uart_puts("ERROR: PMM bootstrap allocator out of memory!\n");
        uart_puts("  Requested: ");
        uart_puthex(size);
        uart_puts(" bytes\n  Available: ");
        uart_puthex(pmm_bootstrap_state.end - aligned);
        uart_puts(" bytes\n");
        return 0;
    }
    
    /* Update state */
    uint64_t result = aligned;
    pmm_bootstrap_state.current = aligned + size;
    pmm_bootstrap_state.used += size;
    
    /* Clear allocated memory */
    uint64_t *ptr = (uint64_t *)result;
    for (size_t i = 0; i < size / sizeof(uint64_t); i++) {
        ptr[i] = 0;
    }
    
    return result;
}

uint64_t pmm_bootstrap_current(void) {
    return pmm_bootstrap_state.current;
}

size_t pmm_bootstrap_used(void) {
    return pmm_bootstrap_state.used;
}