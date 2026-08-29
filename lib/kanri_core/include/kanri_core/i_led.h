#pragma once
// ============================================================================
//  kanri_core/i_led.h — Porta do LED de status
// ============================================================================
//  A logica decide QUANDO acender (led_pattern.h). Esta interface so sabe
//  acender e apagar. Trocar o LED da placa por um LED externo, por um RGB ou
//  por um buzzer nao muda uma linha de logica.
// ============================================================================

namespace kanri::core {

class ILed {
 public:
  virtual ~ILed() = default;

  /// Prepara o pino. @return false se nao foi possivel usar o LED.
  virtual bool begin() = 0;

  /// Liga ou desliga. Deve ser barato: e chamado a cada volta do loop.
  virtual void set(bool on) = 0;
};

}  // namespace kanri::core
