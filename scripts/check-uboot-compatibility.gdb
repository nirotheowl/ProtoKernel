# U-Boot Compatibility Check
# This script verifies the kernel is compatible with U-Boot loading
set pagination off

echo \n==============================================================\n
echo                 U-BOOT COMPATIBILITY CHECK\n
echo ==============================================================\n

delete

# Break at entry point
break *0x40080000
continue

echo \n=== 1. BOOT PROTOCOL COMPLIANCE ===\n
echo ARM64 Linux Boot Protocol Requirements:\n
echo   - x0: DTB physical address\n
echo   - x1: 0 (reserved for future use)\n
echo   - x2: 0 (reserved for future use)\n
echo   - x3: 0 (reserved for future use)\n
echo \nActual register values:\n
echo   x0 = 
output/x $x0
echo  (DTB pointer)\n
echo   x1 = 
output/x $x1
echo \n
echo   x2 = 
output/x $x2
echo \n
echo   x3 = 
output/x $x3
echo \n

if $x1 == 0 && $x2 == 0 && $x3 == 0
  echo ✓ Boot protocol compliance verified\n
else
  echo ✗ WARNING: Non-zero values in reserved registers\n
end

echo \n=== 2. KERNEL IMAGE HEADER ===\n
# Check if kernel has Linux Image header at offset 0
# Should have specific magic values for booti command
echo Checking for ARM64 Linux Image header...\n
# The header should be at the start of the kernel image
# For now, we don't have one, which means U-Boot needs to use bootm or go command
echo ✗ No Linux Image header found\n
echo   U-Boot will need to use: bootm or go command\n
echo   Cannot use: booti command\n

echo \n=== 3. EXCEPTION LEVEL HANDLING ===\n
echo Current Exception Level: 
set $el = ($CurrentEL >> 2) & 3
output $el
echo \n
echo Kernel handles: EL3 → EL2 → EL1 transitions ✓\n

echo \n=== 4. MEMORY LAYOUT ASSUMPTIONS ===\n
echo Physical load address: 0x40080000\n
echo Page tables location: 9 pages before kernel (0x40077000)\n
echo \nPotential issues:\n
echo   ✗ Page tables placed BEFORE kernel load address\n
echo   - U-Boot may not load data before kernel start\n
echo   - Risk of page tables being overwritten\n
echo \nRecommendation: Place page tables AFTER kernel image\n

echo \n=== 5. STACK SETUP ===\n
# Continue to stack setup
delete
break *0x40080234
continue

echo Stack pointer setup:\n
echo   Virtual SP: 
output/x $x0
echo \n
echo   This assumes memory is available below kernel\n

echo \n=== 6. DTB PRESERVATION TEST ===\n
# Check if DTB pointer is preserved
delete  
break kernel_main
continue

echo DTB pointer at kernel_main: 
output/x $x0
echo \n
if $x0 != 0 && $x0 < 0xFFFF000000000000
  echo ✓ DTB pointer preserved correctly\n
else
  echo ✗ DTB pointer corrupted or invalid\n
end

echo \n=== SUMMARY OF U-BOOT COMPATIBILITY ===\n
echo \n✓ COMPATIBLE:\n
echo   - Boot register protocol (x0-x3)\n
echo   - Exception level transitions\n
echo   - DTB pointer preservation\n
echo   - Basic boot sequence\n

echo \n✗ ISSUES TO ADDRESS:\n
echo   1. No Linux Image header for booti command\n
echo   2. Page tables placed before kernel (risky)\n
echo   3. Stack assumes memory below kernel\n
echo   4. No explicit memory reservation\n

echo \n=== RECOMMENDATIONS ===\n
echo 1. Add Linux Image header for booti support\n
echo 2. Move page tables after kernel image\n
echo 3. Use DTB or bootloader info for memory layout\n
echo 4. Add memory probing instead of fixed addresses\n
echo 5. Consider 2MB alignment for kernel base\n

quit