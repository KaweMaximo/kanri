// ============================================================================
//  Testes da suavizacao do painel
// ============================================================================
//  O que se quer provar aqui e uma sensacao: que o painel pareca um
//  instrumento e nao um contador digital. Sensacao nao se testa — mas as
//  propriedades que a produzem, sim:
//
//    - o valor ANDA em vez de saltar        (deixa de ser robotico)
//    - mas CHEGA, e nao fica tremendo       (senao trocamos um defeito por outro)
//    - mudanca grande vai DIRETO            (senao o painel fica lento, que e pior)
//    - leitura invalida nao deixa rastro    (senao mostra medida que o carro nao teve)
// ============================================================================

#include <unity.h>

#include "kanri_display/smoothing.h"

using kanri::display::step_toward;
using kanri::display::ValueSmoother;

void setUp(void) {}
void tearDown(void) {}

namespace {
// Roda N quadros mirando o mesmo alvo.
float assentar(ValueSmoother& s, float alvo, int quadros = 60) {
  float v = s.value();
  for (int i = 0; i < quadros; ++i) v = s.update(alvo);
  return v;
}
}  // namespace

// ---------------------------------------------------------------------------
//  O comportamento central
// ---------------------------------------------------------------------------

// A PRIMEIRA leitura vale inteira. Deslizar a partir de zero mostraria a
// temperatura do motor subindo do nada, na frente do motorista.
void test_primeira_leitura_nao_desliza_a_partir_do_zero(void) {
  ValueSmoother s(8000.0F);
  TEST_ASSERT_EQUAL_FLOAT(933.0F, s.update(933.0F));
}

// O CASO QUE MOTIVA O MODULO: o rodizio entrega uma medida por segundo, e
// escrevendo direto o numero fica parado e SALTA. Suavizado, ele anda.
void test_valor_anda_em_vez_de_saltar(void) {
  ValueSmoother s(8000.0F);
  s.update(900.0F);

  const float primeiro = s.update(1000.0F);
  TEST_ASSERT_TRUE_MESSAGE(primeiro > 900.0F, "nao andou");
  TEST_ASSERT_TRUE_MESSAGE(primeiro < 1000.0F, "saltou direto");
}

// Mas precisa CHEGAR. Um passo proporcional puro se aproxima para sempre sem
// alcancar, e o ultimo digito ficaria tremendo — o defeito que viemos tirar.
void test_valor_chega_ao_alvo_e_para(void) {
  ValueSmoother s(8000.0F);
  s.update(900.0F);
  TEST_ASSERT_EQUAL_FLOAT(1000.0F, assentar(s, 1000.0F));

  // E parado, nao se mexe mais.
  for (int i = 0; i < 100; ++i) {
    TEST_ASSERT_EQUAL_FLOAT(1000.0F, s.update(1000.0F));
  }
}

// Pisar no acelerador nao pode chegar ao painel um segundo depois. Mudanca
// grande e evento, nao ruido: vai direto.
void test_mudanca_grande_vai_direto(void) {
  ValueSmoother s(8000.0F);
  s.update(900.0F);
  TEST_ASSERT_EQUAL_FLOAT(4000.0F, s.update(4000.0F));
}

// O limiar acompanha a FAIXA da grandeza. 100 e ruido em rpm e e enorme em
// graus — um limiar absoluto serviria a uma medida e estragaria as outras.
void test_limiar_de_salto_acompanha_a_grandeza(void) {
  ValueSmoother rotacao(8000.0F);  // 10% = 800 rpm
  rotacao.update(900.0F);
  const float r = rotacao.update(1200.0F);  // 300 rpm: desliza
  TEST_ASSERT_TRUE(r > 900.0F && r < 1200.0F);

  ValueSmoother temp(255.0F);  // 10% = 25 graus
  temp.update(80.0F);
  TEST_ASSERT_EQUAL_FLOAT(120.0F, temp.update(120.0F));  // 40 graus: salta
}

// ---------------------------------------------------------------------------
//  Direcao e convergencia
// ---------------------------------------------------------------------------

void test_desce_tao_bem_quanto_sobe(void) {
  ValueSmoother s(8000.0F);
  s.update(1000.0F);
  const float v = s.update(900.0F);
  TEST_ASSERT_TRUE(v < 1000.0F && v > 900.0F);
  TEST_ASSERT_EQUAL_FLOAT(900.0F, assentar(s, 900.0F));
}

// Converge em poucos quadros. A 30 Hz, 20 quadros sao 0,66 s — o painel nao
// pode levar mais que isso para acompanhar uma mudanca comum.
void test_converge_rapido_o_bastante(void) {
  ValueSmoother s(8000.0F);
  s.update(1000.0F);
  for (int i = 0; i < 20; ++i) s.update(1500.0F);
  TEST_ASSERT_FLOAT_WITHIN(1.0F, 1500.0F, s.value());
}

