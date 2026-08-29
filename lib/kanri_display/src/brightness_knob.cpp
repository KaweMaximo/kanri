#include "kanri_display/brightness_knob.h"

namespace kanri::display {
namespace {

/// Largura nominal de cada faixa, em contagens do ADC.
constexpr std::uint16_t kFaixa = (kAdcMax + 1) / kKnobLevels;  // 819

/// A fronteira SUPERIOR da faixa `level`.
constexpr std::uint16_t fronteira(std::uint8_t level) {
  return static_cast<std::uint16_t>((level + 1) * kFaixa);
}

// Percentuais dos cinco niveis.
//
// 3 % nao apaga: e o passo mais fraco do MAX7219 (1/32 do ciclo), pensado
// para dirigir a noite sem o painel ofuscar. 100 % e para sol direto, que e
// a referencia do produto (FuelTech WB-O2 Nano).
constexpr std::uint8_t kPercentuais[kKnobLevels] = {3, 12, 30, 60, 100};

}  // namespace

std::uint8_t knob_level_percent(std::uint8_t level) {
  if (level >= kKnobLevels) level = kKnobLevels - 1;
  return kPercentuais[level];
}

std::uint8_t knob_level_for(std::uint16_t raw, std::uint8_t current) {
  if (raw > kAdcMax) raw = kAdcMax;
  if (current >= kKnobLevels) current = kKnobLevels - 1;

  // A histerese e assimetrica de proposito: exigimos margem para SAIR da
  // faixa atual, em qualquer direcao. Sem isso, uma leitura exatamente na
  // fronteira alternaria entre dois niveis a cada amostra.
  //
  // Subir: precisa passar da fronteira de cima COM margem.
  if (current + 1 < kKnobLevels &&
      raw > static_cast<std::uint32_t>(fronteira(current)) + kKnobHysteresis) {
    // Pode ter girado varias faixas de uma vez: encontra a certa.
    std::uint8_t novo = current;
    while (novo + 1 < kKnobLevels &&
           raw > static_cast<std::uint32_t>(fronteira(novo)) + kKnobHysteresis) {
      ++novo;
    }
    return novo;
  }

  // Descer: precisa cair abaixo da fronteira de baixo COM margem.
  if (current > 0) {
    const std::uint16_t abaixo = fronteira(static_cast<std::uint8_t>(current - 1));
    if (raw + kKnobHysteresis < abaixo) {
      std::uint8_t novo = current;
      while (novo > 0 &&
             raw + kKnobHysteresis <
                 fronteira(static_cast<std::uint8_t>(novo - 1))) {
        --novo;
      }
      return novo;
    }
  }

  return current;  // dentro da zona morta: nada muda
}

bool BrightnessKnob::update(std::uint16_t raw) {
  if (raw > kAdcMax) raw = kAdcMax;
  const std::uint8_t sugerido = knob_level_for(raw, nivel_);

  if (sugerido == nivel_) {
    // Voltou para onde ja estava: a tentativa anterior era ruido.
    candidato_ = nivel_;
    confirmacoes_ = 0;
    return false;
  }

  // Longe demais da leitura que abriu a candidatura? Entao nao e uma posicao
  // de potenciometro: e ruido, ou o pino esta solto. Recomeca ancorando aqui.
  const std::uint16_t distancia =
      (raw > ancora_) ? static_cast<std::uint16_t>(raw - ancora_)
                      : static_cast<std::uint16_t>(ancora_ - raw);

  if (sugerido != candidato_ || distancia > kKnobMaxJitter) {
    candidato_ = sugerido;
    ancora_ = raw;
    confirmacoes_ = 1;
    return false;
  }

  ++confirmacoes_;
  if (confirmacoes_ < kKnobConfirmations) return false;

  nivel_ = candidato_;
  confirmacoes_ = 0;
  return true;
}

}  // namespace kanri::display
