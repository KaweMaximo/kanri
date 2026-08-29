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
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "hal/arduino_clock.h"
#include "hal/bt_serial_transport.h"
#include "hal/gpio_led.h"
#include "hal/nvs_config_store.h"
#include "hal/serial_display.h"
#include "kanri_config/settings.h"
#include "kanri_core/led_pattern.h"
#include "kanri_core/retry_policy.h"
#include "kanri_core/state_machine.h"
#include "kanri_core/telemetry.h"
#include "kanri_core/version.h"
#include "kanri_obd/obd_client.h"
#include "kanri_obd/pid_decoder.h"
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

// LED de status. Numa DevKit v1, o LED AZUL esta no GPIO 2 — e o unico
// controlavel por software. O LED VERMELHO dessas placas costuma ser o de
// ENERGIA: fica ligado direto no regulador, sem GPIO nenhum, e nao ha como
// apaga-lo. Ver o comentario em src/hal/gpio_led.h.
constexpr std::uint8_t kLedPin = 2;
constexpr bool kLedAtivoBaixo = false;

// Quanto tempo cada varredura Bluetooth dura.
constexpr std::uint32_t kScanMs = 5000;

// Quantas leituras seguidas podem falhar antes de considerarmos o link caido.
// Uma resposta ruim isolada e rotina; cinco seguidas nao sao.
constexpr std::uint8_t kMaxFalhasSeguidas = 5;

// --- Adaptadores de hardware (as escolhas concretas da v0.1) --------------
kanri::hal::ArduinoClock g_clock;
kanri::hal::SerialDisplay g_display;
kanri::hal::NvsConfigStore g_config_store;
kanri::hal::BtSerialTransport g_transport("Kanri");
kanri::hal::GpioLed g_led(kLedPin, kLedAtivoBaixo);
kanri::obd::ObdClient g_obd(g_transport, g_clock);

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
/// Intervalo a cumprir na espera atual, fixado ao entrar em Degraded.
std::uint32_t g_retry_delay_ms = 0;
/// Quando o estado atual comecou. E a origem de tempo do padrao do LED: sem
/// isso, trocar de estado nao reiniciaria o desenho da piscada.
std::uint32_t g_state_since_ms = 0;
/// Quando a varredura atual comecou. Sem isso nao ha como saber a hora de
/// encerrar um scan que roda em outra task.
std::uint32_t g_scan_since_ms = 0;

