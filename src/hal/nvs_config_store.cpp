#include "nvs_config_store.h"

#include <Arduino.h>

namespace kanri::hal {
namespace {

// Espaco de nomes na NVS. Maximo de 15 caracteres.
constexpr const char* kNamespace = "kanri";
// Chave do blob com a struct inteira.
constexpr const char* kKey = "settings";

}  // namespace

bool NvsConfigStore::begin() {
  // false = leitura e escrita.
  ready_ = prefs_.begin(kNamespace, false);
  return ready_;
}

bool NvsConfigStore::load(config::KanriSettings& out) {
  // Contrato do IConfigStore: em caso de falha, `out` fica com os padroes de
  // fabrica — nunca meio preenchido. Comecamos por eles.
  out = config::default_settings();
  corrigido_ = false;
  if (!ready_) return false;

  const std::size_t tamanho = prefs_.getBytesLength(kKey);
  if (tamanho != sizeof(config::KanriSettings)) {
    // Nada gravado, ou gravado por um firmware com outro layout. Em ambos os
    // casos os padroes sao a resposta certa.
    return false;
  }

  config::KanriSettings lido{};
  const std::size_t obtidos = prefs_.getBytes(kKey, &lido, sizeof(lido));
  if (obtidos != sizeof(lido)) return false;

  // A FLASH E ENTRADA NAO CONFIAVEL. Uma queda de tensao durante a gravacao
  // deixa bytes pela metade aqui. clamp_to_valid() garante que, daqui para
  // frente, a configuracao e utilizavel.
  corrigido_ = config::clamp_to_valid(lido);
  out = lido;
  return true;
}

bool NvsConfigStore::save(const config::KanriSettings& settings) {
  if (!ready_) return false;

  // Nunca gravamos algo que nao passaria na validacao: seria plantar na
  // flash o problema que a leitura teria de consertar depois.
  config::KanriSettings copia = settings;
  config::clamp_to_valid(copia);

  const std::size_t escritos = prefs_.putBytes(kKey, &copia, sizeof(copia));
  return escritos == sizeof(copia);
}

bool NvsConfigStore::clear() {
  if (!ready_) return false;
  return prefs_.remove(kKey);
}

}  // namespace kanri::hal
