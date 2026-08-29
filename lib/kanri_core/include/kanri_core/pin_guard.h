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

}  // namespace kanri::core
