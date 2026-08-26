#pragma once
// ============================================================================
//  FakeDisplay — guarda o ultimo frame em vez de desenhar
// ============================================================================
//  Assim o teste pode afirmar coisas como "quando o link cai, a tela mostra
//  aviso" sem OLED, sem I2C e sem osciloscopio.
// ============================================================================

#include "kanri_display/i_display.h"

namespace kanri::test {

class FakeDisplay final : public display::IDisplay {
 public:
  bool begin() override { return begin_ok_; }

  void render(const display::DisplayFrame& frame) override {
    last_ = frame;
    ++render_count_;
  }

  void set_brightness(std::uint8_t percent) override { brightness_ = percent; }
  void clear() override { ++clear_count_; }

  // --- Inspecao pelo teste ----------------------------------------------
  const display::DisplayFrame& last_frame() const { return last_; }
  std::uint32_t render_count() const { return render_count_; }
  std::uint32_t clear_count() const { return clear_count_; }
  std::uint8_t brightness() const { return brightness_; }
  void fail_begin() { begin_ok_ = false; }

 private:
  display::DisplayFrame last_{};
  std::uint32_t render_count_ = 0;
  std::uint32_t clear_count_ = 0;
  std::uint8_t brightness_ = 0;
  bool begin_ok_ = true;
};

}  // namespace kanri::test
