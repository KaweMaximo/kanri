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

// --- Os UNICOS dois modos permitidos -------------------------------------
constexpr std::uint8_t kModeCurrentData = 0x01;  ///< Dados do instante (RPM, temp...)
constexpr std::uint8_t kModeVehicleInfo = 0x09;  ///< Info do veiculo (VIN, calibracao)

// --- Modos explicitamente PROIBIDOS (aqui apenas para documentar) --------
//  0x02 dados do freeze frame  (leitura, mas fora de escopo)
//  0x03 ler DTCs               (leitura, mas fora de escopo)
//  0x04 LIMPAR DTCs            <-- ESCRITA. Apaga historico de falhas.
//  0x05 resultados de teste O2  (fora de escopo)
//  0x06 resultados de teste     (fora de escopo)
//  0x07 DTCs pendentes          (fora de escopo)
//  0x08 CONTROLE DE OPERACAO   <-- ESCRITA. Comanda atuadores.
//  0x0A DTCs permanentes        (fora de escopo)
//  0x2E/0x2F/0x31/0x3E UDS     <-- ESCRITA/rotinas. Fora do OBD2 padrao.

/// Verdadeiro somente para 0x01 e 0x09. `constexpr` para que o compilador
/// possa avaliar em tempo de compilacao onde o modo for constante.
constexpr bool is_read_only_mode(std::uint8_t mode) {
  return mode == kModeCurrentData || mode == kModeVehicleInfo;
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
