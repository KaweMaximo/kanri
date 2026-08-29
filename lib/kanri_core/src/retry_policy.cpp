#include "kanri_core/retry_policy.h"

namespace kanri::core {

// Teto de tentativas contadas. Depois de 32 dobras qualquer delay razoavel
// ja bateu no maximo; parar de contar evita estouro do contador em uma
// sessao muito longa (carro parado com a ignicao ligada, por exemplo).
namespace {
constexpr std::uint32_t kMaxTrackedAttempts = 32;
}

std::uint32_t RetryPolicy::current_delay_ms() const {
  std::uint32_t delay = base_delay_ms_;
  for (std::uint32_t i = 0; i < attempts_; ++i) {
    // Checa ANTES de multiplicar: dobrar um valor grande estouraria o
    // uint32_t e nos daria um delay minusculo — exatamente o bug que o
    // backoff existe para evitar.
    if (delay >= max_delay_ms_ / 2) return max_delay_ms_;
    delay *= 2;
  }
  return delay > max_delay_ms_ ? max_delay_ms_ : delay;
}

std::uint32_t RetryPolicy::record_failure() {
  // Le ANTES de avancar: o intervalo devolvido e o desta falha. Avancar
  // primeiro faria a primeira espera ja ser o dobro do valor base.
  const std::uint32_t delay = current_delay_ms();
  on_failure();
  return delay;
}

void RetryPolicy::on_failure() {
  if (attempts_ < kMaxTrackedAttempts) ++attempts_;
}

void RetryPolicy::on_success() { attempts_ = 0; }

}  // namespace kanri::core
