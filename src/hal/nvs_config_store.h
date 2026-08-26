#pragma once
// ============================================================================
//  NvsConfigStore — persistencia na flash (ESQUELETO, real na v0.2)
// ============================================================================
//  NVS = Non-Volatile Storage, a area da flash do ESP32 para dados que
//  sobrevivem ao desligamento. No framework Arduino, a lib `Preferences`
//  embrulha isso numa API simples de chave/valor.
//
//  STATUS v0.1: os metodos existem, compilam e devolvem false. O firmware
//  entao roda com default_settings() — o caminho fail-safe, que fica
//  exercitado desde o primeiro dia.
//
//  A implementacao da v0.2 sera, em essencia:
//
//      Preferences prefs;
//      prefs.begin("kanri", false);
//      prefs.putBytes("settings", &settings, sizeof(settings));
//      prefs.getBytes("settings", &out, sizeof(out));
//
//  ...seguido SEMPRE de config::clamp_to_valid(out), porque bytes vindos da
//  flash sao entrada nao confiavel como qualquer outra.
// ============================================================================

#include "kanri_config/i_config_store.h"

namespace kanri::hal {

class NvsConfigStore final : public config::IConfigStore {
 public:
  bool begin() override;
  bool load(config::KanriSettings& out) override;
  bool save(const config::KanriSettings& settings) override;
  bool clear() override;

 private:
  bool ready_ = false;
};

}  // namespace kanri::hal
