# Identity Mapping Verification Script
# Use: ./scripts/debug-qemu.sh scripts/verify-identity-map.gdb
#
# This script verifies that the identity mapping (VA = PA) is correctly
# set up before attempting to enable the MMU.

delete

# Helper function to walk page tables and verify mapping
define check_mapping
  set $va = $arg0
  set $ttbr = $arg1
  
  # Calculate indices
  set $l0_idx = ($va >> 39) & 0x1FF
  set $l1_idx = ($va >> 30) & 0x1FF
  set $l2_idx = ($va >> 21) & 0x1FF
  
  # Walk L0 -> L1 -> L2
  set $l0_entry = *(unsigned long *)($ttbr + $l0_idx * 8)
  set $l1_base = $l0_entry & 0xFFFFFFFFF000
  set $l1_entry = *(unsigned long *)($l1_base + $l1_idx * 8)
  set $l2_base = $l1_entry & 0xFFFFFFFFF000
  set $l2_entry = *(unsigned long *)($l2_base + $l2_idx * 8)
  
  # Extract PA from L2 block entry
  set $pa = $l2_entry & 0xFFFFFFE00000
  set $expected_pa = $va & 0xFFFFFFE00000
  
  printf "VA 0x%08lx -> PA 0x%08lx ", $va, $pa
  if $pa == $expected_pa
    printf "[OK]\n"
  else
    printf "[FAIL - expected 0x%08lx]\n", $expected_pa
  end
end

# Break just before MMU enable
break *0x40080148
continue

echo \n=== Verifying Identity Mapping ===\n
printf "Page tables at: 0x%016lx\n", $x23

# Check critical mappings
echo \nChecking kernel region mappings:\n
check_mapping 0x40080000 $x23
check_mapping 0x40081000 $x23
check_mapping 0x40077000 $x23

echo \nChecking other regions:\n
check_mapping 0x00000000 $x23
check_mapping 0x12345000 $x23
check_mapping 0x40000000 $x23

# Continue to MMU enable
echo \n=== Enabling MMU ===\n
break *0x40080158
continue

echo MMU enabled successfully!\n
printf "Continuing execution at PC = 0x%016lx\n", $pc

# Let it run and see if it reaches kernel_main
break *0x40081a40
continue

if $pc == 0x40081a40
  echo \n=== SUCCESS: Reached kernel_main ===\n
  info registers pc sp x0
else
  echo \n=== Failed to reach kernel_main ===\n
  printf "Stopped at PC = 0x%016lx\n", $pc
end