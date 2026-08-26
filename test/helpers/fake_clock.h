#pragma once
// ============================================================================
//  FakeClock — relogio controlado pelo teste
// ============================================================================
//  Com isso, testar um timeout de 30 segundos leva microssegundos: o teste
//  simplesmente "adianta o relogio". Nenhum sleep, nenhum teste lento,
//  nenhum teste instavel (flaky).
// ============================================================================

#include "kanri_core/i_clock.h"

namespace kanri::test {

class FakeClock final : public core::IClock {
 public:
  std::uint32_t now_ms() const override { return now_; }

  void set(std::uint32_t ms) { now_ = ms; }
  void advance(std::uint32_t ms) { now_ += ms; }

 private:
  std::uint32_t now_ = 0;
};

}  // namespace kanri::test
