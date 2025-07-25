# Comprehensive Page Table Integrity and Correctness Check
# This script thoroughly validates both TTBR0 and TTBR1 page tables
set pagination off

echo \n==============================================================\n
echo                    PAGE TABLE INTEGRITY CHECK\n
echo ==============================================================\n

delete

# Break after page tables are set up but before MMU enable
break *0x400801b0
continue

echo \n=== 1. SYSTEM CONFIGURATION ===\n
echo Physical load address: 0x40080000\n
echo Virtual base address: 0xFFFF000040080000\n
echo Page size: 4KB\n
echo VA size: 48-bit (T0SZ=T1SZ=16)\n

# Get page table bases - they're now after kernel
# Based on actual calculation: kernel start + 92KB
set $ttbr0_l0 = 0x40097000
set $ttbr1_l0 = 0x4009b000

echo \nPage table locations:\n
echo   TTBR0 L0: 
output/x $ttbr0_l0
echo \n
echo   TTBR1 L0: 
output/x $ttbr1_l0
echo \n

echo \n=== 2. TTBR0 IDENTITY MAPPING VERIFICATION ===\n
echo Purpose: Maps PA=VA for bootstrap and device access\n
echo Coverage: First 1GB of physical memory\n

# Check L0[0]
set $entry_addr = $ttbr0_l0
set $entry = *(unsigned long *)$entry_addr
echo \nL0[0] @ 
output/x $entry_addr
echo :\n  Raw value: 
output/x $entry
echo \n
if ($entry & 3) != 3
  echo   ERROR: Not a valid table descriptor!\n
else
  echo   Valid table descriptor\n
  set $l1_base = $entry & 0xFFFFFFFFF000
  echo   Points to L1 @ 
  output/x $l1_base
  echo \n
  
  # Check L1[0] - covers 0-512MB
  set $entry_addr = $l1_base
  set $entry = *(unsigned long *)$entry_addr
  echo \nL1[0] @ 
  output/x $entry_addr
  echo  (0-512MB):\n  Raw value: 
  output/x $entry
  echo \n
  if ($entry & 3) != 3
    echo   ERROR: Not a valid table descriptor!\n
  else
    echo   Valid table descriptor\n
    set $l2_base = $entry & 0xFFFFFFFFF000
    echo   Points to L2 @ 
    output/x $l2_base
    echo \n
  end
  
  # Check L1[1] - covers 512MB-1GB (where kernel is)
  set $entry_addr = $l1_base + 8
  set $entry = *(unsigned long *)$entry_addr
  echo \nL1[1] @ 
  output/x $entry_addr
  echo  (512MB-1GB):\n  Raw value: 
  output/x $entry
  echo \n
  if ($entry & 3) != 3
    echo   ERROR: Not a valid table descriptor!\n
  else
    echo   Valid table descriptor\n
    set $l2_base = $entry & 0xFFFFFFFFF000
    echo   Points to L2 @ 
    output/x $l2_base
    echo \n
    
    # Check specific L2 entry for kernel location
    # Kernel at 0x40080000, which is entry 0 in second L2 table
    set $entry_addr = $l2_base
    set $entry = *(unsigned long *)$entry_addr
    echo \n  L2[0] @ 
    output/x $entry_addr
    echo  (maps PA 0x40000000):\n    Raw value: 
    output/x $entry
    echo \n
    if ($entry & 3) == 1
      echo     Valid 2MB block\n
      set $pa = $entry & 0xFFFFFFE00000
      echo     Physical address: 
      output/x $pa
      echo \n
      # Decode attributes
      set $attrs = $entry & 0xFFF
      echo     Attributes: 
      output/x $attrs
      echo  (AF=
      output (($attrs >> 10) & 1)
      echo , SH=
      output (($attrs >> 8) & 3)
      echo , AP=
      output (($attrs >> 6) & 3)
      echo , MAIR=
      output (($attrs >> 2) & 7)
      echo )\n
    else
      echo     ERROR: Not a valid block descriptor!\n
    end
  end
end

echo \n=== 3. TTBR1 HIGHER-HALF MAPPING VERIFICATION ===\n
echo Purpose: Maps kernel to 0xFFFF000040080000\n
echo Target VA: 0xFFFF000040080000\n
echo Expected PA: 0x40000000 (2MB block containing 0x40080000)\n

