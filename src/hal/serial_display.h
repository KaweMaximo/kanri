#pragma once
// ============================================================================
//  SerialDisplay — "display" no monitor serial
// ============================================================================
//  Enquanto o OLED nao chega (v0.3), este driver desenha o DisplayFrame em
//  texto no monitor serial. Nao e um placeholder inutil: e o mesmo
//  DisplayFrame, pela mesma interface IDisplay. Quando o OLED entrar, a
//  logica nao muda uma linha — so trocamos qual adaptador e instanciado no
//  main.cpp.
// ============================================================================

#include "kanri_display/i_display.h"

namespace kanri::hal {

class SerialDisplay final : public display::IDisplay {
 public:
  bool begin() override;
  void render(const display::DisplayFrame& frame) override;
  void set_brightness(std::uint8_t percent) override;
  void clear() override;

 private:
  std::uint8_t brightness_ = 100;
};

}  // namespace kanri::hal
