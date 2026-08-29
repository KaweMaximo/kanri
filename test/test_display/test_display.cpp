// ============================================================================
//  Testes da camada de display
// ============================================================================
//  Repare que nao ha OLED, I2C nem ESP32 aqui — e ainda assim testamos a
//  interface. E o que se ganha ao separar "o QUE mostrar" (DisplayFrame, puro)
//  de "COMO desenhar" (driver em src/hal).
//
//  Na v0.1 build_frame() ainda e esqueleto: devolve apenas a tela de splash.
//  Os testes abaixo cobrem o que EXISTE hoje mais as invariantes que precisam
//  valer para sempre — em especial "todo texto termina em nulo", que e o que
//  separa um display com lixo na tela de um firmware seguro.
// ============================================================================

#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "fake_display.h"
#include "kanri_core/version.h"
#include "kanri_display/view_model.h"

using kanri::core::AppState;
using kanri::core::TelemetrySnapshot;
using kanri::display::build_frame;
using kanri::display::DisplayFrame;

void setUp(void) {}
void tearDown(void) {}

namespace {

using kanri::display::ScreenId;
using kanri::display::ViewContext;

/// Telemetria com valores plausiveis de um 4B11 em marcha lenta quente.
TelemetrySnapshot lenta(std::uint32_t agora = 1000) {
  TelemetrySnapshot t;
  t.engine_rpm = {750.0F, true, agora};
  t.coolant_temp_c = {83.0F, true, agora};
  t.vehicle_speed_kmh = {0.0F, true, agora};
  t.battery_voltage_v = {13.8F, true, agora};
  return t;
}

ViewContext ctx_de(const TelemetrySnapshot& t, AppState estado,
                   std::uint32_t agora = 1000, bool metrico = true) {
  ViewContext c;
  c.state = estado;
  c.telemetry = &t;
  c.now_ms = agora;
  c.metric_units = metrico;
  return c;
}

bool contem(const DisplayFrame& f, const char* trecho) {
  if (std::strstr(f.title, trecho) != nullptr) return true;
  for (std::size_t i = 0; i < kanri::display::kFrameLines; ++i) {
    if (std::strstr(f.lines[i], trecho) != nullptr) return true;
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
//  AS QUATRO TELAS
// ---------------------------------------------------------------------------

void test_boot_mostra_splash_com_versao(void) {
  const TelemetrySnapshot t;
  const DisplayFrame f = build_frame(ctx_de(t, AppState::Boot));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenId::Splash),
                        static_cast<int>(f.screen));
  TEST_ASSERT_EQUAL_STRING("KANRI", f.title);
  TEST_ASSERT_EQUAL_STRING(KANRI_VERSION_STRING, f.lines[0]);
  // "somente leitura" na tela de abertura nao e decoracao: e o compromisso
  // do projeto, visivel para quem instala o aparelho.
  TEST_ASSERT_TRUE(contem(f, "somente leitura"));
  TEST_ASSERT_FALSE(f.warning);
}

// Enquanto conecta, o motorista precisa saber EM QUE PONTO esta — e,
// principalmente, QUEM estamos procurando. Nome configurado errado e o erro
// mais comum na primeira instalacao.
void test_tela_de_conexao_mostra_etapa_e_alvo(void) {
  const TelemetrySnapshot t;
  ViewContext c = ctx_de(t, AppState::ScanningAdapter);
  c.adapter_name = "OBDII";
  c.retry_attempt = 3;

  const DisplayFrame f = build_frame(c);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenId::Connecting),
                        static_cast<int>(f.screen));
  TEST_ASSERT_TRUE(contem(f, "Procurando"));
  TEST_ASSERT_TRUE(contem(f, "OBDII"));
  TEST_ASSERT_TRUE(contem(f, "3"));
}

void test_cada_etapa_da_conexao_tem_rotulo_proprio(void) {
  const TelemetrySnapshot t;
  TEST_ASSERT_TRUE(contem(build_frame(ctx_de(t, AppState::ScanningAdapter)),
                          "Procurando"));
  TEST_ASSERT_TRUE(contem(build_frame(ctx_de(t, AppState::ConnectingAdapter)),
                          "Conectando"));
  TEST_ASSERT_TRUE(contem(build_frame(ctx_de(t, AppState::InitializingElm)),
                          "ELM"));
  TEST_ASSERT_TRUE(contem(build_frame(ctx_de(t, AppState::ConnectingVehicle)),
                          "ECU"));
}

