#pragma once
// ============================================================================
//  kanri_obd/adapter_matcher.h — Qual dos dispositivos e o NOSSO adaptador?
// ============================================================================
//  Quando o ESP32 varre o Bluetooth dentro de um carro, ele acha de tudo: o
//  celular do motorista, um fone, a central multimidia, o celular do carro do
//  lado. Alguns desses aparelhos aceitam conexao serial.
//
//  Escolher errado nao e so "nao funciona": e abrir um canal com um aparelho
//  desconhecido e passar a interpretar o que ele responder como se fosse
//  telemetria do motor. Por isso a escolha e uma decisao explicita, com
//  regras escritas — e nao "conecta no primeiro que parecer".
//
//  ENTRADA HOSTIL: o nome de um dispositivo Bluetooth e escolhido por QUEM O
//  ANUNCIA. Nada impede um aparelho de se chamar "OBDII", nem de anunciar um
//  nome sem terminador ou com bytes de controle. Tratamos esses nomes com o
//  mesmo rigor das respostas do ELM327 — ver docs/SAFETY.md.
//
//  Este arquivo e codigo puro: nao conhece BluetoothSerial nem ESP32. O
//  adaptador de hardware (src/hal/bt_serial_transport) faz a varredura e
//  entrega a lista aqui. Por isso da para testar "o que acontece se dois
//  aparelhos se chamarem OBDII" sem nenhum aparelho.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace kanri::obd {

/// Limites de tamanho. Iguais aos de KanriSettings, de proposito: o alvo
/// configurado e o nome varrido precisam caber no mesmo espaco para poderem
/// ser comparados sem truncamento surpresa.
constexpr std::size_t kMaxDeviceNameLen = 32;
constexpr std::size_t kMaxDeviceMacLen = 18;  ///< "AA:BB:CC:DD:EE:FF" + nulo

/// Quantos resultados de varredura guardamos. Um limite explicito evita que
/// um ambiente cheio de aparelhos (um estacionamento, por exemplo) consuma
/// RAM sem controle.
constexpr std::size_t kMaxScanResults = 20;

/// Um dispositivo visto na varredura.
struct DiscoveredDevice {
  char name[kMaxDeviceNameLen];  ///< Pode vir vazio: nem todo aparelho anuncia nome.
  char mac[kMaxDeviceMacLen];    ///< "AA:BB:CC:DD:EE:FF".
  std::int8_t rssi;              ///< Potencia do sinal em dBm. Menos negativo = mais perto.
};

/// Por que a escolha deu certo ou errado.
enum class MatchResult : std::uint8_t {
  Found = 0,        ///< Achamos exatamente um alvo. `index` e valido.
  NoDevices,        ///< A varredura nao devolveu nada.
  NoTarget,         ///< Configuracao sem nome e sem MAC: nao ha o que procurar.
  MacNotFound,      ///< MAC configurado, mas nenhum dispositivo bate com ele.
  NameNotFound,     ///< Nome configurado, mas nenhum dispositivo bate com ele.
};

/// Resultado da escolha.
struct MatchOutcome {
  MatchResult result = MatchResult::NoDevices;
  int index = -1;  ///< Indice em `devices`, ou -1 quando nao houve escolha.

  bool found() const { return result == MatchResult::Found && index >= 0; }
};

/// Escolhe qual dispositivo da varredura e o adaptador configurado.
///
/// REGRAS, nesta ordem:
///
///  1. **MAC tem prioridade e e exclusivo.** Se `target_mac` estiver
///     preenchido, so casamos por MAC. Nao caimos para o nome se o MAC nao
///     aparecer — quem fixou um MAC quer AQUELE aparelho, e conectar em outro
///     que por acaso tenha o mesmo nome seria justamente o erro que fixar o
///     MAC pretendia evitar.
///
///  2. **Sem MAC, casamos por nome exato**, ignorando maiusculas/minusculas.
///     Nao usamos "contem" nem prefixo: "OBDII_FAKE" nao pode casar com
///     "OBDII".
///
///  3. **Empate no nome: vence o melhor sinal (RSSI maior).** Se dois
///     aparelhos se chamam "OBDII", o mais proximo tende a ser o do proprio
///     carro, e nao o do veiculo ao lado. E um desempate por probabilidade,
///     nao uma garantia — por isso, depois de conectar uma vez, vale gravar
///     o MAC nas configuracoes e passar a usar a regra 1.
///
///  4. Dispositivo com nome ou MAC malformado (sem terminador nulo, ou com
///     bytes nao imprimiveis) e **ignorado**, nunca escolhido.
///
/// @param devices      lista da varredura; pode ser nullptr se count for 0.
/// @param count        quantos itens validos ha em `devices`.
/// @param target_name  nome procurado; "" ou nullptr = nao procurar por nome.
/// @param target_mac   MAC procurado; "" ou nullptr = nao procurar por MAC.
MatchOutcome select_adapter(const DiscoveredDevice* devices, std::size_t count,
                            const char* target_name, const char* target_mac);

/// Nome legivel do resultado, para log e display. Nunca devolve nullptr.
const char* to_string(MatchResult result);

}  // namespace kanri::obd
