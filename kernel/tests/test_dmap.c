/*
 * kernel/tests/test_dmap.c
 *
 * Tests for the new DMAP implementation
 */

#include <memory/vmm.h>
#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <drivers/fdt.h>
#include <drivers/fdt_mgr.h>
#include <uart.h>
#include <string.h>
#include <stdbool.h>

/* External globals from vmm.c */
extern uint64_t dmap_phys_base;
extern uint64_t dmap_phys_max;

/* Test 1: Verify DMAP bounds and basic conversions */
void test_dmap_bounds(void) {
    uart_puts("\n=== Test 1: DMAP Bounds ===\n");
    
    uart_puts("DMAP physical base: ");
    uart_puthex(dmap_phys_base);
    uart_puts("\nDMAP physical max:  ");
    uart_puthex(dmap_phys_max);
    uart_puts("\nDMAP size:          ");
    uart_puthex(dmap_phys_max - dmap_phys_base);
    uart_puts("\n");
    
    /* Test first byte */
    uint64_t phys = dmap_phys_base;
    uint64_t dmap_va = PHYS_TO_DMAP(phys);
    uart_puts("\nFirst byte: phys ");
    uart_puthex(phys);
    uart_puts(" -> DMAP ");
    uart_puthex(dmap_va);
    if (dmap_va != DMAP_BASE) {
        uart_puts(" ERROR: Expected ");
        uart_puthex(DMAP_BASE);
    }
    uart_puts("\n");
    
    /* Test last byte */
    phys = dmap_phys_max - 1;
    dmap_va = PHYS_TO_DMAP(phys);
    uart_puts("Last byte:  phys ");
    uart_puthex(phys);
    uart_puts(" -> DMAP ");
    uart_puthex(dmap_va);
    uart_puts("\n");
}

/* Test 2: Test invalid addresses return 0 */
void test_dmap_invalid_addresses(void) {
    uart_puts("\n=== Test 2: Invalid Addresses ===\n");
    
    /* Before physical base */
    uint64_t phys = dmap_phys_base - 1;
    uint64_t dmap_va = PHYS_TO_DMAP(phys);
    uart_puts("Before base: phys ");
    uart_puthex(phys);
    uart_puts(" -> DMAP ");
    uart_puthex(dmap_va);
    if (dmap_va != 0) {
        uart_puts(" ERROR: Expected 0");
    }
    uart_puts("\n");
    
    /* After physical max */
    phys = dmap_phys_max;
    dmap_va = PHYS_TO_DMAP(phys);
    uart_puts("After max:   phys ");
    uart_puthex(phys);
    uart_puts(" -> DMAP ");
    uart_puthex(dmap_va);
    if (dmap_va != 0) {
        uart_puts(" ERROR: Expected 0");
    }
    uart_puts("\n");
    
    /* Device memory (UART) */
    phys = 0x09000000;
    if (phys < dmap_phys_base || phys >= dmap_phys_max) {
        dmap_va = PHYS_TO_DMAP(phys);
        uart_puts("Device mem:  phys ");
        uart_puthex(phys);
        uart_puts(" -> DMAP ");
        uart_puthex(dmap_va);
        if (dmap_va != 0) {
            uart_puts(" ERROR: Expected 0");
        }
        uart_puts("\n");
    }
}

/* Test 3: Round-trip conversions */
void test_dmap_round_trip(void) {
    uart_puts("\n=== Test 3: Round-trip Conversions ===\n");
    
    uint64_t test_offsets[] = {0, 0x1000, 0x100000, 0x1000000};
    
    for (int i = 0; i < 4; i++) {
        uint64_t test_phys = dmap_phys_base + test_offsets[i];
        if (test_phys >= dmap_phys_max) continue;
        
        uint64_t dmap_va = PHYS_TO_DMAP(test_phys);
        uint64_t phys_back = DMAP_TO_PHYS(dmap_va);
        
        uart_puts("Offset ");
        uart_puthex(test_offsets[i]);
        uart_puts(": ");
        uart_puthex(test_phys);
        uart_puts(" -> ");
        uart_puthex(dmap_va);
        uart_puts(" -> ");
        uart_puthex(phys_back);
        
        if (phys_back != test_phys) {
            uart_puts(" ERROR!");
        }
        uart_puts("\n");
    }
}

