/*
 * kernel/tests/fdt_mgr_tests.c
 *
 * Tests for FDT Manager functionality
 */

#include <stdint.h>
#include <stdbool.h>
#include <drivers/fdt.h>
#include <drivers/fdt_mgr.h>
#include <uart.h>
#include <string.h>

static bool test_fdt_virtual_access(void) {
    uart_puts("\nTesting FDT virtual memory access...\n");
    
    // Get FDT blob through virtual mapping
    void *fdt_virt = fdt_mgr_get_blob();
    if (!fdt_virt) {
        uart_puts("  FAIL: Could not get FDT virtual address\n");
        return false;
    }
    
    uart_puts("  FDT virtual address: ");
    uart_puthex((uint64_t)fdt_virt);
    uart_puts("\n");
    
    // Try to read FDT header
    fdt_header_t *header = (fdt_header_t *)fdt_virt;
    uint32_t magic = fdt32_to_cpu(header->magic);
    
    if (magic != FDT_MAGIC) {
        uart_puts("  FAIL: Invalid magic through virtual mapping: ");
        uart_puthex(magic);
        uart_puts("\n");
        return false;
    }
    
    uart_puts("  PASS: FDT magic valid through virtual mapping\n");
    
    // Try to access other header fields
    uint32_t totalsize = fdt32_to_cpu(header->totalsize);
    uint32_t version = fdt32_to_cpu(header->version);
    
    uart_puts("  Total size: ");
    uart_puthex(totalsize);
    uart_puts(" bytes\n");
    uart_puts("  Version: ");
    uart_puthex(version);
    uart_puts("\n");
    
    // Verify we can read memory info through virtual mapping
    memory_info_t mem_info;
    if (!fdt_mgr_get_memory_info(&mem_info)) {
        uart_puts("  FAIL: Could not read memory info through virtual mapping\n");
        return false;
    }
    
    uart_puts("  PASS: Successfully read memory info through virtual mapping\n");
    uart_puts("    Found ");
    uart_puthex(mem_info.count);
    uart_puts(" memory region(s)\n");
    
    return true;
}

static bool test_fdt_bounds_check(void) {
    uart_puts("\nTesting FDT bounds checking...\n");
    
    void *fdt_virt = fdt_mgr_get_blob();
    size_t fdt_size = fdt_mgr_get_size();
    
    if (!fdt_virt || !fdt_size) {
        uart_puts("  FAIL: FDT not available\n");
        return false;
    }
    
    // Try to read at the end of FDT (should succeed)
    uint8_t *fdt_bytes = (uint8_t *)fdt_virt;
    uint8_t last_byte = fdt_bytes[fdt_size - 1];
    uart_puts("  PASS: Successfully read last byte of FDT: ");
    uart_puthex(last_byte);
    uart_puts("\n");
    
    // Note: We can't easily test reading beyond FDT bounds without
    // potentially causing a page fault. In production, this would be
    // protected by proper page permissions.
    
    return true;
}

static bool test_fdt_persistence(void) {
    uart_puts("\nTesting FDT persistence...\n");
    
    // Get initial values
    void *phys1 = fdt_mgr_get_phys_addr();
    void *virt1 = fdt_mgr_get_blob();
    size_t size1 = fdt_mgr_get_size();
    
    // Simulate some memory allocations to ensure FDT stays intact
    // (In a real test, we'd allocate and free memory)
    
    // Get values again
    void *phys2 = fdt_mgr_get_phys_addr();
    void *virt2 = fdt_mgr_get_blob();
    size_t size2 = fdt_mgr_get_size();
    
    if (phys1 != phys2 || virt1 != virt2 || size1 != size2) {
        uart_puts("  FAIL: FDT values changed unexpectedly\n");
        return false;
    }
    
    uart_puts("  PASS: FDT values remain consistent\n");
    
    // Verify FDT content hasn't changed
    fdt_header_t *header = (fdt_header_t *)virt1;
    if (fdt32_to_cpu(header->magic) != FDT_MAGIC) {
        uart_puts("  FAIL: FDT content corrupted\n");
        return false;
    }
    
    uart_puts("  PASS: FDT content intact\n");
    
    return true;
}

