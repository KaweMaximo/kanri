#pragma once
// ============================================================================
//  kanri_core/retry_policy.h — Backoff exponencial
// ============================================================================
//  Quando o Bluetooth cai, o instinto e "tentar reconectar imediatamente, em
//  loop". Isso e ruim: torra bateria do carro, entope o radio 2.4 GHz e
//  esconde o problema real do usuario.
//
//  Backoff exponencial: espere 1s, depois 2s, 4s, 8s... ate um teto. Assim as
//  primeiras tentativas sao rapidas (falha passageira) e as seguintes sao
//  espacadas (falha real, ex.: carro desligado).
//
//  Tudo aqui e aritmetica pura de inteiros -> facil de testar no PC.
// ============================================================================

#include <cstdint>

namespace kanri::core {

/// Politica de retentativa com dobra do intervalo ate um teto.
class RetryPolicy {
 public:
  /// @param base_delay_ms  espera da primeira tentativa
  /// @param max_delay_ms   teto: o intervalo nunca passa disso
  constexpr RetryPolicy(std::uint32_t base_delay_ms, std::uint32_t max_delay_ms)
      : base_delay_ms_(base_delay_ms == 0 ? 1 : base_delay_ms),
        max_delay_ms_(max_delay_ms < base_delay_ms ? base_delay_ms : max_delay_ms) {}

  /// Quanto esperar antes da proxima tentativa, no estado atual.
  std::uint32_t current_delay_ms() const;

  /// Quantas falhas seguidas aconteceram desde o ultimo sucesso.
  std::uint32_t attempt_count() const { return attempts_; }

  /// Registra uma falha: o proximo delay dobra.
  void on_failure();

  /// Registra um sucesso: volta ao delay base. SEMPRE chame isso ao
  /// reconectar, senao o backoff fica preso no teto para sempre.
  void on_success();

 private:
  std::uint32_t base_delay_ms_;
  std::uint32_t max_delay_ms_;
  std::uint32_t attempts_ = 0;
};

}  // namespace kanri::core
