#pragma once
// ============================================================================
//  kanri_core/state_machine.h — Maquina de estados da aplicacao
// ============================================================================
//  Uma maquina de estados responde a uma pergunta: "estou em X, aconteceu Y,
//  para onde eu vou?". Isso substitui um emaranhado de `if (conectado && ...)`
//  por uma tabela explicita, que da para ler, desenhar e TESTAR.
//
//  A funcao de transicao aqui e PURA: sem hardware, sem tempo, sem I/O.
//  Mesma entrada -> mesma saida, sempre. E por isso que ela e testavel no PC.
//
//  REGRA DE SEGURANCA embutida no desenho: nao existe transicao que leve a
//  um reboot. Falhas caem em `Degraded`, que espera e tenta de novo. Um
//  firmware que reinicia em loop dentro do carro e pior do que um firmware
//  que mostra "SEM CONEXAO" na tela. Ver docs/SAFETY.md.
//
//  Diagrama completo em docs/ARCHITECTURE.md.
// ============================================================================

#include <cstdint>

namespace kanri::core {

/// Onde o firmware esta agora.
enum class AppState : std::uint8_t {
  Boot = 0,           ///< Logo apos o reset. Inicializando perifericos.
  LoadingConfig,      ///< Lendo as configuracoes da NVS.
  ScanningAdapter,    ///< Procurando o ELM327 no ar (Bluetooth).
  ConnectingAdapter,  ///< Pareando/abrindo o canal SPP com o ELM327.
  InitializingElm,    ///< Enviando a sequencia AT de inicializacao.
  ConnectingVehicle,  ///< ELM327 negociando o protocolo com a ECU.
  Polling,            ///< Operacao normal: lendo PIDs em ciclo.
  Degraded,           ///< Perdemos algo. Mostra erro na tela e vai retentar.
  Fault,              ///< Falha que nao se recupera sozinha (ex.: display morto).
};

/// O que aconteceu no mundo.
enum class AppEvent : std::uint8_t {
  HardwareReady = 0,  ///< Perifericos (serial, display, WDT) inicializados.
  ConfigLoaded,       ///< NVS lida com sucesso (ou defaults aplicados).
  ConfigFailed,       ///< NVS ilegivel. Seguimos com defaults, mas registramos.
  AdapterFound,       ///< Achamos um dispositivo com o nome/MAC esperado.
  AdapterNotFound,    ///< Varredura terminou sem achar nada.
  AdapterConnected,   ///< Canal serial Bluetooth aberto.
  AdapterLost,        ///< Bluetooth caiu em qualquer ponto.
  ElmReady,           ///< O ELM327 respondeu a sequencia AT como esperado.
  ElmFailed,          ///< O ELM327 nao respondeu ou respondeu lixo.
  VehicleLinkUp,      ///< A ECU respondeu. Ha barramento vivo.
  VehicleLinkDown,    ///< "UNABLE TO CONNECT" / ignicao desligada.
  DataValid,          ///< Uma resposta passou pelo parser e pela validacao.
  DataInvalid,        ///< Uma resposta foi rejeitada (nao e fatal por si so).
  RetryTimerExpired,  ///< Hora de tentar de novo (backoff — ver retry_policy.h).
  DisplayFailed,      ///< O display nao inicializou. Nao ha como avisar o usuario.
};

/// A funcao de transicao. PURA.
///
/// Se o evento nao faz sentido no estado atual, ele e IGNORADO e a funcao
/// devolve o mesmo estado. Isso e intencional: eventos fora de ordem chegam
/// o tempo todo em sistemas reais (ex.: `AdapterLost` duplicado). Ignorar em
/// silencio e mais seguro do que travar ou cair num estado indefinido.
AppState next_state(AppState current, AppEvent event);

/// true quando o firmware esta em operacao normal, lendo dados do carro.
constexpr bool is_operational(AppState s) { return s == AppState::Polling; }

/// true quando ha algo a comunicar ao usuario na tela de erro.
constexpr bool is_error_state(AppState s) {
  return s == AppState::Degraded || s == AppState::Fault;
}

/// true quando o estado espera um timer de retentativa para avancar.
constexpr bool waits_for_retry(AppState s) { return s == AppState::Degraded; }

// --------------------------------------------------------------------------
//  Predicados de TRANSICAO (nao de estado)
// --------------------------------------------------------------------------
//  Existem para tirar da "cola" (main.cpp) decisoes que dependem do par
//  (anterior, atual). Comparar estados na mao ali e barato de escrever e caro
//  de descobrir: foi assim que o backoff ficou preso no teto por 28
//  reconexoes seguidas, porque ninguem se lembrou de chamar on_success().
//
//  A regra vira uma funcao pura com nome proprio, e o teste cobra.
// --------------------------------------------------------------------------

/// true quando a transicao ENTRA em operacao normal — ou seja, RECUPERAMOS.
///
/// Este e o momento de zerar o backoff (RetryPolicy::on_success()). Sem isso,
/// o intervalo de retentativa so cresce durante toda a vida do aparelho, e
/// uma queda momentanea passa a custar 30 s de tela apagada em vez de 1 s.
constexpr bool entered_operation(AppState previous, AppState current) {
  return is_operational(current) && !is_operational(previous);
}

/// true quando a transicao SAI de operacao normal.
///
/// Este e o momento de invalidar as medidas na tela: um numero de 10 segundos
/// atras, parado, e pior do que um "--" honesto.
constexpr bool left_operation(AppState previous, AppState current) {
  return is_operational(previous) && !is_operational(current);
}

/// Nomes legiveis, para log e para o display. Nunca devolve nullptr.
const char* to_string(AppState s);
const char* to_string(AppEvent e);

}  // namespace kanri::core
