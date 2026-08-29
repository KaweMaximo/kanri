#pragma once
// ============================================================================
//  GpioLed — ILed num pino comum
// ============================================================================
//  Adaptador burro de proposito: liga e desliga um pino. Toda a inteligencia
//  (qual padrao, quando acender) esta em lib/kanri_core/led_pattern.h, que
//  tem teste. Se este arquivo precisasse de teste, seria sinal de que ele tem
//  logica demais.
//
//  SOBRE QUAL LED USAR
//  -------------------
//  Numa ESP32 DevKit v1 costumam existir DOIS LEDs:
//
//    - LED VERMELHO -> quase sempre e o de ENERGIA. Fica ligado direto no
//      regulador 3,3 V e NAO tem ligacao com nenhum GPIO. Software nenhum
//      consegue apaga-lo — ele so indica que a placa esta alimentada.
//    - LED AZUL -> ligado ao GPIO 2. E este que da para controlar.
//
//  Por isso o padrao aqui e o GPIO 2. Se a sua placa tiver o LED de status em
//  outro pino, basta mudar em src/main.cpp — a logica nao muda.
//
//  `ativo_baixo` existe porque em algumas placas o LED acende com o pino em
//  nivel BAIXO (catodo no GPIO). Se o seu LED piscar invertido — aceso quando
//  deveria apagar — inverta esse parametro.
// ============================================================================

#include <Arduino.h>

#include "kanri_core/i_led.h"

namespace kanri::hal {

class GpioLed final : public core::ILed {
 public:
  explicit GpioLed(std::uint8_t pino, bool ativo_baixo = false)
      : pino_(pino), ativo_baixo_(ativo_baixo) {}

  bool begin() override {
    pinMode(pino_, OUTPUT);
    set(false);
    return true;
  }

  void set(bool on) override {
    // Escreve so quando muda. digitalWrite e barato, mas este metodo roda a
    // cada volta do loop; evitar trabalho repetido e habito bom em firmware.
    if (on == aceso_) return;
    aceso_ = on;
    digitalWrite(pino_, (on != ativo_baixo_) ? HIGH : LOW);
  }

 private:
  std::uint8_t pino_;
  bool ativo_baixo_;
  bool aceso_ = false;
};

}  // namespace kanri::hal
