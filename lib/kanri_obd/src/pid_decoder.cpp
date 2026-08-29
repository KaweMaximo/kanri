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

/// Monta o valor final ja conferindo a faixa fisica. Uma funcao so para os
/// dois passos evita o erro de aplicar a formula e esquecer a validacao.
DecodedValue aceitar(float valor, float minimo, float maximo,
                     const char* unidade) {
  DecodedValue v;
  v.unit = unidade;
  if (valor < minimo || valor > maximo) {
    v.status = DecodeStatus::OutOfRange;
    return v;
  }
  v.status = DecodeStatus::Ok;
  v.value = valor;
  return v;
}

float A(const ParsedFrame& f) { return static_cast<float>(f.data[0]); }
float B(const ParsedFrame& f) { return static_cast<float>(f.data[1]); }

}  // namespace

bool is_decodable(std::uint8_t mode, std::uint8_t pid) {
  if (mode != kModeCurrentData) return false;
  switch (pid) {
    case 0x04: case 0x05: case 0x0B: case 0x0C: case 0x0D:
    case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x1F:
    case 0x2F: case 0x42: case 0x43: case 0x46: case 0x5C:
      return true;
    default:
      return false;
  }
}

DecodedValue decode(const ParsedFrame& frame) {
  if (!frame.ok()) return recusa(DecodeStatus::FrameNotOk);

  // O frame guarda o modo ECOADO (0x41). A formula e indexada pelo modo
  // PEDIDO (0x01), entao desfazemos o +0x40 antes de consultar.
  const std::uint8_t modo = static_cast<std::uint8_t>(frame.mode - 0x40);
  const std::uint8_t pid = frame.pid;

  // Modo diferente de 01 nao tem formula aqui (o 09 devolve texto, nao
  // numero). O PID e conferido pelo `default:` do switch, adiante — deixar a
  // checagem junto das formulas evita que as duas listas divirjam.
  if (modo != kModeCurrentData) return recusa(DecodeStatus::NotDecodable);

  // Quantos bytes a formula precisa. Conferir aqui, e nao confiar no
  // catalogo, mantem a decodificacao segura mesmo se alguem editar a tabela.
  const std::uint8_t precisa = (pid == 0x0C || pid == 0x10 || pid == 0x1F ||
                                pid == 0x42 || pid == 0x43)
                                   ? 2
                                   : 1;
  if (frame.length < precisa) return recusa(DecodeStatus::WrongLength);

  switch (pid) {
    // -- Dois bytes ---------------------------------------------------------
    case 0x0C:  // rotacao: (A*256 + B) / 4
      return aceitar((A(frame) * 256.0F + B(frame)) / 4.0F, kMinRpm, kMaxRpm,
                     "rpm");
    case 0x10:  // fluxo de ar: (A*256 + B) / 100
      return aceitar((A(frame) * 256.0F + B(frame)) / 100.0F, kMinMaf, kMaxMaf,
                     "g/s");
    case 0x1F:  // tempo de motor ligado: A*256 + B
      return aceitar(A(frame) * 256.0F + B(frame), 0.0F, 65535.0F, "s");
    case 0x42:  // tensao do modulo: (A*256 + B) / 1000
      return aceitar((A(frame) * 256.0F + B(frame)) / 1000.0F, kMinVolts,
                     kMaxVolts, "V");
    case 0x43:  // carga absoluta: (A*256 + B) * 100 / 255
      return aceitar((A(frame) * 256.0F + B(frame)) * 100.0F / 255.0F, 0.0F,
                     25700.0F, "%");

    // -- Um byte ------------------------------------------------------------
    case 0x04:  // carga do motor: A * 100 / 255
    case 0x11:  // posicao da borboleta
    case 0x2F:  // nivel de combustivel
      return aceitar(A(frame) * 100.0F / 255.0F, kMinPercent, kMaxPercent, "%");

    case 0x05:  // temperatura do liquido de arrefecimento: A - 40
    case 0x0F:  // temperatura do ar admitido
    case 0x46:  // temperatura ambiente
    case 0x5C:  // temperatura do oleo
      return aceitar(A(frame) - 40.0F, kMinTempC, kMaxTempC, "C");

    case 0x0B:  // pressao absoluta no coletor: A
      return aceitar(A(frame), kMinKpa, kMaxKpa, "kPa");

    case 0x0D:  // velocidade: A
      return aceitar(A(frame), kMinSpeedKmh, kMaxSpeedKmh, "km/h");

    case 0x0E:  // avanco de ignicao: A/2 - 64
      return aceitar(A(frame) / 2.0F - 64.0F, -64.0F, 64.0F, "deg");

    default:
      return recusa(DecodeStatus::NotDecodable);
  }
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