void test_dashboard_mostra_as_medidas(void) {
  const TelemetrySnapshot t = lenta();
  const DisplayFrame f = build_frame(ctx_de(t, AppState::Polling));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenId::Dashboard),
                        static_cast<int>(f.screen));
  TEST_ASSERT_TRUE(contem(f, "750"));    // rpm
  TEST_ASSERT_TRUE(contem(f, "83"));     // temperatura
  TEST_ASSERT_TRUE(contem(f, "13.8"));   // tensao
  TEST_ASSERT_TRUE(contem(f, "km/h"));
  TEST_ASSERT_FALSE(f.warning);
}

void test_tela_de_erro_diz_o_que_houve_e_quando_retenta(void) {
  const TelemetrySnapshot t;
  ViewContext c = ctx_de(t, AppState::Degraded);
  c.retry_attempt = 4;
  c.retry_in_ms = 8000;

  const DisplayFrame f = build_frame(c);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenId::Error),
                        static_cast<int>(f.screen));
  TEST_ASSERT_TRUE(contem(f, "SEM CONEXAO"));
  TEST_ASSERT_TRUE(contem(f, "4"));
  // Em segundos: "8 s" se le de relance, "8000 ms" nao.
  TEST_ASSERT_TRUE(contem(f, "8"));
  TEST_ASSERT_TRUE(contem(f, "s"));
  TEST_ASSERT_TRUE(f.warning);
}

void test_fault_orienta_o_que_fazer(void) {
  const TelemetrySnapshot t;
  const DisplayFrame f = build_frame(ctx_de(t, AppState::Fault));
  TEST_ASSERT_TRUE(contem(f, "FALHA"));
  TEST_ASSERT_TRUE(contem(f, "reinicie"));
  TEST_ASSERT_TRUE(f.warning);
}

// Estados de erro precisam pedir destaque visual: o motorista tem de notar
// sem tirar os olhos da estrada por muito tempo.
void test_estado_de_erro_liga_o_aviso(void) {
  const TelemetrySnapshot t;
  TEST_ASSERT_TRUE(build_frame(ctx_de(t, AppState::Degraded)).warning);
  TEST_ASSERT_TRUE(build_frame(ctx_de(t, AppState::Fault)).warning);
  TEST_ASSERT_FALSE(build_frame(ctx_de(lenta(), AppState::Polling)).warning);
}

// ---------------------------------------------------------------------------
//  A REGRA MAIS IMPORTANTE DA TELA
// ---------------------------------------------------------------------------

// Medida nunca lida vira "--", nunca um numero. Mostrar "0 rpm" quando a
// rotacao e desconhecida e pior do que nao mostrar nada: o motorista
// acreditaria no zero. Ver docs/SAFETY.md.
void test_medida_nunca_lida_vira_tracos(void) {
  const TelemetrySnapshot vazia;  // nada valido
  const DisplayFrame f = build_frame(ctx_de(vazia, AppState::Polling));
  TEST_ASSERT_TRUE(contem(f, "--"));
  TEST_ASSERT_FALSE(contem(f, "0 rpm"));
}

// Dado velho tambem nao vale: um valor de 10 s atras exibido como atual
// enganaria tanto quanto um valor inventado.
void test_medida_velha_vira_tracos(void) {
  TelemetrySnapshot t = lenta(1000);
  // 1000 + 5000 = agora esta 5 s depois da leitura, alem do limite de 3 s.
  const DisplayFrame f = build_frame(ctx_de(t, AppState::Polling, 6000));
  TEST_ASSERT_TRUE(contem(f, "--"));
  TEST_ASSERT_FALSE(contem(f, "750"));
}

// Dentro da janela, o valor continua valendo.
void test_medida_recente_continua_valendo(void) {
  const TelemetrySnapshot t = lenta(1000);
  const DisplayFrame f = build_frame(ctx_de(t, AppState::Polling, 3000));
  TEST_ASSERT_TRUE(contem(f, "750"));
}

// "--" aparece sozinho, sem unidade: "-- rpm" sugere uma medida que nao existe.
void test_tracos_aparecem_sem_unidade(void) {
  const TelemetrySnapshot vazia;
  const DisplayFrame f = build_frame(ctx_de(vazia, AppState::Polling));
  for (std::size_t i = 0; i < kanri::display::kFrameLines; ++i) {
    if (std::strstr(f.lines[i], "--") != nullptr) {
      TEST_ASSERT_NULL(std::strstr(f.lines[i], "-- rpm"));
      TEST_ASSERT_NULL(std::strstr(f.lines[i], "-- C"));
    }
  }
}

