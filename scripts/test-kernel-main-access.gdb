# Test what happens when kernel_main tries to access virtual addresses

set pagination off

echo \n========================================\n
echo === KERNEL_MAIN VIRTUAL ACCESS TEST ===\n
echo ========================================\n

break *0x40080068
continue
delete breakpoints

echo \n=== 1. FAST FORWARD TO KERNEL_MAIN ===\n
break *0x40080210
continue

echo At branch to kernel_main:\n
echo PC: 
output/x $pc
echo \n
x/1i $pc

echo \n=== 2. STEP INTO KERNEL_MAIN ===\n
stepi
echo Now in kernel_main at PC: 
output/x $pc
echo \n

echo First few instructions of kernel_main:\n
x/10i $pc

echo \n=== 3. WATCH KERNEL_MAIN EXECUTION ===\n
echo Stepping through kernel_main to see where it faults...\n

set $inst_count = 0
while $inst_count < 20
  echo \nInstruction 
  output $inst_count
  echo : PC=
  output/x $pc
  echo \n
  x/1i $pc
  
  # Check if we're about to access a high virtual address
  set $next_inst = *(unsigned int *)$pc
  
  # Step and see what happens
  stepi
  
  # Check if PC jumped to exception vector
  if $pc == 0x200 || $pc == 0x280 || $pc == 0x400 || $pc == 0x480
    echo \n!!! EXCEPTION OCCURRED !!!\n
    echo Exception vector at PC: 
    output/x $pc
    echo \n
    break
  end
  
  set $inst_count = $inst_count + 1
end

echo \n=== 4. FINAL STATE ===\n
echo Final PC: 
output/x $pc
echo \n
echo Register state:\n
info registers x0 x1 x2 x3 sp pc

echo \n=== 5. MEMORY MAPPING STATUS ===\n
echo Testing if mappings are still active:\n
echo Identity mapping (0x40080000): 
x/1xw 0x40080000
echo Higher-half mapping (0xFFFF000040080000): 
x/1xw 0xFFFF000040080000

quit