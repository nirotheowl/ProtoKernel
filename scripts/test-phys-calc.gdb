# Test physical address calculation

# Break at el1_entry
break *0x40080068
continue

# Break at the end of physical address calculation
break *0x400800a4
continue

echo \n=== After Physical Address Calculation ===\n
echo x28 (calculated phys base) = 
print/x $x28
echo x26 (x28 copied) = 
print/x $x26

# Continue to see page table allocation
break *0x400800b8
continue

echo \n=== After Page Table Base Calculation ===\n
echo x26 (page table base) = 
print/x $x26
echo x25 (current free page) = 
print/x $x25

# Continue to TTBR1 allocation
break *0x40080148
continue

echo \n=== TTBR1 Allocation ===\n
echo x25 (current free before TTBR1) = 
print/x $x25
echo x19 (TTBR1 L0) = 
print/x $x19

# Let it run further
continue