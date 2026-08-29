// ============================================================================
//  Testes do padrao de pisca do LED
// ============================================================================
//  Dentro do carro o LED e o unico canal de status. Se o padrao de "operando"
//  ficar parecido com o de "degradado", o motorista le errado o estado do
//  aparelho — e nao adianta o firmware estar certo por dentro.
//
//  Como tudo aqui e funcao pura do tempo, testar um heartbeat de 2 segundos
//  custa microssegundos: basta avaliar a funcao nos instantes que interessam.
// ============================================================================

#include <unity.h>

#include "kanri_core/led_pattern.h"

using kanri::core::AppState;
using kanri::core::led_should_be_on;
using kanri::core::LedPattern;
using kanri::core::pattern_cycle_ms;
using kanri::core::pattern_for;

void setUp(void) {}
void tearDown(void) {}

static constexpr AppState kTodosEstados[] = {
    AppState::Boot,              AppState::LoadingConfig,
    AppState::ScanningAdapter,   AppState::ConnectingAdapter,
    AppState::InitializingElm,   AppState::ConnectingVehicle,
    AppState::Polling,           AppState::Degraded,
    AppState::Fault,
};

/// Conta as bordas de subida (apagado -> aceso) ao longo de uma janela.
/// E assim que uma pessoa conta piscadas olhando para o LED.
static int contar_piscadas(AppState estado, std::uint32_t janela_ms) {
  int piscadas = 0;
  bool anterior = false;
  for (std::uint32_t t = 0; t < janela_ms; ++t) {
    const bool agora = led_should_be_on(estado, t);
    if (agora && !anterior) ++piscadas;
    anterior = agora;
  }
  return piscadas;
}

// ---------------------------------------------------------------------------
//  O VOCABULARIO VISUAL
// ---------------------------------------------------------------------------

// O pedido original: enquanto varre o Bluetooth, o LED pisca rapido para
// mostrar que a busca esta acontecendo.
void test_procurando_pisca_rapido_e_continuo(void) {
  const LedPattern p = pattern_for(AppState::ScanningAdapter);
  TEST_ASSERT_EQUAL_UINT32(200, pattern_cycle_ms(p));  // 5 Hz

  // Em 1 segundo, cinco piscadas.
  TEST_ASSERT_EQUAL_INT(5, contar_piscadas(AppState::ScanningAdapter, 1000));

  // E realmente alterna: aceso no comeco do pulso, apagado no meio do ciclo.
  TEST_ASSERT_TRUE(led_should_be_on(AppState::ScanningAdapter, 0));
  TEST_ASSERT_TRUE(led_should_be_on(AppState::ScanningAdapter, 99));
  TEST_ASSERT_FALSE(led_should_be_on(AppState::ScanningAdapter, 100));
  TEST_ASSERT_FALSE(led_should_be_on(AppState::ScanningAdapter, 199));
  TEST_ASSERT_TRUE(led_should_be_on(AppState::ScanningAdapter, 200));
}

// Progresso da conexao: 2, 3, 4 piscadas por ciclo. Da para acompanhar o
// avanco olhando so para o LED.
void test_progressao_de_piscadas_na_conexao(void) {
  TEST_ASSERT_EQUAL_UINT8(2, pattern_for(AppState::ConnectingAdapter).pulse_count);
  TEST_ASSERT_EQUAL_UINT8(3, pattern_for(AppState::InitializingElm).pulse_count);
  TEST_ASSERT_EQUAL_UINT8(4, pattern_for(AppState::ConnectingVehicle).pulse_count);

  // Contado como uma pessoa contaria, num ciclo completo de cada um.
  TEST_ASSERT_EQUAL_INT(
      2, contar_piscadas(AppState::ConnectingAdapter,
                         pattern_cycle_ms(pattern_for(AppState::ConnectingAdapter))));
  TEST_ASSERT_EQUAL_INT(
      3, contar_piscadas(AppState::InitializingElm,
                         pattern_cycle_ms(pattern_for(AppState::InitializingElm))));
  TEST_ASSERT_EQUAL_INT(
      4, contar_piscadas(AppState::ConnectingVehicle,
                         pattern_cycle_ms(pattern_for(AppState::ConnectingVehicle))));
}

