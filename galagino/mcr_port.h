/*
 * Build glue shared by the Moon Cresta core. The core is plain C with no Pico
 * dependencies, so the same sources build for the board and for the host
 * harness in hosttest/. On the board the hot paths go to SRAM.
 */
#ifndef MCR_PORT_H
#define MCR_PORT_H

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define MCR_HOT(f) __not_in_flash_func(f)
#else
#define MCR_HOT(f) f
#endif

#endif