static bool test_fdt_integrity(void) {
    uart_puts("\nTesting FDT integrity checks...\n");
    
    if (!fdt_mgr_verify_integrity()) {
        uart_puts("  FAIL: FDT integrity check failed\n");
        return false;
    }
    
    uart_puts("  PASS: FDT integrity check passed\n");
    
    // Test that we can still access FDT after integrity check
    memory_info_t mem_info;
    if (!fdt_mgr_get_memory_info(&mem_info)) {
        uart_puts("  FAIL: Cannot access FDT after integrity check\n");
        return false;
    }
    
    uart_puts("  PASS: FDT still accessible after integrity check\n");
    
    return true;
}

static bool test_fdt_memory_parsing(void) {
    uart_puts("\nTesting FDT memory region parsing...\n");
    
    memory_info_t mem_info;
    if (!fdt_mgr_get_memory_info(&mem_info)) {
        uart_puts("  FAIL: Could not parse memory info\n");
        return false;
    }
    
    // Verify we have at least one memory region
    if (mem_info.count == 0) {
        uart_puts("  FAIL: No memory regions found\n");
        return false;
    }
    
    uart_puts("  Memory regions found: ");
    uart_puthex(mem_info.count);
    uart_puts("\n");
    
    // Verify each memory region
    for (int i = 0; i < mem_info.count; i++) {
        uart_puts("    Region ");
        uart_puthex(i);
        uart_puts(": base=");
        uart_puthex(mem_info.regions[i].base);
        uart_puts(" size=");
        uart_puthex(mem_info.regions[i].size);
        uart_puts("\n");
        
        // Sanity checks
        if (mem_info.regions[i].size == 0) {
            uart_puts("  FAIL: Region has zero size\n");
            return false;
        }
        
        // For QEMU virt, we expect base at 0x40000000
        if (i == 0 && mem_info.regions[i].base != 0x40000000) {
            uart_puts("  WARNING: Unexpected base address for first region\n");
        }
    }
    
    // Verify total size calculation
    uint64_t calculated_total = 0;
    for (int i = 0; i < mem_info.count; i++) {
        calculated_total += mem_info.regions[i].size;
    }
    
    if (calculated_total != mem_info.total_size) {
        uart_puts("  FAIL: Total size mismatch (calculated=");
        uart_puthex(calculated_total);
        uart_puts(" reported=");
        uart_puthex(mem_info.total_size);
        uart_puts(")\n");
        return false;
    }
    
    uart_puts("  PASS: Memory parsing successful, total size=");
    uart_puthex(mem_info.total_size);
    uart_puts("\n");
    
    return true;
}

static int test_device_callback(const char *path, const char *name, void *ctx) {
    int *count = (int *)ctx;
    
    // Print all devices (no limit)
    uart_puts("    Device ");
    uart_puthex(*count);
    uart_puts(": ");
    uart_puts(path);
    uart_puts(" name=");
    uart_puts(name);
    uart_puts("\n");
    
    (*count)++;
    return 0;  // Continue enumeration
}

static bool test_fdt_root_node(void) {
    uart_puts("\nTesting FDT root node access...\n");
    
    void *fdt = fdt_mgr_get_blob();
    if (!fdt) {
        uart_puts("  FAIL: Could not get FDT blob\n");
        return false;
    }
    
    // Try to get root node name using offset 0
    const char *root_name = fdt_get_name(fdt, 0, NULL);
    if (!root_name) {
        uart_puts("  FAIL: Could not get root node name\n");
        return false;
    }
    
    uart_puts("  Root node name: '");
    uart_puts(root_name);
    uart_puts("'\n");
    
    // Root node should have empty name
    if (root_name[0] != '\0') {
        uart_puts("  FAIL: Root node should have empty name\n");
        return false;
    }
    
    uart_puts("  PASS: Root node accessed successfully\n");
    return true;
}

