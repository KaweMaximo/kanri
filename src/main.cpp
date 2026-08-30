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
#include "hal/max7219_display.h"
#include "hal/serial_display.h"
#include "kanri_config/command_parser.h"
#include "kanri_config/settings.h"
#include "kanri_core/led_pattern.h"
#include "kanri_core/retry_policy.h"
#include "kanri_core/state_machine.h"
#include "kanri_core/telemetry.h"
#include "kanri_core/version.h"
#include "kanri_obd/obd_client.h"
#include "kanri_obd/pid_decoder.h"
#include "kanri_obd/dtc.h"
#include "kanri_obd/pid_support.h"
#include <cstring>

#include "kanri_core/button.h"
#include "kanri_core/pin_guard.h"
#include "kanri_display/brightness_knob.h"
#include "kanri_display/smoothing.h"
#include "kanri_display/max7219.h"
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

// Mostrador do painel: MAX7219 por SPI. Tres fios, decisao de Jose Rodrigues
// — ver docs/HARDWARE.md. LOAD e o mesmo pino que o MAX7221 chama de CS.
constexpr std::uint8_t kSegDin = 23;
constexpr std::uint8_t kSegClk = 18;
constexpr std::uint8_t kSegLoad = 5;

// Botao que troca a medida exibida. INPUT_PULLUP: solto le HIGH, apertado LOW.
// GPIO 17 tem pull-up interno e nao e pino de strapping — os da faixa 34-39
// NAO teriam pull-up, e o `pinMode` nem reclamaria. Ver docs/HARDWARE.md.
constexpr std::uint8_t kBotaoPin = 17;

// Potenciometro de brilho (decisao de Jose Rodrigues, 29/08/2026): pernas
// externas em 3,3 V e GND, cursor no GPIO 36 (VP).
//
// Os dois motivos de ser justamente este pino:
//   - e do ADC1. O ADC2 NAO funciona com o radio ligado, e o Bluetooth fica
//     ligado o tempo todo — usar ADC2 daria um botao que para de responder
//     assim que o adaptador conecta;
//   - e input-only, entao nao desperdica um pino que sirva para outra coisa.
constexpr std::uint8_t kPotPin = 36;

// De quanto em quanto tempo o botao e lido.
//
// Foi 200 ms, e era o que fazia o brilho demorar 1,2 s para acompanhar o
// giro. O intervalo alto nao comprava nada: analogRead() custa ~100 us, ou
// seja 0,5% do tempo mesmo a 20 ms.
//
// Baixando para 20 ms, a resposta caiu para ~160 ms — imperceptivel — e
// ainda sobrou folga para SUBIR as confirmacoes de 6 para 8, deixando a
// rejeicao de pino solto mais forte do que era. Ver brightness_knob.h.
constexpr std::uint32_t kPotIntervalMs = 20;

// Fotorresistor (LDR) com resistor de 10 kOhm, no GPIO 34.
//
// Mesmo motivo do potenciometro no 36: e do ADC1 (o ADC2 nao funciona com o
// radio ligado) e e INPUT-ONLY. Os input-only nao servem para acionar nada,
// e por isso sao perfeitos para sensor — que e entrada por natureza. Assim
// nenhum pino capaz de acionar coisa e desperdicado.
//
// Faz o brilho acompanhar a luz do ambiente: claro de dia, fraco a noite. E
// o que o FuelTech faz, e a razao de existir num painel de carro e simples —
// o brilho bom de dia CEGA a noite.
constexpr std::uint8_t kLdrPin = 34;

// Como o divisor foi montado: LDR no lado de cima (3,3 V) ou de baixo (GND)?
//
// Isso INVERTE o sentido da leitura, e errar aqui produz o pior resultado
// possivel: o painel escurece no sol e ofusca no escuro — exatamente ao
// contrario do que se quer, e sem nada indicar que ha erro.
//
// Confira com o comando `luz`: TAPE o sensor com a mao e veja para onde o
// numero vai. Se ele SOBE no escuro, esta constante precisa virar true.
constexpr bool kLdrEscuroLeAlto = false;

// De quanto em quanto tempo o mostrador do carro e redesenhado. Mais rapido
// que o texto porque o piscar do alerta precisa de resolucao.
constexpr std::uint32_t kSegRenderIntervalMs = 100;

// Quanto tempo cada varredura Bluetooth dura.
constexpr std::uint32_t kScanMs = 5000;

// Quantas leituras seguidas podem falhar antes de considerarmos o link caido.
// Uma resposta ruim isolada e rotina; cinco seguidas nao sao.
constexpr std::uint8_t kMaxFalhasSeguidas = 5;

// De quanto em quanto tempo o firmware repete o estado atual.
//
// As linhas [estado] so aparecem numa TRANSICAO. Quem abre o painel no meio
// de uma sessao estavel — o firmware ja em Polling ha minutos — nao veria
// nenhuma delas, e ficaria sem saber em que estado o aparelho esta.
// Esta batida resolve isso, e de quebra prova que o loop continua girando
// mesmo quando nenhum valor muda (motor desligado, carro parado).
constexpr std::uint32_t kHeartbeatMs = 3000;

// --- Adaptadores de hardware (as escolhas concretas da v0.1) --------------
kanri::hal::ArduinoClock g_clock;
kanri::hal::SerialDisplay g_display;
kanri::hal::Max7219Display g_seg(kSegDin, kSegClk, kSegLoad);
kanri::core::Button g_botao;
kanri::display::BrightnessKnob g_knob;

// O sensor de luz usa MUITO mais confirmacoes que o potenciometro. Um botao
// so muda quando alguem gira; a luz ambiente muda o tempo todo ao dirigir.
kanri::display::BrightnessKnob g_luz(kanri::display::kAmbientConfirmations);

