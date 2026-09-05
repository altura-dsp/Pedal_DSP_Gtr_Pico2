#ifndef RT_SAFETY_HARDENING_H
#define RT_SAFETY_HARDENING_H

#include <stdint.h>
#include <stddef.h>

// Barrera de Sincronización de Datos: Obliga a que todas las transferencias DMA 
// pendientes se completen antes de que la CPU lea o escriba en la SRAM.
// En RP2350 (Cortex-M33) la SRAM no tiene caché L1, por lo que las funciones CMSIS 
// de caché son no-ops. La única forma correcta de coherencia es DSB.
static inline void rt_dma_sram_sync() {
    __asm volatile ("dsb 0xF" ::: "memory");
}

// Wrappers retrocompatibles (Punto Único de Verdad)
// Se mapean directamente a la barrera de memoria.
static inline void rt_invalidate_dcache(void* addr, size_t size) {
    (void)addr; (void)size;
    rt_dma_sram_sync();
}

static inline void rt_clean_dcache(void* addr, size_t size) {
    (void)addr; (void)size;
    rt_dma_sram_sync();
}

#endif // RT_SAFETY_HARDENING_H