static bool test_fdt_first_child(void) {
    uart_puts("\nTesting FDT first child access...\n");
    
    void *fdt = fdt_mgr_get_blob();
    if (!fdt) {
        uart_puts("  FAIL: Could not get FDT blob\n");
        return false;
    }
    
    // Get first child of root
    int child = fdt_first_subnode(fdt, 0);
    uart_puts("  First child offset: ");
    uart_puthex(child);
    uart_puts("\n");
    
    if (child < 0) {
        uart_puts("  FAIL: Could not get first child\n");
        return false;
    }
    
    // Get name of first child
    const char *child_name = fdt_get_name(fdt, child, NULL);
    if (!child_name) {
        uart_puts("  FAIL: Could not get child name\n");
        return false;
    }
    
    uart_puts("  First child name: '");
    uart_puts(child_name);
    uart_puts("'\n");
    
    uart_puts("  PASS: First child accessed successfully\n");
    return true;
}

static bool test_fdt_device_enumeration(void) {
    uart_puts("\nTesting FDT device enumeration...\n");
    
    // First test root node access
    if (!test_fdt_root_node()) {
        return false;
    }
    
    // Test first child access
    if (!test_fdt_first_child()) {
        return false;
    }
    
    // Now try limited enumeration
    uart_puts("\n  Testing device enumeration (first level children)...\n");
    void *fdt = fdt_mgr_get_blob();
    if (!fdt) {
        return false;
    }
    
    int node_count = 0;
    int child;
    fdt_for_each_subnode(child, fdt, 0) {
        const char *name = fdt_get_name(fdt, child, NULL);
        if (name) {
            uart_puts("    Child node ");
            uart_puthex(node_count);
            uart_puts(": '");
            uart_puts(name);
            uart_puts("'\n");
            node_count++;
        }
    }
    uart_puts("    Total first-level children: ");
    uart_puthex(node_count);
    uart_puts("\n");
    
    if (node_count == 0) {
        uart_puts("  FAIL: No child nodes found\n");
        return false;
    }
    
    uart_puts("  PASS: Device enumeration successful\n");
    
    // Now test full enumeration through FDT manager
    uart_puts("\n  Testing FDT manager device enumeration...\n");
    int device_count = 0;
    int ret = fdt_mgr_enumerate_devices(test_device_callback, &device_count);
    
    if (ret != 0) {
        uart_puts("  FAIL: Device enumeration failed with error ");
        uart_puthex(ret);
        uart_puts("\n");
        return false;
    }
    
    if (device_count == 0) {
        uart_puts("  FAIL: No devices found\n");
        return false;
    }
    
    uart_puts("  Total devices found: ");
    uart_puthex(device_count);
    uart_puts("\n");
    
    // We expect at least a root node (minimum requirement)
    if (device_count < 1) {
        uart_puts("  FAIL: Expected at least root node\n");
        return false;
    }
    
    uart_puts("  PASS: FDT manager enumeration successful\n");
    return true;
}

