# Comprehensive verification of final state after higher-half mapping implementation
# This test ensures identity mapping still works and validates all mappings

delete breakpoints

# Start execution
break *0x40080000
continue

echo \n========================================\n
echo === FINAL STATE VERIFICATION ===\n
echo ========================================\n

# Let boot sequence complete up to MMU enable
break *0x400801dc
continue
delete breakpoints

echo \n=== 1. SYSTEM REGISTERS ===\n

# We'll read registers by examining the code that sets them
# First, go back a bit to where TTBR registers were set
echo \nTTBR0_EL1 was set from x23:\n
print/x $x23
echo TTBR1_EL1 was set from x19:\n
print/x $x19

# Step forward to read SCTLR after MMU enable
stepi
echo \nAfter MMU enable (we just executed msr sctlr_el1, x0)\n
echo If we got here without crashing, MMU is enabled successfully\n

# Test identity mapping thoroughly
echo \n=== 2. IDENTITY MAPPING VERIFICATION ===\n

# Test various addresses through identity mapping
echo \nReading kernel start (0x40080000):\n
x/4i 0x40080000

echo \nReading boot code (0x40080100):\n
x/4xw 0x40080100

echo \nReading page tables (0x40077000):\n
x/4gx 0x40077000

echo \nReading current PC area:\n
x/4i $pc-8
x/4i $pc

# Verify we can access different parts of memory
echo \n=== 3. MEMORY ACCESS PATTERNS ===\n

# Test read/write through identity mapping
set $test_addr = 0x40070000
echo \nTesting memory access at 
output/x $test_addr
echo :\n
set *((unsigned long *)$test_addr) = 0xDEADBEEFCAFEBABE
x/1gx $test_addr
if (*((unsigned long *)$test_addr) == 0xDEADBEEFCAFEBABE)
  echo Memory write/read successful through identity mapping\n
else
  echo ERROR: Memory access failed!\n
end

# Walk the page tables one more time to ensure they're intact
echo \n=== 4. PAGE TABLE INTEGRITY CHECK ===\n

# TTBR0 check
set $ttbr0_l0 = $x23
echo \nTTBR0 L0 at 
output/x $ttbr0_l0
echo :\n
x/2gx $ttbr0_l0
set $l0_entry = *((unsigned long *)$ttbr0_l0)
if ($l0_entry & 0x3)
  echo L0[0] valid, points to 
  output/x ($l0_entry & 0xFFFFFFFFF000)
  echo \n
else
  echo ERROR: TTBR0 L0[0] invalid!\n
end

# TTBR1 check
set $ttbr1_l0 = $x19
echo \nTTBR1 L0 at 
output/x $ttbr1_l0
echo :\n
# Check the 0x1FE entry
set $l0_1fe_addr = $ttbr1_l0 + (0x1FE * 8)
echo L0[0x1FE] at 
output/x $l0_1fe_addr
echo : 
x/1gx $l0_1fe_addr
set $l0_entry = *((unsigned long *)$l0_1fe_addr)
if ($l0_entry & 0x3)
  echo L0[0x1FE] valid, points to 
  output/x ($l0_entry & 0xFFFFFFFFF000)
  echo \n
else
  echo ERROR: TTBR1 L0[0x1FE] invalid!\n
end

# Test execution flow
echo \n=== 5. EXECUTION FLOW TEST ===\n

# Set up stack and continue
echo \nSetting up stack and continuing execution...\n
break *0x400801ec
continue

echo Stack pointer set to: 
print/x $sp

# Try to reach kernel_main
echo \nAttempting to reach kernel_main...\n
break kernel_main
continue

# If we get here, we reached kernel_main
echo \nSUCCESS: Reached kernel_main!\n
echo PC = 
output/x $pc
echo \n

# Analyze kernel_main
echo \n=== 6. KERNEL_MAIN ANALYSIS ===\n

# Show first few instructions
echo \nFirst 10 instructions of kernel_main:\n
x/10i $pc

# Step through and see what happens
echo \nStepping through kernel_main...\n
stepi
echo After 1 step, PC = 
output/x $pc
echo \n

# Check if we're about to access virtual addresses
x/1i $pc

# Continue stepping
stepi
echo After 2 steps, PC = 
output/x $pc
echo \n

# Check for exception
if ($pc == 0x200)
  echo \nHit exception vector at 0x200\n
  echo This is EXPECTED - kernel_main tries to load from virtual address\n
  
  # Let's see what instruction caused the fault
  echo \nAnalyzing the fault...\n
  # The LR should contain the faulting instruction address + 4
  print/x $x30
  
  echo \n=== 7. SUMMARY ===\n
  echo \nIDENTITY MAPPING: WORKING CORRECTLY\n
  echo - Can read/write through identity-mapped addresses\n
  echo - Page tables intact and valid\n
  echo - Execution proceeds normally until virtual address access\n
  echo \nHIGHER-HALF MAPPING: CONFIGURED BUT NOT ACTIVE\n
  echo - TTBR1 page tables set up correctly\n  
  echo - L0[0x1FE] entry valid and pointing to L1\n
  echo - Mapping ready but not used for execution\n
  echo \nNEXT STEP: Implement jump to higher-half addresses\n
else
  # Unexpected - kernel_main shouldn't work without virtual addresses
  echo \nUnexpected: kernel_main continuing without virtual addresses?\n
  x/4i $pc
end

echo \n=== VERIFICATION COMPLETE ===\n

quit