// ---------------------------------------------------------------------------
//  O painel roda no OUTRO nucleo
// ---------------------------------------------------------------------------
//  Porque o laco principal BLOQUEIA. BluetoothSerial::connect() segura ate
//  10 s; cada leitura de PID segura ate 1 s. Enquanto isso, no desenho
//  antigo, o potenciometro nao era lido, o botao nao respondia e o mostrador
//  nao redesenhava.
//
//  O sintoma que isso produzia: "so funciona conectado". Com o carro
//  respondendo, o laco gira rapido e tudo parecia bem; sem conexao, o painel
//  congelava por 10 s a cada tentativa.
//
//  Num aparelho fixo no carro isso e inaceitavel — o brilho tem de responder
//  com a ignicao ligada e o motor desligado, com o adaptador fora do ar, com
//  o que for. O painel nao depende do carro para ser painel.
//
//  Entao ele virou uma task no nucleo 0 (o laco do Arduino roda no 1). O
//  Bluetooth pode bloquear a vontade que a tela continua viva.
//
//  REGRA que mantem isso seguro: TODO acesso ao mostrador acontece na task
//  do painel. O console nao escreve no SPI — ele deixa um pedido aqui, sob
//  trava, e a task atende. Dois nucleos disputando o mesmo barramento SPI
//  dariam corrupcao intermitente, do tipo que nao se reproduz.
struct PainelCompartilhado {
  kanri::core::TelemetrySnapshot telemetria;
  kanri::core::AppState estado = kanri::core::AppState::Boot;
  std::uint8_t brilho_alvo_pct = 30;
  bool manual = false;
  std::uint8_t manual_digits[kanri::display::kSegDigits] = {};
  std::size_t teste_passo = kanri::display::kSegTestSteps;
  std::uint16_t pot_raw = 0;
  std::uint8_t pot_nivel = 0;
  std::uint16_t luz_raw = 0;
  std::uint8_t luz_nivel = 0;

  /// Barra de LEDs. Hoje serve para testar a fiacao; e a base do contagiro.
  std::uint8_t barra[kanri::core::kMaxPinList] = {};
  std::uint8_t barra_count = 0;
  bool barra_piscando = false;
  /// Passo da varredura de autoteste; 0xFF = parada.
  std::uint8_t barra_teste = 0xFF;
};

portMUX_TYPE g_painel_mux = portMUX_INITIALIZER_UNLOCKED;
PainelCompartilhado g_painel;

/// Periodo da task de painel. 20 ms = 50 Hz: suave para o olho, e barato —
/// tres palavras de 16 bits a 1 MHz sao 48 us, 0,24% do tempo.
constexpr std::uint32_t kPainelPeriodoMs = 20;
kanri::hal::NvsConfigStore g_config_store;
kanri::hal::BtSerialTransport g_transport("Kanri");
kanri::hal::GpioLed g_led(kLedPin, kLedAtivoBaixo);
kanri::obd::ObdClient g_obd(g_transport, g_clock);
kanri::obd::PidSupport g_suporte;

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
std::size_t g_medida_idx = 0;

// Autoteste do mostrador. kSegTestSteps = "parado"; qualquer valor abaixo e
// o passo em curso. Roda pelo laco, sem bloquear: 14 passos a 1,2 s dariam
// 17 segundos, e o watchdog estoura em 8.

constexpr std::uint32_t kTestePassoMs = 1200;
/// Intervalo a cumprir na espera atual, fixado ao entrar em Degraded.
std::uint32_t g_retry_delay_ms = 0;
/// Quando o estado atual comecou. E a origem de tempo do padrao do LED: sem
/// isso, trocar de estado nao reiniciaria o desenho da piscada.
std::uint32_t g_state_since_ms = 0;
/// Quando a varredura atual comecou. Sem isso nao ha como saber a hora de
/// encerrar um scan que roda em outra task.
std::uint32_t g_scan_since_ms = 0;
std::uint32_t g_last_heartbeat_ms = 0;

/// Linha sendo digitada no console serial.
char g_linha[kanri::config::kMaxCommandLen + 1];
std::size_t g_linha_len = 0;

/// Leva os tempos da configuracao para o cliente OBD.
///
/// Sem isto, o comando `timeout` do console nao surtia efeito nenhum: o
/// ObdClient seguia com o valor padrao. Um ajuste que o usuario faz e que nao
/// muda nada e pior do que nao existir.
/// Registra no log cada comando que vai para o barramento.
///
/// O Kanri e read-only, e essa garantia e imposta por safety.h com teste
/// exaustivo dos 256 modos OBD2. Mas quem esta com o carro na frente nao ve
/// os testes — ve o log. Este registro torna a garantia OBSERVAVEL: se algum
/// dia aparecer aqui um comando que nao seja Modo 01, Modo 09 ou um AT da
/// allowlist, e porque algo esta errado, e da para ver na hora.
void auditar(const char* comando) {
  Serial.printf("[audit] -> %s\n", comando);
}