// Operando: uma batida curta a cada 2 s. Discreto de proposito — nao pode
// competir com a estrada pela atencao do motorista.
void test_operando_e_um_heartbeat_discreto(void) {
  const LedPattern p = pattern_for(AppState::Polling);
  TEST_ASSERT_EQUAL_UINT32(2000, pattern_cycle_ms(p));
  TEST_ASSERT_EQUAL_INT(1, contar_piscadas(AppState::Polling, 2000));

  TEST_ASSERT_TRUE(led_should_be_on(AppState::Polling, 0));
  TEST_ASSERT_TRUE(led_should_be_on(AppState::Polling, 59));
  TEST_ASSERT_FALSE(led_should_be_on(AppState::Polling, 60));
  TEST_ASSERT_FALSE(led_should_be_on(AppState::Polling, 1500));
  TEST_ASSERT_TRUE(led_should_be_on(AppState::Polling, 2000));  // proximo ciclo

  // Aceso menos de 5% do tempo: e uma batida, nao uma luz.
  int aceso = 0;
  for (std::uint32_t t = 0; t < 2000; ++t) {
    if (led_should_be_on(AppState::Polling, t)) ++aceso;
  }
  TEST_ASSERT_LESS_THAN_INT(100, aceso);
}

// Falha terminal e o UNICO padrao que nao pisca. Reconhecivel de relance,
// sem precisar contar nada.
void test_falha_terminal_fica_aceso_fixo(void) {
  // Aceso em TODO instante — inclusive muito depois, para descartar um ciclo
  // longo disfarcado de "fixo".
  for (std::uint32_t t = 0; t < 60000; t += 37) {
    TEST_ASSERT_TRUE(led_should_be_on(AppState::Fault, t));
  }
  // E nao tem ciclo: nao ha o que repetir num padrao constante.
  TEST_ASSERT_EQUAL_UINT32(0, pattern_cycle_ms(pattern_for(AppState::Fault)));

  // Nao usamos contar_piscadas() aqui de proposito: aquele contador comeca
  // supondo o LED apagado, entao um padrao SEMPRE aceso registra uma borda
  // artificial no instante zero. Contar bordas responde "quantas vezes
  // piscou"; a pergunta certa para o Fault e "ele chega a apagar?".
}

void test_degradado_pisca_lento_e_simetrico(void) {
  const LedPattern p = pattern_for(AppState::Degraded);
  TEST_ASSERT_EQUAL_UINT16(p.pulse_on_ms, p.pulse_off_ms);  // simetrico
  TEST_ASSERT_EQUAL_UINT32(800, pattern_cycle_ms(p));       // 1,25 Hz
}

// ---------------------------------------------------------------------------
//  O QUE TORNA O VOCABULARIO LEGIVEL
// ---------------------------------------------------------------------------

// Se dois estados piscassem igual, o LED mentiria. Este teste garante que
// cada estado tem uma assinatura propria.
void test_cada_estado_tem_padrao_distinguivel(void) {
  for (std::size_t i = 0; i < 9; ++i) {
    for (std::size_t j = i + 1; j < 9; ++j) {
      const LedPattern a = pattern_for(kTodosEstados[i]);
      const LedPattern b = pattern_for(kTodosEstados[j]);
      const bool iguais = a.pulse_on_ms == b.pulse_on_ms &&
                          a.pulse_off_ms == b.pulse_off_ms &&
                          a.pulse_count == b.pulse_count &&
                          a.rest_ms == b.rest_ms;
      // Boot e LoadingConfig podem compartilhar: sao "acordando", duram
      // milissegundos e o usuario nao os distingue mesmo.
      const bool excecao =
          (kTodosEstados[i] == AppState::Boot &&
           kTodosEstados[j] == AppState::LoadingConfig);
      if (!excecao) {
        TEST_ASSERT_FALSE_MESSAGE(
            iguais, "dois estados piscam igual: o LED ficaria ambiguo");
      }
    }
  }
}

// Operando tem de ser visivelmente MENOS agitado que procurando. Se o
// heartbeat fosse rapido, "tudo certo" pareceria "com problema".
void test_operando_e_menos_agitado_que_procurando(void) {
  const int procurando = contar_piscadas(AppState::ScanningAdapter, 4000);
  const int operando = contar_piscadas(AppState::Polling, 4000);
  TEST_ASSERT_GREATER_THAN_INT(operando * 5, procurando);
}

