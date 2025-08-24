#ifndef _IRQ_TESTS_H
#define _IRQ_TESTS_H

// Basic IRQ subsystem tests
void run_irq_basic_tests(void);

// Comprehensive allocator tests
void run_irq_allocator_tests(void);

// Comprehensive descriptor tests  
void run_irq_descriptor_tests(void);

// Run all IRQ tests
void run_all_irq_tests(void);

// ARM-specific hardware interrupt tests
void run_arm_hardware_interrupt_tests(void);

// Comprehensive ARM GIC tests
void run_arm_gic_comprehensive_tests(void);

// ARM GICv3-specific tests
void test_arm_gic_v3(void);

// ARM real hardware interrupt tests
void run_arm_real_interrupt_tests(void);

// RISCV hardware interrupt tests
void test_riscv_hardware_interrupts(void);

// RISCV PLIC comprehensive tests  
void test_riscv_plic_comprehensive(void);

// Hierarchical domain tests
void test_hierarchical_domains(void);

// Radix tree tests (for sparse domain support)
void test_radix_tree_all(void);

// Comprehensive tree domain tests for MSI support
void test_tree_domains_comprehensive(void);

#endif /* _IRQ_TESTS_H */