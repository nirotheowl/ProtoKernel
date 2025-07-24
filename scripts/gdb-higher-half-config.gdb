# GDB configuration for debugging higher-half kernel before MMU is enabled
# This handles the virtual/physical address mismatch

# Helper function to set breakpoint at physical address corresponding to a symbol
define break-phys
    # Get symbol's virtual address
    set $virt = (unsigned long)&$arg0
    # Calculate physical address (subtract 0xFFFF000000000000)
    set $phys = $virt - 0xFFFF000000000000
    # Set breakpoint at physical address
    break *$phys
    printf "Breakpoint set at physical 0x%lx (virtual %s = 0x%lx)\n", $phys, "$arg0", $virt
end

# Helper to display code at current physical location with source correlation
define show-code
    # Show assembly at current location
    x/10i $pc
    # Try to correlate with source
    set $virt_pc = $pc + 0xFFFF000000000000
    info line *$virt_pc
end

# Override TUI refresh to handle physical addresses
define refresh-tui
    if $pc < 0x100000000
        # We're in physical address space, show assembly only
        layout asm
        # Update TUI to show current physical location
        tui reg general
    else
        # We're in virtual space (after MMU enabled)
        layout src
    end
end

# Custom step that handles address translation
define step-phys
    # Get next instruction address
    set $next = $pc + 4
    # Single step
    stepi
    # Show where we are
    printf "Now at physical 0x%lx\n", $pc
    show-code
end

# Custom next that handles physical addresses  
define next-phys
    # This is tricky without debug symbols at physical addresses
    # For now, just step instruction by instruction
    stepi
    printf "Now at physical 0x%lx\n", $pc
    show-code
end

# Set up common breakpoints at physical addresses
echo === Setting up breakpoints at physical addresses ===\n

# _start is at virtual 0xFFFF000040200000, physical 0x40080000 (when loaded by QEMU)
break *0x40080000
echo Breakpoint at _start (physical)\n

# el1_entry is at offset 0x68
break *0x40080068  
echo Breakpoint at el1_entry (physical)\n

# Position-independent code at offset 0x90
break *0x40080090
echo Breakpoint at address calculation code (physical)\n

# kernel_main is at virtual 0xFFFF000040201980, physical 0x40081980
break *0x40081980
echo Breakpoint at kernel_main (physical)\n

# Override the step and next commands for physical mode
echo \n=== Commands available ===\n
echo break-phys <symbol> - Set breakpoint at physical address of symbol\n
echo show-code          - Display code at current location\n  
echo step-phys          - Step one instruction (physical mode)\n
echo next-phys          - Next instruction (physical mode)\n
echo \nExample: break-phys kernel_main\n

# Continue to first breakpoint
echo \n=== Continuing to _start ===\n
continue

# Now at _start, show initial state
echo \n=== At _start ===\n
info registers pc sp x0 x19
show-code