/// Ponto unico de entrada de eventos na maquina de estados.
///
/// Centralizar aqui garante que TODA transicao seja registrada em log e que
/// os efeitos colaterais de entrada em cada estado (como zerar a telemetria)
/// nunca sejam esquecidos.
void dispatch(kanri::core::AppEvent event) {
  const kanri::core::AppState previous = g_state;
  g_state = kanri::core::next_state(previous, event);

  if (g_state == previous) return;  // evento ignorado: nao poluir o log

  // Sair de ScanningAdapter por QUALQUER caminho encerra a varredura. Sem
  // isso, o radio continuaria buscando em segundo plano depois que o estado
  // ja mudou — gastando energia e entregando resultados fora de contexto.
  if (previous == kanri::core::AppState::ScanningAdapter) {
    g_transport.scan_stop();
    g_scan_since_ms = 0;
  }

  g_state_since_ms = g_clock.now_ms();

  Serial.printf("[estado] %s --(%s)--> %s\n", kanri::core::to_string(previous),
                kanri::core::to_string(event), kanri::core::to_string(g_state));

  // Ao sair de operacao normal, invalidamos as medidas na hora. Melhor a tela
  // mostrar "--" do que um valor de 10 segundos atras parecendo atual.
  if (previous == kanri::core::AppState::Polling &&
      g_state != kanri::core::AppState::Polling) {
    kanri::core::invalidate_all(g_telemetry);
  }

  if (g_state == kanri::core::AppState::Degraded) {
    // record_failure() devolve o intervalo DESTA falha e ja avanca o contador.
    // Um metodo so, sem ordem para errar — ver retry_policy.h.
    g_retry_delay_ms = g_retry.record_failure();
    g_degraded_since_ms = g_clock.now_ms();
    Serial.printf("[retry] tentativa %u, proxima em %u ms\n",
                  static_cast<unsigned>(g_retry.attempt_count()),
                  static_cast<unsigned>(g_retry_delay_ms));
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

/// Atualiza o LED conforme o padrao do estado atual.
///
/// Chamado a cada volta do loop: o padrao e uma funcao pura do tempo, entao
/// basta perguntar "aceso agora?" e obedecer. Nenhum delay, nenhum contador
/// espalhado pelo codigo.
void atualizar_led() {
  const std::uint32_t decorrido =
      kanri::core::elapsed_ms(g_clock.now_ms(), g_state_since_ms);
  g_led.set(kanri::core::led_should_be_on(g_state, decorrido));
}

/// Consolida a varredura, registra o que apareceu e decide o proximo evento.
/// So e chamada depois de scan_stop(), quando nao ha mais callbacks em voo.
kanri::core::AppEvent avaliar_varredura() {
  const std::size_t achados = g_transport.result_count();
  Serial.printf("[bt] %u dispositivo(s) no ar\n",
                static_cast<unsigned>(achados));
  for (std::size_t i = 0; i < achados; ++i) {
    const auto& d = g_transport.results()[i];
    Serial.printf("[bt]   %-24s %s  %d dBm\n",
                  d.name[0] ? d.name : "(sem nome)", d.mac, d.rssi);
  }

  const auto escolha = g_transport.match();
  Serial.printf("[bt] escolha: %s\n", kanri::obd::to_string(escolha.result));
  if (!escolha.found()) return kanri::core::AppEvent::AdapterNotFound;

  Serial.printf("[bt] alvo: %s (%s)\n",
                g_transport.results()[escolha.index].name,
                g_transport.results()[escolha.index].mac);
  return kanri::core::AppEvent::AdapterFound;
}

// PIDs consultados em rodizio durante a operacao normal. Um por vez, para
// que o loop nunca bloqueie por muito tempo — ler os cinco de uma vez
// congelaria o LED e a tela por quase meio segundo.
constexpr struct {
  std::uint8_t mode;
  std::uint8_t pid;
  kanri::core::TelemetryValue kanri::core::TelemetrySnapshot::*campo;
} kRodizio[] = {
    {0x01, 0x0C, &kanri::core::TelemetrySnapshot::engine_rpm},
    {0x01, 0x05, &kanri::core::TelemetrySnapshot::coolant_temp_c},
    {0x01, 0x11, &kanri::core::TelemetrySnapshot::throttle_pct},
    {0x01, 0x42, &kanri::core::TelemetrySnapshot::battery_voltage_v},
    {0x01, 0x0D, &kanri::core::TelemetrySnapshot::vehicle_speed_kmh},
};
constexpr std::size_t kRodizioLen = sizeof(kRodizio) / sizeof(kRodizio[0]);

std::size_t g_rodizio_idx = 0;
std::uint32_t g_last_poll_ms = 0;
std::uint8_t g_falhas_seguidas = 0;

/// Le UM pid do rodizio, respeitando o intervalo configurado.
///
/// Uma resposta ruim isolada e rotina no barramento e nao derruba o link. Mas
/// muitas seguidas significam que a ECU parou de responder — ai sim emitimos
/// VehicleLinkDown e a maquina de estados degrada. Essa decisao mora aqui, e
/// nao na maquina de estados, para manter a funcao de transicao pura.
void ler_um_pid() {
  const std::uint32_t agora = g_clock.now_ms();
  if (kanri::core::elapsed_ms(agora, g_last_poll_ms) <
      g_settings.poll_interval_ms) {
    return;
  }
  g_last_poll_ms = agora;

  const auto& alvo = kRodizio[g_rodizio_idx];
  g_rodizio_idx = (g_rodizio_idx + 1) % kRodizioLen;

  const auto frame = g_obd.read_pid(alvo.mode, alvo.pid);
  if (!frame.ok()) {
    if (++g_falhas_seguidas >= kMaxFalhasSeguidas) {
      Serial.printf("[obd] %u falhas seguidas — considerando o link caido\n",
                    static_cast<unsigned>(g_falhas_seguidas));
      g_falhas_seguidas = 0;
      dispatch(kanri::core::AppEvent::VehicleLinkDown);
    }
    return;
  }
  g_falhas_seguidas = 0;

  const auto valor = kanri::obd::decode(frame);
  if (!valor.ok()) {
    // O frame chegou intacto, mas o numero e impossivel. Nao vai para a tela.
    Serial.printf("[obd] PID %02X recusado na decodificacao: %s\n", alvo.pid,
                  kanri::obd::to_string(valor.status));
    return;
  }

  kanri::core::TelemetryValue& destino = g_telemetry.*(alvo.campo);
  destino.value = valor.value;
  destino.valid = true;
  destino.updated_at_ms = agora;
  ++g_telemetry.frames_ok;
  g_telemetry.last_ok_ms = agora;

  Serial.printf("[obd] %02X = %.1f %s\n", alvo.pid,
                static_cast<double>(valor.value), valor.unit);
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
  // Por que a placa ligou? Distinguir "liguei na tomada" de "o watchdog me
  // reiniciou" vale ouro: um reset por watchdog e sintoma de que alguma
  // coisa bloqueou o loop, e sem este log ele passa despercebido.
  Serial.printf("[boot] motivo do reset: %d (1=power-on 3=software "
                "5=deep-sleep 6=brownout 7/8/9=watchdog)\n",
                static_cast<int>(esp_reset_reason()));
  Serial.println(F("Alvo: Mitsubishi Lancer 2.0 2014 (4B11)"));

  // Watchdog: 8 s de prazo, com panic (reset) ao estourar.
  esp_task_wdt_init(kWatchdogTimeoutSec, true);
  esp_task_wdt_add(nullptr);  // nullptr = observa a task atual (loopTask)
  Serial.printf("[wdt] watchdog armado: %u s\n",
                static_cast<unsigned>(kWatchdogTimeoutSec));

  g_settings = kanri::config::default_settings();

  g_led.begin();
  Serial.printf("[led] status no GPIO %u\n", static_cast<unsigned>(kLedPin));

  if (!g_display.begin()) {
    // Sem tela nao ha como comunicar nada ao motorista: e o unico caminho
    // para AppState::Fault.
    Serial.println(F("[display] FALHA na inicializacao"));
    dispatch(kanri::core::AppEvent::DisplayFailed);
    return;
  }
  g_display.set_brightness(g_settings.display_brightness);

  (void)g_config_store.begin();

  if (!g_transport.begin()) {
    // Sem radio nao ha o que procurar. Nao e fatal: a maquina de estados vai
    // degradar e retentar, e o LED comunica isso.
    Serial.println(F("[bt] FALHA ao inicializar o Bluetooth"));
  } else {
    Serial.println(F("[bt] Bluetooth pronto (modo mestre)"));
  }
  g_transport.set_target(g_settings.adapter_name, g_settings.adapter_mac,
                         g_settings.adapter_pin);

  g_state_since_ms = g_clock.now_ms();
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

    case kanri::core::AppState::ScanningAdapter: {
      // Varredura NAO bloqueante: o radio trabalha em outra task e este loop
      // continua girando. E o que mantem o LED piscando durante a busca e o
      // watchdog alimentado. Uma versao bloqueante de 5 s congelava o LED e
      // fazia o ESP32 reiniciar pelo watchdog.
      if (!g_transport.scan_active()) {
        if (g_scan_since_ms == 0) {
          Serial.printf("[bt] varrendo por %u ms, procurando \"%s\"...\n",
                        static_cast<unsigned>(kScanMs), g_settings.adapter_name);
          if (g_transport.scan_start()) {
            g_scan_since_ms = g_clock.now_ms();
          } else {
            Serial.println(F("[bt] nao consegui iniciar a varredura"));
            dispatch(kanri::core::AppEvent::AdapterNotFound);
          }
        } else {
          // scan_start deu certo mas o radio ja encerrou por conta propria.
          g_scan_since_ms = 0;
          dispatch(avaliar_varredura());
        }
      } else if (kanri::core::elapsed_ms(g_clock.now_ms(), g_scan_since_ms) >=
                 kScanMs) {
        g_transport.scan_stop();
        g_scan_since_ms = 0;
        dispatch(avaliar_varredura());
      }
      break;
    }

    case kanri::core::AppState::ConnectingAdapter:
      if (g_transport.connect()) {
        Serial.println(F("[bt] canal SPP aberto"));
        dispatch(kanri::core::AppEvent::AdapterConnected);
      } else {
        Serial.println(F("[bt] nao consegui abrir o canal"));
        dispatch(kanri::core::AppEvent::AdapterLost);
      }
      break;

    case kanri::core::AppState::InitializingElm:
      if (g_obd.initialize()) {
        Serial.println(F("[elm] adaptador inicializado"));
        float volts = 0.0F;
        if (g_obd.read_adapter_voltage(volts)) {
          Serial.printf("[elm] tensao no conector: %.1f V\n",
                        static_cast<double>(volts));
        }
        dispatch(kanri::core::AppEvent::ElmReady);
      } else {
        Serial.println(F("[elm] sequencia AT falhou"));
        dispatch(kanri::core::AppEvent::ElmFailed);
      }
      break;

    case kanri::core::AppState::ConnectingVehicle: {
      // Uma leitura de teste confirma que ha ECU do outro lado. Usamos a
      // rotacao: e o PID que todo carro OBD2 implementa.
      const auto frame = g_obd.read_pid(0x01, 0x0C);
      if (frame.ok()) {
        Serial.println(F("[obd] ECU respondeu — barramento vivo"));
        dispatch(kanri::core::AppEvent::VehicleLinkUp);
      } else {
        Serial.printf("[obd] sem resposta da ECU: %s\n",
                      kanri::obd::to_string(frame.status));
        dispatch(kanri::core::AppEvent::VehicleLinkDown);
      }
      break;
    }

    case kanri::core::AppState::Polling:
      ler_um_pid();
      break;

    case kanri::core::AppState::Degraded: {
      // Espera o backoff e tenta de novo. Note: nenhum reboot, nenhum
      // `while(1)`, nenhuma tentativa em rajada. Degradacao graciosa.
      const std::uint32_t waited =
          kanri::core::elapsed_ms(g_clock.now_ms(), g_degraded_since_ms);
      if (waited >= g_retry_delay_ms) {
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

  atualizar_led();
  render_if_due();

  // Cede tempo ao scheduler do FreeRTOS. Sem isso, tarefas de sistema (rede,
  // watchdog interno) ficariam sem CPU. Um loop apertado em firmware
  // cooperativo e bug, nao otimizacao.
  delay(10);
}
