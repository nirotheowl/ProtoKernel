#include <tests/irq_tests.h>
#include <uart.h>

// Global test statistics
static int total_tests_run = 0;
static int total_tests_passed = 0;
static int total_tests_failed = 0;

// Run all IRQ subsystem tests
void run_all_irq_tests(void) {
    uart_puts("\n");
    uart_puts("================================================================\n");
    uart_puts("            IRQ SUBSYSTEM TEST SUITE\n");
    uart_puts("================================================================\n");
    
    // Run architecture-specific tests
#ifdef __aarch64__
    // Run ARM hardware interrupt integration tests
    uart_puts("\n>>> Testing ARM Hardware Interrupt Integration...\n");
    run_arm_hardware_interrupt_tests();
    
    // Run comprehensive ARM GIC tests
    uart_puts("\n>>> Running Comprehensive ARM GIC Tests...\n");
    run_arm_gic_comprehensive_tests();
    
    // Run real ARM hardware interrupt tests
    uart_puts("\n>>> Running Real ARM Hardware Interrupt Tests...\n");
    run_arm_real_interrupt_tests();
#elif defined(__riscv)
    // RISC-V PLIC tests will go here
    uart_puts("\n>>> RISC-V PLIC tests not yet implemented\n");
#endif
    
    uart_puts("\n");
    uart_puts("================================================================\n");
    uart_puts("                    ALL TESTS COMPLETE\n");
    uart_puts("================================================================\n");
}