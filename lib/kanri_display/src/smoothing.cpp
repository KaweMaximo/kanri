#include "kanri_display/smoothing.h"

namespace kanri::display {
namespace {

/// Acima disto, a mudanca e um evento e nao ruido: vai direto.
///
/// Uma fracao da faixa da grandeza. Pisar no acelerador muda a rotacao em
/// milhares de rpm; deslizar ate la levaria quase um segundo, e o painel
/// pareceria atrasado — que e pior do que pular.
constexpr float kSnapFraction = 0.10F;

bool eh_finito(float v) {
  // Sem <cmath>: NaN e o unico valor diferente de si mesmo, e o infinito nao
  // sobrevive a comparacao com um limite absurdo. Evita puxar a biblioteca
  // matematica inteira para o firmware por causa de duas checagens.
  return (v == v) && (v < 1e30F) && (v > -1e30F);
}

}  // namespace

float ValueSmoother::update(float alvo) {
  if (!eh_finito(alvo)) return atual_;  // lixo nao entra no historico

  if (!iniciado_) {
    // A primeira leitura NAO desliza a partir de zero: subiria do nada ate a
    // temperatura do motor na frente do motorista.
    atual_ = alvo;
    iniciado_ = true;
    return atual_;
  }

  const float delta = alvo - atual_;
  const float distancia = (delta < 0) ? -delta : delta;

  if (distancia > span_ * kSnapFraction) {
    atual_ = alvo;  // evento, nao ruido
    return atual_;
  }

  atual_ += delta * (static_cast<float>(kSmoothStepPercent) / 100.0F);

  // O passo proporcional nunca chega ao alvo, so se aproxima para sempre. Sem
  // este fecho, o ultimo digito ficaria tremendo eternamente — justamente o
  // defeito que viemos corrigir.
  if (((alvo - atual_) < 0 ? (atual_ - alvo) : (alvo - atual_)) <
      span_ * 0.001F) {
    atual_ = alvo;
  }
  return atual_;
}

void ValueSmoother::reset() {
  iniciado_ = false;
  atual_ = 0.0F;
}

std::uint8_t step_toward(std::uint8_t atual, std::uint8_t alvo) {
  if (atual < alvo) return static_cast<std::uint8_t>(atual + 1);
  if (atual > alvo) return static_cast<std::uint8_t>(atual - 1);
  return atual;
}

}  // namespace kanri::display
