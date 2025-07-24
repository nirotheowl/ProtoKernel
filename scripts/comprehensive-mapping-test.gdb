# Comprehensive test of all page table mappings
# This test thoroughly verifies both identity and higher-half mappings

# Disable default breakpoints
delete breakpoints

# Start at el1_entry
break *0x40080068
continue
delete breakpoints

echo \n========================================\n
echo === COMPREHENSIVE MAPPING TEST ===\n
echo ========================================\n

# Let the page table setup complete
# Just before TCR configuration
break *0x400801c8
continue

echo \n=== 1. PHYSICAL ADDRESS CALCULATION ===\n
echo Physical base (x28): 
print/x $x28
echo Page table base (x26): 
print/x $x26
set $ptbase = $x26

echo \n=== 2. PAGE TABLE POINTERS ===\n
echo --- TTBR0 (Identity Mapping) ---\n
echo L0 table (x23): 
print/x $x23
echo L1 table (x22): 
print/x $x22
echo L2 table first 512MB (x21): 
print/x $x21
echo L2 table second 512MB (x20): 
print/x $x20

echo \n--- TTBR1 (Higher-Half Mapping) ---\n
echo L0 table (x19): 
print/x $x19
echo L1 table (x18): 
print/x $x18
echo L2 table (x17): 
print/x $x17

echo \n=== 3. TTBR0 PAGE TABLE WALK (Identity Mapping) ===\n

# Check mapping for kernel at 0x40080000
set $test_addr = 0x40080000
echo \nTesting identity mapping for address 
output/x $test_addr
echo  (kernel start)\n

# Calculate indices
set $l0_idx = ($test_addr >> 39) & 0x1FF
set $l1_idx = ($test_addr >> 30) & 0x1FF
set $l2_idx = ($test_addr >> 21) & 0x1FF

echo L0 index: 
output/x $l0_idx
echo , L1 index: 
output/x $l1_idx
echo , L2 index: 
output/x $l2_idx
echo \n

# Walk the page tables
echo L0[
output $l0_idx
echo ] at 
output/x $x23 + ($l0_idx * 8)
echo : 
x/1gx $x23 + ($l0_idx * 8)
set $l0_entry = *((unsigned long *)($x23 + ($l0_idx * 8)))

# Check if valid
if ($l0_entry & 0x1)
  echo L0 entry valid, points to L1 table at 
  set $l1_table = $l0_entry & 0xFFFFFFFFF000
  output/x $l1_table
  echo \n
  
  echo L1[
  output $l1_idx
  echo ] at 
  output/x $l1_table + ($l1_idx * 8)
  echo : 
  x/1gx $l1_table + ($l1_idx * 8)
  set $l1_entry = *((unsigned long *)($l1_table + ($l1_idx * 8)))
  
  if ($l1_entry & 0x1)
    echo L1 entry valid, points to L2 table at 
    set $l2_table = $l1_entry & 0xFFFFFFFFF000
    output/x $l2_table
    echo \n
    
    echo L2[
    output $l2_idx
    echo ] at 
    output/x $l2_table + ($l2_idx * 8)
    echo : 
    x/1gx $l2_table + ($l2_idx * 8)
    set $l2_entry = *((unsigned long *)($l2_table + ($l2_idx * 8)))
    
    if ($l2_entry & 0x1)
      echo L2 entry valid, maps to physical address 
      set $phys_addr = $l2_entry & 0xFFFFFFFFF000
      output/x $phys_addr
      echo \n
      
      # Verify this matches our test address
      if ($phys_addr == ($test_addr & 0xFFE00000))
        echo SUCCESS: Identity mapping correct for kernel region\n
      else
        echo ERROR: Identity mapping incorrect!\n
        echo Expected: 
        output/x ($test_addr & 0xFFE00000)
        echo , Got: 
        output/x $phys_addr
        echo \n
      end
    else
      echo ERROR: L2 entry invalid!\n
    end
  else
    echo ERROR: L1 entry invalid!\n
  end
else
  echo ERROR: L0 entry invalid!\n
end

echo \n=== 4. TTBR1 PAGE TABLE WALK (Higher-Half Mapping) ===\n

# Check mapping for 0xFFFF000040080000
set $virt_addr = 0xFFFF000040080000
set $expected_phys = 0x40080000
echo \nTesting higher-half mapping for address 
output/x $virt_addr
echo \n
echo Expected to map to physical 
output/x $expected_phys
echo \n

# Calculate indices for higher-half address
# Note: For 0xFFFF addresses, bit 47 is set, so L0 index is 0x1FE
set $l0_idx = 0x1FE
set $l1_idx = ($virt_addr >> 30) & 0x1FF
set $l2_idx = ($virt_addr >> 21) & 0x1FF

echo L0 index: 
output/x $l0_idx
echo , L1 index: 
output/x $l1_idx
echo , L2 index: 
output/x $l2_idx
echo \n

# Walk TTBR1 page tables
echo L0[
output/x $l0_idx
echo ] at 
output/x $x19 + ($l0_idx * 8)
echo : 
x/1gx $x19 + ($l0_idx * 8)
set $l0_entry = *((unsigned long *)($x19 + ($l0_idx * 8)))

