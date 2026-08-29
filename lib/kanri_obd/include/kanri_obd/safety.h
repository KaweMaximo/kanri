#pragma once
// ============================================================================
//  kanri_obd/safety.h — O PORTAO READ-ONLY
// ============================================================================
//  ESTE E O ARQUIVO MAIS IMPORTANTE DO PROJETO.
//
//  O barramento OBD2 nao e so um "cano de leitura". Pelo mesmo conector e
//  possivel apagar codigos de falha, comandar atuadores, reprogramar modulos e
//  colocar a ECU em modo de diagnostico. Com o carro em movimento, isso vai de
//  irritante a perigoso.
//
//  A regra do Kanri: o firmware NUNCA escreve nada na ECU. Somente
//  Modo 01 (dados do instante) e Modo 09 (informacao do veiculo).
//
//  Documentar essa regra no README nao basta — documento nao compila. Aqui ela
//  virou CODIGO: toda requisicao passa por check_obd_request() /
//  check_at_command() antes de ir para o transporte, e essas funcoes tem
//  testes unitarios que rodam em todo Pull Request. Se alguem (inclusive uma
//  IA numa sessao futura) tentar adicionar o Modo 04, o teste fica vermelho.
//
//  Ver docs/SAFETY.md para o racional completo.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace kanri::obd {

// --- Os modos de LEITURA permitidos --------------------------------------
//
//  A linha que separa o que pode do que nao pode nao e "quantos modos", e sim
//  LER versus ALTERAR. Todos os modos abaixo apenas perguntam ao carro; nenhum
//  muda um bit sequer dentro da ECU.
//
constexpr std::uint8_t kModeCurrentData = 0x01;   ///< Dados do instante (RPM, temp...)
constexpr std::uint8_t kModeFreezeFrame = 0x02;   ///< Condicoes no instante da falha
constexpr std::uint8_t kModeStoredDtc = 0x03;     ///< Codigos de falha gravados
constexpr std::uint8_t kModeO2Test = 0x05;        ///< Resultados de teste da sonda O2
constexpr std::uint8_t kModeMonitorTest = 0x06;   ///< Monitores de bordo
constexpr std::uint8_t kModePendingDtc = 0x07;    ///< Codigos pendentes
constexpr std::uint8_t kModeVehicleInfo = 0x09;   ///< Info do veiculo (VIN, calibracao)
constexpr std::uint8_t kModePermanentDtc = 0x0A;  ///< Codigos permanentes

// --- Os modos PROIBIDOS, e o motivo de cada um ---------------------------
//
//  0x04 LIMPAR DTCs           <-- ESCRITA. Apaga o historico de falhas do
//                                 carro. Irreversivel, e destroi justamente a
//                                 informacao que um diagnostico usaria.
//  0x08 CONTROLE DE OPERACAO  <-- ESCRITA. Comanda atuadores. Com o carro
//                                 andando, isso e risco fisico.
//  0x2E/0x31/0x3E UDS         <-- ESCRITA/rotinas. Podem reprogramar modulos.
//
//  E o 0x22 (UDS ReadDataByIdentifier), que leria PIDs proprietarios da
//  montadora? Ele e LEITURA, mas na pratica exige o comando ATSH para
//  enderecar a ECU — e o ATSH permite montar QUALQUER quadro CAN, inclusive
//  de escrita. Liberar um para ganhar o outro trocaria uma garantia
//  estrutural por disciplina. Fica de fora por decisao, nao por esquecimento.

/// Verdadeiro para os modos de leitura permitidos.
///
/// `constexpr` para que o compilador avalie em tempo de compilacao onde o
/// modo for constante — e para que a lista viva num lugar so.
constexpr bool is_read_only_mode(std::uint8_t mode) {
  return mode == kModeCurrentData || mode == kModeFreezeFrame ||
         mode == kModeStoredDtc || mode == kModeO2Test ||
         mode == kModeMonitorTest || mode == kModePendingDtc ||
         mode == kModeVehicleInfo || mode == kModePermanentDtc;
}

/// Contra qual catalogo de PIDs este modo deve ser validado?
///
/// O freeze frame (0x02) usa EXATAMENTE os mesmos PIDs do modo 0x01 — ele
/// devolve as mesmas grandezas, congeladas no instante em que a falha
/// ocorreu. Por isso aponta para o catalogo do 0x01.
///
/// Os modos 0x05 e 0x06 usam identificadores de TESTE, que sao uma numeracao
/// propria e nao PIDs. Nao ha catalogo para eles: devolvem 0, e a checagem de
/// PID e pulada. Isso NAO afrouxa a seguranca — a barreira que impede escrita
/// e a do modo, e ela ja passou. O catalogo e defesa em profundidade contra
/// pedir coisa inexistente, que custa banda, nao risco.
///
/// @return o modo cujo catalogo consultar, ou 0 se nao houver catalogo.
constexpr std::uint8_t pid_catalog_mode(std::uint8_t mode) {
  if (mode == kModeCurrentData || mode == kModeFreezeFrame) {
    return kModeCurrentData;
  }
  if (mode == kModeVehicleInfo) return kModeVehicleInfo;
  return 0;
}

/// Este modo recebe um PID junto?
///
/// Os modos de codigo de falha (0x03, 0x07, 0x0A) sao pedidos SOZINHOS — a
/// ECU devolve a lista inteira do que tem. Mandar um PID junto deles seria
/// malformado, e alguns adaptadores respondem "?" a isso.
constexpr bool mode_takes_pid(std::uint8_t mode) {
  return mode != kModeStoredDtc && mode != kModePendingDtc &&
         mode != kModePermanentDtc;
}

/// Resultado da checagem de seguranca de uma requisicao.
enum class RequestVerdict : std::uint8_t {
  Allowed = 0,          ///< Pode ir para o barramento.
  ForbiddenMode,        ///< Modo diferente de 01/09. Bloqueado.
  ForbiddenPid,         ///< PID que nao esta no catalogo de obd_pid.h.
  ForbiddenAtCommand,   ///< Comando AT fora da allowlist. Bloqueado.
  Malformed,            ///< Entrada vazia, longa demais ou com lixo.
};

/// Portao para requisicoes OBD (modo + PID).
/// Falha fechado: qualquer duvida devolve algo diferente de Allowed.
RequestVerdict check_obd_request(std::uint8_t mode, std::uint8_t pid);

/// Portao para comandos AT (configuracao do proprio adaptador ELM327).
///
/// Usa ALLOWLIST, nao blocklist. Blocklist e o padrao errado aqui: se
/// esquecermos de listar um comando perigoso, ele passa. Com allowlist, o
/// esquecimento apenas bloqueia algo inofensivo — falha para o lado seguro.
///
/// @param cmd  comando sem CR, ex.: "ATSP0". Nao precisa ser maiusculo.
/// @param len  tamanho de `cmd` sem o terminador nulo.
RequestVerdict check_at_command(const char* cmd, std::size_t len);

/// Nome legivel do veredito. Nunca devolve nullptr.
const char* to_string(RequestVerdict v);

}  // namespace kanri::obd
