// ============================================================================
//  Testes da maquina de estados e da politica de retentativa
// ============================================================================
//  Aqui verificamos o comportamento de FAIL-SAFE exigido em docs/SAFETY.md:
//  nenhum caminho de falha reinicia o firmware, e todo estado degradado tem
//  saida de volta para a operacao normal.
// ============================================================================

#include <unity.h>

#include "kanri_core/i_clock.h"
#include "kanri_core/retry_policy.h"
#include "kanri_core/state_machine.h"

using kanri::core::AppEvent;
using kanri::core::AppState;
using kanri::core::next_state;
using kanri::core::RetryPolicy;

void setUp(void) {}
void tearDown(void) {}

static void assert_state(AppState expected, AppState actual) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

// Lista completa dos estados e eventos, usada nos testes de invariante.
static constexpr AppState kAllStates[] = {
    AppState::Boot,              AppState::LoadingConfig,
    AppState::ScanningAdapter,   AppState::ConnectingAdapter,
    AppState::InitializingElm,   AppState::ConnectingVehicle,
    AppState::Polling,           AppState::Degraded,
    AppState::Fault,
};

static constexpr AppEvent kAllEvents[] = {
    AppEvent::HardwareReady,   AppEvent::ConfigLoaded,
    AppEvent::ConfigFailed,    AppEvent::AdapterFound,
    AppEvent::AdapterNotFound, AppEvent::AdapterConnected,
    AppEvent::AdapterLost,     AppEvent::ElmReady,
    AppEvent::ElmFailed,       AppEvent::VehicleLinkUp,
    AppEvent::VehicleLinkDown, AppEvent::DataValid,
    AppEvent::DataInvalid,     AppEvent::RetryTimerExpired,
    AppEvent::DisplayFailed,
};

// ---------------------------------------------------------------------------
//  O CAMINHO FELIZ, do boot ate ler dados do carro
// ---------------------------------------------------------------------------

void test_caminho_completo_do_boot_ate_polling(void) {
  AppState state = AppState::Boot;

  state = next_state(state, AppEvent::HardwareReady);
  assert_state(AppState::LoadingConfig, state);

  state = next_state(state, AppEvent::ConfigLoaded);
  assert_state(AppState::ScanningAdapter, state);

  state = next_state(state, AppEvent::AdapterFound);
  assert_state(AppState::ConnectingAdapter, state);

  state = next_state(state, AppEvent::AdapterConnected);
  assert_state(AppState::InitializingElm, state);

  state = next_state(state, AppEvent::ElmReady);
  assert_state(AppState::ConnectingVehicle, state);

  state = next_state(state, AppEvent::VehicleLinkUp);
  assert_state(AppState::Polling, state);

  TEST_ASSERT_TRUE(kanri::core::is_operational(state));
  TEST_ASSERT_FALSE(kanri::core::is_error_state(state));
}

// ---------------------------------------------------------------------------
//  COMPORTAMENTO FAIL-SAFE
// ---------------------------------------------------------------------------

// Configuracao corrompida na flash NAO impede o firmware de funcionar: ele
// segue com os valores padrao. Um aparelho que se recusa a ligar no
// estacionamento nao serve para nada.
void test_falha_de_config_nao_impede_o_boot(void) {
  const AppState state =
      next_state(AppState::LoadingConfig, AppEvent::ConfigFailed);
  assert_state(AppState::ScanningAdapter, state);
}

// Uma resposta corrompida isolada e rotina no barramento. Ela nao pode
// derrubar o link: quem decide "muitas falhas seguidas = link caiu" e a
// orquestracao, que entao emite VehicleLinkDown.
void test_resposta_invalida_isolada_nao_muda_de_estado(void) {
  assert_state(AppState::Polling,
               next_state(AppState::Polling, AppEvent::DataInvalid));
  assert_state(AppState::Polling,
               next_state(AppState::Polling, AppEvent::DataValid));
}

void test_perda_de_bluetooth_degrada_de_qualquer_ponto(void) {
  const AppState from[] = {
      AppState::ScanningAdapter, AppState::ConnectingAdapter,
      AppState::InitializingElm, AppState::ConnectingVehicle,
      AppState::Polling,
  };
  for (const AppState state : from) {
    assert_state(AppState::Degraded, next_state(state, AppEvent::AdapterLost));
  }
}

void test_ignicao_desligada_degrada_sem_travar(void) {
  assert_state(AppState::Degraded,
               next_state(AppState::Polling, AppEvent::VehicleLinkDown));
  assert_state(AppState::Degraded,
               next_state(AppState::ConnectingVehicle, AppEvent::VehicleLinkDown));
}

// Degradado nao e beco sem saida: o timer de retentativa traz de volta.
void test_degradado_sempre_tem_saida(void) {
  assert_state(AppState::ScanningAdapter,
               next_state(AppState::Degraded, AppEvent::RetryTimerExpired));
  // Atalho: se o canal voltou sozinho, nao precisa varrer de novo.
  assert_state(AppState::InitializingElm,
               next_state(AppState::Degraded, AppEvent::AdapterConnected));
}

