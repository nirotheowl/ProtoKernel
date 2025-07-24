# Debug script to find where kernel hangs after MMU enable
# Use: ./scripts/debug-qemu.sh scripts/debug-mmu-hang.gdb

delete

# Set up display format
set print pretty on

# Calculate the load address offset
set $load_offset = 0x40080000 - 0x40200000

# Break at MMU enable instruction
break *($load_offset + 0x168)
commands
  echo === About to enable MMU ===\n
  printf "PC: 0x%lx\n", $pc
  printf "SCTLR_EL1 before: 0x%lx\n", $x0
  stepi
  echo === MMU Enabled ===\n
  printf "PC after MMU enable: 0x%lx\n", $pc
end

# Continue to MMU enable
continue

# Step through the next few instructions to see where it hangs
echo \n=== Stepping through post-MMU code ===\n
set $count = 0
while $count < 20
  printf "Step %d - PC: 0x%lx  ", $count, $pc
  x/i $pc
  stepi
  set $count = $count + 1
end

# Check if we made it to kernel_main
printf "\n=== Current state ===\n"
printf "PC: 0x%lx\n", $pc
printf "SP: 0x%lx\n", $sp
printf "X0: 0x%lx\n", $x0
printf "X19 (DTB): 0x%lx\n", $x19

# Try to see what instruction we're stuck on
echo \n=== Current and next instructions ===\n
x/5i $pc