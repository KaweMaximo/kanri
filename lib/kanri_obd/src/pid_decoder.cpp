#include "kanri_obd/pid_decoder.h"

#include "kanri_obd/obd_pid.h"
#include "kanri_obd/safety.h"

namespace kanri::obd {
namespace {

DecodedValue recusa(DecodeStatus status) {
  DecodedValue v;
  v.status = status;
  return v;
}

float A(const ParsedFrame& f) { return static_cast<float>(f.data[0]); }
float B(const ParsedFrame& f) { return static_cast<float>(f.data[1]); }
float AB(const ParsedFrame& f) { return A(f) * 256.0F + B(f); }

}  // namespace

std::uint8_t formula_byte_count(PidFormula f) {
  switch (f) {
    case PidFormula::RawAB:
    case PidFormula::Rpm:
    case PidFormula::MafRate:
    case PidFormula::Voltage:
    case PidFormula::CatalystTemp:
    case PidFormula::RailPressure:
    case PidFormula::RailGauge:
    case PidFormula::FuelRate:
    case PidFormula::AbsLoad:
    case PidFormula::EvapPressure:
      return 2;
    case PidFormula::RawA:
    case PidFormula::PercentA:
    case PidFormula::SignedPercent:
    case PidFormula::TempA:
    case PidFormula::TimingAdvance:
    case PidFormula::FuelPressure:
      return 1;
    case PidFormula::None:
      return 0;
  }
  return 0;  // valor fora do enum: nao decodificavel
}

namespace {

/// Aplica a formula. Nao valida faixa: isso e o passo seguinte, de proposito
/// separado, para que a conversao e a barreira fisica nao se misturem.
float aplicar(PidFormula formula, const ParsedFrame& f) {
  switch (formula) {
    case PidFormula::RawA:          return A(f);
    case PidFormula::RawAB:         return AB(f);
    case PidFormula::PercentA:      return A(f) * 100.0F / 255.0F;
    // Fuel trim e EGR sao deslocados por 128: negativo = a ECU esta TIRANDO
    // combustivel (mistura rica); positivo = adicionando (mistura pobre).
    case PidFormula::SignedPercent: return (A(f) - 128.0F) * 100.0F / 128.0F;
    // O -40 permite representar temperatura negativa num byte sem sinal.
    case PidFormula::TempA:         return A(f) - 40.0F;
    case PidFormula::Rpm:           return AB(f) / 4.0F;
    case PidFormula::MafRate:       return AB(f) / 100.0F;
    case PidFormula::Voltage:       return AB(f) / 1000.0F;
    case PidFormula::TimingAdvance: return A(f) / 2.0F - 64.0F;
    case PidFormula::CatalystTemp:  return AB(f) / 10.0F - 40.0F;
    case PidFormula::FuelPressure:  return A(f) * 3.0F;
    case PidFormula::RailPressure:  return AB(f) * 0.079F;
    case PidFormula::RailGauge:     return AB(f) * 10.0F;
    case PidFormula::FuelRate:      return AB(f) / 20.0F;
    case PidFormula::AbsLoad:       return AB(f) * 100.0F / 255.0F;
    // Pressao do sistema evaporativo e o unico com sinal de verdade: os dois
    // bytes formam um inteiro de 16 bits COM sinal, dividido por 4.
    case PidFormula::EvapPressure: {
      const std::int16_t bruto = static_cast<std::int16_t>(
          (static_cast<std::uint16_t>(f.data[0]) << 8) | f.data[1]);
      return static_cast<float>(bruto) / 4.0F;
    }
    // GCOVR_EXCL_START
    // Inalcancavel: decode() so chama esta funcao depois de confirmar que a
    // formula precisa de pelo menos um byte, o que exclui None. Os casos
    // ficam para que o compilador continue avisando (-Wswitch) quando uma
    // formula nova for acrescentada ao enum e esquecida aqui.
    case PidFormula::None:
      return 0.0F;
  }
  return 0.0F;
  // GCOVR_EXCL_STOP
}

}  // namespace

bool is_decodable(std::uint8_t mode, std::uint8_t pid) {
  const PidDescriptor* d = find_pid(mode, pid);
  return d != nullptr && d->formula != PidFormula::None;
}

DecodedValue decode(const ParsedFrame& frame) {
  if (!frame.ok()) return recusa(DecodeStatus::FrameNotOk);

  // O frame guarda o modo ECOADO (0x41). A tabela e indexada pelo modo
  // PEDIDO (0x01), entao desfazemos o +0x40 antes de consultar.
  const std::uint8_t modo = static_cast<std::uint8_t>(frame.mode - 0x40);

  const PidDescriptor* d = find_pid(modo, frame.pid);
  if (d == nullptr) return recusa(DecodeStatus::NotDecodable);

  // Zero bytes significa "sem formula" (mapa de bits, status) ou formula
  // desconhecida. Uma checagem so, no lugar de duas que poderiam divergir.
  const std::uint8_t precisa = formula_byte_count(d->formula);
  if (precisa == 0) return recusa(DecodeStatus::NotDecodable);
  if (frame.length < precisa) return recusa(DecodeStatus::WrongLength);

  DecodedValue v;
  v.unit = d->unit;
  const float valor = aplicar(d->formula, frame);

  // SEGUNDA BARREIRA. Aplicar a formula nao basta: um frame pode passar pelo
  // parser — hex valido, modo e PID certos, tamanho certo — e ainda conter
  // valor impossivel, porque e isso que ruido eletrico produz. Ver
  // docs/SAFETY.md.
  if (valor < d->min_value || valor > d->max_value) {
    v.status = DecodeStatus::OutOfRange;
    return v;
  }

  v.status = DecodeStatus::Ok;
  v.value = valor;
  return v;
}

const char* to_string(DecodeStatus status) {
  switch (status) {
    case DecodeStatus::Ok:           return "Ok";
    case DecodeStatus::NotDecodable: return "NotDecodable";
    case DecodeStatus::WrongLength:  return "WrongLength";
    case DecodeStatus::OutOfRange:   return "OutOfRange";
    case DecodeStatus::FrameNotOk:   return "FrameNotOk";
  }
  return "Unknown";
}

}  // namespace kanri::obd