# Calculate indices for VA 0xFFFF000040080000
echo \nAddress translation (48-bit VA):\n
echo   VA = 0xFFFF000040080000\n
echo   L0 index = (VA >> 39) & 0x1FF = 0x000\n
echo   L1 index = (VA >> 30) & 0x1FF = 0x001\n
echo   L2 index = (VA >> 21) & 0x1FF = 0x000\n

# Check L0[0] - MUST be at index 0, not 0x1FE!
set $entry_addr = $ttbr1_l0
set $entry = *(unsigned long *)$entry_addr
echo \nL0[0] @ 
output/x $entry_addr
echo :\n  Raw value: 
output/x $entry
echo \n
if ($entry & 3) != 3
  echo   ERROR: Not a valid table descriptor!\n
  echo   This is the critical L0 index issue - entry must be at L0[0], not L0[0x1FE]\n
  # Check if mistakenly at 0x1FE
  set $wrong_addr = $ttbr1_l0 + (0x1FE * 8)
  set $wrong_entry = *(unsigned long *)$wrong_addr
  echo \n  Checking L0[0x1FE] @ 
  output/x $wrong_addr
  echo :\n    Raw value: 
  output/x $wrong_entry
  echo \n
  if ($wrong_entry & 3) == 3
    echo     WARNING: Valid entry found at wrong index 0x1FE!\n
  end
else
  echo   Valid table descriptor\n
  set $l1_base = $entry & 0xFFFFFFFFF000
  echo   Points to L1 @ 
  output/x $l1_base
  echo \n
  
  # Check L1[1]
  set $entry_addr = $l1_base + (1 * 8)
  set $entry = *(unsigned long *)$entry_addr
  echo \nL1[1] @ 
  output/x $entry_addr
  echo :\n  Raw value: 
  output/x $entry
  echo \n
  if ($entry & 3) != 3
    echo   ERROR: Not a valid table descriptor!\n
  else
    echo   Valid table descriptor\n
    set $l2_base = $entry & 0xFFFFFFFFF000
    echo   Points to L2 @ 
    output/x $l2_base
    echo \n
    
    # Check L2[0]
    set $entry_addr = $l2_base
    set $entry = *(unsigned long *)$entry_addr
    echo \n  L2[0] @ 
    output/x $entry_addr
    echo :\n    Raw value: 
    output/x $entry
    echo \n
    if ($entry & 3) == 1
      echo     Valid 2MB block\n
      set $pa = $entry & 0xFFFFFFE00000
      echo     Physical address: 
      output/x $pa
      echo \n
      
      # Decode attributes
      set $attrs = $entry & 0xFFF
      set $uxn = ($entry >> 54) & 1
      set $pxn = ($entry >> 53) & 1
      echo     Attributes: 
      output/x $attrs
      echo \n
      echo     PXN=
      output $pxn
      if $pxn == 1
        echo  (ERROR: Privileged execution disabled!)\n
      else
        echo  (OK: Privileged execution allowed)\n
      end
      echo     UXN=
      output $uxn
      echo  (User execution 
      if $uxn == 1
        echo disabled)\n
      else
        echo allowed)\n
      end
    else
      echo     ERROR: Not a valid block descriptor!\n
    end
  end
end

echo \n=== 4. SUMMARY ===\n
set $ttbr0_ok = 1
set $ttbr1_ok = 1

# Quick validity checks
set $entry = *(unsigned long *)$ttbr0_l0
if ($entry & 3) != 3
  set $ttbr0_ok = 0
end

set $entry = *(unsigned long *)$ttbr1_l0
if ($entry & 3) != 3
  set $ttbr1_ok = 0
end

if $ttbr0_ok == 1
  echo TTBR0 (Identity): VALID ✓\n
else
  echo TTBR0 (Identity): INVALID ✗\n
end

if $ttbr1_ok == 1
  echo TTBR1 (Higher-half): VALID ✓\n
else
  echo TTBR1 (Higher-half): INVALID ✗\n
  echo   Check that L0 entry is at index 0, not 0x1FE!\n
end

echo \nKey points:\n
echo - Identity mapping covers first 1GB for bootstrap\n
echo - Higher-half maps kernel from 0xFFFF000040080000\n
echo - Both use 2MB blocks for efficiency\n
echo - PXN must be 0 for code execution\n

quit