/* Test 4: Memory access through DMAP */
void test_dmap_memory_access(void) {
    uart_puts("\n=== Test 4: Memory Access ===\n");
    
    if (!pmm_is_initialized()) {
        uart_puts("PMM not initialized, skipping\n");
        return;
    }
    
    /* Allocate a page */
    uint64_t phys_page = pmm_alloc_page();
    if (phys_page == 0) {
        uart_puts("Failed to allocate page\n");
        return;
    }
    
    uart_puts("Allocated page at phys ");
    uart_puthex(phys_page);
    uart_puts("\n");
    
    /* Get DMAP address */
    uint64_t dmap_addr = PHYS_TO_DMAP(phys_page);
    uart_puts("DMAP address: ");
    uart_puthex(dmap_addr);
    uart_puts("\n");
    
    if (dmap_addr == 0) {
        uart_puts("ERROR: PHYS_TO_DMAP returned 0\n");
        pmm_free_page(phys_page);
        return;
    }
    
    /* Write and read test patterns */
    volatile uint64_t *ptr = (volatile uint64_t *)dmap_addr;
    uint64_t patterns[] = {0xDEADBEEF12345678, 0xCAFEBABE87654321, 0xFFFFFFFFFFFFFFFF, 0};
    
    for (int i = 0; i < 4; i++) {
        ptr[i] = patterns[i];
        uint64_t read_val = ptr[i];
        uart_puts("  Write/Read ");
        uart_puthex(patterns[i]);
        if (read_val != patterns[i]) {
            uart_puts(" ERROR: got ");
            uart_puthex(read_val);
        }
        uart_puts("\n");
    }
    
    pmm_free_page(phys_page);
}

/* Test 5: Multiple memory regions */
void test_dmap_multiple_regions(void) {
    uart_puts("\n=== Test 5: Multiple Memory Regions ===\n");
    
    memory_info_t mem_info;
    if (!fdt_mgr_get_memory_info(&mem_info)) {
        uart_puts("No memory info available\n");
        return;
    }
    
    uart_puts("Memory regions: ");
    uart_puthex(mem_info.count);
    uart_puts("\n");
    
    for (int i = 0; i < mem_info.count; i++) {
        uint64_t base = mem_info.regions[i].base;
        uint64_t size = mem_info.regions[i].size;
        
        uart_puts("\nRegion ");
        uart_puthex(i);
        uart_puts(": base=");
        uart_puthex(base);
        uart_puts(" size=");
        uart_puthex(size);
        uart_puts("\n");
        
        /* Test first byte of region */
        uint64_t dmap_va = PHYS_TO_DMAP(base);
        uart_puts("  First byte DMAP: ");
        uart_puthex(dmap_va);
        if (dmap_va != 0) {
            /* Try to access it */
            volatile uint64_t *ptr = (volatile uint64_t *)dmap_va;
            *ptr = 0xABCDEF00 + i;
            uint64_t val = *ptr;
            uart_puts(" (accessible, read ");
            uart_puthex(val);
            uart_puts(")");
        }
        uart_puts("\n");
    }
}

/* Test 6: Stress test with multiple pages */
void test_dmap_stress(void) {
    uart_puts("\n=== Test 6: Stress Test ===\n");
    
    if (!pmm_is_initialized()) {
        uart_puts("PMM not initialized, skipping\n");
        return;
    }
    
    /* Try to allocate 8 pages */
    uint64_t pages[8];
    int allocated = 0;
    
    for (int i = 0; i < 8; i++) {
        pages[i] = pmm_alloc_page();
        if (pages[i] == 0) break;
        allocated++;
    }
    
    uart_puts("Allocated ");
    uart_puthex(allocated);
    uart_puts(" pages\n");
    
    /* Write unique pattern to each */
    for (int i = 0; i < allocated; i++) {
        uint64_t dmap_addr = PHYS_TO_DMAP(pages[i]);
        if (dmap_addr != 0) {
            volatile uint32_t *ptr = (volatile uint32_t *)dmap_addr;
            /* Write page number to first word of each page */
            ptr[0] = 0xBAD00000 | i;
            ptr[1] = pages[i] & 0xFFFFFFFF;
            ptr[2] = pages[i] >> 32;
        }
    }
    
    /* Verify patterns */
    uart_puts("Verifying patterns...\n");
    int errors = 0;
    for (int i = 0; i < allocated; i++) {
        uint64_t dmap_addr = PHYS_TO_DMAP(pages[i]);
        if (dmap_addr != 0) {
            volatile uint32_t *ptr = (volatile uint32_t *)dmap_addr;
            if (ptr[0] != (0xBAD00000 | i)) {
                uart_puts("  Page ");
                uart_puthex(i);
                uart_puts(" corrupted! Expected ");
                uart_puthex(0xBAD00000 | i);
                uart_puts(" got ");
                uart_puthex(ptr[0]);
                uart_puts("\n");
                errors++;
            }
        }
    }
    
    if (errors == 0) {
        uart_puts("All patterns verified OK\n");
    }
    
    /* Free pages */
    for (int i = 0; i < allocated; i++) {
        pmm_free_page(pages[i]);
    }
}

