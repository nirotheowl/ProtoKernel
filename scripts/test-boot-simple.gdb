# Simple boot test

# Break at _start
break *0x40080000
continue

# Step through initial setup
echo \n=== Initial registers ===\n
info registers x0 x1 x2 x3 x19 x20 x21 x22

# Step to el1_entry
break *0x40080068
continue

echo \n=== At el1_entry ===\n
# Step through the initial instructions
stepi 10

# Now at the adr instruction
echo \n=== About to execute adr x28, _start ===\n
echo PC = 
print/x $pc
x/1i $pc

# Execute the adr
stepi
echo x28 after adr = 
print/x $x28

# Execute the first ldr
echo \n=== About to execute ldr x29, =_start ===\n
x/1i $pc
stepi
echo x29 after ldr = 
print/x $x29

# Check memory at the literal pool location
echo \n=== Checking literal pool ===\n
echo Memory at 0x40080230: 
x/2gx 0x40080230

# Continue execution
continue