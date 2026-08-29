#pragma once
// ============================================================================
//  kanri_obd/pid_support.h — Quais PIDs esta ECU realmente implementa?
// ============================================================================
//  Nenhuma montadora implementa todos os PIDs do padrao. O catalogo em
//  obd_pid.h e o que o Kanri SABE PEDIR; nao e o que o Lancer SABE RESPONDER.
//
//  Perguntar e melhor do que chutar. A norma reserva tres PIDs para isso:
//
//      0x00 -> quais dos PIDs 0x01..0x20 eu suporto
//      0x20 -> quais dos PIDs 0x21..0x40
//      0x40 -> quais dos PIDs 0x41..0x60
//
//  Cada um responde 4 bytes = 32 bits, um por PID, do mais significativo para
//  o menos. Para a resposta do PID 0x00:
//
//      bit 31 (0x80 do byte A) -> PID 0x01
//      bit 30                  -> PID 0x02
//      ...
//      bit  0 (0x01 do byte D) -> PID 0x20
//
//  O ULTIMO BIT DE CADA BLOCO E ESPECIAL: ele diz se o PROXIMO bloco existe.
//  Se o bit do PID 0x20 estiver ligado na resposta do 0x00, entao vale a pena
//  perguntar o 0x20. Se nao estiver, parar ali economiza duas consultas ao
//  barramento em toda partida.
//
//  POR QUE ISSO IMPORTA NA PRATICA
//  --------------------------------
//  Sem descoberta, o firmware pede PIDs que a ECU nao tem e recebe "NO DATA"
//  em cada ciclo. Nao quebra nada — o parser trata —, mas desperdica tempo do
//  barramento que poderia estar lendo o que existe. Num rodizio de 5 PIDs com
//  200 ms, um PID inutil custa 20% da banca de leitura.
//
//  Codigo puro: entra o payload de 4 bytes, sai a resposta. Sem hardware.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace kanri::obd {

/// Os PIDs que carregam o mapa de suporte.
constexpr std::uint8_t kSupportPid0 = 0x00;   ///< cobre 0x01..0x20
constexpr std::uint8_t kSupportPid20 = 0x20;  ///< cobre 0x21..0x40
constexpr std::uint8_t kSupportPid40 = 0x40;  ///< cobre 0x41..0x60

/// Payload esperado de um mapa de suporte, em bytes.
constexpr std::uint8_t kSupportPayloadBytes = 4;

/// O que a ECU declarou suportar, no Modo 01.
///
/// Guarda 256 bits (32 bytes) — um por PID possivel. Cabe na pilha e nao
/// precisa de alocacao.
class PidSupport {
 public:
  /// Esquece tudo. Chame ao reconectar: pode ser outro carro.
  void reset();

  /// Registra um bloco do mapa.
  ///
  /// @param base_pid  0x00, 0x20 ou 0x40 — o PID que foi consultado.
  /// @param data      payload da resposta.
  /// @param len       tamanho do payload; precisa ser 4.
  /// @return false se o bloco foi recusado (base invalida, tamanho errado,
  ///         ponteiro nulo). Nesse caso nada e registrado.
  bool apply_block(std::uint8_t base_pid, const std::uint8_t* data,
                   std::uint8_t len);

  /// A ECU declarou suportar este PID?
  ///
  /// Devolve false para PIDs sobre os quais nada foi declarado — inclusive
  /// quando nenhum bloco foi aplicado ainda. Na duvida, "nao suportado" e a
  /// resposta segura: pedir a mais custa banda, deixar de pedir so custa uma
  /// medida.
  bool supports(std::uint8_t pid) const;

  /// Vale a pena consultar o proximo bloco depois deste?
  ///
  /// Responde pelo bit especial no fim do bloco. Perguntar por um bloco que
  /// a ECU nao anuncia so rende "NO DATA".
  bool has_next_block(std::uint8_t base_pid) const;

  /// Quantos PIDs foram declarados suportados.
  std::uint16_t count() const;

  /// Algum bloco chegou a ser aplicado?
  bool any_block_applied() const { return blocos_ != 0; }

 private:
  std::uint8_t bits_[32] = {};  ///< bit N = PID N
  std::uint8_t blocos_ = 0;     ///< mascara dos blocos ja aplicados
};

/// O proximo PID de mapa depois deste, ou 0 se nao houver.
/// 0x00 -> 0x20 -> 0x40 -> 0
std::uint8_t next_support_pid(std::uint8_t base_pid);

/// `pid` e um dos PIDs de mapa de suporte?
constexpr bool is_support_pid(std::uint8_t pid) {
  return pid == kSupportPid0 || pid == kSupportPid20 || pid == kSupportPid40;
}

}  // namespace kanri::obd