// Nunca ultrapassa o alvo: um painel que passa do valor e volta pareceria
// instavel, mesmo convergindo.
void test_nunca_passa_do_alvo(void) {
  ValueSmoother s(8000.0F);
  s.update(0.0F);
  for (int i = 0; i < 200; ++i) {
    const float v = s.update(500.0F);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(500.0F, v);
  }
}

// ---------------------------------------------------------------------------
//  Entrada nao confiavel
// ---------------------------------------------------------------------------

// Mesma regra do resto do projeto: lixo nao entra. E aqui e pior que mostrar
// errado uma vez — entraria no historico e contaminaria os quadros seguintes.
void test_valor_invalido_nao_contamina_o_historico(void) {
  ValueSmoother s(8000.0F);
  s.update(1000.0F);

  const float nan = 0.0F / 0.0F;
  TEST_ASSERT_EQUAL_FLOAT(1000.0F, s.update(nan));
  TEST_ASSERT_EQUAL_FLOAT(1000.0F, s.update(1e38F));
  TEST_ASSERT_EQUAL_FLOAT(1000.0F, s.value());
}

// Ao voltar de uma leitura invalida, o valor NAO pode deslizar a partir do
// numero antigo: mostraria por alguns quadros uma medida que o carro nao teve.
void test_reset_faz_a_proxima_leitura_valer_inteira(void) {
  ValueSmoother s(8000.0F);
  s.update(3000.0F);
  s.reset();
  TEST_ASSERT_FALSE(s.started());
  TEST_ASSERT_EQUAL_FLOAT(800.0F, s.update(800.0F));
}

// Faixa degenerada nao pode virar divisao por zero nem limiar absurdo.
void test_faixa_invalida_cai_num_padrao_utilizavel(void) {
  ValueSmoother zero(0.0F);
  zero.update(10.0F);
  const float v = zero.update(12.0F);
  TEST_ASSERT_TRUE(v >= 10.0F && v <= 12.0F);

  ValueSmoother negativa(-5.0F);
  negativa.update(10.0F);
  TEST_ASSERT_TRUE(negativa.value() > 0.0F);
}

// ---------------------------------------------------------------------------
//  Rampa do brilho
// ---------------------------------------------------------------------------

// O brilho e inteiro e pequeno (0..15 no chip). Uma media daria degraus
// fracionarios que o chip nao tem, entao a rampa anda de um em um.
void test_rampa_anda_um_passo_por_quadro(void) {
  TEST_ASSERT_EQUAL_UINT8(6, step_toward(5, 15));
  TEST_ASSERT_EQUAL_UINT8(4, step_toward(5, 0));
  TEST_ASSERT_EQUAL_UINT8(5, step_toward(5, 5));
}

void test_rampa_chega_e_para(void) {
  std::uint8_t v = 0;
  for (int i = 0; i < 100; ++i) v = step_toward(v, 15);
  TEST_ASSERT_EQUAL_UINT8(15, v);

  for (int i = 0; i < 100; ++i) v = step_toward(v, 3);
  TEST_ASSERT_EQUAL_UINT8(3, v);
}

// Ir de uma ponta a outra da escala do MAX7219 nao pode demorar: 15 quadros
// a 30 Hz sao 0,5 s, que e transicao — acima disso viraria atraso.
void test_rampa_atravessa_a_escala_em_poucos_quadros(void) {
  std::uint8_t v = 0;
  int quadros = 0;
  while (v != 15 && quadros < 100) {
    v = step_toward(v, 15);
    ++quadros;
  }
  TEST_ASSERT_EQUAL_UINT8(15, v);
  TEST_ASSERT_LESS_OR_EQUAL_INT(15, quadros);
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_primeira_leitura_nao_desliza_a_partir_do_zero);
  RUN_TEST(test_valor_anda_em_vez_de_saltar);
  RUN_TEST(test_valor_chega_ao_alvo_e_para);
  RUN_TEST(test_mudanca_grande_vai_direto);
  RUN_TEST(test_limiar_de_salto_acompanha_a_grandeza);

  RUN_TEST(test_desce_tao_bem_quanto_sobe);
  RUN_TEST(test_converge_rapido_o_bastante);
  RUN_TEST(test_nunca_passa_do_alvo);

  RUN_TEST(test_valor_invalido_nao_contamina_o_historico);
  RUN_TEST(test_reset_faz_a_proxima_leitura_valer_inteira);
  RUN_TEST(test_faixa_invalida_cai_num_padrao_utilizavel);

  RUN_TEST(test_rampa_anda_um_passo_por_quadro);
  RUN_TEST(test_rampa_chega_e_para);
  RUN_TEST(test_rampa_atravessa_a_escala_em_poucos_quadros);
  return UNITY_END();
}
