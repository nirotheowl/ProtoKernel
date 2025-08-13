/*
 * kernel/include/drivers/uart_drivers.h
 * 
 * UART driver subsystem interface
 */

#ifndef __UART_DRIVERS_H
#define __UART_DRIVERS_H

#include <drivers/driver_class.h>

struct device;

/* UART framework functions */
int uart_framework_init(void);
int uart_softc_init(struct uart_softc *sc, struct device *dev, const struct uart_class *class);
struct uart_softc *uart_device_get_softc(struct device *dev);
int uart_console_attach(struct uart_softc *sc);
struct uart_softc *uart_console_get(void);
void uart_list_devices(void);

/* Console selection */
int uart_console_auto_select(void *fdt);

#endif /* __UART_DRIVERS_H */