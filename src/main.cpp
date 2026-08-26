// ============================================================================
//  Kanri (管理) — main.cpp
//  Telemetria OBD2 read-only para Mitsubishi Lancer 2.0 2014 (motor 4B11)
// ============================================================================
//  ESQUELETO DA v0.1. O que este arquivo faz hoje:
//    1. inicializa serial, watchdog e display;
//    2. carrega a configuracao (hoje sempre os padroes de fabrica);
//    3. roda a maquina de estados;
//    4. tenta conectar pelo NullTransport, falha, degrada, mostra o erro e
//       retenta com backoff exponencial.
//
//  O item 4 e de proposito. Ele exercita o caminho FAIL-SAFE completo no
//  hardware de verdade antes de existir uma linha de Bluetooth. Se o
//  comportamento degradado esta certo com um transporte que nunca conecta,
//  ele tende a estar certo quando o Bluetooth cair no meio da estrada.
//
//  ORGANIZACAO: este arquivo e a "cola". Ele decide QUAIS adaptadores de
//  hardware usar e junta tudo. Nenhuma regra de negocio mora aqui — regra de
//  negocio vive em lib/, onde tem teste. Ver docs/ARCHITECTURE.md.
// ============================================================================

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "hal/arduino_clock.h"
#include "hal/null_transport.h"
#include "hal/nvs_config_store.h"
#include "hal/serial_display.h"
#include "kanri_config/settings.h"
#include "kanri_core/retry_policy.h"
#include "kanri_core/state_machine.h"
#include "kanri_core/telemetry.h"
#include "kanri_core/version.h"
#include "kanri_display/view_model.h"