/* Test 7: Page table walk for DMAP addresses */
void test_dmap_page_walk(void) {
    uart_puts("\n=== Test 7: Page Table Walk ===\n");
    
    vmm_context_t *ctx = vmm_get_kernel_context();
    
    /* Walk first DMAP address */
    uart_puts("Walking DMAP_BASE:\n");
    vmm_debug_walk(ctx, DMAP_BASE);
    
    /* Walk a 2MB offset */
    uart_puts("\nWalking DMAP_BASE + 2MB:\n");
    vmm_debug_walk(ctx, DMAP_BASE + 0x200000);
}

/* Test 8: Exact boundary conditions */
void test_dmap_boundaries(void) {
    uart_puts("\n=== Test 8: Exact Boundary Conditions ===\n");
    
    /* Test exact start boundary */
    uint64_t phys = dmap_phys_base;
    uint64_t dmap_va = PHYS_TO_DMAP(phys);
    uart_puts("Exact start: phys ");
    uart_puthex(phys);
    uart_puts(" -> DMAP ");
    uart_puthex(dmap_va);
    uart_puts("\n");
    
    /* Test one before start */
    if (dmap_phys_base > 0) {
        phys = dmap_phys_base - 1;
        dmap_va = PHYS_TO_DMAP(phys);
        uart_puts("One before:  phys ");
        uart_puthex(phys);
        uart_puts(" -> DMAP ");
        uart_puthex(dmap_va);
        if (dmap_va != 0) {
            uart_puts(" ERROR: Should be 0");
        }
        uart_puts("\n");
    }
    
    /* Test exact end boundary */
    phys = dmap_phys_max - 1;
    dmap_va = PHYS_TO_DMAP(phys);
    uart_puts("Exact end:   phys ");
    uart_puthex(phys);
    uart_puts(" -> DMAP ");
    uart_puthex(dmap_va);
    uart_puts("\n");
    
    /* Test one after end */
    phys = dmap_phys_max;
    dmap_va = PHYS_TO_DMAP(phys);
    uart_puts("One after:   phys ");
    uart_puthex(phys);
    uart_puts(" -> DMAP ");
    uart_puthex(dmap_va);
    if (dmap_va != 0) {
        uart_puts(" ERROR: Should be 0");
    }
    uart_puts("\n");
    
    /* Test DMAP_TO_PHYS boundaries */
    dmap_va = DMAP_BASE;
    phys = DMAP_TO_PHYS(dmap_va);
    uart_puts("\nDMAP start:  DMAP ");
    uart_puthex(dmap_va);
    uart_puts(" -> phys ");
    uart_puthex(phys);
    if (phys != dmap_phys_base) {
        uart_puts(" ERROR!");
    }
    uart_puts("\n");
    
    /* One before DMAP start */
    dmap_va = DMAP_BASE - 1;
    phys = DMAP_TO_PHYS(dmap_va);
    uart_puts("Before DMAP: DMAP ");
    uart_puthex(dmap_va);
    uart_puts(" -> phys ");
    uart_puthex(phys);
    if (phys != 0) {
        uart_puts(" ERROR: Should be 0");
    }
    uart_puts("\n");
}

