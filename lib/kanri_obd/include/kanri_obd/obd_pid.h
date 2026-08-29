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
//  ⚠️ ARMADILHA DE NOMENCLATURA — MODO nao e PID
//  ---------------------------------------------
//  O numero 0x2E aparece nos dois lugares, significando coisas OPOSTAS:
//
//      MODO 0x2E  = UDS WriteDataByIdentifier  -> ESCRITA. PROIBIDO.
//      PID  0x2E  (dentro do Modo 01)          -> "comando de purga do canister"
//                                                  LEITURA pura. Permitido.
//
//  O mesmo vale para 0x31 e outros. A allowlist de MODOS (safety.h) e a de
//  PIDs (aqui) sao listas independentes, e confundi-las levaria a bloquear uma
//  leitura legitima — ou, muito pior, a liberar uma escrita achando que e PID.
//
//  ALVO: Mitsubishi Lancer 2.0 2014, motor 4B11 (ISO 15765-4, CAN 11 bits,
//  500 kbit/s). Nenhuma montadora implementa todos os PIDs: o que a ECU de
//  fato responde e descoberto em runtime por pid_support.h.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace kanri::obd {

/// Como converter os bytes crus em unidade de engenharia.
///
/// Fica na TABELA, e nao num switch dentro do decodificador. Com ~60 PIDs, um
/// switch viraria centenas de linhas em que e facil colar a formula errada no
/// PID vizinho — e o resultado seria um numero plausivel e errado, o pior tipo
/// de defeito. Aqui a formula fica na mesma linha do PID que ela decodifica.
enum class PidFormula : std::uint8_t {
  None = 0,      ///< Sem grandeza: mapa de bits, status, contadores brutos.
  RawA,          ///< A
  RawAB,         ///< A*256 + B
  PercentA,      ///< A * 100/255           -> 0..100 %
  SignedPercent, ///< (A-128) * 100/128     -> -100..+99,2 % (fuel trim, EGR)
  TempA,         ///< A - 40                -> -40..215 C
  Rpm,           ///< (A*256+B) / 4         -> rpm
  MafRate,       ///< (A*256+B) / 100       -> g/s
  Voltage,       ///< (A*256+B) / 1000      -> V
  TimingAdvance, ///< A/2 - 64              -> graus
  CatalystTemp,  ///< (A*256+B)/10 - 40     -> C
  FuelPressure,  ///< A * 3                 -> kPa
  RailPressure,  ///< (A*256+B) * 0,079     -> kPa
  RailGauge,     ///< (A*256+B) * 10        -> kPa
  FuelRate,      ///< (A*256+B) / 20        -> L/h
  AbsLoad,       ///< (A*256+B) * 100/255   -> %
  EvapPressure,  ///< ((A*256+B) com sinal) / 4 -> Pa
};

/// Metadados de um PID.
struct PidDescriptor {
  std::uint8_t mode;            ///< Sempre 0x01 ou 0x09. Verificado em teste.
  std::uint8_t pid;             ///< Codigo do parametro.
  std::uint8_t expected_bytes;  ///< Bytes de dados que a ECU deve devolver.
  PidFormula formula;           ///< Como converter.
  float min_value;              ///< Faixa fisica aceita: abaixo disso e ruido.
  float max_value;              ///< Acima disso tambem.
  const char* key;              ///< Identificador estavel, para log e API.
  const char* label;            ///< Rotulo curto em PT-BR.
  const char* unit;             ///< Unidade de engenharia.
};