// Sem display nao ha como avisar o motorista de nada. E o unico caminho para
// Fault, e ele vale de qualquer estado.
void test_falha_de_display_leva_a_fault_de_qualquer_estado(void) {
  for (const AppState state : kAllStates) {
    assert_state(AppState::Fault, next_state(state, AppEvent::DisplayFailed));
  }
}

void test_fault_e_terminal(void) {
  for (const AppEvent event : kAllEvents) {
    assert_state(AppState::Fault, next_state(AppState::Fault, event));
  }
}

void test_eventos_fora_de_contexto_sao_ignorados(void) {
  // "ElmReady" enquanto ainda procuramos o adaptador nao faz sentido.
  assert_state(AppState::ScanningAdapter,
               next_state(AppState::ScanningAdapter, AppEvent::ElmReady));
  // AdapterLost duplicado (chega sempre, na pratica).
  assert_state(AppState::Degraded,
               next_state(AppState::Degraded, AppEvent::AdapterLost));
}

// ---------------------------------------------------------------------------
//  INVARIANTES — para TODA combinacao (estado x evento)
// ---------------------------------------------------------------------------

// A GARANTIA CENTRAL DE SEGURANCA: Boot e inalcancavel a partir de qualquer
// outro estado. Voltar para Boot seria, na pratica, um reinicio — e um
// firmware que reinicia em loop dentro do carro e o pior resultado possivel.
//
// Note a condicao `state == AppState::Boot -> continue`: estar em Boot e
// ignorar um evento que nao se aplica devolve Boot, e isso e correto. O que
// nao pode existir e um caminho de VOLTA para Boot.
void test_boot_e_inalcancavel_de_qualquer_outro_estado(void) {
  for (const AppState state : kAllStates) {
    if (state == AppState::Boot) continue;
    for (const AppEvent event : kAllEvents) {
      const AppState next = next_state(state, event);
      TEST_ASSERT_TRUE_MESSAGE(
          next != AppState::Boot,
          "existe uma transicao que volta para Boot (reinicio)");
    }
  }
}

// Complemento do teste acima: em Boot, o unico evento que avanca e
// HardwareReady (e DisplayFailed, que leva a Fault). Todo o resto e ignorado.
void test_boot_so_avanca_com_hardware_pronto(void) {
  for (const AppEvent event : kAllEvents) {
    const AppState next = next_state(AppState::Boot, event);
    if (event == AppEvent::HardwareReady) {
      assert_state(AppState::LoadingConfig, next);
    } else if (event == AppEvent::DisplayFailed) {
      assert_state(AppState::Fault, next);
    } else {
      assert_state(AppState::Boot, next);
    }
  }
}

// A funcao e total: nunca devolve um estado fora do enum, para qualquer par.
void test_transicao_sempre_devolve_estado_valido(void) {
  for (const AppState state : kAllStates) {
    for (const AppEvent event : kAllEvents) {
      const AppState next = next_state(state, event);
      bool known = false;
      for (const AppState candidate : kAllStates) {
        if (next == candidate) known = true;
      }
      TEST_ASSERT_TRUE_MESSAGE(known, "transicao para um estado desconhecido");
      TEST_ASSERT_NOT_NULL(kanri::core::to_string(next));
    }
  }
}

// A funcao e pura: chamar duas vezes com a mesma entrada da o mesmo resultado.
void test_transicao_e_deterministica(void) {
  for (const AppState state : kAllStates) {
    for (const AppEvent event : kAllEvents) {
      assert_state(next_state(state, event), next_state(state, event));
    }
  }
}

void test_to_string_cobre_todos_os_estados_e_eventos(void) {
  for (const AppState state : kAllStates) {
    const char* name = kanri::core::to_string(state);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(name[0] != '\0');
  }
  for (const AppEvent event : kAllEvents) {
    const char* name = kanri::core::to_string(event);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(name[0] != '\0');
  }
}

// Memoria corrompida pode deixar um enum com valor fora da lista. Como
// AppState tem base uint8_t, TODOS os 256 valores sao representaveis — logo
// este cast e legal em C++, nao e comportamento indefinido, e nos permite
// testar de verdade a defesa em vez de so confiar nela.
void test_estado_corrompido_cai_em_fault(void) {
  const AppState corrupted = static_cast<AppState>(99);
  assert_state(AppState::Fault, next_state(corrupted, AppEvent::DataValid));
  // to_string tem de responder algo utilizavel, nunca nullptr: ele alimenta
  // log e display, e um nullptr ali viraria travamento.
  TEST_ASSERT_EQUAL_STRING("Unknown", kanri::core::to_string(corrupted));
}

