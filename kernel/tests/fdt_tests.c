/*
 * kernel/tests/fdt_tests.c
 * 
 * Comprehensive test suite for the FDT parser
 */

#include <uart.h>
#include <drivers/fdt.h>
#include <stdint.h>
#include <string.h>

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Helper macro for test assertions */
#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (!(condition)) { \
        uart_puts("[FAIL] "); \
        uart_puts(message); \
        uart_puts("\n"); \
        tests_failed++; \
        return false; \
    } else { \
        tests_passed++; \
    } \
} while(0)

/* Helper to create a minimal valid FDT */
static void create_minimal_fdt(uint8_t *buffer) {
    fdt_header_t *header = (fdt_header_t *)buffer;
    
    /* Clear buffer */
    memset(buffer, 0, 512);
    
    /* Fill header */
    header->magic = __builtin_bswap32(FDT_MAGIC);
    header->totalsize = __builtin_bswap32(512);
    header->off_dt_struct = __builtin_bswap32(sizeof(fdt_header_t));
    header->off_dt_strings = __builtin_bswap32(256);
    header->version = __builtin_bswap32(17);
    header->last_comp_version = __builtin_bswap32(16);
    header->size_dt_strings = __builtin_bswap32(64);
    header->size_dt_struct = __builtin_bswap32(128);
    
    /* Add structure data */
    uint32_t *struct_ptr = (uint32_t *)((uint8_t *)buffer + sizeof(fdt_header_t));
    
    /* Root node */
    *struct_ptr++ = __builtin_bswap32(FDT_BEGIN_NODE);
    *struct_ptr++ = 0; /* Empty name for root */
    
    /* End root node */
    *struct_ptr++ = __builtin_bswap32(FDT_END_NODE);
    
    /* End of structure */
    *struct_ptr++ = __builtin_bswap32(FDT_END);
}

/* Test: NULL pointer handling */
static bool test_null_pointer(void) {
    uart_puts("\n[TEST] Null pointer handling... ");
    
    TEST_ASSERT(fdt_valid(NULL) == false, "fdt_valid should return false for NULL");
    
    memory_info_t mem_info;
    TEST_ASSERT(fdt_get_memory(NULL, &mem_info) == false, 
                "fdt_get_memory should return false for NULL fdt");
    
    TEST_ASSERT(fdt_get_memory((void*)0x1000, NULL) == false,
                "fdt_get_memory should return false for NULL mem_info");
    
    uart_puts("PASS\n");
    return true;
}

/* Test: Invalid magic number */
static bool test_invalid_magic(void) {
    uart_puts("[TEST] Invalid magic number... ");
    
    uint8_t buffer[512];
    fdt_header_t *header = (fdt_header_t *)buffer;
    
    /* Create FDT with wrong magic */
    memset(buffer, 0, sizeof(buffer));
    header->magic = __builtin_bswap32(0xdeadbeef);
    header->version = __builtin_bswap32(17);
    
    TEST_ASSERT(fdt_valid(buffer) == false, 
                "fdt_valid should return false for invalid magic");
    
    uart_puts("PASS\n");
    return true;
}

/* Test: Version too old */
static bool test_old_version(void) {
    uart_puts("[TEST] Version too old... ");
    
    uint8_t buffer[512];
    fdt_header_t *header = (fdt_header_t *)buffer;
    
    /* Create FDT with old version */
    memset(buffer, 0, sizeof(buffer));
    header->magic = __builtin_bswap32(FDT_MAGIC);
    header->version = __builtin_bswap32(16); /* Version 16 is too old */
    
    TEST_ASSERT(fdt_valid(buffer) == false,
                "fdt_valid should return false for version < 17");
    
    uart_puts("PASS\n");
    return true;
}

/* Test: Valid minimal FDT */
static bool test_minimal_valid_fdt(void) {
    uart_puts("[TEST] Minimal valid FDT... ");
    
    uint8_t buffer[512];
    create_minimal_fdt(buffer);
    
    TEST_ASSERT(fdt_valid(buffer) == true,
                "fdt_valid should return true for minimal valid FDT");
    
    memory_info_t mem_info;
    bool result = fdt_get_memory(buffer, &mem_info);
    TEST_ASSERT(result == false || mem_info.count == 0,
                "Minimal FDT should have no memory regions");
    
    uart_puts("PASS\n");
    return true;
}

