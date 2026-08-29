#include "kanri_core/button.h"

#include "kanri_core/i_clock.h"

namespace kanri::core {

ButtonEvent Button::update(bool pressed_raw, std::uint32_t now_ms) {
  // Primeira chamada: adota a leitura como verdade, sem gerar evento. Sem
  // isto, um aparelho que liga com o botao pressionado (dedo no botao durante
  // a partida) registraria um clique fantasma.
  if (!iniciado_) {
    iniciado_ = true;
    estavel_ = pressed_raw;
    ultimo_bruto_ = pressed_raw;
    mudou_em_ = now_ms;
    apertou_em_ = now_ms;
    return ButtonEvent::None;
  }

  // A leitura crua mudou: reinicia a contagem de estabilidade. Enquanto o
  // contato treme, isto acontece muitas vezes e nada e confirmado.
  if (pressed_raw != ultimo_bruto_) {
    ultimo_bruto_ = pressed_raw;
    mudou_em_ = now_ms;
    return ButtonEvent::None;
  }

  // Estavel ha tempo suficiente para virar verdade?
  if (pressed_raw != estavel_ &&
      elapsed_ms(now_ms, mudou_em_) >= debounce_ms_) {
    estavel_ = pressed_raw;

    if (estavel_) {
      apertou_em_ = now_ms;
      longo_disparado_ = false;
      return ButtonEvent::None;  // o evento vem ao soltar
    }

    // Soltou. Se o toque longo ja foi avisado, o clique NAO vem junto —
    // senao uma acao dispararia as duas coisas.
    return longo_disparado_ ? ButtonEvent::Released : ButtonEvent::Click;
  }

  // Segurando ha tempo bastante: avisa uma vez so.
  if (estavel_ && !longo_disparado_ &&
      elapsed_ms(now_ms, apertou_em_) >= long_press_ms_) {
    longo_disparado_ = true;
    return ButtonEvent::LongPress;
  }

  return ButtonEvent::None;
}

}  // namespace kanri::core
