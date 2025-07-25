# Higher-Half Jump Verification
# This script verifies the jump to higher half virtual addresses
set pagination off

echo \n==============================================================\n
echo                 HIGHER-HALF JUMP VERIFICATION\n
echo ==============================================================\n

delete

# Break at the jump sequence (right before br x1)
break *0x40080228
continue

echo \n=== 1. PRE-JUMP STATE ===\n
echo Current PC (physical): 
output/x $pc
echo \n

echo \n=== 2. JUMP TARGET ===\n
# The jump instruction is at 0x40080228: br x1
# Check what's in x1 (should be virtual address)
echo Jump target in x1: 
output/x $x1
echo \n

# Verify it's a higher-half address
if $x1 > 0xFFFF000000000000
  echo ✓ Target is in higher half\n
else
  echo ✗ ERROR: Target is not in higher half!\n
end

echo \n=== 3. EXECUTING THE JUMP ===\n
echo About to execute: br x1\n

# Step the branch instruction
stepi

echo \nAfter jump:\n
echo New PC: 
output/x $pc
echo \n

# Check if we made it to higher half
if $pc > 0xFFFF000000000000
  echo ✓ SUCCESS: Now executing in higher half!\n
  
  echo \n=== 4. TESTING EXECUTION ===\n
  echo Stepping through first few instructions...\n
  
  # Try to execute a few instructions
  stepi
  echo   PC after 1st instruction: 
  output/x $pc
  echo \n
  
  if $pc < 0x1000
    echo ✗ FAILED: Hit exception vector!\n
  else
    if $pc > 0xFFFF000000000000
      echo   ✓ Still in higher half\n
      
      stepi
      echo   PC after 2nd instruction: 
      output/x $pc
      echo \n
      
      if $pc > 0xFFFF000000000000
        echo   ✓ Still in higher half\n
        
        stepi
        echo   PC after 3rd instruction: 
        output/x $pc
        echo \n
        
        if $pc > 0xFFFF000000000000
          echo   ✓ Still in higher half\n
          echo \n✓ Higher-half execution verified!\n
        else
          echo   ✗ Fell back to physical addresses\n
        end
      else
        echo   ✗ Fell back to physical addresses\n
      end
    else
      echo   ✗ Not in higher half anymore\n
    end
  end
else
  echo ✗ FAILED: Jump did not reach higher half\n
  if $pc < 0x1000
    echo   Hit exception vector at 
    output/x $pc
    echo \n
  end
end

echo \n=== 5. SUMMARY ===\n
if $pc > 0xFFFF000000000000
  echo ✓ Kernel successfully jumped to higher half\n
  echo ✓ Execution continuing at virtual addresses\n
else
  echo ✗ Failed to maintain higher-half execution\n
end

quit