/* Test: FDT with memory node */
static bool test_memory_node(void) {
    uart_puts("[TEST] FDT with memory node... ");
    
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    fdt_header_t *header = (fdt_header_t *)buffer;
    
    /* Fill header */
    header->magic = __builtin_bswap32(FDT_MAGIC);
    header->totalsize = __builtin_bswap32(1024);
    header->off_dt_struct = __builtin_bswap32(sizeof(fdt_header_t));
    header->off_dt_strings = __builtin_bswap32(512);
    header->version = __builtin_bswap32(17);
    header->last_comp_version = __builtin_bswap32(16);
    header->size_dt_strings = __builtin_bswap32(128);
    header->size_dt_struct = __builtin_bswap32(256);
    
    /* Add strings */
    char *strings = (char *)buffer + 512;
    strcpy(strings, "reg"); /* Offset 0 */
    strcpy(strings + 4, "device_type"); /* Offset 4 */
    strcpy(strings + 16, "memory"); /* Offset 16 */
    
    /* Add structure data */
    uint32_t *struct_ptr = (uint32_t *)((uint8_t *)buffer + sizeof(fdt_header_t));
    
    /* Root node */
    *struct_ptr++ = __builtin_bswap32(FDT_BEGIN_NODE);
    *struct_ptr++ = 0; /* Empty name for root */
    
    /* Memory node */
    *struct_ptr++ = __builtin_bswap32(FDT_BEGIN_NODE);
    strcpy((char *)struct_ptr, "memory@40000000");
    struct_ptr = (uint32_t *)((uint8_t *)struct_ptr + 20); /* Skip name + padding */
    
    /* device_type property */
    *struct_ptr++ = __builtin_bswap32(FDT_PROP);
    *struct_ptr++ = __builtin_bswap32(7); /* Length */
    *struct_ptr++ = __builtin_bswap32(4); /* String offset for "device_type" */
    strcpy((char *)struct_ptr, "memory");
    struct_ptr = (uint32_t *)((uint8_t *)struct_ptr + 8); /* 7 bytes + padding */
    
    /* reg property (base=0x40000000, size=256MB) */
    *struct_ptr++ = __builtin_bswap32(FDT_PROP);
    *struct_ptr++ = __builtin_bswap32(16); /* Length (2 x 64-bit values) */
    *struct_ptr++ = __builtin_bswap32(0); /* String offset for "reg" */
    *struct_ptr++ = __builtin_bswap32(0); /* Base high */
    *struct_ptr++ = __builtin_bswap32(0x40000000); /* Base low */
    *struct_ptr++ = __builtin_bswap32(0); /* Size high */
    *struct_ptr++ = __builtin_bswap32(0x10000000); /* Size low (256MB) */
    
    /* End memory node */
    *struct_ptr++ = __builtin_bswap32(FDT_END_NODE);
    
    /* End root node */
    *struct_ptr++ = __builtin_bswap32(FDT_END_NODE);
    
    /* End of structure */
    *struct_ptr++ = __builtin_bswap32(FDT_END);
    
    /* Test parsing */
    memory_info_t mem_info;
    bool result = fdt_get_memory(buffer, &mem_info);
    
    TEST_ASSERT(result == true, "Should successfully parse memory node");
    TEST_ASSERT(mem_info.count == 1, "Should find exactly one memory region");
    TEST_ASSERT(mem_info.regions[0].base == 0x40000000, 
                "Memory base should be 0x40000000");
    TEST_ASSERT(mem_info.regions[0].size == 0x10000000,
                "Memory size should be 256MB");
    TEST_ASSERT(mem_info.total_size == 0x10000000,
                "Total memory should be 256MB");
    
    uart_puts("PASS\n");
    return true;
}