void aplicar_tempos_do_obd() {
  kanri::obd::ObdClientConfig cfg = g_obd.config();
  cfg.response_timeout_ms = g_settings.elm_timeout_ms;
  g_obd.set_config(cfg);
}

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
  if (kanri::core::left_operation(previous, g_state)) {
    kanri::core::invalidate_all(g_telemetry);
    // Esquece o mapa de suporte: a proxima conexao pode ser outro carro, e
    // herdar o mapa do anterior faria o firmware pular PIDs que existem.
    g_suporte.reset();
  }

  // Voltamos a operar: o backoff cumpriu o papel dele e precisa voltar ao
  // valor base. Sem esta linha o intervalo so cresce, e depois de algumas
  // reconexoes qualquer queda momentanea custa 30 s de tela apagada dentro
  // do carro. Ver test_reconectar_devolve_o_backoff_ao_valor_base.
  if (kanri::core::entered_operation(previous, g_state)) {
    g_retry.on_success();
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

  g_transport.set_target(g_settings.adapter_name, g_settings.adapter_mac,
                         g_settings.adapter_pin);

  if (!loaded) {
    Serial.println(F("[config] sem dados na flash — usando padroes de fabrica"));
    return kanri::core::AppEvent::ConfigFailed;
  }
  Serial.printf("[config] carregado da flash: nome=\"%s\"\n",
                g_settings.adapter_name);
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

/// Pergunta a ECU quais PIDs ela implementa.
///
/// Sem isto, o firmware pede PIDs que este motor nao tem e recebe "NO DATA"
/// em cada ciclo. Nao quebra nada — o parser trata —, mas desperdica tempo do
/// barramento: num rodizio de 5 PIDs, um PID inutil custa 20% da banca.
///
/// Os blocos sao encadeados: o ultimo bit de cada um diz se vale perguntar o
/// proximo. Parar quando ele esta desligado economiza consultas em toda
/// partida.
void descobrir_pids() {
  g_suporte.reset();

  std::uint8_t base = kanri::obd::kSupportPid0;
  while (base != 0) {
    const auto frame = g_obd.read_pid(0x01, base);
    if (!frame.ok()) {
      Serial.printf("[obd] mapa de suporte %02X indisponivel: %s\n", base,
                    kanri::obd::to_string(frame.status));
      break;
    }
    if (!g_suporte.apply_block(base, frame.data, frame.length)) {
      Serial.printf("[obd] mapa de suporte %02X malformado\n", base);
      break;
    }
    if (!g_suporte.has_next_block(base)) break;
    base = kanri::obd::next_support_pid(base);
  }

  if (!g_suporte.any_block_applied()) {
    // A ECU nao respondeu ao mapa. Seguimos com o catalogo inteiro: e melhor
    // tentar e receber alguns "NO DATA" do que nao ler nada.
    Serial.println(F("[obd] sem mapa de suporte — usando o catalogo completo"));
    return;
  }

  Serial.printf("[obd] a ECU declara %u PIDs suportados\n",
                static_cast<unsigned>(g_suporte.count()));
  for (std::size_t i = 0; i < kRodizioLen; ++i) {
    Serial.printf("[obd]   %02X %s\n", kRodizio[i].pid,
                  g_suporte.supports(kRodizio[i].pid) ? "sim" : "NAO — sera pulado");
  }
}

/// Este PID entra no rodizio?
///
/// Enquanto nao houver mapa, tudo entra: o catalogo e a melhor aposta
/// disponivel. Com mapa, so o que a ECU declarou.
bool vale_pedir(std::uint8_t pid) {
  return !g_suporte.any_block_applied() || g_suporte.supports(pid);
}

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

  // Avanca ate um PID que valha a pena pedir. O limite de voltas evita laco
  // infinito caso a ECU nao suporte nenhum do rodizio.
  std::size_t tentativas = 0;
  while (tentativas < kRodizioLen && !vale_pedir(kRodizio[g_rodizio_idx].pid)) {
    g_rodizio_idx = (g_rodizio_idx + 1) % kRodizioLen;
    ++tentativas;
  }
  if (tentativas >= kRodizioLen) return;  // nenhum PID do rodizio e suportado

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

// ---------------------------------------------------------------------------
//  Console serial de configuracao
//
//  Existe por um motivo pratico: o nome Bluetooth do adaptador varia entre
//  modelos ("OBDII", "V-LINK", "Android-Vlink"). Sem isto, descobrir que o
//  seu se chama diferente exigiria recompilar e regravar o firmware — dentro
//  do carro, com o notebook no colo.
//
//  A interpretacao da linha e testada em lib/kanri_config/command_parser.
//  Aqui so lemos bytes e aplicamos o resultado.
// ---------------------------------------------------------------------------

/// Traduz esp_reset_reason(). Os valores vem de esp_system.h — copiar a
/// legenda de cabeca e como este log ja apontou para a causa errada uma vez:
/// o codigo 6 e TASK_WDT (watchdog de task), nao brownout, que e 9.
const char* motivo_do_reset() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on (energia ligada)";
    case ESP_RST_EXT:       return "reset externo (pino)";
    case ESP_RST_SW:        return "software (ESP.restart)";
    case ESP_RST_PANIC:     return "PANIC (excecao no firmware)";
    case ESP_RST_INT_WDT:   return "WATCHDOG de interrupcao";
    case ESP_RST_TASK_WDT:  return "WATCHDOG de task (o loop travou)";
    case ESP_RST_WDT:       return "WATCHDOG (outro)";
    case ESP_RST_DEEPSLEEP: return "saida de deep sleep";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (tensao caiu)";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "desconhecido";
  }
}

/// Executa uma operacao LONGA E CONHECIDA sem o watchdog vigiando.
///
/// POR QUE ISSO EXISTE
/// -------------------
/// BluetoothSerial::connect() bloqueia ate ~10 s esperando o pareamento, e a
/// sequencia AT do ELM327 pode levar outro tanto. O watchdog e de 8 s: ele
/// disparava no meio da conexao e reiniciava a placa, num LOOP — o aparelho
/// achava o adaptador e reiniciava antes de conseguir conversar com ele.
///
/// A alternativa seria afrouxar o watchdog para 20 s, o que enfraqueceria a
/// protecao no resto do tempo. Aqui dizemos, de forma cirurgica: "esta
/// operacao especifica e demorada por natureza, nao me vigie por enquanto".
/// O loop inteiro segue protegido.
///
/// A operacao ainda tem limite proprio — o BluetoothSerial e o ObdClient tem
/// timeout interno. Isto remove a vigilancia, nao o prazo.
template <typename F>
auto sem_watchdog(F&& operacao) -> decltype(operacao()) {
  esp_task_wdt_delete(nullptr);
  const auto resultado = operacao();
  esp_task_wdt_add(nullptr);
  esp_task_wdt_reset();
  return resultado;
}

/// Le e imprime os codigos de falha dos tres tipos.
///
/// Distinguir os tres importa no diagnostico: um PENDENTE ainda pode sumir
/// sozinho se o defeito nao se repetir; um GRAVADO ja acendeu a luz; e um
/// PERMANENTE resiste ate a propria ECU confirmar que o problema acabou —
/// e por isso que "apagar os codigos" nao resolve inspecao.
void ler_e_mostrar_dtcs() {
  if (!g_obd.ready()) {
    Serial.println(F("[dtc] adaptador nao inicializado — conecte primeiro"));
    return;
  }

  const kanri::obd::DtcKind tipos[] = {
      kanri::obd::DtcKind::Stored,
      kanri::obd::DtcKind::Pending,
      kanri::obd::DtcKind::Permanent,
  };

  int total = 0;
  for (const kanri::obd::DtcKind tipo : tipos) {
    // A leitura de codigos pode demorar mais que uma leitura de PID: a ECU
    // percorre a lista inteira antes de responder.
    const auto lista = sem_watchdog([&] { return g_obd.read_dtcs(tipo); });
    const char* rotulo = kanri::obd::to_string(tipo);

    if (g_obd.last_dtc_status() != kanri::obd::ParseStatus::Ok) {
      Serial.printf("[dtc] %s: nao consegui ler (%s)\n", rotulo,
                    kanri::obd::to_string(g_obd.last_dtc_status()));
      continue;
    }
    if (lista.count == 0) {
      Serial.printf("[dtc] %s: nenhum\n", rotulo);
      continue;
    }
    for (std::uint8_t i = 0; i < lista.count; ++i) {
      Serial.printf("[dtc] %s: %s\n", rotulo, lista.items[i].text);
      ++total;
    }
    if (lista.truncated) {
      Serial.printf("[dtc] %s: ha mais codigos do que cabe na lista\n", rotulo);
    }
  }
  Serial.printf("[dtc] total: %d codigo(s)\n", total);
}

