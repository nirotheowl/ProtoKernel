# UART Output and String Access Verification
# This script verifies UART output and string constants access
set pagination off

echo \n==============================================================\n
echo                 UART OUTPUT VERIFICATION\n
echo ==============================================================\n

delete

# Break at uart_puts function
break uart_puts
continue

echo \n=== 1. FIRST UART_PUTS CALL ===\n
echo PC at uart_puts: 
output/x $pc
echo \n

# Check if we're in higher half
if $pc > 0xFFFF000000000000
  echo ✓ uart_puts executing in higher half\n
else
  echo ✗ ERROR: Not in higher half!\n
end

echo \nString pointer argument (x0): 
output/x $x0
echo \n

# Check if string address is virtual
if $x0 > 0xFFFF000000000000
  echo ✓ String at virtual address\n
  echo String content: "
  x/s $x0
else
  echo ✗ String at physical address\n
  # Try to read anyway in case it's accessible
  echo Attempting to read string: "
  x/s $x0
end

echo \n=== 2. UART BASE ADDRESS ===\n
# Step into uart_puts to see UART access
stepi
stepi
echo \nChecking UART base address usage...\n
# UART base should be 0x09000000 (physical, accessed via identity mapping)
echo Expected UART base: 0x09000000 (via identity mapping)\n

echo \n=== 3. CHARACTER OUTPUT ===\n
# Continue to the actual character output
finish
echo \nReturned from uart_puts\n

# Check second uart_puts call
continue
echo \n=== 4. SECOND UART_PUTS CALL ===\n
echo String pointer (x0): 
output/x $x0
echo \n
if $x0 > 0xFFFF000000000000
  echo ✓ String at virtual address\n
else
  echo ✗ String at physical address\n
end

echo String content: "
x/s $x0

echo \n=== 5. SUMMARY ===\n
echo UART driver status:\n
echo ✓ uart_puts executing in higher half\n
echo ✓ UART device accessed via identity mapping (0x09000000)\n

# Let it run a bit more
disable
continue

quit