/* Test 9: Large memory operations across DMAP */
void test_dmap_large_ops(void) {
    uart_puts("\n=== Test 9: Large Memory Operations ===\n");
    
    if (!pmm_is_initialized()) {
        uart_puts("PMM not initialized, skipping\n");
        return;
    }
    
    /* Allocate 2 pages */
    uint64_t src_page = pmm_alloc_page();
    uint64_t dst_page = pmm_alloc_page();
    
    if (src_page == 0 || dst_page == 0) {
        uart_puts("Failed to allocate pages\n");
        if (src_page) pmm_free_page(src_page);
        if (dst_page) pmm_free_page(dst_page);
        return;
    }
    
    uint64_t src_dmap = PHYS_TO_DMAP(src_page);
    uint64_t dst_dmap = PHYS_TO_DMAP(dst_page);
    
    uart_puts("Source page: phys ");
    uart_puthex(src_page);
    uart_puts(" DMAP ");
    uart_puthex(src_dmap);
    uart_puts("\nDest page:   phys ");
    uart_puthex(dst_page);
    uart_puts(" DMAP ");
    uart_puthex(dst_dmap);
    uart_puts("\n");
    
    /* Fill source with pattern */
    volatile uint8_t *src = (volatile uint8_t *)src_dmap;
    volatile uint8_t *dst = (volatile uint8_t *)dst_dmap;
    
    for (int i = 0; i < PAGE_SIZE; i++) {
        src[i] = i & 0xFF;
    }
    
    /* Copy using DMAP addresses */
    uart_puts("Copying 4KB via DMAP...\n");
    for (int i = 0; i < PAGE_SIZE; i++) {
        dst[i] = src[i];
    }
    
    /* Verify */
    int errors = 0;
    for (int i = 0; i < PAGE_SIZE; i++) {
        if (dst[i] != (i & 0xFF)) {
            errors++;
            if (errors < 5) {
                uart_puts("  Error at offset ");
                uart_puthex(i);
                uart_puts(": expected ");
                uart_puthex(i & 0xFF);
                uart_puts(" got ");
                uart_puthex(dst[i]);
                uart_puts("\n");
            }
        }
    }
    
    if (errors == 0) {
        uart_puts("Large copy successful\n");
    } else {
        uart_puts("Total errors: ");
        uart_puthex(errors);
        uart_puts("\n");
    }
    
    pmm_free_page(src_page);
    pmm_free_page(dst_page);
}

/* Test 10: Test DMAP info display */
void test_dmap_info(void) {
    uart_puts("\n=== Test 10: DMAP Information ===\n");
    
    uart_puts("DMAP Information:\n");
    uart_puts("  Physical base: ");
    uart_puthex(dmap_phys_base);
    uart_puts("\n  Physical max:  ");
    uart_puthex(dmap_phys_max);
    uart_puts("\n  Total size:    ");
    uart_puthex(dmap_phys_max - dmap_phys_base);
    uart_puts("\n  DMAP base VA:  ");
    uart_puthex(DMAP_BASE);
    uart_puts("\n  DMAP end VA:   ");
    uart_puthex(DMAP_BASE + (dmap_phys_max - dmap_phys_base));
    uart_puts("\n  Ready:         ");
    uart_puts(vmm_is_dmap_ready() ? "Yes" : "No");
    uart_puts("\n");
}

/* Test 11: Different alignment access patterns */
void test_dmap_alignment_access(void) {
    uart_puts("\n=== Test 11: Alignment Access Patterns ===\n");
    
    if (!pmm_is_initialized()) {
        uart_puts("PMM not initialized, skipping\n");
        return;
    }
    
    uint64_t page = pmm_alloc_page();
    if (page == 0) {
        uart_puts("Failed to allocate page\n");
        return;
    }
    
    uint64_t dmap_addr = PHYS_TO_DMAP(page);
    uart_puts("Test page at DMAP ");
    uart_puthex(dmap_addr);
    uart_puts("\n");
    
    /* Clear the page first */
    volatile uint8_t *base = (volatile uint8_t *)dmap_addr;
    for (int i = 0; i < PAGE_SIZE; i++) {
        base[i] = 0;
    }
    
    /* Test 1-byte access */
    uart_puts("Testing 1-byte access: ");
    volatile uint8_t *p8 = (volatile uint8_t *)dmap_addr;
    p8[0] = 0xAA;
    p8[1] = 0xBB;
    if (p8[0] == 0xAA && p8[1] == 0xBB) {
        uart_puts("OK\n");
    } else {
        uart_puts("FAILED\n");
    }
    
    /* Test 2-byte access */
    uart_puts("Testing 2-byte access: ");
    volatile uint16_t *p16 = (volatile uint16_t *)dmap_addr;
    p16[0] = 0x1234;
    p16[1] = 0x5678;
    if (p16[0] == 0x1234 && p16[1] == 0x5678) {
        uart_puts("OK\n");
    } else {
        uart_puts("FAILED\n");
    }
    
    /* Test 4-byte access */
    uart_puts("Testing 4-byte access: ");
    volatile uint32_t *p32 = (volatile uint32_t *)dmap_addr;
    p32[0] = 0xDEADBEEF;
    p32[1] = 0xCAFEBABE;
    if (p32[0] == 0xDEADBEEF && p32[1] == 0xCAFEBABE) {
        uart_puts("OK\n");
    } else {
        uart_puts("FAILED\n");
    }
    
    /* Test 8-byte access */
    uart_puts("Testing 8-byte access: ");
    volatile uint64_t *p64 = (volatile uint64_t *)dmap_addr;
    p64[0] = 0x123456789ABCDEF0;
    p64[1] = 0xFEDCBA9876543210;
    if (p64[0] == 0x123456789ABCDEF0 && p64[1] == 0xFEDCBA9876543210) {
        uart_puts("OK\n");
    } else {
        uart_puts("FAILED\n");
    }
    
    /* Test unaligned access */
    uart_puts("Testing unaligned access: ");
    volatile uint32_t *unaligned = (volatile uint32_t *)(dmap_addr + 1);
    *unaligned = 0x12345678;
    if (*unaligned == 0x12345678) {
        uart_puts("OK\n");
    } else {
        uart_puts("FAILED\n");
    }
    
    pmm_free_page(page);
}