// ---------------------------------------------------------------------------
//  A tabela. `constexpr` = fica na flash, nao gasta RAM.
//
//  As faixas fisicas sao a SEGUNDA barreira do decodificador: um frame pode
//  passar pelo parser e ainda conter valor impossivel, porque e isso que ruido
//  eletrico produz. Ver docs/SAFETY.md.
// ---------------------------------------------------------------------------
inline constexpr PidDescriptor kSupportedPids[] = {
    // -- Mapas de suporte e status (sem grandeza) --------------------------
    {0x01, 0x00, 4, PidFormula::None, 0, 0, "pids_supported_01_20", "Suporte 01-20", ""},
    {0x01, 0x01, 4, PidFormula::None, 0, 0, "monitor_status", "Status monitores", ""},
    {0x01, 0x03, 2, PidFormula::None, 0, 0, "fuel_system_status", "Sist. combustivel", ""},
    {0x01, 0x13, 1, PidFormula::None, 0, 0, "o2_sensors_present", "Sondas O2", ""},
    {0x01, 0x1C, 1, PidFormula::None, 0, 0, "obd_standard", "Norma OBD", ""},
    {0x01, 0x20, 4, PidFormula::None, 0, 0, "pids_supported_21_40", "Suporte 21-40", ""},
    {0x01, 0x40, 4, PidFormula::None, 0, 0, "pids_supported_41_60", "Suporte 41-60", ""},
    {0x01, 0x41, 4, PidFormula::None, 0, 0, "monitor_this_cycle", "Monitores ciclo", ""},
    {0x01, 0x51, 1, PidFormula::None, 0, 0, "fuel_type", "Tipo combustivel", ""},

    // -- Motor --------------------------------------------------------------
    {0x01, 0x04, 1, PidFormula::PercentA,      0,    100,  "engine_load",   "Carga motor",   "%"},
    {0x01, 0x05, 1, PidFormula::TempA,       -40,    150,  "coolant_temp",  "Temp. agua",    "C"},
    {0x01, 0x0B, 1, PidFormula::RawA,          0,    255,  "intake_map",    "Pressao adm.",  "kPa"},
    {0x01, 0x0C, 2, PidFormula::Rpm,           0,   8000,  "engine_rpm",    "Rotacao",       "rpm"},
    {0x01, 0x0D, 1, PidFormula::RawA,          0,    255,  "vehicle_speed", "Velocidade",    "km/h"},
    {0x01, 0x0E, 1, PidFormula::TimingAdvance, -64,    64,  "timing_advance","Avanco",        "deg"},
    {0x01, 0x0F, 1, PidFormula::TempA,       -40,    150,  "intake_temp",   "Temp. ar",      "C"},
    {0x01, 0x10, 2, PidFormula::MafRate,       0, 655.35F, "maf_rate",      "Fluxo ar",      "g/s"},
    {0x01, 0x11, 1, PidFormula::PercentA,      0,    100,  "throttle_pos",  "Borboleta",     "%"},
    {0x01, 0x1F, 2, PidFormula::RawAB,         0,  65535,  "run_time",      "Tempo motor",   "s"},
    {0x01, 0x43, 2, PidFormula::AbsLoad,       0,  25700,  "absolute_load", "Carga abs.",    "%"},
    {0x01, 0x45, 1, PidFormula::PercentA,      0,    100,  "rel_throttle",  "Borboleta rel.","%"},
    {0x01, 0x4C, 1, PidFormula::PercentA,      0,    100,  "cmd_throttle",  "Borb. comand.", "%"},
    {0x01, 0x5C, 1, PidFormula::TempA,       -40,    215,  "oil_temp",      "Temp. oleo",    "C"},
    {0x01, 0x5D, 2, PidFormula::None,          0,      0,  "inj_timing",    "Avanco inj.",   "deg"},

    // -- Mistura e combustivel ---------------------------------------------
    // Fuel trim negativo = a ECU esta TIRANDO combustivel (mistura rica);
    // positivo = adicionando (mistura pobre). E o primeiro lugar onde uma
    // entrada de ar falsa aparece.
    {0x01, 0x06, 1, PidFormula::SignedPercent, -100, 99.3F, "stft_b1", "Ajuste curto B1", "%"},
    {0x01, 0x07, 1, PidFormula::SignedPercent, -100, 99.3F, "ltft_b1", "Ajuste longo B1", "%"},
    {0x01, 0x08, 1, PidFormula::SignedPercent, -100, 99.3F, "stft_b2", "Ajuste curto B2", "%"},
    {0x01, 0x09, 1, PidFormula::SignedPercent, -100, 99.3F, "ltft_b2", "Ajuste longo B2", "%"},
    {0x01, 0x0A, 1, PidFormula::FuelPressure,     0,   765, "fuel_pressure", "Pressao comb.", "kPa"},
    {0x01, 0x22, 2, PidFormula::RailPressure,     0,  5178, "rail_pressure", "Pressao rail",  "kPa"},
    {0x01, 0x23, 2, PidFormula::RailGauge,        0, 655350, "rail_gauge",   "Rail (abs.)",   "kPa"},
    {0x01, 0x2F, 1, PidFormula::PercentA,         0,   100, "fuel_level",    "Combustivel",   "%"},
    {0x01, 0x52, 1, PidFormula::PercentA,         0,   100, "ethanol_pct",   "Etanol",        "%"},
    {0x01, 0x5E, 2, PidFormula::FuelRate,         0,  3276, "fuel_rate",     "Consumo",       "L/h"},

    // -- Emissoes -----------------------------------------------------------
    // ATENCAO: o PID 0x2E aqui e "comando de purga do canister" — LEITURA.
    // Nao confundir com o MODO 0x2E (UDS WriteDataByIdentifier), que e
    // escrita e esta proibido em safety.h. Ver o aviso no topo deste arquivo.
    {0x01, 0x2C, 1, PidFormula::PercentA,      0,   100, "cmd_egr",      "EGR comandado", "%"},
    {0x01, 0x2D, 1, PidFormula::SignedPercent, -100, 99.3F, "egr_error", "Erro EGR",      "%"},
    {0x01, 0x2E, 1, PidFormula::PercentA,      0,   100, "evap_purge",   "Purga canister","%"},
    {0x01, 0x32, 2, PidFormula::EvapPressure, -8192, 8192, "evap_pressure","Pressao evap.", "Pa"},
    {0x01, 0x33, 1, PidFormula::RawA,          0,   255, "baro_pressure","Pressao atm.",  "kPa"},
    {0x01, 0x3C, 2, PidFormula::CatalystTemp, -40, 6513, "cat_temp_b1s1","Cat. B1S1",     "C"},
    {0x01, 0x3D, 2, PidFormula::CatalystTemp, -40, 6513, "cat_temp_b2s1","Cat. B2S1",     "C"},
    {0x01, 0x3E, 2, PidFormula::CatalystTemp, -40, 6513, "cat_temp_b1s2","Cat. B1S2",     "C"},
    {0x01, 0x3F, 2, PidFormula::CatalystTemp, -40, 6513, "cat_temp_b2s2","Cat. B2S2",     "C"},

    // -- Eletrica e ambiente -------------------------------------------------
    {0x01, 0x42, 2, PidFormula::Voltage, 0,  30, "module_voltage", "Tensao ECU",  "V"},
    {0x01, 0x46, 1, PidFormula::TempA, -40, 150, "ambient_temp",   "Temp. amb.",  "C"},

    // -- Contadores desde a ultima limpeza de codigos -----------------------
    // Uteis no diagnostico: "o defeito voltou depois de quantos km?"
    {0x01, 0x21, 2, PidFormula::RawAB, 0, 65535, "dist_mil_on",   "Dist. c/ luz",   "km"},
    {0x01, 0x30, 1, PidFormula::RawA,  0,   255, "warmups",       "Aquecimentos",   ""},
    {0x01, 0x31, 2, PidFormula::RawAB, 0, 65535, "dist_cleared",  "Dist. p/ limpar","km"},
    {0x01, 0x4D, 2, PidFormula::RawAB, 0, 65535, "time_mil_on",   "Tempo c/ luz",   "min"},
    {0x01, 0x4E, 2, PidFormula::RawAB, 0, 65535, "time_cleared",  "Tempo p/ limpar","min"},

    // -- Modo 09: informacao do veiculo (leitura pura) ----------------------
    // VIN vem em varios frames; o suporte multi-frame entra depois.
    {0x09, 0x02, 17, PidFormula::None, 0, 0, "vin", "Chassi", ""},
};

/// Quantos PIDs a tabela tem.
inline constexpr std::size_t kSupportedPidCount =
    sizeof(kSupportedPids) / sizeof(kSupportedPids[0]);

/// Busca um PID na tabela.
/// @return ponteiro para a entrada, ou nullptr se nao estiver na allowlist.
const PidDescriptor* find_pid(std::uint8_t mode, std::uint8_t pid);

/// Confere se um frame validado tem o tamanho de payload que a tabela espera.
/// Uma resposta com o tamanho errado, mesmo bem formada, e suspeita: pode ser
/// resposta de outra ECU ou de outro pedido.
bool has_expected_length(std::uint8_t mode, std::uint8_t pid,
                         std::uint8_t actual_length);

}  // namespace kanri::obd
