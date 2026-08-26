#pragma once
// ============================================================================
//  kanri_obd/obd_pid.h — Catalogo de PIDs (somente leitura)
// ============================================================================
//  PID = "Parameter ID". E o endereco de uma grandeza dentro de um modo OBD2.
//  Ex.: Modo 01 + PID 0x0C = rotacao do motor.
//
//  Esta tabela e uma ALLOWLIST: e a lista fechada do que o Kanri sabe pedir.
//  Nao existe caminho no codigo para pedir um PID que nao esteja aqui.
//
//  ALVO: Mitsubishi Lancer 2.0 2014, motor 4B11.
//  Esse carro usa ISO 15765-4 (CAN, 11 bits, 500 kbit/s) — o padrao de
//  praticamente todo veiculo pos-2008. O ELM327 detecta isso sozinho com
//  o comando ATSP0.
//
//  IMPORTANTE: nao existe garantia de que a ECU suporte TODOS estes PIDs.
//  Cada montadora implementa um subconjunto. A forma certa de descobrir e
//  perguntar a ECU: os PIDs 0x00, 0x20 e 0x40 devolvem um mapa de bits com
//  "quais PIDs eu suporto". Fazer essa descoberta em runtime (em vez de
//  chumbar uma lista) esta no roadmap da v0.2 — ver docs/ROADMAP.md.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace kanri::obd {

/// Metadados de um PID. Sem formula de conversao ainda: a decodificacao para
/// unidades de engenharia entra na v0.2, junto com seus proprios testes.
struct PidDescriptor {
  std::uint8_t mode;            ///< Sempre 0x01 ou 0x09. Verificado em teste.
  std::uint8_t pid;             ///< Codigo do parametro.
  std::uint8_t expected_bytes;  ///< Bytes de dados que a ECU deve devolver.
  const char* key;              ///< Identificador estavel para codigo/log.
  const char* label;            ///< Rotulo curto em PT-BR para o display.
  const char* unit;             ///< Unidade de engenharia ("rpm", "C", "V"...).
};

// ---------------------------------------------------------------------------
//  A tabela. `constexpr` = fica na flash, nao gasta RAM.
// ---------------------------------------------------------------------------
inline constexpr PidDescriptor kSupportedPids[] = {
    // -- Mapas de suporte: usados para descobrir o que a ECU implementa ----
    {0x01, 0x00, 4, "pids_supported_01_20", "Suporte 01-20", ""},
    {0x01, 0x20, 4, "pids_supported_21_40", "Suporte 21-40", ""},
    {0x01, 0x40, 4, "pids_supported_41_60", "Suporte 41-60", ""},

    // -- Grandezas do motor -------------------------------------------------
    {0x01, 0x04, 1, "engine_load",      "Carga motor",  "%"},
    {0x01, 0x05, 1, "coolant_temp",     "Temp. agua",   "C"},
    {0x01, 0x0B, 1, "intake_map",       "Pressao adm.", "kPa"},
    {0x01, 0x0C, 2, "engine_rpm",       "Rotacao",      "rpm"},
    {0x01, 0x0D, 1, "vehicle_speed",    "Velocidade",   "km/h"},
    {0x01, 0x0E, 1, "timing_advance",   "Avanco",       "deg"},
    {0x01, 0x0F, 1, "intake_temp",      "Temp. ar",     "C"},
    {0x01, 0x10, 2, "maf_rate",         "Fluxo ar",     "g/s"},
    {0x01, 0x11, 1, "throttle_pos",     "Borboleta",    "%"},
    {0x01, 0x1F, 2, "run_time",         "Tempo motor",  "s"},

    // -- Combustivel e eletrica --------------------------------------------
    {0x01, 0x2F, 1, "fuel_level",       "Combustivel",  "%"},
    {0x01, 0x42, 2, "module_voltage",   "Tensao ECU",   "V"},
    {0x01, 0x43, 2, "absolute_load",    "Carga abs.",   "%"},
    {0x01, 0x46, 1, "ambient_temp",     "Temp. amb.",   "C"},

    // -- Provavelmente NAO suportado no 4B11; mantido para descoberta ------
    {0x01, 0x5C, 1, "oil_temp",         "Temp. oleo",   "C"},

    // -- Modo 09: informacao do veiculo (leitura pura) ---------------------
    // VIN vem em varios frames; o suporte multi-frame entra na v0.2.
    {0x09, 0x02, 17, "vin", "Chassi", ""},
};

/// Quantos PIDs a tabela tem. `sizeof` de array em tempo de compilacao.
inline constexpr std::size_t kSupportedPidCount =
    sizeof(kSupportedPids) / sizeof(kSupportedPids[0]);

/// Busca um PID na tabela.
/// @return ponteiro para a entrada, ou nullptr se nao estiver na allowlist.
const PidDescriptor* find_pid(std::uint8_t mode, std::uint8_t pid);

/// Confere se um frame validado tem o tamanho de payload que a tabela espera.
/// Uma resposta com o tamanho errado, mesmo bem formada, e suspeita: pode ser
/// resposta de outra ECU ou de outro pedido. Rejeitar e o correto.
bool has_expected_length(std::uint8_t mode, std::uint8_t pid,
                         std::uint8_t actual_length);

}  // namespace kanri::obd
