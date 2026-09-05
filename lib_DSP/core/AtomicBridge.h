#pragma once

#include <stdint.h>
#include <atomic>
#include "hardware/sync.h" // __dmb() en RP2040/RP2350

/**
 * @brief Double-Buffer Lock-Free (SSOT canónico — lib_DSP/core).
 *
 * Cero "Parameter Tearing" al inyectar parámetros desde el Core 1 (UI/Control)
 * mientras el Core 0 procesa audio. @tparam T debe ser trivialmente copiable.
 *
 * Implementación con std::atomic + __dmb() (Regla RT-Safety: Lock-Free IPC).
 * Este header es el Punto Único de Verdad (SSOT) de AtomicBridge: se CONSUME vía
 * lib_extra_dirs desde 1_Pedal / 2_Aguilar / 3_Autotune. Prohibido duplicar
 * copias locales en src/ que lo sombreen (anti-patrón A16 — Deriva SSOT, Regla 3).
 */
template <typename T>
class AtomicBridge {
private:
    T buffers[2];
    std::atomic<uint8_t> active_idx; // Índice del buffer que el DSP (Core 0) lee
    std::atomic_bool dirty_flag;     // Bandera lock-free: avisa de cambios de estado

public:
    AtomicBridge() : active_idx(0), dirty_flag(false) {}

    /**
     * @brief Inicializa ambos buffers con los mismos valores. Llamar solo en setup().
     */
    void init(const T& initial_state) {
        buffers[0] = initial_state;
        buffers[1] = initial_state;
        active_idx.store(0, std::memory_order_relaxed);
        dirty_flag.store(false, std::memory_order_relaxed);
        __dmb(); // Data Memory Barrier
    }

    /**
     * @brief Hot-path (Core 0): lee los parámetros seguros sin tearing.
     */
    const T& get_active() const {
        uint8_t idx = active_idx.load(std::memory_order_acquire);
        __dmb(); // Doble protección ARM
        return buffers[idx];
    }

    /**
     * @brief Core 1 (UI/Control): inyecta nuevos parámetros sin bloquear al Core 0.
     */
    void update(const T& new_state) {
        uint8_t next_idx = 1 - active_idx.load(std::memory_order_relaxed);
        buffers[next_idx] = new_state;
        __dmb(); // Asegura escritura completa antes de voltear el índice
        active_idx.store(next_idx, std::memory_order_release);
        dirty_flag.store(true, std::memory_order_release);
    }

    /**
     * @brief El Core 0 revisa si hay nuevos datos. Si los hay, limpia la bandera.
     * Retorna true cuando es necesario recalcular coeficientes (ej. Biquads).
     */
    bool check_and_clear_dirty() {
        return dirty_flag.exchange(false, std::memory_order_acquire);
    }
};