void test_evento_corrompido_e_ignorado_com_seguranca(void) {
  const AppEvent corrupted = static_cast<AppEvent>(200);
  // Evento desconhecido em Polling nao pode derrubar a operacao normal.
  assert_state(AppState::Polling, next_state(AppState::Polling, corrupted));
  TEST_ASSERT_EQUAL_STRING("Unknown", kanri::core::to_string(corrupted));
}

// ---------------------------------------------------------------------------
//  BACKOFF EXPONENCIAL
// ---------------------------------------------------------------------------

void test_backoff_dobra_a_cada_falha(void) {
  RetryPolicy policy(1000, 32000);
  TEST_ASSERT_EQUAL_UINT32(1000, policy.current_delay_ms());
  policy.on_failure();
  TEST_ASSERT_EQUAL_UINT32(2000, policy.current_delay_ms());
  policy.on_failure();
  TEST_ASSERT_EQUAL_UINT32(4000, policy.current_delay_ms());
  policy.on_failure();
  TEST_ASSERT_EQUAL_UINT32(8000, policy.current_delay_ms());
}

void test_backoff_para_no_teto(void) {
  RetryPolicy policy(1000, 5000);
  for (int i = 0; i < 50; ++i) policy.on_failure();
  TEST_ASSERT_EQUAL_UINT32(5000, policy.current_delay_ms());
}

// O bug classico do backoff: dobrar um numero grande estoura o inteiro e o
// delay volta a ser minusculo, virando um loop de reconexao agressivo.
void test_backoff_nao_estoura_o_inteiro(void) {
  RetryPolicy policy(1000000, 4000000000U);
  for (int i = 0; i < 1000; ++i) {
    policy.on_failure();
    const std::uint32_t delay = policy.current_delay_ms();
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1000000, delay);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(4000000000U, delay);
  }
}

// Sem isto, o backoff ficaria preso no teto para sempre depois de uma unica
// sequencia de falhas — e a reconexao seguinte demoraria minutos sem motivo.
void test_sucesso_reseta_o_backoff(void) {
  RetryPolicy policy(1000, 32000);
  for (int i = 0; i < 10; ++i) policy.on_failure();
  TEST_ASSERT_EQUAL_UINT32(32000, policy.current_delay_ms());

  policy.on_success();
  TEST_ASSERT_EQUAL_UINT32(1000, policy.current_delay_ms());
  TEST_ASSERT_EQUAL_UINT32(0, policy.attempt_count());
}

void test_backoff_com_parametros_degenerados(void) {
  RetryPolicy zero_base(0, 1000);  // base 0 seria um loop apertado
  TEST_ASSERT_GREATER_THAN_UINT32(0, zero_base.current_delay_ms());

  RetryPolicy inverted(5000, 1000);  // teto menor que a base
  inverted.on_failure();
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(5000, inverted.current_delay_ms());
}

// millis() do ESP32 envolve a cada ~49,7 dias. Se calcularmos intervalos com
// subtracao sem sinal, a conta continua certa na virada.
void test_intervalo_correto_no_overflow_do_contador(void) {
  const std::uint32_t before = 0xFFFFFFF0U;  // 16 ms antes de virar
  const std::uint32_t after = 0x00000010U;   // 16 ms depois de virar
  TEST_ASSERT_EQUAL_UINT32(32, kanri::core::elapsed_ms(after, before));
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_caminho_completo_do_boot_ate_polling);

  RUN_TEST(test_falha_de_config_nao_impede_o_boot);
  RUN_TEST(test_resposta_invalida_isolada_nao_muda_de_estado);
  RUN_TEST(test_perda_de_bluetooth_degrada_de_qualquer_ponto);
  RUN_TEST(test_ignicao_desligada_degrada_sem_travar);
  RUN_TEST(test_degradado_sempre_tem_saida);
  RUN_TEST(test_falha_de_display_leva_a_fault_de_qualquer_estado);
  RUN_TEST(test_fault_e_terminal);
  RUN_TEST(test_eventos_fora_de_contexto_sao_ignorados);

  RUN_TEST(test_boot_e_inalcancavel_de_qualquer_outro_estado);
  RUN_TEST(test_boot_so_avanca_com_hardware_pronto);
  RUN_TEST(test_transicao_sempre_devolve_estado_valido);
  RUN_TEST(test_transicao_e_deterministica);
  RUN_TEST(test_to_string_cobre_todos_os_estados_e_eventos);
  RUN_TEST(test_estado_corrompido_cai_em_fault);
  RUN_TEST(test_evento_corrompido_e_ignorado_com_seguranca);

  RUN_TEST(test_backoff_dobra_a_cada_falha);
  RUN_TEST(test_backoff_para_no_teto);
  RUN_TEST(test_backoff_nao_estoura_o_inteiro);
  RUN_TEST(test_sucesso_reseta_o_backoff);
  RUN_TEST(test_backoff_com_parametros_degenerados);
  RUN_TEST(test_intervalo_correto_no_overflow_do_contador);

  return UNITY_END();
}
