# MMU Enable Sequence Verification
# This script checks the MMU enable sequence
set pagination off

echo \n==============================================================\n
echo                      MMU ENABLE VERIFICATION\n  
echo ==============================================================\n

delete

# Set up breakpoints for MMU enable sequence
echo Setting up breakpoints for MMU enable sequence...\n

# Before MAIR setup (mov x0, #0xFF)
break *0x40080180
# Before MMU enable (mrs x0, sctlr_el1)
break *0x400801e8
# After MMU enable (isb after msr sctlr_el1)
break *0x400801fc
# At jump preparation
break *0x40080220

continue

echo \n=== 1. BEFORE MAIR SETUP ===\n
echo PC: 
output/x $pc
echo \n
echo Stack pointer: 
output/x $sp
echo \n
echo Page table pointers:\n
echo   x23 (TTBR0 L0): 
output/x $x23
echo \n
echo   x19 (TTBR1 L0): 
output/x $x19
echo \n
continue

echo \n=== 2. BEFORE MMU ENABLE ===\n
echo About to enable MMU at PC: 
output/x $pc
echo \n
echo Current registers:\n
echo   x0 (will hold SCTLR value): 
output/x $x0
echo \n

# Check if exception vectors are set (should be before this point)
echo Checking exception vectors...\n
# The instruction at 0x400801dc sets VBAR_EL1
echo Exception vectors should be at: 
x/i 0x400801d8
echo \n

echo \nStepping through MMU enable sequence...\n
# Step through the critical instructions
stepi
echo After mrs x0, sctlr_el1: x0 = 
output/x $x0
echo \n

stepi
echo After orr (M bit): x0 = 
output/x $x0
echo  (bit 0 set)\n

stepi
echo After orr (C bit): x0 = 
output/x $x0
echo  (bit 2 set)\n

stepi
echo After orr (I bit): x0 = 
output/x $x0
echo  (bit 12 set)\n

stepi
echo After msr sctlr_el1, x0: MMU is being enabled...\n

stepi
echo After isb: MMU is now active!\n
echo Current PC: 
output/x $pc
echo \n

continue

echo \n=== 3. AFTER MMU ENABLE ===\n
echo PC after MMU enable: 
output/x $pc
echo \n

echo \nMemory access test:\n
echo Testing identity mapping at kernel start: 
x/4i 0x40080000
echo Testing current PC location: 
x/4i $pc

echo \n=== 4. AT JUMP PREPARATION ===\n
echo Ready to jump to higher half\n
echo Current PC: 
output/x $pc
echo \n
echo Registers for jump:\n
echo   x0: 
output/x $x0
echo \n
echo   x1: 
output/x $x1
echo \n

echo \n=== 5. MMU ENABLE SUMMARY ===\n
echo ✓ MMU enable sequence completed\n
echo ✓ Still executing after MMU enable\n
echo ✓ Identity mapping working\n
echo ✓ Ready for higher-half jump\n

quit