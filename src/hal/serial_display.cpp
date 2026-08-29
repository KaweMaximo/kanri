#include "serial_display.h"

#include <Arduino.h>

namespace kanri::hal {

bool SerialDisplay::begin() {
  // Serial ja foi inicializado em setup(). Nao ha hardware a sondar aqui,
  // entao este driver nunca falha — e por isso nunca gera DisplayFailed.
  // O driver do OLED (v0.3) VAI poder falhar: se o chip nao responder no
  // barramento I2C, begin() devolvera false e a maquina de estados ira para
  // AppState::Fault.
  return true;
}

void SerialDisplay::render(const display::DisplayFrame& frame) {
  // A largura vem de kFrameTextLen, e nao de um numero digitado aqui: o
  // format_row() preenche a linha inteira, e uma moldura mais estreita que o
  // conteudo faria os valores alinhados a direita vazarem para fora dela.
  static constexpr int kLargura = static_cast<int>(display::kFrameTextLen) - 1;

  Serial.println(F("+-------------------------+"));
  Serial.printf("| %-*s |\n", kLargura, frame.title);
  Serial.println(F("+-------------------------+"));
  for (std::size_t i = 0; i < display::kFrameLines; ++i) {
    if (frame.lines[i][0] == '\0') continue;
    Serial.printf("| %-*s |\n", kLargura, frame.lines[i]);
  }
  if (frame.warning) {
    Serial.printf("| %-*s |\n", kLargura, "      [ ATENCAO ]");
  }
  Serial.println(F("+-------------------------+"));
}

void SerialDisplay::set_brightness(std::uint8_t percent) {
  // Monitor serial nao tem brilho. Guardamos o valor para que a leitura
  // via IDisplay continue coerente.
  brightness_ = percent;
}

void SerialDisplay::clear() { Serial.println(); }

}  // namespace kanri::hal