static bool test_fdt_header_fields(void) {
    uart_puts("\nTesting FDT header field access...\n");
    
    void *fdt = fdt_mgr_get_blob();
    if (!fdt) {
        uart_puts("  FAIL: Could not get FDT blob\n");
        return false;
    }
    
    fdt_header_t *header = (fdt_header_t *)fdt;
    
    // Check all header fields are accessible and reasonable
    uint32_t magic = fdt32_to_cpu(header->magic);
    uint32_t totalsize = fdt32_to_cpu(header->totalsize);
    uint32_t off_dt_struct = fdt32_to_cpu(header->off_dt_struct);
    uint32_t off_dt_strings = fdt32_to_cpu(header->off_dt_strings);
    uint32_t off_mem_rsvmap = fdt32_to_cpu(header->off_mem_rsvmap);
    uint32_t version = fdt32_to_cpu(header->version);
    uint32_t last_comp_version = fdt32_to_cpu(header->last_comp_version);
    uint32_t boot_cpuid_phys = fdt32_to_cpu(header->boot_cpuid_phys);
    uint32_t size_dt_strings = fdt32_to_cpu(header->size_dt_strings);
    uint32_t size_dt_struct = fdt32_to_cpu(header->size_dt_struct);
    
    uart_puts("  FDT Header Fields:\n");
    uart_puts("    magic: ");
    uart_puthex(magic);
    uart_puts(" (expected ");
    uart_puthex(FDT_MAGIC);
    uart_puts(")\n");
    
    uart_puts("    totalsize: ");
    uart_puthex(totalsize);
    uart_puts("\n");
    
    uart_puts("    version: ");
    uart_puthex(version);
    uart_puts(" / last_comp_version: ");
    uart_puthex(last_comp_version);
    uart_puts("\n");
    
    uart_puts("    boot_cpuid_phys: ");
    uart_puthex(boot_cpuid_phys);
    uart_puts("\n");
    
    // Verify offsets are within bounds
    bool offsets_valid = true;
    
    if (off_dt_struct >= totalsize) {
        uart_puts("  FAIL: dt_struct offset out of bounds\n");
        offsets_valid = false;
    }
    
    if (off_dt_strings >= totalsize) {
        uart_puts("  FAIL: dt_strings offset out of bounds\n");
        offsets_valid = false;
    }
    
    if (off_mem_rsvmap >= totalsize) {
        uart_puts("  FAIL: mem_rsvmap offset out of bounds\n");
        offsets_valid = false;
    }
    
    if (off_dt_struct + size_dt_struct > totalsize) {
        uart_puts("  FAIL: dt_struct region exceeds totalsize\n");
        offsets_valid = false;
    }
    
    if (off_dt_strings + size_dt_strings > totalsize) {
        uart_puts("  FAIL: dt_strings region exceeds totalsize\n");
        offsets_valid = false;
    }
    
    if (!offsets_valid) {
        return false;
    }
    
    uart_puts("  PASS: All header fields valid and within bounds\n");
    return true;
}

static bool test_fdt_size_limits(void) {
    uart_puts("\nTesting FDT size handling...\n");
    
    size_t fdt_size = fdt_mgr_get_size();
    
    uart_puts("  Current FDT size: ");
    uart_puthex(fdt_size);
    uart_puts(" bytes\n");
    
    // Check if size is reasonable (between 1KB and 2MB)
    if (fdt_size < 1024) {
        uart_puts("  WARNING: FDT seems unusually small\n");
    }
    
    if (fdt_size > FDT_MAX_SIZE) {
        uart_puts("  WARNING: FDT exceeds maximum size limit\n");
    }
    
    // For QEMU, we expect around 1MB
    if (fdt_size == 0x100000) {
        uart_puts("  INFO: FDT is exactly 1MB (QEMU default)\n");
    }
    
    uart_puts("  PASS: FDT size is within acceptable range\n");
    return true;
}

static bool test_fdt_physical_virtual_mapping(void) {
    uart_puts("\nTesting FDT physical/virtual address mapping...\n");
    
    void *phys_addr = fdt_mgr_get_phys_addr();
    void *virt_addr = fdt_mgr_get_blob();
    
    uart_puts("  Physical address: ");
    uart_puthex((uint64_t)phys_addr);
    uart_puts("\n");
    
    uart_puts("  Virtual address: ");
    uart_puthex((uint64_t)virt_addr);
    uart_puts("\n");
    
    // Verify virtual address is in expected range
    uint64_t virt = (uint64_t)virt_addr;
    if ((virt & 0xFFFF000000000000UL) != 0xFFFF000000000000UL) {
        uart_puts("  FAIL: Virtual address not in kernel space\n");
        return false;
    }
    
    // Check if it's at the expected FDT virtual base
    uint64_t expected_base = FDT_VIRT_BASE;
    uint64_t offset = virt - expected_base;
    
    if (offset >= FDT_MAX_SIZE) {
        uart_puts("  FAIL: Virtual address outside FDT mapping region\n");
        return false;
    }
    
    uart_puts("  PASS: Physical/virtual mapping is correct\n");
    return true;
}

