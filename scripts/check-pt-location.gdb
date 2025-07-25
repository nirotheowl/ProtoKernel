# Page Table Location Verification
# This script verifies page tables are placed after kernel
set pagination off

echo \n==============================================================\n
echo                 PAGE TABLE LOCATION VERIFICATION\n
echo ==============================================================\n

delete

# Break after page table address calculation
break *0x40080108
continue

echo \n=== 1. KERNEL MEMORY LAYOUT ===\n
echo Kernel load address: 0x40080000\n
echo Kernel size estimate: ~40KB (0xA000)\n
echo Expected kernel end: ~0x4008A000\n

echo \n=== 2. PAGE TABLE ADDRESS CALCULATION ===\n
echo After address calculation:\n
echo   x26 (page table base): 
output/x $x26
echo \n

# Calculate offset from kernel start
set $offset = $x26 - 0x40080000
echo   Offset from kernel start: 
output/x $offset
echo  (
output $offset / 1024
echo KB)\n

if $x26 > 0x40080000
  echo ✓ Page tables placed AFTER kernel start\n
  if $x26 > 0x4008A000
    echo ✓ Page tables appear to be after kernel end\n
  else
    echo ⚠️  Page tables might overlap with kernel\n
  end
else
  echo ✗ ERROR: Page tables placed BEFORE kernel!\n
end

# Continue to where page tables are set up
delete
break *0x400801b0
continue

echo \n=== 3. PAGE TABLE POINTERS ===\n
echo TTBR0 tables:\n
echo   L0: 
output/x $x23
echo \n
echo   L1: 
output/x $x22
echo \n
echo   L2 (first): 
output/x $x21
echo \n
echo   L2 (second): 
output/x $x20
echo \n

echo \nTTBR1 tables:\n
echo   L0: 
output/x $x19
echo \n
echo   L1: 
output/x $x18
echo \n
echo   L2: 
output/x $x17
echo \n

# Calculate total page table span
set $pt_start = $x23
set $pt_end = $x17 + 0x1000
set $pt_size = $pt_end - $pt_start
echo \nPage table memory range:\n
echo   Start: 
output/x $pt_start
echo \n
echo   End: 
output/x $pt_end
echo \n
echo   Total size: 
output/x $pt_size
echo  (
output $pt_size / 1024
echo KB)\n

echo \n=== 4. MEMORY SAFETY CHECK ===\n
# Check if page tables are in safe memory
if $pt_start >= 0x40080000 && $pt_start < 0x48000000
  echo ✓ Page tables in RAM region\n
  if $pt_end < 0x48000000
    echo ✓ Page tables don't overlap with DTB (at 0x48000000)\n
  else
    echo ✗ ERROR: Page tables overlap with DTB!\n
  end
else
  echo ✗ ERROR: Page tables in invalid memory region!\n
end

echo \n=== 5. U-BOOT COMPATIBILITY ===\n
if $pt_start > 0x40080000
  echo ✓ U-Boot compatible: Page tables after kernel load address\n
  echo   U-Boot can load kernel at 0x40080000 safely\n
else
  echo ✗ Not U-Boot compatible: Page tables before kernel\n
end

quit