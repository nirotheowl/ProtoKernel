/*
 * arch/include/arch_device.h
 *
 * Architecture-specific device type detection interface
 */

#ifndef _ARCH_DEVICE_H_
#define _ARCH_DEVICE_H_

#include <device/device.h>

/* Architecture must provide this function to identify device types
 * from device tree compatible strings */
device_type_t arch_get_device_type(const char *compatible);

#endif /* _ARCH_DEVICE_H_ */