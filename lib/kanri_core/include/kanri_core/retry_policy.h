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

  /// Registra uma falha E devolve quanto esperar ANTES da proxima tentativa.
  ///
  /// **Use este metodo, nao a dupla current_delay_ms() + on_failure().**
  ///
  /// Por que ele existe: a ordem das duas chamadas separadas importa, e
  /// errar a ordem e silencioso. Chamando on_failure() primeiro, a primeira
  /// falha ja dobra o intervalo e o valor base NUNCA e usado — o firmware
  /// espera 2s, 4s, 8s em vez de 1s, 2s, 4s.
  ///
  /// Esse bug esteve no main.cpp da v0.1 e passou por 122 testes, porque a
  /// ordem de uso morava na "cola" — a unica parte do projeto sem teste. Foi
  /// preciso gravar o firmware e ler o log serial para ve-lo.
  ///
  /// A licao virou codigo: com um metodo so, nao ha ordem para errar.
  ///
  /// Sequencia correta, com base=1000 e teto=30000:
  ///   1a falha -> devolve  1000  (o valor base, como documentado)
  ///   2a falha -> devolve  2000
  ///   3a falha -> devolve  4000
  ///   ...ate o teto.
  ///
  /// @return o intervalo a esperar por causa DESTA falha.
  std::uint32_t record_failure();

  /// Registra uma falha sem devolver nada: o proximo delay dobra.
  ///
  /// Prefira record_failure(). Este metodo continua disponivel para quem
  /// so quer avancar o contador (por exemplo, ao contabilizar uma falha que
  /// nao gera espera).
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