/* Test: Multiple memory regions */
static bool test_multiple_memory_regions(void) {
    uart_puts("[TEST] Multiple memory regions... ");
    
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    fdt_header_t *header = (fdt_header_t *)buffer;
    
    /* Fill header */
    header->magic = __builtin_bswap32(FDT_MAGIC);
    header->totalsize = __builtin_bswap32(1024);
    header->off_dt_struct = __builtin_bswap32(sizeof(fdt_header_t));
    header->off_dt_strings = __builtin_bswap32(512);
    header->version = __builtin_bswap32(17);
    header->size_dt_strings = __builtin_bswap32(128);
    header->size_dt_struct = __builtin_bswap32(256);
    
    /* Add strings */
    char *strings = (char *)buffer + 512;
    strcpy(strings, "reg"); /* Offset 0 */
    
    /* Add structure data */
    uint32_t *struct_ptr = (uint32_t *)((uint8_t *)buffer + sizeof(fdt_header_t));
    
    /* Root node */
    *struct_ptr++ = __builtin_bswap32(FDT_BEGIN_NODE);
    *struct_ptr++ = 0;
    
    /* Memory node */
    *struct_ptr++ = __builtin_bswap32(FDT_BEGIN_NODE);
    strcpy((char *)struct_ptr, "memory");
    struct_ptr = (uint32_t *)((uint8_t *)struct_ptr + 8); /* name + padding */
    
    /* reg property with 2 regions */
    *struct_ptr++ = __builtin_bswap32(FDT_PROP);
    *struct_ptr++ = __builtin_bswap32(32); /* Length (4 x 64-bit values) */
    *struct_ptr++ = __builtin_bswap32(0); /* String offset for "reg" */
    
    /* Region 1: 0x40000000, 128MB */
    *struct_ptr++ = __builtin_bswap32(0);
    *struct_ptr++ = __builtin_bswap32(0x40000000);
    *struct_ptr++ = __builtin_bswap32(0);
    *struct_ptr++ = __builtin_bswap32(0x08000000);
    
    /* Region 2: 0x80000000, 64MB */
    *struct_ptr++ = __builtin_bswap32(0);
    *struct_ptr++ = __builtin_bswap32(0x80000000);
    *struct_ptr++ = __builtin_bswap32(0);
    *struct_ptr++ = __builtin_bswap32(0x04000000);
    
    /* End nodes */
    *struct_ptr++ = __builtin_bswap32(FDT_END_NODE);
    *struct_ptr++ = __builtin_bswap32(FDT_END_NODE);
    *struct_ptr++ = __builtin_bswap32(FDT_END);
    
    /* Test parsing */
    memory_info_t mem_info;
    bool result = fdt_get_memory(buffer, &mem_info);
    
    TEST_ASSERT(result == true, "Should successfully parse multiple regions");
    TEST_ASSERT(mem_info.count == 2, "Should find exactly two memory regions");
    TEST_ASSERT(mem_info.regions[0].base == 0x40000000, 
                "First region base should be 0x40000000");
    TEST_ASSERT(mem_info.regions[0].size == 0x08000000,
                "First region size should be 128MB");
    TEST_ASSERT(mem_info.regions[1].base == 0x80000000,
                "Second region base should be 0x80000000");
    TEST_ASSERT(mem_info.regions[1].size == 0x04000000,
                "Second region size should be 64MB");
    TEST_ASSERT(mem_info.total_size == 0x0C000000,
                "Total memory should be 192MB");
    
    uart_puts("PASS\n");
    return true;
}

