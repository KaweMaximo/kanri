#include "kanri_core/telemetry.h"

namespace kanri::core {

void invalidate_all(TelemetrySnapshot& snapshot) {
  // Zera apenas a procedencia, nao os contadores de saude: eles contam a
  // historia da sessao e sao usados para diagnostico na tela de erro.
  TelemetryValue* values[] = {
      &snapshot.engine_rpm,        &snapshot.coolant_temp_c,
      &snapshot.intake_temp_c,     &snapshot.map_kpa,
      &snapshot.battery_voltage_v, &snapshot.vehicle_speed_kmh,
      &snapshot.engine_load_pct,   &snapshot.throttle_pct,
      &snapshot.fuel_level_pct,
  };
  for (TelemetryValue* v : values) {
    v->valid = false;
    v->value = 0.0F;
  }
}

}  // namespace kanri::core
