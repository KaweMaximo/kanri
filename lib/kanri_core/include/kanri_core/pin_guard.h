#pragma once
// ============================================================================
//  kanri_core/pin_guard.h — Quem pode ser acionado, e quem nao pode
// ============================================================================
//  Existe por causa de uma pergunta real feita na bancada: "liga pra mim o
//  GPIO 28". O GPIO 28 NAO EXISTE no ESP32 — 20, 24, 28, 29, 30 e 31 sao
//  buracos na numeracao do chip.
//
//  E o problema nao e o pedido: e que `digitalWrite(28, HIGH)` COMPILA sem
//  aviso e nao faz nada. Quem pediu vai medir um pino que nao existe
//  procurando defeito na fiacao.
//
//  A mesma armadilha aparece de outras formas:
//    - 34, 35, 36 e 39 sao input-only. `pinMode(34, OUTPUT)` compila, roda,
//      nao da erro, e o pino nunca muda de nivel.
//    - 6 a 11 sao a flash SPI. Acionar um deles trava o chip na hora.
//    - 1 e 3 sao o console USB. Acionar derruba o painel e a gravacao.
//    - 12 e strapping: HIGH no boot e o ESP32 nao inicia.
//
//  Todas essas falham em SILENCIO ou de forma dificil de atribuir. Por isso
//  a checagem e uma funcao pura, testada, e nao um `if` na hora do comando.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace kanri::core {

/// Por que um pino pode nao servir como saida.
enum class PinVerdict : std::uint8_t {
  Ok = 0,
  DoesNotExist,  ///< 20, 24, 28-31: buracos na numeracao do ESP32.
  OutOfRange,    ///< Acima de 39.
  InputOnly,     ///< 34, 35, 36, 39: sem driver de saida.
  SpiFlash,      ///< 6-11: acionar trava o chip.
  UsbSerial,     ///< 1, 3: derruba o console.
  BootStrapping, ///< 12: HIGH no boot impede o ESP32 de iniciar.
  Reserved,      ///< Ja usado por este firmware.
};

/// O pino serve como saida de uso geral?
///
/// @param pin       numero do GPIO.
/// @param reserved  pinos que ESTE firmware ja usa; pode ser nullptr.
/// @param count     quantos itens em `reserved`.
PinVerdict check_output_pin(std::uint8_t pin, const std::uint8_t* reserved,
                            std::size_t count);

/// Explicacao curta do veredito, para o console. Nunca devolve nullptr.
const char* to_string(PinVerdict verdict);

// ---------------------------------------------------------------------------
//  Lista de pinos
// ---------------------------------------------------------------------------

/// Teto de pinos numa lista. Oito e o suficiente para uma barra de contagiro
/// e cabe num bitmask de 8 bits.
constexpr std::size_t kMaxPinList = 8;

/// Por que uma lista foi recusada.
enum class PinListError : std::uint8_t {
  None = 0,
  Empty,       ///< Nenhum pino informado.
  TooMany,     ///< Mais que kMaxPinList.
  NotANumber,  ///< Algo que nao e numero no meio da lista.
  Duplicate,   ///< O mesmo pino duas vezes.
  BadPin,      ///< Um pino reprovou em check_output_pin(); ver `verdict`.
};

/// O resultado de interpretar uma lista de pinos.
struct PinList {
  std::uint8_t pins[kMaxPinList] = {};
  std::size_t count = 0;
  PinListError error = PinListError::None;
  std::uint8_t offending = 0;              ///< Qual pino causou o erro.
  PinVerdict verdict = PinVerdict::Ok;     ///< Por que, quando BadPin.

  bool ok() const { return error == PinListError::None; }
};

/// Interpreta "22,21,19" ou "22 21 19" (ou os dois misturados).
///
/// Valida CADA pino com check_output_pin(). Aceitar a lista e recusar depois,
/// na hora de acionar, deixaria metade dos LEDs funcionando e a outra metade
/// em silencio — e o silencio e justamente o que estamos combatendo.
///
/// Repetidos sao recusados: um pino duas vezes na barra piscaria fora de
/// ritmo com ele mesmo, e ninguem entenderia por que.
PinList parse_pin_list(const char* text, const std::uint8_t* reserved,
                       std::size_t reserved_count);

/// Explicacao curta do erro de lista. Nunca devolve nullptr.
const char* to_string(PinListError error);

// ---------------------------------------------------------------------------
//  Barra de LEDs
// ---------------------------------------------------------------------------
//  A ideia final e um CONTAGIRO: uma fileira de LEDs que acende conforme a
//  rotacao sobe. Por enquanto ela serve para testar a fiacao, mas o conceito
//  ja nasce certo — barra, e nao "um pino piscando".
//
//  As funcoes devolvem BITMASK (bit 0 = primeiro pino da lista) em vez de
//  mexer em hardware. E o que permite testar a sequencia inteira no PC.

/// Quais LEDs acender no instante `now_ms`, piscando todos juntos.
std::uint8_t bar_blink_mask(std::uint32_t now_ms, std::size_t count,
                            std::uint32_t period_ms = 600);

/// Quantos passos tem a varredura de autoteste de uma barra de `count` LEDs.
std::size_t bar_test_steps(std::size_t count);

/// Mascara do passo `index` da varredura: um LED por vez, depois todos, e
/// termina apagado.
///
/// Um por vez e o que localiza fio trocado — com todos acesos, dois LEDs
/// invertidos ficam indistinguiveis.
std::uint8_t bar_test_mask(std::size_t index, std::size_t count);

}  // namespace kanri::core
