/*
 * kernel/include/exceptions/exceptions.h
 *
 * Architecture-independent exception handling interface
 */

#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <stdint.h>

/* Get architecture-specific exception definitions */
#include <arch_exceptions.h>

/* The architecture provides:
 * - struct exception_context
 * - Exception handler functions
 * - Helper functions for exception handling
 */

/* Common exception handler functions that all architectures must provide */
void exception_init(void);

#endif /* EXCEPTIONS_H */