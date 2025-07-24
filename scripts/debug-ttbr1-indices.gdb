# Debug TTBR1 index calculations

break *0x40080068
continue
delete breakpoints

# Let setup complete
break *0x400801c8
continue

echo \n=== TTBR1 Index Calculations ===\n

# Test address 0xFFFF000040200000
set $virt_addr = 0xFFFF000040200000

echo Virtual address: 
output/x $virt_addr
echo \n

# Manual calculation
echo \nManual calculation:\n
# We know this should be 0x1FE
set $l0_idx_manual = 0x1FE
echo Expected L0 index: 
output/x $l0_idx_manual
echo \n

# GDB calculation with explicit casting
set $l0_idx = ((unsigned long long)$virt_addr >> 39) & 0x1FF
echo Calculated L0 index (with ULL cast): 
output/x $l0_idx
echo \n

# Check the entry
echo \nChecking L0[0x1FE]:\n
echo Address: 
output/x $x19 + (0x1FE * 8)
echo \nValue: 
x/1gx $x19 + (0x1FE * 8)

# Also check what we incorrectly stored
echo \nWhat's at L0[0]?: 
x/1gx $x19

# Continue checking other indices
set $l1_idx = (($virt_addr >> 30) & 0x1FF)
set $l2_idx = (($virt_addr >> 21) & 0x1FF)

echo \nL1 index: 
output/x $l1_idx
echo \nL2 index: 
output/x $l2_idx
echo \n

continue