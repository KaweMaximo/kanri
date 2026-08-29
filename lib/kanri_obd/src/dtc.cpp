#include "kanri_obd/dtc.h"

namespace kanri::obd {
namespace {

/// A letra do sistema, pelos dois bits mais significativos.
char letra_do_sistema(std::uint16_t raw) {
  switch ((raw >> 14) & 0x03U) {
    case 0: return 'P';  // powertrain: motor e cambio
    case 1: return 'C';  // chassis: freios, suspensao, direcao
    case 2: return 'B';  // body: airbag, vidros, ar-condicionado
    default: return 'U'; // network: comunicacao entre modulos
  }
}

char hex_digito(std::uint8_t v) {
  return static_cast<char>(v < 10 ? ('0' + v) : ('A' + (v - 10)));
}

}  // namespace

bool decode_dtc(std::uint16_t raw, char* out, std::size_t cap) {
  if (out == nullptr || cap < kDtcTextLen) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return false;
  }

  out[0] = letra_do_sistema(raw);
  // O primeiro digito vem dos bits 13-12 e vale de 0 a 3 — nunca precisa de
  // hexadecimal, mas usamos a mesma funcao por uniformidade.
  out[1] = hex_digito(static_cast<std::uint8_t>((raw >> 12) & 0x03U));
  out[2] = hex_digito(static_cast<std::uint8_t>((raw >> 8) & 0x0FU));
  out[3] = hex_digito(static_cast<std::uint8_t>((raw >> 4) & 0x0FU));
  out[4] = hex_digito(static_cast<std::uint8_t>(raw & 0x0FU));
  out[5] = '\0';
  return true;
}

DtcList parse_dtc_response(const ParsedFrame& frame, DtcKind kind) {
  DtcList lista;
  if (!frame.ok()) return lista;

  // `frame.length` e um uint8_t e pode dizer ate 255, mas `frame.data` tem
  // apenas kMaxPayloadBytes. O parser garante essa relacao, mas esta funcao e
  // publica: um frame montado a mao, ou um campo corrompido em memoria,
  // faria o laco ler alem do vetor. Prendemos ao tamanho real do buffer.
  const std::uint8_t bytes =
      frame.length > kMaxPayloadBytes
          ? static_cast<std::uint8_t>(kMaxPayloadBytes)
          : frame.length;

  // Os codigos vem em pares. Um payload impar significa resposta truncada —
  // usamos os pares completos e descartamos o byte solto.
  const std::uint8_t pares = static_cast<std::uint8_t>(bytes / 2);

  for (std::uint8_t i = 0; i < pares; ++i) {
    const std::uint16_t raw =
        static_cast<std::uint16_t>((frame.data[i * 2] << 8) |
                                   frame.data[(i * 2) + 1]);

    // 0x0000 e o preenchimento que a ECU usa quando sobra espaco na resposta.
    // Nao e um codigo, e listar "P0000" confundiria quem le.
    if (raw == 0) continue;

    // GCOVR_EXCL_START
    // Inalcancavel hoje, e mantida de proposito.
    //
    // O frame comporta kMaxPayloadBytes (32) bytes = 16 pares, e kMaxDtcs
    // tambem e 16 — entao a lista nunca chega a encher. A guarda existe para
    // o dia em que um desses dois numeros mudar: sem ela, aumentar
    // kMaxPayloadBytes viraria escrita fora de `items`.
    if (lista.count >= kMaxDtcs) {
      lista.truncated = true;
      break;
    }
    // GCOVR_EXCL_STOP

    Dtc& dtc = lista.items[lista.count];
    decode_dtc(raw, dtc.text, sizeof(dtc.text));
    dtc.raw = raw;
    dtc.kind = kind;
    ++lista.count;
  }
  return lista;
}

const char* to_string(DtcKind kind) {
  switch (kind) {
    case DtcKind::Stored:    return "gravado";
    case DtcKind::Pending:   return "pendente";
    case DtcKind::Permanent: return "permanente";
  }
  return "desconhecido";
}

}  // namespace kanri::obd
