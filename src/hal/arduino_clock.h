#pragma once
// ============================================================================
//  ArduinoClock — IClock usando millis() de verdade
// ============================================================================
//  A implementacao inteira e uma linha. E esse e o ponto: o adaptador de
//  hardware deve ser burro o suficiente para nao precisar de teste. Toda a
//  inteligencia (timeouts, backoff, deteccao de dado velho) fica em lib/,
//  onde e testavel.
//
//  Esse padrao tem nome: "humble object" (objeto humilde).
// ============================================================================

#include <Arduino.h>

#include "kanri_core/i_clock.h"

namespace kanri::hal {

class ArduinoClock final : public core::IClock {
 public:
  std::uint32_t now_ms() const override {
    // millis() envolve a cada ~49,7 dias. Isso NAO e tratado aqui, e sim em
    // quem calcula intervalos, com subtracao sem sinal (core::elapsed_ms).
    return static_cast<std::uint32_t>(millis());
  }
};

}  // namespace kanri::hal
