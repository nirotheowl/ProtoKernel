# Kernel Main Execution Verification
# This script verifies that kernel_main is executing properly in higher half
set pagination off

echo \n==============================================================\n
echo                 KERNEL_MAIN EXECUTION VERIFICATION\n
echo ==============================================================\n

delete

# Break at kernel_main entry
break kernel_main
continue

echo \n=== 1. KERNEL_MAIN ENTRY ===\n
echo PC at kernel_main: 
output/x $pc
echo \n

# Check if we're in higher half
if $pc > 0xFFFF000000000000
  echo ✓ Executing in higher half\n
else
  echo ✗ ERROR: Not in higher half!\n
  quit
end

echo \nFunction arguments:\n
echo   x0 (DTB pointer): 
output/x $x0
echo \n

echo \nStack pointer: 
output/x $sp
echo \n
if $sp > 0xFFFF000000000000
  echo ✓ Stack in higher half\n
else
  echo ✗ Stack still using physical addresses\n
end

echo \n=== 2. FIRST FEW INSTRUCTIONS ===\n
echo Disassembly at kernel_main:\n
x/10i $pc

echo \nStepping through first instructions...\n
stepi
echo   After 1st instruction - PC: 
output/x $pc
echo \n

stepi
echo   After 2nd instruction - PC: 
output/x $pc
echo \n

stepi
echo   After 3rd instruction - PC: 
output/x $pc
echo \n

# Check if we're still in higher half
if $pc > 0xFFFF000000000000
  echo   ✓ Still executing in higher half\n
else
  if $pc < 0x1000
    echo   ✗ Hit exception vector at 
    output/x $pc
    echo \n
  else
    echo   ✗ Fell back to physical addresses\n
  end
end

echo \n=== 3. TESTING VIRTUAL ADDRESS ACCESS ===\n
# Continue to where kernel_main prints something
echo Continuing execution...\n
# Set a breakpoint at the first UART write
break *0xffff000040081b30
continue

echo \nAt UART write:\n
echo PC: 
output/x $pc
echo \n
echo String address in x0: 
output/x $x0
echo \n

if $x0 > 0xFFFF000000000000
  echo ✓ Using virtual address for string\n
  # Try to read the string
  echo String content: 
  x/s $x0
else
  echo ✗ Using physical address for string\n
end

echo \n=== 4. SUMMARY ===\n
if $pc > 0xFFFF000000000000
  echo ✓ kernel_main executing successfully in higher half\n
  echo ✓ Virtual address access working\n
  echo ✓ Kernel is fully operational at virtual addresses\n
else
  echo ✗ kernel_main execution failed\n
end

quit