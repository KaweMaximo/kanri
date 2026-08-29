// ============================================================================
//  Testes do botao
// ============================================================================
//  Contato mecanico treme: ao pressionar, o sinal pula entre aberto e fechado
//  dezenas de vezes antes de assentar. Lido cru, um toque vira cinco — e o
//  motorista veria a grandeza pular de temperatura para tensao sem entender.
//
//  Reproduzir tremulacao de proposito no hardware e dificil. Como funcao pura
//  do tempo, cada padrao vira um teste de microssegundos.
// ============================================================================

#include <unity.h>

#include "kanri_core/button.h"

using kanri::core::Button;
using kanri::core::ButtonEvent;

void setUp(void) {}
void tearDown(void) {}

namespace {

/// Alimenta o botao ao longo de um intervalo, contando os eventos.
struct Contagem {
  int clicks = 0, longos = 0, soltas = 0;
};

void alimentar(Button& b, Contagem& c, bool nivel, std::uint32_t de,
               std::uint32_t ate, std::uint32_t passo = 1) {
  for (std::uint32_t t = de; t < ate; t += passo) {
    switch (b.update(nivel, t)) {
      case ButtonEvent::Click:     ++c.clicks; break;
      case ButtonEvent::LongPress: ++c.longos; break;
      case ButtonEvent::Released:  ++c.soltas; break;
      case ButtonEvent::None:      break;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
//  O PROBLEMA QUE ESTE MODULO EXISTE PARA RESOLVER
// ---------------------------------------------------------------------------

// Tremulacao ao pressionar: o sinal alterna rapido antes de assentar. Isso
// tem de virar UM clique, nunca varios.
void test_tremulacao_vira_um_clique_so(void) {
  Button b;
  Contagem c;
  std::uint32_t t = 0;

  b.update(false, t++);

  // 20 alternancias em 20 ms — tremulacao tipica de um botao tatil.
  for (int i = 0; i < 20; ++i) {
    if (b.update(i % 2 == 0, t++) == ButtonEvent::Click) ++c.clicks;
  }
  // Assenta pressionado.
  alimentar(b, c, true, t, t + 100);
  t += 100;
  // Solta, tremendo de novo.
  for (int i = 0; i < 20; ++i) {
    if (b.update(i % 2 == 0, t++) == ButtonEvent::Click) ++c.clicks;
  }
  alimentar(b, c, false, t, t + 100);

  TEST_ASSERT_EQUAL_INT(1, c.clicks);
}

void test_toque_limpo_gera_um_clique(void) {
  Button b;
  Contagem c;
  alimentar(b, c, false, 0, 100);
  alimentar(b, c, true, 100, 300);
  alimentar(b, c, false, 300, 400);
  TEST_ASSERT_EQUAL_INT(1, c.clicks);
  TEST_ASSERT_EQUAL_INT(0, c.longos);
}

// Ruido curto demais — interferencia eletrica, nao dedo — nao pode virar
// clique. Dentro do carro isso acontece: o cabo do botao passa perto da
// ignicao.
void test_pulso_curto_demais_e_ignorado(void) {
  Button b;
  Contagem c;
  alimentar(b, c, false, 0, 100);
  alimentar(b, c, true, 100, 115);   // 15 ms, abaixo do debounce de 30
  alimentar(b, c, false, 115, 300);
  TEST_ASSERT_EQUAL_INT(0, c.clicks);
}

// ---------------------------------------------------------------------------
//  TOQUE LONGO
// ---------------------------------------------------------------------------

void test_toque_longo_dispara_uma_vez(void) {
  Button b;
  Contagem c;
  alimentar(b, c, false, 0, 100);
  alimentar(b, c, true, 100, 3000);  // segura por quase 3 s
  TEST_ASSERT_EQUAL_INT(1, c.longos);
  TEST_ASSERT_EQUAL_INT(0, c.clicks);
}

// Depois de um toque longo, soltar NAO pode gerar clique tambem: uma acao
// dispararia as duas coisas.
void test_toque_longo_nao_vira_clique_ao_soltar(void) {
  Button b;
  Contagem c;
  alimentar(b, c, false, 0, 100);
  alimentar(b, c, true, 100, 1500);
  alimentar(b, c, false, 1500, 1700);
  TEST_ASSERT_EQUAL_INT(1, c.longos);
  TEST_ASSERT_EQUAL_INT(0, c.clicks);
  TEST_ASSERT_EQUAL_INT(1, c.soltas);
}

void test_toque_no_limite_ainda_e_clique(void) {
  Button b;
  Contagem c;
  alimentar(b, c, false, 0, 100);
  alimentar(b, c, true, 100, 100 + 799);  // logo abaixo dos 800 ms
  alimentar(b, c, false, 899, 1000);
  TEST_ASSERT_EQUAL_INT(1, c.clicks);
  TEST_ASSERT_EQUAL_INT(0, c.longos);
}

// ---------------------------------------------------------------------------
//  CASOS DE BORDA
// ---------------------------------------------------------------------------

// Ligar o aparelho com o dedo no botao — ou com o botao em curto — nao pode
// gerar um clique fantasma logo no boot.
void test_ligar_com_o_botao_pressionado_nao_gera_clique(void) {
  Button b;
  Contagem c;
  alimentar(b, c, true, 0, 2000);   // ja comeca pressionado
  TEST_ASSERT_EQUAL_INT(0, c.clicks);
  // Mas o toque longo conta, porque o dedo esta la de verdade.
  TEST_ASSERT_EQUAL_INT(1, c.longos);
}

void test_varios_toques_seguidos(void) {
  Button b;
  Contagem c;
  std::uint32_t t = 0;
  alimentar(b, c, false, t, t + 100); t += 100;
  for (int i = 0; i < 5; ++i) {
    alimentar(b, c, true, t, t + 200); t += 200;
    alimentar(b, c, false, t, t + 200); t += 200;
  }
  TEST_ASSERT_EQUAL_INT(5, c.clicks);
}

// millis() envolve a cada ~49,7 dias. Com o aparelho ligado direto no carro,
// isso vai acontecer — e um toque na virada nao pode ser perdido nem
// duplicado.
void test_funciona_na_virada_do_contador(void) {
  Button b;
  Contagem c;
  const std::uint32_t base = 0xFFFFFF00U;  // 256 ms antes de virar

  alimentar(b, c, false, base, base + 100, 1);
  // Pressiona atravessando a virada.
  for (std::uint32_t i = 0; i < 200; ++i) {
    if (b.update(true, base + 100 + i) == ButtonEvent::Click) ++c.clicks;
  }
  for (std::uint32_t i = 0; i < 200; ++i) {
    if (b.update(false, base + 300 + i) == ButtonEvent::Click) ++c.clicks;
  }
  TEST_ASSERT_EQUAL_INT(1, c.clicks);
}

void test_estado_estavel_e_consultavel(void) {
  Button b;
  b.update(false, 0);
  TEST_ASSERT_FALSE(b.is_pressed());
  for (std::uint32_t t = 10; t < 100; ++t) b.update(true, t);
  TEST_ASSERT_TRUE(b.is_pressed());
  for (std::uint32_t t = 100; t < 200; ++t) b.update(false, t);
  TEST_ASSERT_FALSE(b.is_pressed());
}

// Tempos customizados devem valer.
void test_debounce_configuravel(void) {
  Button rapido(5, 300);
  Contagem c;
  alimentar(rapido, c, false, 0, 50);
  alimentar(rapido, c, true, 50, 60);   // 10 ms basta com debounce de 5
  alimentar(rapido, c, false, 60, 120);
  TEST_ASSERT_EQUAL_INT(1, c.clicks);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tremulacao_vira_um_clique_so);
  RUN_TEST(test_toque_limpo_gera_um_clique);
  RUN_TEST(test_pulso_curto_demais_e_ignorado);
  RUN_TEST(test_toque_longo_dispara_uma_vez);
  RUN_TEST(test_toque_longo_nao_vira_clique_ao_soltar);
  RUN_TEST(test_toque_no_limite_ainda_e_clique);
  RUN_TEST(test_ligar_com_o_botao_pressionado_nao_gera_clique);
  RUN_TEST(test_varios_toques_seguidos);
  RUN_TEST(test_funciona_na_virada_do_contador);
  RUN_TEST(test_estado_estavel_e_consultavel);
  RUN_TEST(test_debounce_configuravel);
  return UNITY_END();
}
