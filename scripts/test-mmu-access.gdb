# Test MMU enable and memory access through both mappings

# Skip default breakpoints
delete breakpoints

# Go straight to after MMU enable
break *0x400801dc
continue

echo \n=== MMU Enabled ===\n
echo Current PC: 
print/x $pc
echo Stack pointer: 
print/x $sp

# Check MMU status
echo \nSCTLR_EL1: 
print/x $SCTLR_EL1

# Test memory access through both mappings
echo \n=== Testing Memory Access ===\n

# Read the first instruction through identity mapping
echo \nReading _start through identity mapping (0x40080000):\n
x/1i 0x40080000

# Read the same instruction through higher-half mapping
echo \nReading _start through higher-half mapping (0xFFFF000040080000):\n
x/1i 0xFFFF000040080000

# Read some data
echo \nReading data at offset 0x100:\n
echo Identity (0x40080100): 
x/1xw 0x40080100
echo Higher-half (0xFFFF000040080100): 
x/1xw 0xFFFF000040080100

# Try to read kernel_main
echo \nReading kernel_main:\n
echo Identity: 
x/1i 0x40081980
echo Higher-half: 
x/1i 0xFFFF000040081980

# Step a few instructions
echo \n=== Stepping through code ===\n
stepi 5
echo Current PC after stepping: 
print/x $pc

# Continue to kernel_main
break *0x40081980
continue

echo \n=== Reached kernel_main ===\n
echo Success! Both mappings are working correctly.\n

quit