# Debug page table setup

# Set breakpoint at the physical address calculation
break *0x40080090
continue

echo \n=== Physical Address Calculation ===\n
echo After adr x28, _start: 
stepi
print/x $x28

echo After ldr x29, =_start: 
stepi
print/x $x29

echo After ldr x30, =KERNEL_VIRT_BASE: 
stepi
print/x $x30

echo After sub x29, x29, x30: 
stepi
print/x $x29

echo After sub x28, x28, x29: 
stepi
print/x $x28

# Continue to page table allocation
echo \n=== Page Table Allocation ===\n
break *0x400800a4
continue

echo x26 (page table base): 
print/x $x26

# Check page table setup
break *0x400800dc
continue

echo \nPage table pointers:\n
echo x23 (TTBR0 L0): 
print/x $x23
echo x22 (TTBR0 L1): 
print/x $x22
echo x21 (TTBR0 L2): 
print/x $x21
echo x20 (TTBR0 L2 second): 
print/x $x20
echo x25 (next free): 
print/x $x25

# Continue to TTBR1 setup
break *0x40080148
continue

echo \n=== TTBR1 Page Tables ===\n
echo x19 (TTBR1 L0): 
print/x $x19
echo x18 (TTBR1 L1): 
print/x $x18
echo x17 (TTBR1 L2): 
print/x $x17

# Check L0 entry calculation
break *0x40080164
continue

echo \nAfter str x0, [x19, x1, lsl #3]:\n
echo x0 (entry value): 
print/x $x0
echo x1 (index): 
print/x $x1
echo Entry at L0[0x1FE]: 
x/1gx $x19+(0x1FE*8)

# Continue and see final state
continue