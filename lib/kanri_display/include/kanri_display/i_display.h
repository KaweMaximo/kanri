#pragma once
// ============================================================================
//  kanri_display/i_display.h — Porta de display
// ============================================================================
//  Implementacoes previstas:
//    - src/hal/serial_display.h -> "desenha" no monitor serial. Serve para
//      desenvolver e depurar antes de o display fisico chegar.
//    - src/hal/ssd1306_display.h (v0.3) -> OLED 128x64 por I2C.
//    - test/helpers/fake_display.h -> guarda o ultimo frame para o teste ler.
//
//  Note que render() recebe o frame por referencia CONSTANTE: o driver pode
//  ler, nunca alterar. Uma garantia dada pelo compilador, de graca.
// ============================================================================

#include <cstdint>

#include "kanri_display/view_model.h"

namespace kanri::display {

class IDisplay {
 public:
  virtual ~IDisplay() = default;

  /// Inicializa o display. @return false se o hardware nao respondeu.
  /// Um false aqui gera AppEvent::DisplayFailed: sem tela nao ha como avisar
  /// o motorista de nada, e esse e o unico caminho para AppState::Fault.
  virtual bool begin() = 0;

  /// Desenha um frame. Deve ser rapido e nunca bloquear indefinidamente.
  virtual void render(const DisplayFrame& frame) = 0;

  /// Brilho de 0 a 100. Drivers sem controle de brilho podem ignorar.
  virtual void set_brightness(std::uint8_t percent) = 0;

  virtual void clear() = 0;
};

}  // namespace kanri::display
