#pragma once
// ============================================================================
//  kanri_core/i_clock.h — Porta de tempo
// ============================================================================
//  POR QUE ISSO EXISTE?
//  No ESP32 o tempo vem de millis(). Mas millis() e do Arduino, e nao existe
//  quando rodamos os testes no PC. Se a nossa logica chamasse millis()
//  diretamente, ela seria impossivel de testar (voce teria que esperar 30s de
//  verdade para testar um timeout de 30s).
//
//  A solucao e uma INTERFACE (aqui chamada de "porta"): a logica depende de
//  IClock, que e so uma promessa de "alguem sabe me dizer que hora e".
//    - No ESP32     -> src/hal/arduino_clock.h  usa millis() de verdade.
//    - Nos testes   -> test/helpers/fake_clock.h avanca o tempo na mao.
//  Esse padrao se chama Inversao de Dependencia (o "D" do SOLID).
// ============================================================================

#include <cstdint>

namespace kanri::core {

class IClock {
 public:
  virtual ~IClock() = default;

  /// Milissegundos desde o boot. Envolve (overflow) a cada ~49,7 dias.
  /// Sempre compare intervalos com subtracao (now - antes), nunca com "<".
  virtual std::uint32_t now_ms() const = 0;
};

/// Calcula um intervalo de forma segura mesmo quando o contador envolve.
/// Aritmetica sem sinal de 32 bits faz a conta certa no overflow.
inline std::uint32_t elapsed_ms(std::uint32_t now, std::uint32_t since) {
  return now - since;
}

}  // namespace kanri::core