void imprimir_status() {
  Serial.println(F("--- status ---"));
  Serial.printf("  estado      : %s\n", kanri::core::to_string(g_state));
  Serial.printf("  firmware    : v%s\n", KANRI_VERSION_STRING);
  Serial.printf("  adaptador   : nome=\"%s\" mac=\"%s\" pin=\"%s\"\n",
                g_settings.adapter_name, g_settings.adapter_mac,
                g_settings.adapter_pin);
  Serial.printf("  intervalo   : %u ms\n",
                static_cast<unsigned>(g_settings.poll_interval_ms));
  Serial.printf("  timeout     : %u ms\n",
                static_cast<unsigned>(g_settings.elm_timeout_ms));
  Serial.printf("  unidades    : %s\n",
                g_settings.use_metric_units ? "metrico" : "imperial");
  Serial.printf("  leituras ok : %u   rejeitadas: %u\n",
                static_cast<unsigned>(g_telemetry.frames_ok),
                static_cast<unsigned>(g_obd.rejected_count()));
  Serial.println(F("  (digite 'ajuda' para ver os comandos)"));
}

void executar(const kanri::config::ParsedCommand& cmd) {
  using kanri::config::CommandAction;

  if (cmd.action == CommandAction::None) return;
  if (!cmd.ok()) {
    Serial.printf("[cfg] %s\n", kanri::config::to_string(cmd.error));
    return;
  }

  switch (cmd.action) {
    case CommandAction::Help:
      for (const char* const* l = kanri::config::help_lines(); *l; ++l) {
        Serial.println(*l);
      }
      return;
    case CommandAction::Status:
      imprimir_status();
      return;
    case CommandAction::Scan:
      Serial.println(F("[cfg] forcando nova varredura"));
      dispatch(kanri::core::AppEvent::RetryTimerExpired);
      return;
    case CommandAction::Save:
      if (g_config_store.save(g_settings)) {
        Serial.println(F("[cfg] gravado na flash"));
      } else {
        Serial.println(F("[cfg] FALHA ao gravar"));
      }
      return;
    case CommandAction::Load: {
      const bool tinha = g_config_store.load(g_settings);
      Serial.printf("[cfg] %s\n",
                    tinha ? "carregado da flash" : "nada gravado — usando padroes");
      g_transport.set_target(g_settings.adapter_name, g_settings.adapter_mac,
                             g_settings.adapter_pin);
      return;
    }
    case CommandAction::ReadDtc:
      ler_e_mostrar_dtcs();
      return;
    case CommandAction::LedBar: {
      static const std::uint8_t kOcupados[] = {kLedPin, kBotaoPin, kPotPin,
                                               kSegDin, kSegClk,   kSegLoad};
      if (cmd.text[0] == '\0') {
        // Sem argumento: mostra a barra atual.
        std::uint8_t n;
        portENTER_CRITICAL(&g_painel_mux);
        n = g_painel.barra_count;
        portEXIT_CRITICAL(&g_painel_mux);
        if (n == 0) {
          Serial.println(F("[leds] nenhuma barra definida — use: leds 22,21,19"));
          return;
        }
        Serial.print(F("[leds] barra:"));
        for (std::uint8_t i = 0; i < n; ++i) {
          Serial.printf(" %u", static_cast<unsigned>(g_painel.barra[i]));
        }
        Serial.println();
        return;
      }

      const auto lista = kanri::core::parse_pin_list(
          cmd.text, kOcupados, sizeof(kOcupados) / sizeof(kOcupados[0]));

      if (!lista.ok()) {
        // A lista INTEIRA e recusada quando um pino e ruim. Aceitar o resto
        // deixaria metade dos LEDs funcionando e a outra metade em silencio.
        Serial.printf("[leds] recusado: %s",
                      kanri::core::to_string(lista.error));
        if (lista.error == kanri::core::PinListError::BadPin ||
            lista.error == kanri::core::PinListError::Duplicate) {
          Serial.printf(" (GPIO %u: %s)", static_cast<unsigned>(lista.offending),
                        kanri::core::to_string(lista.verdict));
        }
        Serial.println();
        return;
      }

      portENTER_CRITICAL(&g_painel_mux);
      g_painel.barra_count = static_cast<std::uint8_t>(lista.count);
      for (std::size_t i = 0; i < lista.count; ++i) {
        g_painel.barra[i] = lista.pins[i];
      }
      g_painel.barra_piscando = false;
      g_painel.barra_teste = 0xFF;  // 0xFF = varredura parada
      portEXIT_CRITICAL(&g_painel_mux);

      for (std::size_t i = 0; i < lista.count; ++i) {
        pinMode(lista.pins[i], OUTPUT);
        digitalWrite(lista.pins[i], LOW);
      }
      Serial.printf("[leds] barra com %u LEDs — use 'piscar' ou 'teste'\n",
                    static_cast<unsigned>(lista.count));
      return;
    }
    case CommandAction::LedBlink: {
      bool ligou = false;
      portENTER_CRITICAL(&g_painel_mux);
      if (g_painel.barra_count > 0) {
        g_painel.barra_piscando = !g_painel.barra_piscando;
        ligou = g_painel.barra_piscando;
      }
      const std::uint8_t n = g_painel.barra_count;
      portEXIT_CRITICAL(&g_painel_mux);

      if (n == 0) {
        Serial.println(F("[leds] defina a barra primeiro: leds 22,21,19"));
        return;
      }
      Serial.printf("[leds] piscar %s\n", ligou ? "LIGADO" : "desligado");
      return;
    }
    case CommandAction::DigitWrite: {
      const std::uint8_t human = (cmd.number > 255)
                                     ? 255
                                     : static_cast<std::uint8_t>(cmd.number);
      const auto v = kanri::display::check_spare_digit(human);
      if (v != kanri::display::SpareDigitVerdict::Ok) {
        Serial.printf("[dig] digito %u recusado: %s\n",
                      static_cast<unsigned>(cmd.number),
                      kanri::display::to_string(v));
        if (v == kanri::display::SpareDigitVerdict::UsedByDisplay) {
          Serial.println(F("      para escrever no mostrador use: seg <texto>"));
        }
        return;
      }

      // ⚠️ O CHIP SO VARRE ATE O ScanLimit. Sem subir isto, os LEDs do
      // digito 4 nao acendem — sem erro, sem pista. Mesmo tipo de falha
      // silenciosa do GPIO 28.
      g_seg.set_scan_limit(kanri::display::scan_limit_for_digit(human));

      const std::uint8_t bits = (cmd.number2 > 255)
                                    ? 0xFF
                                    : static_cast<std::uint8_t>(cmd.number2);
      g_seg.set_digit_raw(kanri::display::spare_digit_register(human), bits);

      Serial.printf("[dig] digito %u = 0x%02X (%u LEDs acesos)\n",
                    static_cast<unsigned>(human), static_cast<unsigned>(bits),
                    static_cast<unsigned>(__builtin_popcount(bits)));
      Serial.printf("      ScanLimit subiu para %u — o mostrador fica mais\n",
                    static_cast<unsigned>(
                        kanri::display::scan_limit_for_digit(human) + 1));
      Serial.println(F("      fraco, porque o ciclo agora se divide entre mais"));
      Serial.println(F("      digitos. E multiplexacao, nao defeito."));
      return;
    }
    case CommandAction::GpioWrite: {
      // Os pinos que ESTE firmware ja usa. Ficam aqui, junto das constantes,
      // porque e aqui que a alocacao mora — o pin_guard e generico e nao
      // tem como saber.
      static const std::uint8_t kOcupados[] = {kLedPin,  kBotaoPin, kPotPin,
                                               kSegDin,  kSegClk,   kSegLoad};

      if (cmd.number > 255) {
        Serial.println(F("[gpio] pino fora da faixa"));
        return;
      }
      const std::uint8_t pino = static_cast<std::uint8_t>(cmd.number);
      const auto veredito = kanri::core::check_output_pin(
          pino, kOcupados, sizeof(kOcupados) / sizeof(kOcupados[0]));

      if (veredito != kanri::core::PinVerdict::Ok) {
        // Recusa EXPLICITA. digitalWrite() num pino inexistente ou
        // input-only compila, roda, nao da erro — e a pessoa vai medir um
        // pino procurando defeito na fiacao. Ver kanri_core/pin_guard.h.
        Serial.printf("[gpio] GPIO %u recusado: %s\n",
                      static_cast<unsigned>(pino),
                      kanri::core::to_string(veredito));
        return;
      }

      const bool nivel = (cmd.number2 != 0);
      pinMode(pino, OUTPUT);
      digitalWrite(pino, nivel ? HIGH : LOW);
      Serial.printf("[gpio] GPIO %u = %s\n", static_cast<unsigned>(pino),
                    nivel ? "ALTO (3,3 V)" : "BAIXO (0 V)");
      return;
    }
    case CommandAction::LightStatus: {
      std::uint16_t raw;
      std::uint8_t nivel;
      portENTER_CRITICAL(&g_painel_mux);
      raw = g_painel.luz_raw;
      nivel = g_painel.luz_nivel;
      portEXIT_CRITICAL(&g_painel_mux);

      Serial.printf("[luz] GPIO %u  adc=%u/%u  nivel %u/%u\n",
                    static_cast<unsigned>(kLdrPin), static_cast<unsigned>(raw),
                    static_cast<unsigned>(kanri::display::kAdcMax),
                    static_cast<unsigned>(nivel + 1),
                    static_cast<unsigned>(kanri::display::kKnobLevels));
      Serial.printf("      montagem: escuro le %s\n",
                    kLdrEscuroLeAlto ? "ALTO" : "BAIXO");
      Serial.println(F("      TAPE o sensor com a mao e rode `luz` de novo."));
      Serial.println(F("      Se o numero for para o lado errado, a constante"));
      Serial.println(F("      kLdrEscuroLeAlto no main.cpp precisa inverter."));
      return;
    }
    case CommandAction::PotStatus: {
      // Le na hora, sem esperar o intervalo: quem digitou o comando esta
      // com a mao no botao querendo ver o numero mexer.
      std::uint16_t raw;
      portENTER_CRITICAL(&g_painel_mux);
      raw = g_painel.pot_raw;
      portEXIT_CRITICAL(&g_painel_mux);
      Serial.printf("[pot] GPIO %u  adc=%u/%u\n",
                    static_cast<unsigned>(kPotPin), static_cast<unsigned>(raw),
                    static_cast<unsigned>(kanri::display::kAdcMax));
      Serial.printf("      nivel %u/%u (%u%%)   candidato: %u\n",
                    static_cast<unsigned>(g_knob.level() + 1),
                    static_cast<unsigned>(kanri::display::kKnobLevels),
                    static_cast<unsigned>(g_knob.percent()),
                    static_cast<unsigned>(g_knob.pending_level() + 1));
      Serial.println(F("      gire de ponta a ponta: adc deve ir de ~0 a ~4095"));
      Serial.println(F("      se ficar pulando sem voce girar, o cursor esta solto"));
      return;
    }
    case CommandAction::SegAuto:
      portENTER_CRITICAL(&g_painel_mux);
      g_painel.manual = false;
      portEXIT_CRITICAL(&g_painel_mux);
      Serial.println(F("[7seg] de volta a telemetria"));
      return;
    case CommandAction::SegTest:
      portENTER_CRITICAL(&g_painel_mux);
      g_painel.manual = false;
      g_painel.teste_passo = 0;
      g_painel.barra_piscando = false;  // a varredura manda, nao o piscar
      portEXIT_CRITICAL(&g_painel_mux);
      Serial.println(F("[7seg] autoteste — confira cada passo no mostrador"));
      return;
    case CommandAction::SegShow: {
      kanri::display::SegFrame f;
      std::strncpy(f.text, cmd.text, sizeof(f.text) - 1);
      std::uint8_t d[kanri::display::kSegDigits];
      if (!kanri::display::encode_frame(f, d, kanri::display::kSegDigits)) {
        // Recusa explicita: sem isto o operador acharia que o mostrador
        // esta com defeito, quando o texto e que nao tem como ser desenhado.
        Serial.printf("[7seg] \"%s\" nao e desenhavel em %u digitos\n", cmd.text,
                      static_cast<unsigned>(kanri::display::kSegDigits));
        return;
      }
      portENTER_CRITICAL(&g_painel_mux);
      g_painel.teste_passo = kanri::display::kSegTestSteps;  // sai do autoteste
      g_painel.manual = true;  // segura a tela ate `auto` ou um toque no botao
      for (std::size_t i = 0; i < kanri::display::kSegDigits; ++i) {
        g_painel.manual_digits[i] = d[i];
      }
      portEXIT_CRITICAL(&g_painel_mux);
      Serial.printf("[7seg] mostrando \"%s\"\n", cmd.text);
      return;
    }
    case CommandAction::Restart:
      Serial.println(F("[cfg] reiniciando..."));
      Serial.flush();
      ESP.restart();
      return;
    default:
      break;  // comandos de escrita, tratados abaixo
  }

  if (kanri::config::apply_command(cmd, g_settings)) {
    Serial.printf("[cfg] %s aplicado (use 'save' para gravar)\n",
                  kanri::config::to_string(cmd.action));
    // O brilho precisa chegar ao CHIP, nao so as configuracoes. Sem esta
    // linha o comando responde "aplicado" e o mostrador nao muda nada — e o
    // brilho e o controle de corrente do MAX7219, entao "nao muda nada" e
    // exatamente o que nao se quer quando a fonte esta no limite.
    if (cmd.action == kanri::config::CommandAction::SetBrightness) {
      // Deixa o ALVO; quem escreve no chip e a task do painel, em rampa.
      portENTER_CRITICAL(&g_painel_mux);
      g_painel.brilho_alvo_pct = g_settings.display_brightness;
      portEXIT_CRITICAL(&g_painel_mux);
    }
    aplicar_tempos_do_obd();
    // O alvo da varredura muda na hora: assim da para corrigir o nome e ver
    // a proxima varredura ja procurando o certo, sem reiniciar.
    g_transport.set_target(g_settings.adapter_name, g_settings.adapter_mac,
                           g_settings.adapter_pin);
  } else {
    Serial.println(F("[cfg] valor recusado — veja 'ajuda'"));
  }
}

