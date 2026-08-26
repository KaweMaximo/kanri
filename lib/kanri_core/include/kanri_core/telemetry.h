#pragma once
// ============================================================================
//  kanri_core/telemetry.h — O "estado do carro" em memoria
// ============================================================================
//  Este e o tipo compartilhado entre quem PRODUZ os dados (kanri_obd) e quem
//  CONSOME (kanri_display). Ninguem escreve no display lendo do OBD direto:
//  o OBD preenche este snapshot, o display le este snapshot. Assim os dois
//  modulos nao se conhecem e podem ser testados isoladamente.
//
//  Note o campo `valid` em cada medida. Isso e um requisito de SEGURANCA:
//  um valor nunca lido, ou lido de uma resposta corrompida, JAMAIS pode ser
//  exibido como se fosse verdade. Ver docs/SAFETY.md.
// ============================================================================

#include <cstdint>

namespace kanri::core {

/// Uma unica medida do veiculo, com procedencia.
struct TelemetryValue {
  float value = 0.0F;             ///< Valor em unidade de engenharia (rpm, C, V...)
  bool valid = false;             ///< false = nunca lido OU ultima leitura rejeitada
  std::uint32_t updated_at_ms = 0;  ///< Quando foi atualizado (IClock::now_ms)
};

/// Idade da medida. Use para decidir "esse dado ficou velho, mostrar '--'".
inline std::uint32_t value_age_ms(const TelemetryValue& v, std::uint32_t now_ms) {
  return v.valid ? (now_ms - v.updated_at_ms) : UINT32_MAX;
}

/// Retrato completo do veiculo num instante.
/// Estrutura plana e de tamanho fixo de proposito: sem alocacao dinamica,
/// que e uma boa pratica obrigatoria em firmware (evita fragmentacao de heap).
struct TelemetrySnapshot {
  TelemetryValue engine_rpm;        ///< PID 0x0C  — rpm
  TelemetryValue coolant_temp_c;    ///< PID 0x05  — graus Celsius
  TelemetryValue intake_temp_c;     ///< PID 0x0F  — graus Celsius
  TelemetryValue map_kpa;           ///< PID 0x0B  — kPa absolutos
  TelemetryValue battery_voltage_v; ///< PID 0x42  — Volts
  TelemetryValue vehicle_speed_kmh; ///< PID 0x0D  — km/h
  TelemetryValue engine_load_pct;   ///< PID 0x04  — %
  TelemetryValue throttle_pct;      ///< PID 0x11  — %
  TelemetryValue fuel_level_pct;    ///< PID 0x2F  — %

  // Contadores de saude do link. Usados pela politica de degradacao.
  std::uint32_t frames_ok = 0;        ///< respostas validas acumuladas
  std::uint32_t frames_rejected = 0;  ///< respostas descartadas pelo parser
  std::uint32_t last_ok_ms = 0;       ///< instante da ultima resposta valida
};

/// Marca TODAS as medidas como invalidas, preservando os contadores.
/// Chamado ao perder o link: melhor mostrar "--" do que um valor antigo.
void invalidate_all(TelemetrySnapshot& snapshot);

}  // namespace kanri::core
