# Debug script to find where kernel hangs after MMU enable
# Use: ./scripts/debug-qemu.sh scripts/debug-mmu-hang2.gdb

delete

# The actual physical address where MMU is enabled
break *0x40080184
commands
  echo === About to enable MMU ===\n
  printf "PC: 0x%lx\n", $pc
  printf "SCTLR_EL1 value: 0x%lx\n", $x0
  info registers x23
end

continue

# Now step through the next instructions
echo \n=== MMU Enable Instruction ===\n
x/i $pc
stepi

echo \n=== After MMU Enable ===\n
printf "PC: 0x%lx\n", $pc
x/i $pc

# Continue stepping to see where it goes
set $steps = 0
while $steps < 10
  stepi
  printf "Step %d - PC: 0x%lx - ", $steps, $pc
  x/i $pc
  set $steps = $steps + 1
end

# Check current state
echo \n=== Current State ===\n
info registers pc sp x0 x19

# See if we can access the stack
echo \n=== Checking Stack Access ===\n
x/4xg $sp

quit