/* Test 11: Cross-page access patterns */
void test_dmap_cross_page(void) {
    uart_puts("\n=== Test 11: Cross-Page Access ===\n");
    
    if (!pmm_is_initialized()) {
        uart_puts("PMM not initialized, skipping\n");
        return;
    }
    
    /* Try to allocate 2 contiguous pages */
    uint64_t pages[2];
    pages[0] = pmm_alloc_page();
    pages[1] = pmm_alloc_page();
    
    if (pages[0] == 0 || pages[1] == 0) {
        uart_puts("Failed to allocate pages\n");
        if (pages[0]) pmm_free_page(pages[0]);
        if (pages[1]) pmm_free_page(pages[1]);
        return;
    }
    
    uart_puts("Page 0: phys ");
    uart_puthex(pages[0]);
    uart_puts("\nPage 1: phys ");
    uart_puthex(pages[1]);
    uart_puts("\n");
    
    /* If pages happen to be contiguous, test cross-page access */
    if (pages[1] == pages[0] + PAGE_SIZE) {
        uart_puts("Pages are contiguous! Testing cross-page access...\n");
        
        uint64_t dmap0 = PHYS_TO_DMAP(pages[0]);
        
        /* Write pattern crossing page boundary */
        volatile uint64_t *cross = (volatile uint64_t *)(dmap0 + PAGE_SIZE - 4);
        *cross = 0xC055BA6E12345678;  /* This will span both pages */
        
        /* Read it back */
        uint64_t val = *cross;
        uart_puts("Cross-page write/read: ");
        uart_puthex(val);
        if (val == 0xC055BA6E12345678) {
            uart_puts(" OK\n");
        } else {
            uart_puts(" FAILED\n");
        }
    } else {
        uart_puts("Pages not contiguous, testing separate access...\n");
        
        /* Test end of first page */
        uint64_t dmap0 = PHYS_TO_DMAP(pages[0]);
        volatile uint32_t *end0 = (volatile uint32_t *)(dmap0 + PAGE_SIZE - 4);
        *end0 = 0xE0D00000;
        
        /* Test start of second page */
        uint64_t dmap1 = PHYS_TO_DMAP(pages[1]);
        volatile uint32_t *start1 = (volatile uint32_t *)dmap1;
        *start1 = 0x57A77100;
        
        uart_puts("End of page 0: ");
        uart_puthex(*end0);
        uart_puts("\nStart of page 1: ");
        uart_puthex(*start1);
        uart_puts("\n");
    }
    
    pmm_free_page(pages[0]);
    pmm_free_page(pages[1]);
}

