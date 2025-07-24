# GDB initialization file for higher-half kernel debugging
# This handles the virtual/physical address mismatch before MMU is enabled

# Calculate physical address offset (QEMU loads at 0x40080000 instead of 0x40200000)
set $phys_offset = 0x40080000 - 0x40200000

# Helper to convert virtual to physical
define v2p
    set $virt = $arg0
    set $phys = $virt - 0xFFFF000000000000 + $phys_offset
    printf "Virtual 0x%lx -> Physical 0x%lx\n", $virt, $phys
end

# Helper to convert physical to virtual
define p2v
    set $phys = $arg0
    set $virt = $phys + 0xFFFF000000000000 - $phys_offset
    printf "Physical 0x%lx -> Virtual 0x%lx\n", $phys, $virt
end

# Enhanced breakpoint function for symbols
define b-phys
    set $virt = (unsigned long)&$arg0
    set $phys = $virt - 0xFFFF000000000000 + $phys_offset
    break *$phys
    printf "Breakpoint at %s: physical 0x%lx (virtual 0x%lx)\n", "$arg0", $phys, $virt
end

# Function to display current context
define ctx
    printf "\n=== Context at PC: 0x%lx ===\n", $pc
    
    # Show key registers
    printf "SP: 0x%016lx  X0: 0x%016lx\n", $sp, $x0
    printf "X19: 0x%016lx  X28: 0x%016lx\n", $x19, $x28
    
    # Show next instructions
    x/5i $pc
    
    # Try to show source location
    set $virt_pc = $pc + 0xFFFF000000000000 - $phys_offset
    printf "\nVirtual PC: 0x%lx\n", $virt_pc
end

# Stepping helpers with context display
define si
    stepi
    ctx
end

define ni
    nexti
    ctx
end

# Common breakpoints for boot process
define setup-boot-breakpoints
    # Clear existing breakpoints
    delete
    
    # Set key breakpoints
    break *0x40080000
    printf "1. _start (physical 0x40080000)\n"
    
    break *0x40080068
    printf "2. el1_entry (physical 0x40080068)\n"
    
    break *0x40080090
    printf "3. Position-independent code (physical 0x40080090)\n"
    
    break *0x40081980
    printf "4. kernel_main (physical 0x40081980)\n"
end

# TUI configuration for physical addresses
define tui-phys
    layout asm
    layout regs
    focus cmd
end

# Show available commands
define help-phys
    printf "\n=== Higher-Half Kernel Debug Commands ===\n"
    printf "\nAddress Translation:\n"
    printf "  v2p <addr>      - Convert virtual to physical address\n"
    printf "  p2v <addr>      - Convert physical to virtual address\n"
    printf "\nBreakpoints:\n"
    printf "  b-phys <symbol> - Set breakpoint at physical address of symbol\n"
    printf "  setup-boot-breakpoints - Set common boot breakpoints\n"
    printf "\nStepping:\n"
    printf "  si              - Step instruction with context\n"
    printf "  ni              - Next instruction with context\n"
    printf "  ctx             - Show current context\n"
    printf "\nTUI:\n"
    printf "  tui-phys        - Setup TUI for physical address debugging\n"
    printf "\nExample: b-phys kernel_main\n\n"
end

# Print banner
printf "\n=== GDB Higher-Half Kernel Debugger ===\n"
printf "Kernel is loaded at physical 0x40080000 by QEMU\n"
printf "Virtual addresses start at 0xFFFF000040200000\n"
printf "Type 'help-phys' for available commands\n\n"

# Set default breakpoints
setup-boot-breakpoints

# Show initial help
help-phys