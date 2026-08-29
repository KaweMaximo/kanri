#pragma once
// ============================================================================
//  kanri_core/led_pattern.h — O LED como canal de comunicacao
// ============================================================================
//  Dentro do carro nao ha monitor serial. O LED e a UNICA coisa que o
//  motorista ve antes de o display existir (v0.3) — e, mesmo depois, e o que
//  ele ve de canto de olho sem tirar a atencao da estrada.
//
//  Por isso o LED nao "pisca": ele FALA. Cada estado tem um padrao proprio, e
//  quem conhece o vocabulario le o estado do aparelho sem conectar nada:
//
//      procurando adaptador ... pisca rapido e continuo   (busca em andamento)
//      conectando ............. 2 piscadas + pausa
//      inicializando ELM327 ... 3 piscadas + pausa
//      falando com a ECU ...... 4 piscadas + pausa
//      operando ............... 1 batida curta a cada 2 s (heartbeat)
//      degradado .............. pisca lento, simetrico    (algo esta errado)
//      falha terminal ......... aceso fixo
//
//  Note a progressao 2, 3, 4: quanto mais perto de funcionar, mais piscadas.
//  Da para acompanhar o progresso da conexao olhando so o LED.
//
//  TUDO AQUI E FUNCAO PURA DO TEMPO. Sem digitalWrite, sem millis(), sem
//  delay(). O adaptador em src/hal so pergunta "aceso ou apagado agora?" e
//  obedece. E por isso que da para testar um heartbeat de 2 segundos sem
//  esperar 2 segundos.
// ============================================================================

#include <cstdint>

// i_clock.h vem junto de proposito: a documentacao de led_should_be_on manda
// calcular o tempo decorrido com core::elapsed_ms(), que mora la. Quem inclui
// este header ja recebe a ferramenta certa para usa-lo.
#include "kanri_core/i_clock.h"
#include "kanri_core/state_machine.h"

namespace kanri::core {

/// Um padrao de pisca: N pulsos iguais, seguidos de uma pausa.
///
/// Ciclo = pulse_count x (pulse_on_ms + pulse_off_ms) + rest_ms
struct LedPattern {
  std::uint16_t pulse_on_ms;   ///< Duracao de cada pulso aceso.
  std::uint16_t pulse_off_ms;  ///< Intervalo entre pulsos do mesmo grupo.
  std::uint8_t pulse_count;    ///< Pulsos por ciclo. Ver os sentinelas abaixo.
  std::uint16_t rest_ms;       ///< Pausa depois do grupo, antes de repetir.
};

/// `pulse_count == kAlwaysOff`: LED apagado o tempo todo.
constexpr std::uint8_t kAlwaysOff = 0;
/// `pulse_count == kAlwaysOn`: LED aceso fixo. Reservado para falha terminal —
/// e o unico padrao que nao pisca, e por isso se distingue de longe.
constexpr std::uint8_t kAlwaysOn = 255;

/// O padrao de cada estado. Funcao pura: mesma entrada, mesma saida.
LedPattern pattern_for(AppState state);

/// O LED deve estar aceso neste instante?
///
/// @param pattern     padrao em vigor
/// @param elapsed_ms  tempo desde que este padrao comecou. Use
///                    elapsed_ms(agora, quando_o_estado_mudou), que trata
///                    corretamente o overflow de millis().
bool led_should_be_on(const LedPattern& pattern, std::uint32_t elapsed_ms);

/// Atalho: o padrao do estado, avaliado no instante dado.
bool led_should_be_on(AppState state, std::uint32_t elapsed_ms);

/// Duracao total de um ciclo do padrao, em ms. Util para log e teste.
/// Devolve 0 para padroes constantes (sempre aceso / sempre apagado).
std::uint32_t pattern_cycle_ms(const LedPattern& pattern);

}  // namespace kanri::core