/* Test 12: DMAP with memory barriers */
void test_dmap_barriers(void) {
    uart_puts("\n=== Test 12: Memory Barriers ===\n");
    
    if (!pmm_is_initialized()) {
        uart_puts("PMM not initialized, skipping\n");
        return;
    }
    
    uint64_t page = pmm_alloc_page();
    if (page == 0) {
        uart_puts("Failed to allocate page\n");
        return;
    }
    
    uint64_t dmap_addr = PHYS_TO_DMAP(page);
    volatile uint64_t *ptr = (volatile uint64_t *)dmap_addr;
    
    /* Test with different barrier types */
    uart_puts("Testing memory barriers...\n");
    
    /* Write with barrier */
    *ptr = 0x1111111111111111;
    // FIXME: AArch64 Specific
    // __asm__ volatile("dmb sy" ::: "memory");  /* Full system barrier */
    
    uint64_t val1 = *ptr;
    uart_puts("After DMB SY: ");
    uart_puthex(val1);
    uart_puts("\n");
    
    /* Write with store barrier */
    *ptr = 0x2222222222222222;
    // FIXME: AArch64 Specific
    // __asm__ volatile("dmb st" ::: "memory");  /* Store barrier */
    
    uint64_t val2 = *ptr;
    uart_puts("After DMB ST: ");
    uart_puthex(val2);
    uart_puts("\n");
    
    /* Write with instruction sync */
    *ptr = 0x3333333333333333;
    // FIXME: AArch64 Specific
    // __asm__ volatile("dsb sy" ::: "memory");  /* Data sync barrier */
    // __asm__ volatile("isb" ::: "memory");     /* Instruction sync */
    
    uint64_t val3 = *ptr;
    uart_puts("After DSB+ISB: ");
    uart_puthex(val3);
    uart_puts("\n");
    
    pmm_free_page(page);
}

/* Test 13: Zero page handling */
void test_dmap_zero_page(void) {
    uart_puts("\n=== Test 13: Zero Page Handling ===\n");
    
    if (!pmm_is_initialized()) {
        uart_puts("PMM not initialized, skipping\n");
        return;
    }
    
    /* Allocate a page (PMM should zero it) */
    uint64_t page = pmm_alloc_page();
    if (page == 0) {
        uart_puts("Failed to allocate page\n");
        return;
    }
    
    uint64_t dmap_addr = PHYS_TO_DMAP(page);
    volatile uint64_t *ptr = (volatile uint64_t *)dmap_addr;
    
    /* Verify page is zeroed */
    uart_puts("Checking if allocated page is zeroed...\n");
    int non_zero = 0;
    for (int i = 0; i < PAGE_SIZE / 8; i++) {
        if (ptr[i] != 0) {
            non_zero++;
            if (non_zero < 5) {
                uart_puts("  Non-zero at offset ");
                uart_puthex(i * 8);
                uart_puts(": ");
                uart_puthex(ptr[i]);
                uart_puts("\n");
            }
        }
    }
    
    if (non_zero == 0) {
        uart_puts("Page correctly zeroed\n");
    } else {
        uart_puts("Found ");
        uart_puthex(non_zero);
        uart_puts(" non-zero values!\n");
    }
    
    /* Write pattern and free */
    for (int i = 0; i < 16; i++) {
        ptr[i] = 0xDEADDEADDEADDEAD;
    }
    
    pmm_free_page(page);
    
    /* Allocate again and check if zeroed */
    page = pmm_alloc_page();
    if (page == 0) {
        uart_puts("Failed to allocate second page\n");
        return;
    }
    
    dmap_addr = PHYS_TO_DMAP(page);
    ptr = (volatile uint64_t *)dmap_addr;
    
    uart_puts("\nChecking reallocation zeroing...\n");
    non_zero = 0;
    for (int i = 0; i < 16; i++) {
        if (ptr[i] != 0) {
            uart_puts("  Offset ");
            uart_puthex(i * 8);
            uart_puts(" not zeroed: ");
            uart_puthex(ptr[i]);
            uart_puts("\n");
            non_zero++;
        }
    }
    
    if (non_zero == 0) {
        uart_puts("Reallocation correctly zeroed\n");
    }
    
    pmm_free_page(page);
}

