#include "kanri_core/led_pattern.h"

namespace kanri::core {

LedPattern pattern_for(AppState state) {
  switch (state) {
    // Acordando: uma batida lenta e calma. Curto o suficiente para nao
    // parecer travado, lento o suficiente para nao parecer erro.
    case AppState::Boot:
    case AppState::LoadingConfig:
      return {400, 600, 1, 0};

    // PROCURANDO: pisca rapido e continuo, 5 Hz. E o padrao mais agitado de
    // todos, de proposito — comunica "estou trabalhando nisso agora".
    case AppState::ScanningAdapter:
      return {100, 100, 1, 0};

    // Progresso da conexao: 2, 3 e 4 piscadas. Quanto mais perto de operar,
    // mais piscadas — da para acompanhar so olhando.
    case AppState::ConnectingAdapter:
      return {80, 120, 2, 700};
    case AppState::InitializingElm:
      return {80, 120, 3, 700};
    case AppState::ConnectingVehicle:
      return {80, 120, 4, 700};

    // Operando: heartbeat discreto. Uma batida curta a cada 2 s diz "vivo e
    // lendo o carro" sem competir com a estrada pela atencao do motorista.
    case AppState::Polling:
      return {60, 0, 1, 1940};

    // Degradado: pisca lento e simetrico. Visivelmente diferente do padrao
    // de busca (que e rapido) e do heartbeat (que e discreto).
    case AppState::Degraded:
      return {400, 400, 1, 0};

    // Falha terminal: aceso fixo. O unico padrao que NAO pisca — reconhecivel
    // de relance, sem precisar contar piscadas.
    case AppState::Fault:
      return {0, 0, kAlwaysOn, 0};
  }
  // Estado corrompido: apagado. Falhar escuro e melhor do que um LED aceso
  // sugerindo um estado que nao existe.
  return {0, 0, kAlwaysOff, 0};
}

bool led_should_be_on(const LedPattern& pattern, std::uint32_t elapsed_ms) {
  if (pattern.pulse_count == kAlwaysOff) return false;
  if (pattern.pulse_count == kAlwaysOn) return true;

  const std::uint32_t passo =
      static_cast<std::uint32_t>(pattern.pulse_on_ms) + pattern.pulse_off_ms;
  if (passo == 0) return false;  // padrao degenerado: nao pisca nada

  const std::uint32_t grupo = passo * pattern.pulse_count;
  const std::uint32_t ciclo = grupo + pattern.rest_ms;
  if (ciclo == 0) return false;

  const std::uint32_t t = elapsed_ms % ciclo;
  if (t >= grupo) return false;  // estamos na pausa entre grupos

  return (t % passo) < pattern.pulse_on_ms;
}

bool led_should_be_on(AppState state, std::uint32_t elapsed_ms) {
  return led_should_be_on(pattern_for(state), elapsed_ms);
}

std::uint32_t pattern_cycle_ms(const LedPattern& pattern) {
  if (pattern.pulse_count == kAlwaysOff || pattern.pulse_count == kAlwaysOn) {
    return 0;
  }
  const std::uint32_t passo =
      static_cast<std::uint32_t>(pattern.pulse_on_ms) + pattern.pulse_off_ms;
  return (passo * pattern.pulse_count) + pattern.rest_ms;
}

}  // namespace kanri::core
