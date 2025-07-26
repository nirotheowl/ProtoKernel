#ifndef __BOOT_CONFIG_H__
#define __BOOT_CONFIG_H__

/*
 * Boot-time configuration constants
 * These values must match between boot.S and C code
 */

/* Number of page tables allocated by boot.S */
#define BOOT_PAGE_TABLE_COUNT   10

/* Size of each page table */
#define BOOT_PAGE_SIZE          4096

/* Total size of boot page tables */
#define BOOT_PAGE_TABLE_SIZE    (BOOT_PAGE_TABLE_COUNT * BOOT_PAGE_SIZE)

/* 
 * Boot.S adds this padding before page tables start
 * This ensures page tables don't overlap with kernel BSS
 */
#define BOOT_PAGE_TABLE_PADDING 0x1000

#endif /* __BOOT_CONFIG_H__ */