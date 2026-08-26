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

void test_frame_do_boot_mostra_nome_e_versao(void) {
  const TelemetrySnapshot telemetry;
  const DisplayFrame frame = build_frame(telemetry, AppState::Boot, true);

  TEST_ASSERT_EQUAL_STRING("KANRI", frame.title);
  TEST_ASSERT_EQUAL_STRING(KANRI_VERSION_STRING, frame.lines[0]);
  TEST_ASSERT_FALSE(frame.warning);
}

void test_frame_mostra_o_estado_atual(void) {
  const TelemetrySnapshot telemetry;
  const DisplayFrame frame = build_frame(telemetry, AppState::Polling, true);
  TEST_ASSERT_EQUAL_STRING("Polling", frame.lines[1]);
}

// Estados de erro precisam pedir destaque visual: o motorista tem de notar
// sem tirar os olhos da estrada por muito tempo.
void test_estado_de_erro_liga_o_aviso(void) {
  const TelemetrySnapshot telemetry;
  TEST_ASSERT_TRUE(build_frame(telemetry, AppState::Degraded, true).warning);
  TEST_ASSERT_TRUE(build_frame(telemetry, AppState::Fault, true).warning);
  TEST_ASSERT_FALSE(build_frame(telemetry, AppState::Polling, true).warning);
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
      const DisplayFrame frame = build_frame(telemetry, state, metric);

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
void test_telemetria_invalida_nao_inventa_valores(void) {
  TelemetrySnapshot telemetry;
  TEST_ASSERT_FALSE(telemetry.engine_rpm.valid);

  const DisplayFrame frame = build_frame(telemetry, AppState::Polling, true);
  // Nenhuma linha pode conter um digito enquanto nada foi lido de verdade.
  for (std::size_t line = 0; line < kanri::display::kFrameLines; ++line) {
    for (const char* c = frame.lines[line]; *c != '\0'; ++c) {
      const bool is_digit = (*c >= '0' && *c <= '9');
      if (is_digit) {
        // A linha da versao ("0.1.0") e a unica excecao legitima.
        TEST_ASSERT_EQUAL_STRING(KANRI_VERSION_STRING, frame.lines[line]);
        break;
      }
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

  const TelemetrySnapshot telemetry;
  display.render(build_frame(telemetry, AppState::Degraded, true));

  TEST_ASSERT_EQUAL_UINT32(1, display.render_count());
  TEST_ASSERT_TRUE(display.last_frame().warning);
  TEST_ASSERT_EQUAL_STRING("KANRI", display.last_frame().title);
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

  RUN_TEST(test_frame_do_boot_mostra_nome_e_versao);
  RUN_TEST(test_frame_mostra_o_estado_atual);
  RUN_TEST(test_estado_de_erro_liga_o_aviso);
  RUN_TEST(test_todo_texto_do_frame_termina_em_nulo);
  RUN_TEST(test_telemetria_invalida_nao_inventa_valores);

  RUN_TEST(test_invalidate_all_apaga_medidas_e_preserva_contadores);
  RUN_TEST(test_idade_de_medida_nunca_lida_e_maxima);

  RUN_TEST(test_driver_recebe_o_frame_que_foi_montado);
  RUN_TEST(test_display_pode_falhar_na_inicializacao);

  return UNITY_END();
}
