#pragma once
// ============================================================================
//  kanri_obd/dtc.h — Codigos de falha (DTC)
// ============================================================================
//  DTC = Diagnostic Trouble Code. E o "P0301" que aparece no scanner quando a
//  luz de injecao acende. Cada codigo cabe em DOIS BYTES, e a codificacao nao
//  e obvia:
//
//      bits 15-14  sistema     00=P (motor/cambio)  01=C (chassi)
//                              10=B (carroceria)    11=U (rede)
//      bits 13-12  1o digito   0 a 3
//      bits 11-8   2o digito   0 a F
//      bits  7-4   3o digito   0 a F
//      bits  3-0   4o digito   0 a F
//
//  Exemplo: 0x0143 -> P0143
//      00 -> P,  00 -> 0,  0001 -> 1,  0100 -> 4,  0011 -> 3
//
//  Tres modos devolvem DTCs, e a diferenca entre eles importa no diagnostico:
//
//      0x03  GRAVADOS    a falha aconteceu e acendeu a luz
//      0x07  PENDENTES   aconteceu uma vez; se repetir, vira gravado
//      0x0A  PERMANENTES gravados que so a propria ECU pode limpar, depois
//                        de confirmar que o defeito sumiu
//
//  O modo 0x0A e justamente o que impede alguem de "resolver" a inspecao
//  apagando codigos: ele resiste ao Modo 04.
//
//  TUDO AQUI E LEITURA. Nenhum destes modos altera coisa alguma na ECU — e o
//  Modo 04, que APAGA codigos, continua proibido e testado. Ver docs/SAFETY.md.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "kanri_obd/elm327_parser.h"

namespace kanri::obd {

/// Tamanho do texto de um codigo: 5 caracteres mais o terminador ("P0143").
constexpr std::size_t kDtcTextLen = 6;

/// Quantos codigos guardamos de uma leitura. Uma ECU com mais de 16 codigos
/// gravados tem problema maior que a nossa capacidade de listar.
constexpr std::size_t kMaxDtcs = 16;

/// De onde o codigo veio. A mesma falha aparece em listas diferentes conforme
/// o estagio, e confundi-las levaria a diagnostico errado.
enum class DtcKind : std::uint8_t {
  Stored = 0,   ///< Modo 0x03 — gravado, acendeu a luz.
  Pending,      ///< Modo 0x07 — aconteceu uma vez, ainda nao confirmado.
  Permanent,    ///< Modo 0x0A — so a ECU limpa, apos confirmar a correcao.
};

/// Um codigo decodificado.
struct Dtc {
  char text[kDtcTextLen] = {};  ///< "P0143"
  std::uint16_t raw = 0;        ///< Os dois bytes originais.
  DtcKind kind = DtcKind::Stored;
};

/// O resultado de ler uma lista de codigos.
struct DtcList {
  Dtc items[kMaxDtcs] = {};
  std::uint8_t count = 0;
  bool truncated = false;  ///< Havia mais codigos do que cabe aqui.
};

/// Converte os dois bytes crus no texto do codigo.
///
/// @param out  recebe algo como "P0143"; sempre terminado em nulo.
/// @return false se `out` for invalido. O codigo 0x0000 NAO e erro: e o
///         preenchimento que a ECU usa para "nenhum codigo", e cabe a quem
///         chama decidir ignora-lo.
bool decode_dtc(std::uint16_t raw, char* out, std::size_t cap);

/// Interpreta a resposta de um pedido de codigos (modos 0x03, 0x07 ou 0x0A).
///
/// O payload sao pares de bytes, um por codigo. Pares zerados sao
/// preenchimento e ficam de fora da lista.
///
/// @param frame  frame ja validado pelo parser.
/// @param kind   de qual modo veio, para rotular os codigos.
DtcList parse_dtc_response(const ParsedFrame& frame, DtcKind kind);

/// O modo OBD2 correspondente a cada tipo.
constexpr std::uint8_t mode_for(DtcKind kind) {
  switch (kind) {
    case DtcKind::Stored:    return 0x03;
    case DtcKind::Pending:   return 0x07;
    case DtcKind::Permanent: return 0x0A;
  }
  return 0x03;
}

/// Nome legivel do tipo, em portugues. Nunca devolve nullptr.
const char* to_string(DtcKind kind);

}  // namespace kanri::obd