// Temperatura alta liga o aviso: e a informacao que o motorista precisa
// notar sem procurar.
void test_motor_quente_liga_o_aviso(void) {
  TelemetrySnapshot t = lenta();
  t.coolant_temp_c = {108.0F, true, 1000};
  TEST_ASSERT_TRUE(build_frame(ctx_de(t, AppState::Polling)).warning);

  t.coolant_temp_c = {90.0F, true, 1000};
  TEST_ASSERT_FALSE(build_frame(ctx_de(t, AppState::Polling)).warning);
}

// Temperatura velha nao pode disparar alarme: seria assustar por um dado que
// nao vale mais.
void test_temperatura_velha_nao_dispara_alarme(void) {
  TelemetrySnapshot t = lenta(1000);
  t.coolant_temp_c = {120.0F, true, 1000};
  TEST_ASSERT_FALSE(build_frame(ctx_de(t, AppState::Polling, 9000)).warning);
}

// ---------------------------------------------------------------------------
//  UNIDADES
// ---------------------------------------------------------------------------

void test_unidades_imperiais(void) {
  TelemetrySnapshot t = lenta();
  t.coolant_temp_c = {100.0F, true, 1000};
  t.vehicle_speed_kmh = {100.0F, true, 1000};

  const DisplayFrame f = build_frame(ctx_de(t, AppState::Polling, 1000, false));
  TEST_ASSERT_TRUE(contem(f, "212"));   // 100 C = 212 F
  TEST_ASSERT_TRUE(contem(f, "62"));    // 100 km/h = 62 mph
  TEST_ASSERT_TRUE(contem(f, "mph"));
  TEST_ASSERT_FALSE(contem(f, "km/h"));
}

// A tensao da bateria e Volt nos dois sistemas: nao pode ser convertida.
void test_tensao_nao_muda_com_o_sistema_de_unidades(void) {
  const TelemetrySnapshot t = lenta();
  TEST_ASSERT_TRUE(contem(build_frame(ctx_de(t, AppState::Polling, 1000, true)), "13.8"));
  TEST_ASSERT_TRUE(contem(build_frame(ctx_de(t, AppState::Polling, 1000, false)), "13.8"));
}

// Telemetria ausente nao pode quebrar a montagem da tela.
void test_dashboard_sem_telemetria_nao_quebra(void) {
  kanri::display::ViewContext c;
  c.state = AppState::Polling;
  c.telemetry = nullptr;
  const DisplayFrame f = build_frame(c);
  TEST_ASSERT_EQUAL_STRING("KANRI", f.title);
}

// INVARIANTE: todo campo de texto do frame termina em nulo, em qualquer
// estado. Um driver que faca drawString() num buffer sem terminador imprime
// lixo da memoria na tela — ou trava.
void test_todo_texto_do_frame_termina_em_nulo(void) {
  const AppState states[] = {
      AppState::Boot,              AppState::LoadingConfig,
      AppState::ScanningAdapter,   AppState::ConnectingAdapter,
      AppState::InitializingElm,   AppState::ConnectingVehicle,
      AppState::Polling,           AppState::Degraded,
      AppState::Fault,
  };
  const TelemetrySnapshot telemetry;

  for (const AppState state : states) {
    for (const bool metric : {true, false}) {
      const DisplayFrame frame = build_frame(ctx_de(telemetry, state, 1000, metric));

      TEST_ASSERT_LESS_THAN_UINT(kanri::display::kFrameTextLen,
                                 std::strlen(frame.title));
      for (std::size_t line = 0; line < kanri::display::kFrameLines; ++line) {
        TEST_ASSERT_LESS_THAN_UINT(kanri::display::kFrameTextLen,
                                   std::strlen(frame.lines[line]));
      }
    }
  }
}

// Telemetria sem nenhuma leitura valida nao pode produzir numeros na tela.
// Mostrar "0 rpm" quando na verdade nao sabemos a rotacao e pior do que
// mostrar "--": o motorista acreditaria no zero.
// A INVARIANTE CENTRAL: com nada lido, nenhum NUMERO DE MEDIDA aparece.
void test_telemetria_invalida_nao_inventa_valores(void) {
  const TelemetrySnapshot vazia;
  TEST_ASSERT_FALSE(vazia.engine_rpm.valid);

  const DisplayFrame f = build_frame(ctx_de(vazia, AppState::Polling));
  for (std::size_t i = 0; i < kanri::display::kFrameLines; ++i) {
    for (const char* c = f.lines[i]; *c != '\0'; ++c) {
      TEST_ASSERT_FALSE_MESSAGE(*c >= '0' && *c <= '9',
                                "numero na tela sem medida valida");
    }
  }
}

