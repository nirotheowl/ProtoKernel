# Test script to verify higher-half mapping setup

# Connect to QEMU
target remote :1234

# Set a breakpoint after page tables are set up but before MMU enable
break *0x400801a4  # Just before MAIR configuration (mov x0, #0xff)

# Run to breakpoint
continue

# Print current physical base calculation
print/x $x28
echo Physical kernel base (x28): 
output/x $x28
echo \n

# Check TTBR0 page tables (identity mapping)
echo \n=== TTBR0 Page Tables (Identity Mapping) ===\n
echo L0 table base (x23): 
output/x $x23
echo \n

# Check TTBR0 L0[0] entry
echo L0[0] entry: 
x/1gx $x23
set $ttbr0_l1 = *(unsigned long *)$x23 & 0xFFFFFFFFF000

# Check TTBR0 L1[0] and L1[1] entries
echo L1[0] entry (0-512MB): 
x/1gx $ttbr0_l1
echo L1[1] entry (512MB-1GB): 
x/1gx $ttbr0_l1+8

# Check TTBR1 page tables (higher-half mapping)
echo \n=== TTBR1 Page Tables (Higher-Half Mapping) ===\n
echo L0 table base (x19): 
output/x $x19
echo \n

# Check TTBR1 L0[0x1FE] entry (for 0xFFFF range)
echo L0[0x1FE] entry: 
x/1gx $x19+(0x1FE*8)
set $ttbr1_l1 = *(unsigned long *)($x19+(0x1FE*8)) & 0xFFFFFFFFF000

# Check TTBR1 L1[1] entry
echo L1[1] entry: 
x/1gx $ttbr1_l1+8
set $ttbr1_l2 = *(unsigned long *)($ttbr1_l1+8) & 0xFFFFFFFFF000

# Check first few L2 entries for kernel mapping
echo \nL2 entries for kernel mapping:\n
x/4gx $ttbr1_l2

# Continue execution to after MMU enable
break *0x400801dc  # After MMU enable (after isb)
continue

# Check MMU status
echo \n=== After MMU Enable ===\n
echo SCTLR_EL1: 
set $sctlr = $SCTLR_EL1
output/x $sctlr
echo \n

echo TCR_EL1: 
set $tcr = $TCR_EL1
output/x $tcr
echo \n

echo TTBR0_EL1: 
set $ttbr0 = $TTBR0_EL1
output/x $ttbr0
echo \n

echo TTBR1_EL1: 
set $ttbr1 = $TTBR1_EL1
output/x $ttbr1
echo \n

# Test address translation
echo \n=== Testing Address Translation ===\n

# Test identity mapping (should work)
echo Testing identity mapping at 0x40080000:\n
x/1i 0x40080000

# Test higher-half mapping (should also work now)
echo \nTesting higher-half mapping at 0xFFFF000040200000:\n
x/1i 0xFFFF000040200000

# Let execution continue a bit more
stepi 10

# Show current PC
echo \nCurrent PC: 
output/x $pc
echo \n

# Continue execution
continue