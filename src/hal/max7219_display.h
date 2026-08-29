#pragma once
// ============================================================================
//  Max7219Display — ISevenSeg sobre o MAX7219, por SPI de hardware
// ============================================================================
//  Adaptador burro de proposito, como o GpioLed. Toda a traducao de "13.8"
//  para bytes mora em kanri_display/max7219.h, testada no PC. Aqui so
//  empurramos palavras de 16 bits no fio.
//
//  LIGACAO (decisao de Jose Rodrigues, 29/08/2026 — ver docs/HARDWARE.md):
//
//      MAX7219 pino  1  DIN   <- GPIO 23
//      MAX7219 pino 13  CLK   <- GPIO 18
//      MAX7219 pino 12  LOAD  <- GPIO  5     (chamado CS no MAX7221)
//
//  Tres fios em vez de onze, e a multiplexacao sai da CPU: uma leitura OBD
//  trava o laco por ate 1 s, e o mostrador continua aceso sozinho.
//
//  ⚠️ NIVEL LOGICO: o ESP32 entrega 3,3 V e o MAX7219 exige 3,5 V de VIH
//  quando alimentado com 5 V. Os tres sinais passam por um 74HCT125. O
//  74HC125 comum NAO serve — mesmo limiar. Ver docs/HARDWARE.md.
// ============================================================================

#include <SPI.h>

#include <cstdint>

#include "kanri_display/i_seven_seg.h"
#include "kanri_display/max7219.h"

namespace kanri::hal {

class Max7219Display : public kanri::display::ISevenSeg {
 public:
  Max7219Display(std::uint8_t pino_din, std::uint8_t pino_clk,
                 std::uint8_t pino_load)
      : din_(pino_din), clk_(pino_clk), load_(pino_load) {}

  bool begin() override;
  void render(const kanri::display::SegFrame& frame) override;
  void render_raw(const std::uint8_t* digits, std::size_t count) override;
  void set_brightness(std::uint8_t percent) override;

  /// Escreve a intensidade CRUA do chip (0..15).
  ///
  /// Existe para a rampa de brilho: ela caminha em passos do MAX7219, e
  /// converter de percentual a cada passo daria degraus repetidos, porque
  /// varios percentuais caem na mesma intensidade.
  void set_intensity(std::uint8_t intensity);
  void clear() override;

 private:
  void escrever(const kanri::display::Max7219Word& palavra);

  std::uint8_t din_;
  std::uint8_t clk_;
  std::uint8_t load_;
  bool pronto_ = false;
};

}  // namespace kanri::hal
