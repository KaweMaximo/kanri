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

/// Nomes legiveis, para log e para o display. Nunca devolve nullptr.
const char* to_string(AppState s);
const char* to_string(AppEvent e);

}  // namespace kanri::core
