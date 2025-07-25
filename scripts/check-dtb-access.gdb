# DTB Pointer Verification
# This script verifies the DTB pointer is preserved and accessible
set pagination off

echo \n==============================================================\n
echo                 DTB POINTER VERIFICATION\n
echo ==============================================================\n

delete

# Break at start to check initial DTB
break *0x40080000
continue

echo \n=== 1. INITIAL DTB POINTER ===\n
echo At _start:\n
echo   x0 (DTB from bootloader): 
output/x $x0
echo \n

# Step a few instructions to see DTB being saved
stepi
stepi
stepi
stepi
stepi

echo \nAfter saving DTB:\n
echo   x27 (saved DTB): 
output/x $x27
echo \n

# Continue to kernel_main
delete
break kernel_main
continue

echo \n=== 2. DTB AT KERNEL_MAIN ===\n
echo At kernel_main entry:\n
echo   x0 (DTB argument): 
output/x $x0
echo \n

if $x0 == $x27
  echo ✓ DTB pointer correctly preserved\n
else
  echo ✗ DTB pointer changed!\n
  echo   Expected: 
  output/x $x27
  echo \n
  echo   Got: 
  output/x $x0
  echo \n
end

echo \n=== 3. DTB ACCESS TEST ===\n
echo DTB is at physical address: 
output/x $x0
echo \n

# DTB should be accessible via identity mapping
echo Attempting to read DTB header (should be 0xd00dfeed):\n
set $dtb_magic = *(unsigned int *)$x0
echo   Magic number: 
output/x $dtb_magic
echo \n

if $dtb_magic == 0xedfe0dd0
  echo ✓ DTB header valid and accessible (big-endian format)\n
  
  # Read more DTB fields (they're all big-endian)
  set $dtb_totalsize = *(unsigned int *)($x0 + 4)
  echo   Total size (big-endian): 
  output/x $dtb_totalsize
  echo \n
  
  set $dtb_struct_offset = *(unsigned int *)($x0 + 8)
  echo   Structure offset (big-endian): 
  output/x $dtb_struct_offset
  echo \n
else
  echo ✗ Invalid DTB magic number!\n
  echo   Expected: 0xedfe0dd0 (big-endian) or 0xd00dfeed (little-endian)\n
  echo   Got: 
  output/x $dtb_magic
  echo \n
end

echo \n=== 4. SUMMARY ===\n
echo DTB handling:\n
if $x0 == $x27 && $dtb_magic == 0xedfe0dd0
  echo ✓ DTB pointer preserved through boot\n
  echo ✓ DTB accessible via identity mapping\n
  echo ✓ DTB header valid\n
  echo \nThe kernel can successfully access the Device Tree\n
else
  echo ✗ DTB handling has issues\n
end

quit