namespace {

// --- Watchdog ---------------------------------------------------------------
//  O Task Watchdog Timer e um contador em hardware. Se o firmware nao o
//  "alimentar" dentro do prazo, o chip reinicia sozinho.
//
//  ATENCAO A DISTINCAO — ela e o coracao de docs/SAFETY.md:
//    - A LOGICA nunca reinicia o aparelho. Bluetooth caiu? Vai para Degraded,
//      mostra o erro, retenta com backoff. Nunca reboot.
//    - O WATCHDOG so age quando o firmware TRAVOU de verdade (loop infinito,
//      deadlock, corrupcao de memoria). Nesse ponto, reiniciar e a unica saida
//      e e melhor do que um aparelho mudo pendurado no painel.
//  Reset por watchdog e a ultima linha de defesa, nao politica de tratamento
//  de erro.
constexpr std::uint32_t kWatchdogTimeoutSec = 8;

// Redesenha a tela nesta cadencia. Nao ha por que desenhar mais rapido do que
// o olho consegue acompanhar.
constexpr std::uint32_t kRenderIntervalMs = 500;

// Backoff de reconexao: comeca em 1 s, dobra, para em 30 s.
constexpr std::uint32_t kRetryBaseMs = 1000;
constexpr std::uint32_t kRetryMaxMs = 30000;

// --- Adaptadores de hardware (as escolhas concretas da v0.1) --------------
kanri::hal::ArduinoClock g_clock;
kanri::hal::SerialDisplay g_display;
kanri::hal::NvsConfigStore g_config_store;
kanri::hal::NullTransport g_transport;

// --- Estado da aplicacao ---------------------------------------------------
//  Objetos globais com tempo de vida estatico, sem `new` em lugar nenhum.
//  Alocacao dinamica em firmware fragmenta o heap e, mais cedo ou mais tarde,
//  falha num momento imprevisivel. Tamanho fixo e previsivel e melhor.
kanri::config::KanriSettings g_settings;
kanri::core::TelemetrySnapshot g_telemetry;
kanri::core::AppState g_state = kanri::core::AppState::Boot;
kanri::core::RetryPolicy g_retry(kRetryBaseMs, kRetryMaxMs);

std::uint32_t g_degraded_since_ms = 0;
std::uint32_t g_last_render_ms = 0;

/// Ponto unico de entrada de eventos na maquina de estados.
///
/// Centralizar aqui garante que TODA transicao seja registrada em log e que
/// os efeitos colaterais de entrada em cada estado (como zerar a telemetria)
/// nunca sejam esquecidos.
void dispatch(kanri::core::AppEvent event) {
  const kanri::core::AppState previous = g_state;
  g_state = kanri::core::next_state(previous, event);

  if (g_state == previous) return;  // evento ignorado: nao poluir o log

  Serial.printf("[estado] %s --(%s)--> %s\n", kanri::core::to_string(previous),
                kanri::core::to_string(event), kanri::core::to_string(g_state));

  // Ao sair de operacao normal, invalidamos as medidas na hora. Melhor a tela
  // mostrar "--" do que um valor de 10 segundos atras parecendo atual.
  if (previous == kanri::core::AppState::Polling &&
      g_state != kanri::core::AppState::Polling) {
    kanri::core::invalidate_all(g_telemetry);
  }

  if (g_state == kanri::core::AppState::Degraded) {
    g_retry.on_failure();
    g_degraded_since_ms = g_clock.now_ms();
    Serial.printf("[retry] tentativa %u, proxima em %u ms\n",
                  static_cast<unsigned>(g_retry.attempt_count()),
                  static_cast<unsigned>(g_retry.current_delay_ms()));
  }
}

/// Carrega a configuracao e garante que ela seja utilizavel.
/// @return o evento a ser despachado em seguida.
kanri::core::AppEvent load_configuration() {
  const bool loaded = g_config_store.load(g_settings);

  // Bytes vindos da flash sao entrada NAO CONFIAVEL, como qualquer outra.
  // clamp_to_valid() garante que, daqui para frente, a configuracao e valida.
  const bool corrected = kanri::config::clamp_to_valid(g_settings);
  if (corrected) {
    Serial.println(F("[config] valores fora de faixa foram corrigidos"));
  }

  if (!loaded) {
    Serial.println(F("[config] sem dados na flash — usando padroes de fabrica"));
    return kanri::core::AppEvent::ConfigFailed;
  }
  return kanri::core::AppEvent::ConfigLoaded;
}

void render_if_due() {
  const std::uint32_t now = g_clock.now_ms();
  if (kanri::core::elapsed_ms(now, g_last_render_ms) < kRenderIntervalMs) {
    return;
  }
  g_last_render_ms = now;
  g_display.render(kanri::display::build_frame(g_telemetry, g_state,
                                               g_settings.use_metric_units != 0));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  // Espera curta e LIMITADA pelo monitor serial. Nunca um `while (!Serial)`
  // sem prazo: se ninguem abrir o monitor, o firmware ficaria preso para
  // sempre — e no carro nunca tem ninguem com o monitor aberto.
  const unsigned long serial_deadline = millis() + 1500;
  while (!Serial && millis() < serial_deadline) {
    delay(10);
  }

  Serial.println();
  Serial.printf("Kanri v%s — telemetria OBD2 (SOMENTE LEITURA)\n",
                KANRI_VERSION_STRING);
  Serial.println(F("Alvo: Mitsubishi Lancer 2.0 2014 (4B11)"));

  // Watchdog: 8 s de prazo, com panic (reset) ao estourar.
  esp_task_wdt_init(kWatchdogTimeoutSec, true);
  esp_task_wdt_add(nullptr);  // nullptr = observa a task atual (loopTask)
  Serial.printf("[wdt] watchdog armado: %u s\n",
                static_cast<unsigned>(kWatchdogTimeoutSec));

  g_settings = kanri::config::default_settings();

  if (!g_display.begin()) {
    // Sem tela nao ha como comunicar nada ao motorista: e o unico caminho
    // para AppState::Fault.
    Serial.println(F("[display] FALHA na inicializacao"));
    dispatch(kanri::core::AppEvent::DisplayFailed);
    return;
  }
  g_display.set_brightness(g_settings.display_brightness);

  (void)g_config_store.begin();

  dispatch(kanri::core::AppEvent::HardwareReady);
}

void loop() {
  // Alimenta o watchdog no comeco de cada iteracao. Se o loop travar, o prazo
  // estoura e o chip reinicia.
  esp_task_wdt_reset();

  switch (g_state) {
    case kanri::core::AppState::LoadingConfig:
      dispatch(load_configuration());
      break;

    case kanri::core::AppState::ScanningAdapter:
      // TODO(v0.2): varredura Bluetooth procurando g_settings.adapter_name
      //             ou g_settings.adapter_mac.
      // Na v0.1 o NullTransport nunca conecta — e o firmware segue para o
      // caminho degradado, que e justamente o que queremos exercitar.
      if (g_transport.connect()) {
        dispatch(kanri::core::AppEvent::AdapterFound);
      } else {
        dispatch(kanri::core::AppEvent::AdapterNotFound);
      }
      break;

    case kanri::core::AppState::ConnectingAdapter:
      // TODO(v0.2): abrir o canal SPP.
      dispatch(kanri::core::AppEvent::AdapterLost);
      break;

    case kanri::core::AppState::InitializingElm:
      // TODO(v0.2): ObdClient::initialize() (sequencia AT).
      dispatch(kanri::core::AppEvent::ElmFailed);
      break;

    case kanri::core::AppState::ConnectingVehicle:
      // TODO(v0.2): primeira leitura de PID para confirmar o barramento.
      dispatch(kanri::core::AppEvent::VehicleLinkDown);
      break;

    case kanri::core::AppState::Polling:
      // TODO(v0.2): ciclo de leitura de PIDs, respeitando
      //             g_settings.poll_interval_ms.
      break;

    case kanri::core::AppState::Degraded: {
      // Espera o backoff e tenta de novo. Note: nenhum reboot, nenhum
      // `while(1)`, nenhuma tentativa em rajada. Degradacao graciosa.
      const std::uint32_t waited =
          kanri::core::elapsed_ms(g_clock.now_ms(), g_degraded_since_ms);
      if (waited >= g_retry.current_delay_ms()) {
        dispatch(kanri::core::AppEvent::RetryTimerExpired);
      }
      break;
    }

    case kanri::core::AppState::Fault:
      // Terminal por decisao de projeto: so sai com reset fisico. Seguimos
      // desenhando a tela de erro e alimentando o watchdog, para que o
      // aparelho continue mostrando o problema em vez de reiniciar em loop.
      break;

    case kanri::core::AppState::Boot:
      // Nao deveria acontecer: setup() sempre sai do Boot. Se acontecer,
      // insistir e mais seguro do que travar.
      dispatch(kanri::core::AppEvent::HardwareReady);
      break;
  }

  render_if_due();

  // Cede tempo ao scheduler do FreeRTOS. Sem isso, tarefas de sistema (rede,
  // watchdog interno) ficariam sem CPU. Um loop apertado em firmware
  // cooperativo e bug, nao otimizacao.
  delay(10);
}