if ($l0_entry & 0x1)
  echo L0 entry valid, points to L1 table at 
  set $l1_table = $l0_entry & 0xFFFFFFFFF000
  output/x $l1_table
  echo \n
  
  echo L1[
  output $l1_idx
  echo ] at 
  output/x $l1_table + ($l1_idx * 8)
  echo : 
  x/1gx $l1_table + ($l1_idx * 8)
  set $l1_entry = *((unsigned long *)($l1_table + ($l1_idx * 8)))
  
  if ($l1_entry & 0x1)
    echo L1 entry valid, points to L2 table at 
    set $l2_table = $l1_entry & 0xFFFFFFFFF000
    output/x $l2_table
    echo \n
    
    echo L2[
    output $l2_idx
    echo ] at 
    output/x $l2_table + ($l2_idx * 8)
    echo : 
    x/1gx $l2_table + ($l2_idx * 8)
    set $l2_entry = *((unsigned long *)($l2_table + ($l2_idx * 8)))
    
    if ($l2_entry & 0x1)
      echo L2 entry valid, maps to physical address 
      set $phys_addr = $l2_entry & 0xFFFFFFFFF000
      output/x $phys_addr
      echo \n
      
      # Verify mapping
      # Since we're at L2 index 0, we expect the first 2MB block from our kernel base
      if ($phys_addr == $expected_phys)
        echo SUCCESS: Higher-half mapping correct!\n
        echo Virtual 
        output/x ($virt_addr & 0xFFE00000)
        echo  maps to physical 
        output/x $phys_addr
        echo  (2MB block)\n
      else
        echo ERROR: Higher-half mapping incorrect!\n
        echo Expected physical base: 
        output/x $expected_phys
        echo , Got: 
        output/x $phys_addr
        echo \n
      end
    else
      echo ERROR: L2 entry invalid!\n
    end
  else
    echo ERROR: L1 entry invalid!\n
  end
else
  echo ERROR: L0 entry invalid!\n
end

echo \n=== 5. PAGE TABLE ENTRY ATTRIBUTES ===\n

# Check a few key entries for correct attributes
echo \nChecking L2 entry attributes for kernel region:\n
# Entry for 0x40000000
set $l2_entry = *((unsigned long *)($x21 + (0x200 * 8)))
echo Entry for 0x40000000: 
output/x $l2_entry
echo \n

# Decode attributes
set $valid = $l2_entry & 0x1
set $block = ($l2_entry & 0x3) == 0x1
set $af = ($l2_entry >> 10) & 0x1
set $sh = ($l2_entry >> 8) & 0x3
set $ap = ($l2_entry >> 6) & 0x3
set $mair_idx = ($l2_entry >> 2) & 0x7

echo   Valid: 
output $valid
echo , Block descriptor: 
output $block
echo \n  Access Flag: 
output $af
echo , Shareability: 
output $sh
echo , AP: 
output $ap
echo , MAIR index: 
output $mair_idx
echo \n

echo \n=== 6. TCR AND TTBR CONFIGURATION ===\n

# Continue to after TCR setup
stepi 20

echo TCR_EL1: 
set $tcr = $TCR_EL1
output/x $tcr
echo \n

# Decode TCR fields
set $t0sz = $tcr & 0x3F
set $t1sz = ($tcr >> 16) & 0x3F
echo   T0SZ: 
output $t0sz
echo  (
output (64 - $t0sz)
echo -bit addresses)\n
echo   T1SZ: 
output $t1sz
echo  (
output (64 - $t1sz)
echo -bit addresses)\n

# Continue to after TTBR setup
stepi 10

echo \nTTBR0_EL1: 
output/x $TTBR0_EL1
echo \nTTBR1_EL1: 
output/x $TTBR1_EL1
echo \n

echo \n=== 7. MMU ENABLE TEST ===\n

# Set breakpoint after MMU enable
break *0x400801dc
continue

echo \nMMU enabled, checking SCTLR_EL1:\n
set $sctlr = $SCTLR_EL1
output/x $sctlr
echo \n
set $mmu = $sctlr & 0x1
set $dcache = ($sctlr >> 2) & 0x1
set $icache = ($sctlr >> 12) & 0x1
echo   MMU enabled: 
output $mmu
echo \n  D-cache enabled: 
output $dcache
echo \n  I-cache enabled: 
output $icache
echo \n

echo \n=== 8. MEMORY ACCESS TEST ===\n

# Test reading through identity mapping
echo \nTesting read through identity mapping at 0x40080000:\n
x/4i 0x40080000

# Test reading through higher-half mapping
echo \nTesting read through higher-half mapping at 0xFFFF000040080000:\n
x/4i 0xFFFF000040080000

# Verify they show the same content
echo \nBoth should show the same instructions (kernel start)\n

echo \n=== 9. ADDITIONAL MAPPING VERIFICATION ===\n

# Check a few more addresses in the mapped range
echo \nChecking multiple addresses in kernel range:\n

set $offset = 0x1000
echo Identity mapping at 0x40081000: 
x/1xw 0x40080000 + $offset
echo Higher-half at 0xFFFF000040081000: 
x/1xw 0xFFFF000040080000 + $offset

set $offset = 0x8000
echo \nIdentity mapping at 0x40088000: 
x/1xw 0x40080000 + $offset
echo Higher-half at 0xFFFF000040088000: 
x/1xw 0xFFFF000040080000 + $offset

echo \n========================================\n
echo === TEST COMPLETE ===\n
echo ========================================\n

# Let execution continue
continue