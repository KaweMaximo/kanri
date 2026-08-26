#pragma once
// ============================================================================
//  FakeConfigStore — "flash" em memoria RAM
// ============================================================================
//  Permite testar os caminhos de erro que sao quase impossiveis de provocar
//  na flash de verdade: leitura falhando, escrita falhando, dados corrompidos.
// ============================================================================

#include "kanri_config/i_config_store.h"

namespace kanri::test {

class FakeConfigStore final : public config::IConfigStore {
 public:
  bool begin() override { return begin_ok_; }

  bool load(config::KanriSettings& out) override {
    if (!has_data_ || !load_ok_) {
      // Contrato do IConfigStore: em caso de falha, `out` fica com os
      // valores de fabrica — nunca com uma struct meio preenchida.
      out = config::default_settings();
      return false;
    }
    out = stored_;
    return true;
  }

  bool save(const config::KanriSettings& settings) override {
    if (!save_ok_) return false;
    stored_ = settings;
    has_data_ = true;
    return true;
  }

  bool clear() override {
    has_data_ = false;
    return true;
  }

  // --- Controle a partir do teste ---------------------------------------
  void preload(const config::KanriSettings& settings) {
    stored_ = settings;
    has_data_ = true;
  }
  void fail_begin() { begin_ok_ = false; }
  void fail_load() { load_ok_ = false; }
  void fail_save() { save_ok_ = false; }
  const config::KanriSettings& stored() const { return stored_; }

 private:
  config::KanriSettings stored_{};
  bool has_data_ = false;
  bool begin_ok_ = true;
  bool load_ok_ = true;
  bool save_ok_ = true;
};

}  // namespace kanri::test
