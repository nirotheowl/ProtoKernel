# Simple test script for higher-half mapping

# Breakpoint at beginning 
break *0x40080000
continue

# Step through boot code and check registers periodically
echo Starting boot sequence...\n

# Step to el1_entry
break *0x40080068
continue
info registers x19 x20 x21 x22 x23 x28

# Breakpoint right before MMU enable
break *0x400801d0
continue

echo \nBefore MMU enable:\n
echo x28 (phys base): 
print/x $x28
echo x23 (TTBR0 L0): 
print/x $x23
echo x19 (TTBR1 L0): 
print/x $x19

# Check TTBR1 L0[0x1FE] entry
echo \nTTBR1 L0[0x1FE]: 
x/1gx $x19+(0x1FE*8)

# Step over MMU enable
stepi 5

# Check if MMU is enabled
echo \nAfter MMU enable:\n
set $sctlr = $SCTLR_EL1
echo SCTLR_EL1: 
print/x $sctlr

# Continue and see where we crash
continue