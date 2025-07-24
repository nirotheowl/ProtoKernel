# Debug script to trace execution in kernel_main
# Use: ./scripts/debug-qemu.sh scripts/debug-kernel-main.gdb

delete

# Break at the bl to kernel_main
break *0x4008019c
continue

echo === About to call kernel_main ===\n
info registers x0 x19 pc sp

# Step into kernel_main
stepi

echo \n=== Entered kernel_main ===\n
printf "PC: 0x%lx\n", $pc

# Step through first few instructions of kernel_main
set $count = 0
while $count < 20
  printf "\nStep %d - PC: 0x%lx\n", $count, $pc
  x/i $pc
  
  # Print relevant registers
  info registers x0 x1 x29 x30 sp
  
  # If it's a memory access instruction, show what it's accessing
  set $instr = *(unsigned int *)$pc
  
  # Check for load/store instructions (bits 27-28 = 01 for loads/stores)
  if (($instr >> 27) & 0x3) == 1
    echo Memory access instruction detected\n
  end
  
  stepi
  set $count = $count + 1
  
  # Break if we hit an exception
  if $pc < 0x40000000 || $pc > 0x50000000
    echo !!! PC jumped to unexpected location !!!\n
    break
  end
end

echo \n=== Final state ===\n
info registers pc sp x0 x19

quit