/* Test: Small memory regions (should be ignored) */
static bool test_small_memory_regions(void) {
    uart_puts("[TEST] Small memory regions... ");
    
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    fdt_header_t *header = (fdt_header_t *)buffer;
    
    /* Fill header */
    header->magic = __builtin_bswap32(FDT_MAGIC);
    header->totalsize = __builtin_bswap32(1024);
    header->off_dt_struct = __builtin_bswap32(sizeof(fdt_header_t));
    header->off_dt_strings = __builtin_bswap32(512);
    header->version = __builtin_bswap32(17);
    header->size_dt_strings = __builtin_bswap32(128);
    header->size_dt_struct = __builtin_bswap32(256);
    
    /* Add strings */
    char *strings = (char *)buffer + 512;
    strcpy(strings, "reg");
    
    /* Add structure data */
    uint32_t *struct_ptr = (uint32_t *)((uint8_t *)buffer + sizeof(fdt_header_t));
    
    /* Root node */
    *struct_ptr++ = __builtin_bswap32(FDT_BEGIN_NODE);
    *struct_ptr++ = 0;
    
    /* Memory node */
    *struct_ptr++ = __builtin_bswap32(FDT_BEGIN_NODE);
    strcpy((char *)struct_ptr, "memory");
    struct_ptr = (uint32_t *)((uint8_t *)struct_ptr + 8);
    
    /* reg property with small region (512KB) */
    *struct_ptr++ = __builtin_bswap32(FDT_PROP);
    *struct_ptr++ = __builtin_bswap32(16);
    *struct_ptr++ = __builtin_bswap32(0);
    *struct_ptr++ = __builtin_bswap32(0);
    *struct_ptr++ = __builtin_bswap32(0x40000000);
    *struct_ptr++ = __builtin_bswap32(0);
    *struct_ptr++ = __builtin_bswap32(0x00080000); /* 512KB */
    
    /* End nodes */
    *struct_ptr++ = __builtin_bswap32(FDT_END_NODE);
    *struct_ptr++ = __builtin_bswap32(FDT_END_NODE);
    *struct_ptr++ = __builtin_bswap32(FDT_END);
    
    /* Test parsing */
    memory_info_t mem_info;
    bool result = fdt_get_memory(buffer, &mem_info);
    
    TEST_ASSERT(result == false || mem_info.count == 0,
                "Small memory regions (<1MB) should be ignored");
    
    uart_puts("PASS\n");
    return true;
}

/* Test: Corrupted structure */
static bool test_corrupted_structure(void) {
    uart_puts("[TEST] Corrupted structure handling... ");
    
    uint8_t buffer[512];
    create_minimal_fdt(buffer);
    
    /* Corrupt the structure by adding invalid token */
    uint32_t *struct_ptr = (uint32_t *)((uint8_t *)buffer + sizeof(fdt_header_t));
    struct_ptr[2] = __builtin_bswap32(0xDEADBEEF); /* Invalid token */
    
    memory_info_t mem_info;
    /* Parser should handle unknown tokens gracefully */
    fdt_get_memory(buffer, &mem_info);
    
    uart_puts("PASS (handled gracefully)\n");
    return true;
}

/* Test: Structure bounds checking */
static bool test_structure_bounds(void) {
    uart_puts("[TEST] Structure bounds checking... ");
    
    uint8_t buffer[512];
    create_minimal_fdt(buffer);
    
    /* Set structure size smaller than actual content */
    fdt_header_t *header = (fdt_header_t *)buffer;
    header->size_dt_struct = __builtin_bswap32(8); /* Too small */
    
    memory_info_t mem_info;
    fdt_get_memory(buffer, &mem_info);
    
    /* Should handle gracefully without crashing */
    uart_puts("PASS (bounds checked)\n");
    return true;
}

/* Main test runner */
void run_fdt_tests(void) {
    uart_puts("\n=======================================\n");
    uart_puts("Running FDT Parser Tests\n");
    uart_puts("=======================================\n");
    
    /* Reset counters */
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;
    
    /* Run all tests */
    test_null_pointer();
    test_invalid_magic();
    test_old_version();
    test_minimal_valid_fdt();
    test_memory_node();
    test_multiple_memory_regions();
    test_small_memory_regions();
    test_corrupted_structure();
    test_structure_bounds();
    
    /* Print summary */
    uart_puts("\n=======================================\n");
    uart_puts("FDT Test Summary:\n");
    uart_puts("Tests run: ");
    uart_puthex(tests_run);
    uart_puts("\nTests passed: ");
    uart_puthex(tests_passed);
    uart_puts("\nTests failed: ");
    uart_puthex(tests_failed);
    uart_puts("\n");
    
    if (tests_failed == 0) {
        uart_puts("\nAll FDT tests PASSED!\n");
    } else {
        uart_puts("\nSome FDT tests FAILED!\n");
    }
    uart_puts("=======================================\n\n");
}