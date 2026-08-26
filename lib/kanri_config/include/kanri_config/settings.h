#pragma once
// ============================================================================
//  kanri_config/settings.h — As configuracoes do firmware
// ============================================================================
//  NVS ("Non-Volatile Storage") e a area da flash do ESP32 onde guardamos
//  dados que precisam sobreviver a um desligamento — aqui, qual adaptador
//  ELM327 procurar e com que frequencia consultar o carro.
//
//  Duas ideias importantes neste arquivo:
//
//  1) SEPARACAO DADO / ARMAZENAMENTO. A struct e a validacao sao codigo puro
//     (testavel no PC). Quem realmente conversa com a flash e a implementacao
//     de IConfigStore em src/hal/. Trocar NVS por cartao SD depois nao mexe
//     em nada aqui.
//
//  2) FAIL-SAFE. A flash pode corromper (queda de tensao durante a escrita —
//     algo comum em 12V automotivo). O firmware NUNCA opera com valores
//     invalidos: ao carregar, chamamos clamp_to_valid(), que puxa cada campo
//     ruim de volta para uma faixa segura. Nao ligar por causa de config
//     corrompida seria pior. Ver docs/SAFETY.md.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace kanri::config {

// --- Faixas aceitaveis (fonte unica da verdade) --------------------------
constexpr std::uint16_t kMinPollIntervalMs = 50;    ///< Abaixo disso o ELM327 nao acompanha.
constexpr std::uint16_t kMaxPollIntervalMs = 5000;
constexpr std::uint16_t kMinElmTimeoutMs = 200;
constexpr std::uint16_t kMaxElmTimeoutMs = 10000;
constexpr std::uint8_t kMaxBrightness = 100;

/// Versao do formato guardado na flash. Incremente ao mudar a struct: assim
/// o firmware novo reconhece dados gravados por um firmware antigo em vez de
/// interpretar bytes velhos com o layout novo.
constexpr std::uint8_t kSettingsSchemaVersion = 1;

constexpr std::size_t kAdapterNameLen = 32;
constexpr std::size_t kAdapterMacLen = 18;  ///< "AA:BB:CC:DD:EE:FF" + nulo
constexpr std::size_t kAdapterPinLen = 8;

/// Tudo que o usuario pode configurar. Tamanho fixo, sem ponteiros:
/// serializa direto para a flash e nao aloca memoria.
///
/// ATENCAO — ESTA STRUCT E UM POD PURO, DE PROPOSITO.
///
/// Repare que nenhum campo tem valor inicial aqui (nada de `= 200`). Isso e
/// deliberado, por dois motivos:
///
///  1. Ela e gravada e lida da flash byte a byte. Para o C++ permitir
///     memcpy/memset em cima dela com seguranca, o tipo precisa ser
///     "trivial" — e um unico valor inicial de membro tiraria essa
///     propriedade. O compilador cobra isso (-Wclass-memaccess).
///  2. Os padroes de fabrica moram em UM lugar so: default_settings().
///     Se estivessem aqui tambem, seriam duas fontes da verdade fadadas a
///     divergir.
///
/// CONSEQUENCIA PRATICA: declarar `KanriSettings s;` deixa a struct com
/// LIXO de memoria. Sempre use `KanriSettings s = default_settings();`
/// (ou `KanriSettings s{};` para zerar) antes de ler qualquer campo.
struct KanriSettings {
  std::uint8_t schema_version;

  char adapter_name[kAdapterNameLen];  ///< Nome Bluetooth, ex.: "OBDII"
  char adapter_mac[kAdapterMacLen];    ///< Opcional; vazio = casar por nome
  char adapter_pin[kAdapterPinLen];    ///< PIN de pareamento, ex.: "1234"

  std::uint16_t poll_interval_ms;  ///< Intervalo entre leituras de PID
  std::uint16_t elm_timeout_ms;    ///< Espera maxima por resposta
  std::uint8_t display_brightness; ///< 0-100

  /// 1 = km/h e Celsius; 0 = mph e Fahrenheit.
  ///
  /// Por que uint8_t e nao bool? Porque esta struct e gravada byte a byte na
  /// flash. A representacao interna de um `bool` nao e especificada pelo C++,
  /// e LER um bool cujo byte nao seja 0 nem 1 e comportamento indefinido —
  /// exatamente o que acontece com flash corrompida. Com uint8_t, qualquer
  /// byte e legal de ler e podemos normalizar em clamp_to_valid().
  std::uint8_t use_metric_units;
};

// Garantias verificadas em tempo de COMPILACAO. Se alguem adicionar um
// std::string ou um valor inicial de membro nesta struct, o build quebra aqui
// com uma mensagem clara — em vez de dar problema estranho na flash.
static_assert(std::is_trivially_copyable<KanriSettings>::value,
              "KanriSettings precisa ser trivialmente copiavel: ela e "
              "gravada byte a byte na flash");
static_assert(std::is_trivial<KanriSettings>::value,
              "KanriSettings precisa ser um POD trivial: nao adicione valores "
              "iniciais de membro — os padroes ficam em default_settings()");

/// Configuracao de fabrica. Tem de ser sempre valida: e o porto seguro para
/// onde voltamos quando a flash esta vazia ou corrompida.
KanriSettings default_settings();

/// O que pode estar errado numa configuracao.
enum class SettingsError : std::uint8_t {
  None = 0,
  SchemaMismatch,          ///< Gravada por outra versao do firmware.
  AdapterNameEmpty,        ///< Sem nome nem MAC: nao ha como achar o adaptador.
  AdapterNameNotTerminated,///< String sem terminador nulo (flash corrompida).
  PollIntervalOutOfRange,
  ElmTimeoutOutOfRange,
  BrightnessOutOfRange,
  MalformedMac,            ///< Preenchido, mas nao no formato AA:BB:...
  InvalidFlag,             ///< Campo booleano com valor diferente de 0 ou 1.
};

/// Confere a configuracao sem alterar nada.
/// @return SettingsError::None quando esta tudo bem.
SettingsError validate(const KanriSettings& settings);

/// Corrige a configuracao no lugar, puxando cada campo invalido para uma
/// faixa segura ou para o valor de fabrica.
///
/// Depois desta chamada, validate() SEMPRE devolve None. E essa a garantia
/// que permite ao firmware seguir operando com flash corrompida.
///
/// @return true se algo teve de ser corrigido (util para registrar em log).
bool clamp_to_valid(KanriSettings& settings);

/// Nome legivel do erro. Nunca devolve nullptr.
const char* to_string(SettingsError error);

}  // namespace kanri::config