// ---------------------------------------------------------------------------
//  INVARIANTES E CASOS DEGENERADOS
// ---------------------------------------------------------------------------

// Nenhum estado pode deixar o LED apagado para sempre: um LED morto e
// indistinguivel de um aparelho sem energia.
void test_nenhum_estado_deixa_o_led_permanentemente_apagado(void) {
  for (const AppState s : kTodosEstados) {
    bool acendeu = false;
    for (std::uint32_t t = 0; t < 6000 && !acendeu; ++t) {
      if (led_should_be_on(s, t)) acendeu = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(acendeu, "existe estado com o LED sempre apagado");
  }
}

// O padrao repete a cada ciclo — e o que faz o LED ser legivel.
void test_padrao_e_periodico(void) {
  for (const AppState s : kTodosEstados) {
    const std::uint32_t ciclo = pattern_cycle_ms(pattern_for(s));
    if (ciclo == 0) continue;  // padrao constante
    for (std::uint32_t t = 0; t < ciclo; t += 7) {
      TEST_ASSERT_EQUAL_INT(led_should_be_on(s, t),
                            led_should_be_on(s, t + ciclo));
      TEST_ASSERT_EQUAL_INT(led_should_be_on(s, t),
                            led_should_be_on(s, t + (ciclo * 3)));
    }
  }
}

// Estado corrompido nao pode acender o LED sugerindo um estado que nao existe.
void test_estado_corrompido_deixa_o_led_apagado(void) {
  const AppState corrompido = static_cast<AppState>(200);
  TEST_ASSERT_EQUAL_UINT8(kanri::core::kAlwaysOff,
                          pattern_for(corrompido).pulse_count);
  for (std::uint32_t t = 0; t < 3000; t += 13) {
    TEST_ASSERT_FALSE(led_should_be_on(corrompido, t));
  }
}

// Padrao mal formado (passo zero) nao pode dividir por zero nem travar.
void test_padroes_degenerados_nao_quebram(void) {
  const LedPattern zerado = {0, 0, 3, 0};
  TEST_ASSERT_FALSE(led_should_be_on(zerado, 0));
  TEST_ASSERT_FALSE(led_should_be_on(zerado, 12345));
  TEST_ASSERT_EQUAL_UINT32(0, pattern_cycle_ms(zerado));

  const LedPattern so_pausa = {0, 0, 1, 500};
  TEST_ASSERT_FALSE(led_should_be_on(so_pausa, 0));
}

// millis() envolve a cada ~49,7 dias. Como o tempo decorrido e calculado com
// subtracao sem sinal, o padrao continua correto na virada.
void test_padrao_sobrevive_ao_overflow_do_contador(void) {
  const std::uint32_t antes = 0xFFFFFF00U;
  const std::uint32_t depois = 0x00000100U;  // 512 ms depois
  const std::uint32_t decorrido = kanri::core::elapsed_ms(depois, antes);
  TEST_ASSERT_EQUAL_UINT32(512, decorrido);
  TEST_ASSERT_EQUAL_INT(led_should_be_on(AppState::ScanningAdapter, 512),
                        led_should_be_on(AppState::ScanningAdapter, decorrido));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_procurando_pisca_rapido_e_continuo);
  RUN_TEST(test_progressao_de_piscadas_na_conexao);
  RUN_TEST(test_operando_e_um_heartbeat_discreto);
  RUN_TEST(test_falha_terminal_fica_aceso_fixo);
  RUN_TEST(test_degradado_pisca_lento_e_simetrico);

  RUN_TEST(test_cada_estado_tem_padrao_distinguivel);
  RUN_TEST(test_operando_e_menos_agitado_que_procurando);

  RUN_TEST(test_nenhum_estado_deixa_o_led_permanentemente_apagado);
  RUN_TEST(test_padrao_e_periodico);
  RUN_TEST(test_estado_corrompido_deixa_o_led_apagado);
  RUN_TEST(test_padroes_degenerados_nao_quebram);
  RUN_TEST(test_padrao_sobrevive_ao_overflow_do_contador);
  return UNITY_END();
}