/// Le o que chegou pelo serial, uma linha por vez. Nao bloqueia.
void ler_console() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) return;

    if (c == '\n' || c == '\r') {
      if (g_linha_len > 0) {
        executar(kanri::config::parse_command(g_linha, g_linha_len));
        g_linha_len = 0;
      }
      continue;
    }
    // Linha longa demais: descarta o excedente em vez de estourar o buffer.
    if (g_linha_len < kanri::config::kMaxCommandLen) {
      g_linha[g_linha_len++] = static_cast<char>(c);
    }
  }
}

/// Repete o estado atual de tempos em tempos.
void heartbeat_if_due() {
  const std::uint32_t agora = g_clock.now_ms();
  if (kanri::core::elapsed_ms(agora, g_last_heartbeat_ms) < kHeartbeatMs) {
    return;
  }
  g_last_heartbeat_ms = agora;
  Serial.printf("[hb] %s ok=%u rej=%u up=%us\n",
                kanri::core::to_string(g_state),
                static_cast<unsigned>(g_telemetry.frames_ok),
                static_cast<unsigned>(g_obd.rejected_count()),
                static_cast<unsigned>(agora / 1000U));
}

// ---------------------------------------------------------------------------
//  A task do painel — roda no nucleo 0, independente do OBD
// ---------------------------------------------------------------------------

