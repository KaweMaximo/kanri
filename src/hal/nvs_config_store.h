#pragma once
// ============================================================================
//  NvsConfigStore — configuracao gravada na flash do ESP32
// ============================================================================
//  NVS ("Non-Volatile Storage") e a area da flash reservada para dados que
//  sobrevivem ao desligamento. A lib `Preferences` do Arduino embrulha isso
//  numa API de chave/valor.
//
//  Gravamos a struct inteira como um blob unico, e nao campo a campo. Duas
//  razoes: e uma escrita so (menos desgaste da flash, que tem ciclos
//  contados) e ou tudo grava, ou nada grava — nao existe estado intermediario
//  com metade dos campos novos e metade antigos.
//
//  O PRECO DISSO e que o layout da struct vira formato de arquivo. Se alguem
//  reordenar um campo, os bytes gravados passam a ser lidos errado. E por isso
//  que KanriSettings carrega `schema_version`: quando o layout muda, a versao
//  sobe, e clamp_to_valid() reconhece o dado antigo e volta aos padroes em vez
//  de interpretar bytes velhos com o layout novo.
//
//  E POR ISSO QUE TODA LEITURA PASSA POR clamp_to_valid(). Bytes vindos da
//  flash sao entrada nao confiavel como qualquer outra: uma queda de tensao no
//  meio de uma gravacao — comum em 12 V automotivo — deixa lixo aqui.
// ============================================================================

#include <Preferences.h>

#include "kanri_config/i_config_store.h"

namespace kanri::hal {

class NvsConfigStore final : public config::IConfigStore {
 public:
  bool begin() override;
  bool load(config::KanriSettings& out) override;
  bool save(const config::KanriSettings& settings) override;
  bool clear() override;

  /// true quando a ultima leitura precisou corrigir algum campo. Util para
  /// avisar no log que a flash trouxe algo estranho.
  bool last_load_was_corrected() const { return corrigido_; }

 private:
  Preferences prefs_;
  bool ready_ = false;
  bool corrigido_ = false;
};

}  // namespace kanri::hal