void test_invalidate_all_apaga_medidas_e_preserva_contadores(void) {
  TelemetrySnapshot telemetry;
  telemetry.engine_rpm.value = 1726.0F;
  telemetry.engine_rpm.valid = true;
  telemetry.coolant_temp_c.value = 83.0F;
  telemetry.coolant_temp_c.valid = true;
  telemetry.frames_ok = 1234;
  telemetry.frames_rejected = 7;

  kanri::core::invalidate_all(telemetry);

  TEST_ASSERT_FALSE(telemetry.engine_rpm.valid);
  TEST_ASSERT_FALSE(telemetry.coolant_temp_c.valid);
  // Os contadores contam a historia da sessao: servem ao diagnostico.
  TEST_ASSERT_EQUAL_UINT32(1234, telemetry.frames_ok);
  TEST_ASSERT_EQUAL_UINT32(7, telemetry.frames_rejected);
}

// Medida nunca lida tem idade "infinita" — e nao zero, que pareceria recem
// atualizada.
void test_idade_de_medida_nunca_lida_e_maxima(void) {
  const kanri::core::TelemetryValue never_read;
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
                           kanri::core::value_age_ms(never_read, 5000));

  kanri::core::TelemetryValue fresh;
  fresh.valid = true;
  fresh.updated_at_ms = 4800;
  TEST_ASSERT_EQUAL_UINT32(200, kanri::core::value_age_ms(fresh, 5000));
}

// ---------------------------------------------------------------------------
//  Contrato do IDisplay, verificado contra o dublê de teste
// ---------------------------------------------------------------------------

void test_driver_recebe_o_frame_que_foi_montado(void) {
  kanri::test::FakeDisplay display;
  TEST_ASSERT_TRUE(display.begin());

  const TelemetrySnapshot t;
  display.render(build_frame(ctx_de(t, AppState::Degraded)));

  TEST_ASSERT_EQUAL_UINT32(1, display.render_count());
  TEST_ASSERT_TRUE(display.last_frame().warning);
  TEST_ASSERT_EQUAL_STRING("SEM CONEXAO", display.last_frame().title);
}

// begin() falhando e o unico caminho para AppState::Fault. O teste garante
// que o dublê consegue simular esse cenario.
void test_display_pode_falhar_na_inicializacao(void) {
  kanri::test::FakeDisplay display;
  display.fail_begin();
  TEST_ASSERT_FALSE(display.begin());
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_boot_mostra_splash_com_versao);
  RUN_TEST(test_tela_de_conexao_mostra_etapa_e_alvo);
  RUN_TEST(test_cada_etapa_da_conexao_tem_rotulo_proprio);
  RUN_TEST(test_dashboard_mostra_as_medidas);
  RUN_TEST(test_tela_de_erro_diz_o_que_houve_e_quando_retenta);
  RUN_TEST(test_fault_orienta_o_que_fazer);
  RUN_TEST(test_estado_de_erro_liga_o_aviso);

  RUN_TEST(test_medida_nunca_lida_vira_tracos);
  RUN_TEST(test_medida_velha_vira_tracos);
  RUN_TEST(test_medida_recente_continua_valendo);
  RUN_TEST(test_tracos_aparecem_sem_unidade);
  RUN_TEST(test_motor_quente_liga_o_aviso);
  RUN_TEST(test_temperatura_velha_nao_dispara_alarme);
  RUN_TEST(test_unidades_imperiais);
  RUN_TEST(test_tensao_nao_muda_com_o_sistema_de_unidades);
  RUN_TEST(test_dashboard_sem_telemetria_nao_quebra);
  RUN_TEST(test_todo_texto_do_frame_termina_em_nulo);
  RUN_TEST(test_telemetria_invalida_nao_inventa_valores);

  RUN_TEST(test_invalidate_all_apaga_medidas_e_preserva_contadores);
  RUN_TEST(test_idade_de_medida_nunca_lida_e_maxima);

  RUN_TEST(test_driver_recebe_o_frame_que_foi_montado);
  RUN_TEST(test_display_pode_falhar_na_inicializacao);

  return UNITY_END();
}