/// Um suavizador por medida: cada grandeza tem historico proprio, senao
/// trocar de medida no botao arrastaria o valor da anterior.
kanri::display::ValueSmoother* suavizador_da_medida(std::size_t idx) {
  static kanri::display::ValueSmoother* tabela[kanri::display::kSegMeasureMax] = {};
  static bool pronto = false;
  if (!pronto) {
    for (std::size_t i = 0; i < kanri::display::kSegMeasureCount; ++i) {
      tabela[i] = new kanri::display::ValueSmoother(
          kanri::display::kSegMeasures[i].span);
    }
    pronto = true;
  }
  if (idx >= kanri::display::kSegMeasureCount) idx = 0;
  return tabela[idx];
}

/// Le o potenciometro. Roda na task do painel, entao NAO para quando o
/// Bluetooth bloqueia — que era exatamente o defeito.
void painel_ler_potenciometro() {
  const std::uint16_t pot_raw = static_cast<std::uint16_t>(analogRead(kPotPin));
  const std::uint16_t luz_raw = static_cast<std::uint16_t>(analogRead(kLdrPin));

  // O divisor pode ter sido montado nos dois sentidos. Inverter aqui deixa o
  // resto da logica igual a do potenciometro: mais luz = nivel maior.
  const std::uint16_t luz_norm =
      kLdrEscuroLeAlto
          ? static_cast<std::uint16_t>(kanri::display::kAdcMax - luz_raw)
          : luz_raw;

  // Os dois comandam o brilho de forma INDEPENDENTE: quem mexeu por ultimo
  // vale. Na pratica o sensor manda quase sempre, porque a luz muda mais que
  // um botao — e e isso que se quer aqui: mais luz, mais brilho.
  //
  // Nao ha combinacao entre eles de proposito. Uma formula juntando os dois
  // ficaria imprevisivel: girar o botao e nao ver efeito porque o sensor
  // limitou seria lido como defeito.
  std::uint8_t nivel;
  const char* origem;

  if (g_luz.update(luz_norm)) {
    nivel = g_luz.level();
    origem = "luz";
  } else if (g_knob.update(pot_raw)) {
    nivel = g_knob.level();
    origem = "botao";
  } else {
    portENTER_CRITICAL(&g_painel_mux);
    g_painel.pot_raw = pot_raw;
    g_painel.pot_nivel = g_knob.level();
    g_painel.luz_raw = luz_raw;
    g_painel.luz_nivel = g_luz.level();
    portEXIT_CRITICAL(&g_painel_mux);
    return;
  }

  const std::uint8_t pct = kanri::display::knob_level_percent(nivel);
  portENTER_CRITICAL(&g_painel_mux);
  g_painel.brilho_alvo_pct = pct;
  portEXIT_CRITICAL(&g_painel_mux);

  Serial.printf("[brilho] %s -> nivel %u/%u (%u%%)  adc luz=%u\n", origem,
                static_cast<unsigned>(nivel + 1),
                static_cast<unsigned>(kanri::display::kKnobLevels),
                static_cast<unsigned>(pct), static_cast<unsigned>(luz_raw));

  portENTER_CRITICAL(&g_painel_mux);
  g_painel.pot_raw = pot_raw;
  g_painel.pot_nivel = g_knob.level();
  g_painel.luz_raw = luz_raw;
  g_painel.luz_nivel = g_luz.level();
  portEXIT_CRITICAL(&g_painel_mux);
}

/// Le o botao que troca a medida exibida.
void painel_ler_botao() {
  const bool apertado = (digitalRead(kBotaoPin) == LOW);

  switch (g_botao.update(apertado, millis())) {
    case kanri::core::ButtonEvent::Click:
      g_medida_idx = (g_medida_idx + 1) % kanri::display::kSegMeasureCount;
      // Um toque quer dizer "quero ver o carro": solta a tela do modo manual.
      portENTER_CRITICAL(&g_painel_mux);
      g_painel.manual = false;
      portEXIT_CRITICAL(&g_painel_mux);
      Serial.printf("[botao] medida -> %s\n",
                    kanri::display::kSegMeasures[g_medida_idx].key);
      break;
    case kanri::core::ButtonEvent::LongPress:
      g_medida_idx = 0;
      Serial.println(F("[botao] toque longo — volta para a primeira medida"));
      break;
    default:
      break;
  }
}

