# Test execution flow carefully

# Start at beginning
break *0x40080000
continue

# Go to el1_entry
break *0x40080068
continue
delete breakpoints

echo \n=== Starting el1_entry execution ===\n

# Step through instruction by instruction
# Should be at 0x40080068 (mrs x0, sctlr_el1)
echo Executing: 
x/1i $pc
stepi

# Continue stepping through MMU disable
stepi 5

# Should be at ic iallu
echo \nAfter MMU disable: 
x/1i $pc
stepi 4

# Should now be at adr x28, _start (0x40080090)
echo \n=== Physical address calculation ===\n
echo About to execute: 
x/1i $pc
echo x28 before = 
print/x $x28

# Execute adr
stepi
echo x28 after adr = 
print/x $x28

# Execute ldr x29
stepi
echo x29 after ldr = 
print/x $x29

# Execute ldr x30
stepi  
echo x30 after ldr = 
print/x $x30

# Execute sub x29, x29, x30
stepi
echo x29 after sub = 
print/x $x29

# Execute sub x28, x28, x29
stepi
echo x28 after final sub = 
print/x $x28
echo This should be 0x40080000\n

# Execute mov x26, x28
stepi
echo x26 after mov = 
print/x $x26

# Check page table base calculation
stepi
echo x26 after sub (page table base) = 
print/x $x26

continue