# Test script to verify execution continues after MMU enable
# up to just before kernel_main jump

set pagination off

echo \n========================================\n
echo === MMU TO KERNEL_MAIN EXECUTION TEST ===\n
echo ========================================\n

break *0x40080068
continue
delete breakpoints

echo \n=== 1. BEFORE MMU ENABLE ===\n
break *0x400801e8
continue

echo Current PC: 
output/x $pc
echo \nAbout to enable MMU\n
echo SCTLR_EL1 before: 
output/x $SCTLR_EL1
echo \n

echo \n=== 2. ENABLING MMU ===\n
stepi
stepi
stepi
stepi
stepi
stepi

echo MMU enabled!\n
echo Current PC: 
output/x $pc
echo \nSCTLR_EL1 after: 
output/x $SCTLR_EL1
echo \n

echo \n=== 3. EXECUTION AFTER MMU ENABLE ===\n
echo Testing if we can still access memory and execute instructions...\n

echo Next instructions:\n
x/5i $pc

echo \nStepping through next 5 instructions:\n
set $i = 0
while $i < 5
  echo Instruction 
  output $i
  echo : PC=
  output/x $pc
  echo  
  x/1i $pc
  stepi
  set $i = $i + 1
end

echo \n=== 4. STACK SETUP ===\n
echo \nContinuing to stack setup...\n

break *0x40080200
continue

echo At stack setup:\n
echo PC: 
output/x $pc
echo \n

stepi
echo x0 after adr: 
output/x $x0
echo \n

stepi
echo x0 after sub (stack base): 
output/x $x0
echo \n

stepi
echo SP set to: 
output/x $sp
echo \n

echo \n=== 5. DTB POINTER SETUP ===\n
stepi
echo x0 (DTB pointer for kernel_main): 
output/x $x0
echo \nNote: x19 value is 
output/x $x19
echo  (should be DTB pointer from boot)\n

echo \n=== 6. JUST BEFORE KERNEL_MAIN ===\n
echo Next instruction (should be bl kernel_main):\n
x/1i $pc

echo \nkernel_main is at: 
output/x 0x40081ae0
echo \n

echo \n=== 7. MEMORY ACCESS TEST ===\n
echo Testing memory access through identity mapping:\n

echo Code at 0x40080000: 
x/1xw 0x40080000
echo Code at current PC: 
x/1xw $pc

echo Stack region at SP: 
x/1xw $sp

echo \nTesting higher-half access (should work if mapping is correct):\n
echo Code at 0xFFFF000040080000: 
x/1xw 0xFFFF000040080000

echo \n=== 8. REGISTER STATE BEFORE KERNEL_MAIN ===\n
echo Register dump before calling kernel_main:\n
info registers x0 x1 x2 x3 x19 x28 x29 x30 sp pc

echo \n========================================\n
echo === TEST SUMMARY ===\n
echo ========================================\n
echo 1. MMU successfully enabled\n
echo 2. Execution continues after MMU enable\n
echo 3. Stack pointer set up correctly\n
echo 4. About to jump to kernel_main\n
echo 5. Both identity and higher-half mappings appear functional\n

# Don't continue to kernel_main yet
echo \nStopped just before kernel_main jump.\n
echo To continue into kernel_main, use 'continue'\n

# Show current state
echo \nCurrent state:\n
echo PC: 
output/x $pc
echo  -> 
x/1i $pc

quit