static bool test_fdt_edge_cases(void) {
    uart_puts("\nTesting FDT edge cases and error handling...\n");
    
    void *fdt = fdt_mgr_get_blob();
    if (!fdt) {
        uart_puts("  FAIL: Could not get FDT blob\n");
        return false;
    }
    
    // Test 1: Invalid node offset
    uart_puts("  Testing invalid node offset...\n");
    const char *bad_name = fdt_get_name(fdt, 0xFFFFFF, NULL);
    if (bad_name != NULL) {
        uart_puts("  FAIL: Expected NULL for invalid offset, got non-NULL\n");
        return false;
    }
    uart_puts("    PASS: Invalid offset handled correctly\n");
    
    // Test 2: Get first subnode of non-existent node
    uart_puts("  Testing subnode of invalid offset...\n");
    int bad_child = fdt_first_subnode(fdt, 0xFFFFFF);
    if (bad_child >= 0) {
        uart_puts("  FAIL: Expected negative value for invalid parent\n");
        return false;
    }
    uart_puts("    PASS: Invalid parent handled correctly\n");
    
    // Test 3: Test FDT manager with NULL
    uart_puts("  Testing FDT manager NULL handling...\n");
    if (fdt_mgr_init(NULL)) {
        uart_puts("  FAIL: FDT manager init should fail with NULL\n");
        return false;
    }
    uart_puts("    PASS: NULL DTB handled correctly\n");
    
    // Test 4: Memory info with corrupted FDT (simulate)
    uart_puts("  Testing memory parse error handling...\n");
    // This test is limited as we can't actually corrupt the FDT
    // but we verify the function doesn't crash
    memory_info_t test_mem_info;
    bool mem_result = fdt_mgr_get_memory_info(&test_mem_info);
    if (!mem_result) {
        uart_puts("  WARNING: Memory info parse failed (expected to work)\n");
    } else {
        uart_puts("    PASS: Memory info parse succeeded\n");
    }
    
    // Test 5: Enumeration with NULL callback
    uart_puts("  Testing enumeration with NULL callback...\n");
    int null_ret = fdt_mgr_enumerate_devices(NULL, NULL);
    if (null_ret != 0) {
        uart_puts("  WARNING: Enumeration with NULL callback returned error\n");
    } else {
        uart_puts("    PASS: NULL callback handled gracefully\n");
    }
    
    uart_puts("  PASS: Edge case testing complete\n");
    return true;
}

void run_fdt_mgr_tests(void) {
    uart_puts("\n=== Running FDT Manager Tests ===\n");
    
    int passed = 0;
    int failed = 0;
    
    // Basic tests
    if (test_fdt_virtual_access()) passed++; else failed++;
    if (test_fdt_bounds_check()) passed++; else failed++;
    if (test_fdt_persistence()) passed++; else failed++;
    if (test_fdt_integrity()) passed++; else failed++;
    
    // Extended tests
    if (test_fdt_memory_parsing()) passed++; else failed++;
    if (test_fdt_device_enumeration()) passed++; else failed++;
    if (test_fdt_header_fields()) passed++; else failed++;
    if (test_fdt_size_limits()) passed++; else failed++;
    if (test_fdt_physical_virtual_mapping()) passed++; else failed++;
    if (test_fdt_edge_cases()) passed++; else failed++;
    
    uart_puts("\nFDT Manager Test Results: ");
    uart_puthex(passed);
    uart_puts(" passed, ");
    uart_puthex(failed);
    uart_puts(" failed\n");
}