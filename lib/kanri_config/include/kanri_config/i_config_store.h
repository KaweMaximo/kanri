#pragma once
// ============================================================================
//  kanri_config/i_config_store.h — Porta de persistencia
// ============================================================================
//  A logica pede "carrega" e "salva" sem saber onde os bytes moram.
//    - No ESP32 -> src/hal/nvs_config_store.h (usa a lib Preferences)
//    - Nos testes -> test/helpers/fake_config_store.h (um struct em RAM)
//
//  Todo metodo devolve bool em vez de lancar excecao: excecoes sao evitadas em
//  firmware porque consomem flash e RAM, e porque o erro precisa ser tratado
//  na hora, ali mesmo.
// ============================================================================

#include "kanri_config/settings.h"

namespace kanri::config {

class IConfigStore {
 public:
  virtual ~IConfigStore() = default;

  /// Abre o armazenamento. Chame uma vez no boot.
  virtual bool begin() = 0;

  /// Le a configuracao gravada.
  /// @param out  em caso de false, DEVE conter default_settings().
  ///             Assim o chamador nunca fica com uma struct meio preenchida.
  /// @return false se nao havia nada gravado ou se estava corrompido.
  virtual bool load(KanriSettings& out) = 0;

  /// Grava a configuracao. O chamador ja deve ter validado.
  virtual bool save(const KanriSettings& settings) = 0;

  /// Apaga tudo, voltando ao estado de fabrica.
  virtual bool clear() = 0;
};

}  // namespace kanri::config