/// Aplica o brilho EM RAMPA, um passo do chip por quadro.
///
/// Girar o potenciometro pula degraus inteiros de intensidade; caminhar um
/// passo por quadro transforma o degrau em transicao. A 50 Hz, atravessar a
/// escala inteira do MAX7219 leva 0,3 s.
void painel_aplicar_brilho() {
  static std::uint8_t atual = 0xFF;  // 0xFF = ainda nao inicializado

  std::uint8_t alvo_pct;
  portENTER_CRITICAL(&g_painel_mux);
  alvo_pct = g_painel.brilho_alvo_pct;
  portEXIT_CRITICAL(&g_painel_mux);

  const std::uint8_t alvo = kanri::display::intensity_from_percent(alvo_pct);
  if (atual == 0xFF) atual = alvo;
  if (atual == alvo) return;

  atual = kanri::display::step_toward(atual, alvo);
  g_seg.set_intensity(atual);
}

/// Aciona a barra de LEDs: piscar ou varredura de autoteste.
///
/// Roda na task do painel pelo mesmo motivo do resto: se ficasse no laco, o
/// LED pararia de piscar durante os 10 s de Bluetooth bloqueado — e quem
/// estivesse conferindo a fiacao concluiria que o LED queimou.
void painel_barra() {
  PainelCompartilhado c;
  portENTER_CRITICAL(&g_painel_mux);
  c = g_painel;
  portEXIT_CRITICAL(&g_painel_mux);

  if (c.barra_count == 0) return;

  std::uint8_t mascara;

  if (c.barra_teste != 0xFF) {
    // Varredura: um LED por vez. Segura cada passo por 600 ms, senao nao da
    // tempo de olhar qual acendeu.
    static std::uint32_t ultimo = 0;
    const std::uint32_t agora = millis();
    if (ultimo != 0 && kanri::core::elapsed_ms(agora, ultimo) < 600) return;
    ultimo = agora;

    const std::size_t passos = kanri::core::bar_test_steps(c.barra_count);
    if (c.barra_teste >= passos) {
      portENTER_CRITICAL(&g_painel_mux);
      g_painel.barra_teste = 0xFF;
      portEXIT_CRITICAL(&g_painel_mux);
      Serial.println(F("[leds] varredura concluida"));
      return;
    }

    mascara = kanri::core::bar_test_mask(c.barra_teste, c.barra_count);
    if (c.barra_teste < c.barra_count) {
      Serial.printf("[leds] passo %u/%u: so o LED do GPIO %u\n",
                    static_cast<unsigned>(c.barra_teste + 1),
                    static_cast<unsigned>(passos),
                    static_cast<unsigned>(c.barra[c.barra_teste]));
    } else if (c.barra_teste == c.barra_count) {
      Serial.printf("[leds] passo %u/%u: TODOS acesos\n",
                    static_cast<unsigned>(c.barra_teste + 1),
                    static_cast<unsigned>(passos));
    }

    portENTER_CRITICAL(&g_painel_mux);
    ++g_painel.barra_teste;
    portEXIT_CRITICAL(&g_painel_mux);

  } else if (c.barra_piscando) {
    mascara = kanri::core::bar_blink_mask(millis(), c.barra_count);
  } else {
    return;  // parada: nao mexe, para nao desfazer um `gpio` manual
  }

  for (std::uint8_t i = 0; i < c.barra_count; ++i) {
    digitalWrite(c.barra[i], ((mascara >> i) & 1U) ? HIGH : LOW);
  }
}

/// Um quadro do painel.
void painel_desenhar() {
  PainelCompartilhado copia;
  portENTER_CRITICAL(&g_painel_mux);
  copia = g_painel;
  portEXIT_CRITICAL(&g_painel_mux);

  // 1) Autoteste manda em tudo enquanto roda.
  if (copia.teste_passo < kanri::display::kSegTestSteps) {
    static std::uint32_t ultimo = 0;
    const std::uint32_t agora = millis();
    if (ultimo != 0 && kanri::core::elapsed_ms(agora, ultimo) < 1200) return;
    ultimo = agora;

    kanri::display::SegTestStep passo;
    if (kanri::display::seg_test_step(copia.teste_passo, &passo)) {
      Serial.printf("[7seg] passo %u/%u: %s\n",
                    static_cast<unsigned>(copia.teste_passo + 1),
                    static_cast<unsigned>(kanri::display::kSegTestSteps),
                    passo.espera);
      g_seg.render_raw(passo.digits, kanri::display::kSegDigits);
    }
    portENTER_CRITICAL(&g_painel_mux);
    ++g_painel.teste_passo;
    portEXIT_CRITICAL(&g_painel_mux);
    if (copia.teste_passo + 1 >= kanri::display::kSegTestSteps) {
      Serial.println(F("[7seg] autoteste concluido"));
      // Encadeia a barra de LEDs, se houver. Um `teste` so confere o
      // aparelho inteiro; dois comandos separados dariam margem a esquecer o
      // segundo — e a fiacao nao conferida e a que falha no carro.
      portENTER_CRITICAL(&g_painel_mux);
      if (g_painel.barra_count > 0) g_painel.barra_teste = 0;
      portEXIT_CRITICAL(&g_painel_mux);
    }
    return;
  }

  // 2) Valor escrito a mao pelo console segura a tela.
  if (copia.manual) {
    g_seg.render_raw(copia.manual_digits, kanri::display::kSegDigits);
    return;
  }

  // 3) Telemetria. Fora de operacao normal nao ha medida em que confiar.
  if (!kanri::core::is_operational(copia.estado)) {
    for (std::size_t i = 0; i < kanri::display::kSegMeasureCount; ++i) {
      suavizador_da_medida(i)->reset();
    }
    kanri::display::SegFrame vazio;
    std::strncpy(vazio.text, kanri::display::kSegNoValue, sizeof(vazio.text) - 1);
    g_seg.render(vazio);
    return;
  }

  const std::size_t idx = g_medida_idx;
  const kanri::core::TelemetryValue& v =
      copia.telemetria.*(kanri::display::kSegMeasures[idx].field);

  // Leitura invalida limpa o historico: sem isso, ao voltar o numero
  // deslizaria a partir do valor ANTIGO, mostrando por alguns quadros uma
  // medida que o carro nunca teve.
  if (!v.valid) {
    suavizador_da_medida(idx)->reset();
    kanri::display::SegFrame vazio;
    std::strncpy(vazio.text, kanri::display::kSegNoValue, sizeof(vazio.text) - 1);
    g_seg.render(vazio);
    return;
  }

  // AQUI mora o fim do "robotico": o rodizio entrega uma medida por segundo,
  // e nos andamos ate ela a 50 Hz em vez de saltar de uma vez.
  kanri::core::TelemetrySnapshot suave = copia.telemetria;
  (suave.*(kanri::display::kSegMeasures[idx].field)).value =
      suavizador_da_medida(idx)->update(v.value);

  const kanri::display::SegFrame frame =
      kanri::display::build_seg_frame(suave, idx, millis());

  if (frame.blink && !kanri::display::blink_visible(millis())) {
    g_seg.clear();
    return;
  }
  g_seg.render(frame);
}