/* Test 14: Device memory exclusion */
void test_dmap_device_exclusion(void) {
    uart_puts("\n=== Test 14: Device Memory Exclusion ===\n");
    
    /* Test known device addresses */
    const char *names[] = {
        "PL011 UART",
        "GIC Distributor", 
        "GIC CPU Interface",
        "PL031 RTC",
        "PL061 GPIO",
        "FW-CFG",
        "Flash",
        "Flash",
        "Virtio MMIO",
        "Virtio MMIO",
        "PCI ECAM"
    };
    
    uint64_t addrs[] = {
        0x09000000,
        0x08000000,
        0x08010000,
        0x09010000,
        0x09030000,
        0x09020000,
        0x00000000,
        0x04000000,
        0x0A000000,
        0x0A001000,
        0x10000000
    };
    
    uart_puts("Testing device addresses are excluded from DMAP:\n");
    
    for (int i = 0; i < 11; i++) {
        uint64_t dmap_va = PHYS_TO_DMAP(addrs[i]);
        uart_puts("  ");
        uart_puts(names[i]);
        uart_puts(" at ");
        uart_puthex(addrs[i]);
        uart_puts(": DMAP=");
        uart_puthex(dmap_va);
        
        if (dmap_va != 0) {
            uart_puts(" ERROR: Should be excluded!");
        } else {
            uart_puts(" (correctly excluded)");
        }
        uart_puts("\n");
    }
    
    /* Also test that these addresses are outside our DMAP bounds */
    uart_puts("\nDMAP bounds check:\n");
    uart_puts("  DMAP covers: ");
    uart_puthex(dmap_phys_base);
    uart_puts(" - ");
    uart_puthex(dmap_phys_max);
    uart_puts("\n");
    
    for (int i = 0; i < 11; i++) {
        if (addrs[i] >= dmap_phys_base && addrs[i] < dmap_phys_max) {
            uart_puts("  WARNING: ");
            uart_puts(names[i]);
            uart_puts(" at ");
            uart_puthex(addrs[i]);
            uart_puts(" is within DMAP physical range!\n");
        }
    }
}

/* Test 15: Maximum offset calculations */
void test_dmap_max_offsets(void) {
    uart_puts("\n=== Test 15: Maximum Offset Tests ===\n");
    
    /* Calculate maximum valid offset */
    uint64_t max_offset = dmap_phys_max - dmap_phys_base - 1;
    uart_puts("Maximum valid offset: ");
    uart_puthex(max_offset);
    uart_puts("\n");
    
    /* Test various large offsets */
    uint64_t test_offsets[] = {
        max_offset,
        max_offset - 1,
        max_offset - PAGE_SIZE,
        max_offset - (2 * PAGE_SIZE),
        max_offset + 1,  /* This should fail */
        max_offset + PAGE_SIZE  /* This should fail */
    };
    
    for (int i = 0; i < 6; i++) {
        uint64_t offset = test_offsets[i];
        uint64_t phys = dmap_phys_base + offset;
        uint64_t dmap_va = PHYS_TO_DMAP(phys);
        
        uart_puts("\nOffset ");
        uart_puthex(offset);
        uart_puts(":\n  phys ");
        uart_puthex(phys);
        uart_puts(" -> DMAP ");
        uart_puthex(dmap_va);
        
        if (offset > max_offset && dmap_va != 0) {
            uart_puts(" ERROR: Should be 0");
        } else if (offset <= max_offset && dmap_va == 0) {
            uart_puts(" ERROR: Should be valid");
        }
        
        /* Test reverse mapping */
        if (dmap_va != 0) {
            uint64_t phys_back = DMAP_TO_PHYS(dmap_va);
            uart_puts("\n  DMAP ");
            uart_puthex(dmap_va);
            uart_puts(" -> phys ");
            uart_puthex(phys_back);
            if (phys_back != phys) {
                uart_puts(" ERROR!");
            }
        }
        uart_puts("\n");
    }
}

/* Main test function */
void test_dmap_all(void) {
    uart_puts("\n========================================\n");
    uart_puts("DMAP Implementation Tests\n");
    uart_puts("========================================\n");
    
    if (!vmm_is_dmap_ready()) {
        uart_puts("ERROR: DMAP not initialized!\n");
        return;
    }
    
    test_dmap_bounds();
    test_dmap_invalid_addresses();
    test_dmap_round_trip();
    test_dmap_memory_access();
    test_dmap_multiple_regions();
    test_dmap_stress();
    test_dmap_page_walk();
    test_dmap_boundaries();
    test_dmap_large_ops();
    test_dmap_info();
    test_dmap_alignment_access();
    test_dmap_cross_page();
    test_dmap_barriers();
    test_dmap_zero_page();
    test_dmap_device_exclusion();
    test_dmap_max_offsets();
    
    uart_puts("\n========================================\n");
    uart_puts("DMAP tests completed\n");
    uart_puts("========================================\n");
}