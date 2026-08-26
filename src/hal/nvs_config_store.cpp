#include "nvs_config_store.h"

namespace kanri::hal {

bool NvsConfigStore::begin() {
  // TODO(v0.2): prefs_.begin("kanri", /*readOnly=*/false)
  ready_ = false;
  return ready_;
}

bool NvsConfigStore::load(config::KanriSettings& out) {
  // TODO(v0.2): getBytesLength + getBytes + clamp_to_valid.
  //
  // O contrato do IConfigStore exige que, ao devolver false, `out` fique com
  // os padroes de fabrica — nunca com uma struct meio preenchida. Cumprir o
  // contrato ja no esqueleto evita que o chamador tenha um caminho especial
  // "so enquanto nao esta implementado".
  out = config::default_settings();
  return false;
}

bool NvsConfigStore::save(const config::KanriSettings& settings) {
  // TODO(v0.2): putBytes.
  (void)settings;
  return false;
}

bool NvsConfigStore::clear() {
  // TODO(v0.2): prefs_.clear().
  return false;
}

}  // namespace kanri::hal