/// A task. Nunca bloqueia por mais que o proprio periodo.
void tarefa_painel(void*) {
  for (;;) {
    painel_ler_potenciometro();
    painel_ler_botao();
    painel_aplicar_brilho();
    painel_barra();
    painel_desenhar();
    vTaskDelay(pdMS_TO_TICKS(kPainelPeriodoMs));
  }
}

void render_if_due() {
  const std::uint32_t now = g_clock.now_ms();
  if (kanri::core::elapsed_ms(now, g_last_render_ms) < kRenderIntervalMs) {
    return;
  }
  g_last_render_ms = now;

  kanri::display::ViewContext ctx;
  ctx.state = g_state;
  ctx.telemetry = &g_telemetry;
  ctx.now_ms = now;
  ctx.metric_units = g_settings.use_metric_units != 0;
  ctx.adapter_name = g_settings.adapter_name;
  ctx.retry_attempt = g_retry.attempt_count();

  // Quanto falta para a proxima tentativa. A tela mostra isso em segundos,
  // para o usuario saber que o aparelho esta esperando, e nao travado.
  if (kanri::core::waits_for_retry(g_state)) {
    const std::uint32_t esperou =
        kanri::core::elapsed_ms(now, g_degraded_since_ms);
    ctx.retry_in_ms =
        (esperou < g_retry_delay_ms) ? (g_retry_delay_ms - esperou) : 0;
  }

  g_display.render(kanri::display::build_frame(ctx));
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
  Serial.printf("[boot] motivo do reset: %s\n", motivo_do_reset());
  Serial.println(F("Alvo: Mitsubishi Lancer 2.0 2014 (4B11)"));
  Serial.println(F("Digite 'ajuda' para configurar pelo serial."));

  // Watchdog: 8 s de prazo, com panic (reset) ao estourar.
  esp_task_wdt_init(kWatchdogTimeoutSec, true);
  esp_task_wdt_add(nullptr);  // nullptr = observa a task atual (loopTask)
  Serial.printf("[wdt] watchdog armado: %u s\n",
                static_cast<unsigned>(kWatchdogTimeoutSec));

  g_settings = kanri::config::default_settings();

  g_obd.set_audit_sink(auditar);

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

  // Botao do painel. Sem pull-up o pino flutua e a medida trocaria sozinha
  // com o motor ligado — o carro e um ambiente eletricamente barulhento.
  pinMode(kBotaoPin, INPUT_PULLUP);
  Serial.printf("[botao] no GPIO %u (INPUT_PULLUP)\n",
                static_cast<unsigned>(kBotaoPin));
  Serial.printf("[pot] brilho no GPIO %u (ADC1), %u niveis\n",
                static_cast<unsigned>(kPotPin),
                static_cast<unsigned>(kanri::display::kKnobLevels));

  // O mostrador do painel. Falhar aqui NAO leva a Fault: o aparelho continua
  // util pelo console e pelo painel web, e um carro sem display e melhor do
  // que um firmware que se recusa a funcionar. Degradacao graciosa.
  if (!g_seg.begin()) {
    Serial.println(F("[7seg] nao inicializou — seguindo sem o mostrador"));
  }

  // O painel no OUTRO nucleo. O laco do Arduino roda no 1; deixamos o 0 para
  // a tela, de modo que 10 s de Bluetooth bloqueado nao congelem o mostrador.
  xTaskCreatePinnedToCore(tarefa_painel, "painel", 4096, nullptr,
                          /*prioridade=*/1, nullptr, /*nucleo=*/0);
  Serial.println(F("[painel] task no nucleo 0, 50 Hz"));

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
      // Com MAC configurado, pulamos a varredura inteira.
      //
      // Nao e so economia de tempo: um adaptador com sinal fraco (o ELM327
      // dentro do carro, com o ESP32 a alguns metros) aparece de forma
      // intermitente na varredura, ou nao aparece. Medido neste projeto: o
      // adaptador respondia a -80/-90 dBm, no limite da deteccao, e a
      // varredura do ESP32 nao o encontrava. A conexao direta funciona
      // porque nao depende de captar o anuncio no intervalo certo.
      if (g_transport.has_target_mac()) {
        Serial.printf("[bt] MAC configurado (%s) — conectando direto\n",
                      g_settings.adapter_mac);
        dispatch(kanri::core::AppEvent::AdapterFound);
        break;
      }

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

    case kanri::core::AppState::ConnectingAdapter: {
      const std::uint32_t t0 = g_clock.now_ms();
      const bool conectou = sem_watchdog([&] {
        return g_transport.has_target_mac() ? g_transport.connect_by_mac()
                                            : g_transport.connect();
      });
      Serial.printf("[bt] tentativa de conexao levou %u ms\n",
                    static_cast<unsigned>(
                        kanri::core::elapsed_ms(g_clock.now_ms(), t0)));
      if (conectou) {
        Serial.println(F("[bt] canal SPP aberto"));
        dispatch(kanri::core::AppEvent::AdapterConnected);
      } else {
        Serial.println(F("[bt] nao consegui abrir o canal"));
        dispatch(kanri::core::AppEvent::AdapterLost);
      }
      break;
    }

    case kanri::core::AppState::InitializingElm:
      // A sequencia AT sao seis comandos, e o ATZ sozinho pode levar 5 s.
      if (sem_watchdog([&] { return g_obd.initialize(); })) {
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
      const auto frame = sem_watchdog([&] { return g_obd.read_pid(0x01, 0x0C); });
      if (frame.ok()) {
        Serial.println(F("[obd] ECU respondeu — barramento vivo"));
        sem_watchdog([&] { descobrir_pids(); return true; });
        // Ler os codigos uma vez ao conectar: e a informacao que o motorista
        // quer saber assim que liga o aparelho, e nao muda a cada segundo.
        ler_e_mostrar_dtcs();
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

  ler_console();
  heartbeat_if_due();
  atualizar_led();
  // Publica o estado para a task do painel. O laco nao toca no mostrador:
  // dois nucleos no mesmo SPI dariam corrupcao intermitente.
  portENTER_CRITICAL(&g_painel_mux);
  g_painel.telemetria = g_telemetry;
  g_painel.estado = g_state;
  portEXIT_CRITICAL(&g_painel_mux);
  render_if_due();

  // Cede tempo ao scheduler do FreeRTOS. Sem isso, tarefas de sistema (rede,
  // watchdog interno) ficariam sem CPU. Um loop apertado em firmware
  // cooperativo e bug, nao otimizacao.
  delay(10);
}
