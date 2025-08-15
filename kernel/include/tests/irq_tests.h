#ifndef _IRQ_TESTS_H
#define _IRQ_TESTS_H

// Basic IRQ subsystem tests
void run_irq_basic_tests(void);

// Comprehensive allocator tests
void run_irq_allocator_tests(void);

// Comprehensive descriptor tests  
void run_irq_descriptor_tests(void);

// Comprehensive domain tests
void run_irq_domain_tests(void);

// Stress and performance tests
void run_irq_stress_tests(void);

// Edge cases and error handling tests
void run_irq_edge_tests(void);

// Run all IRQ tests
void run_all_irq_tests(void);

// ARM-specific hardware interrupt tests
void run_arm_hardware_interrupt_tests(void);

// Comprehensive ARM GIC tests
void run_arm_gic_comprehensive_tests(void);

// ARM real hardware interrupt tests
void run_arm_real_interrupt_tests(void);

#endif /* _IRQ_TESTS_H */