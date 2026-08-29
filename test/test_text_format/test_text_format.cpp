// ============================================================================
//  Testes da formatacao de numeros para o display
// ============================================================================
//  Estas funcoes substituem o snprintf, que arrastaria ~10 KB de flash. O
//  preco de escrever a propria e ter de provar que ela nao estoura buffer,
//  nao esquece o terminador e arredonda certo. E o que estes testes fazem.
// ============================================================================

#include <unity.h>

#include <cstring>

#include "kanri_display/text_format.h"

using kanri::display::copy_text;
using kanri::display::format_fixed;
using kanri::display::format_int;
using kanri::display::format_row;

void setUp(void) {}
void tearDown(void) {}

void test_inteiros(void) {
  char b[16];
  format_int(0, b, sizeof(b));      TEST_ASSERT_EQUAL_STRING("0", b);
  format_int(1726, b, sizeof(b));   TEST_ASSERT_EQUAL_STRING("1726", b);
  format_int(-40, b, sizeof(b));    TEST_ASSERT_EQUAL_STRING("-40", b);
  format_int(7, b, sizeof(b));      TEST_ASSERT_EQUAL_STRING("7", b);
  format_int(2147483647, b, sizeof(b)); TEST_ASSERT_EQUAL_STRING("2147483647", b);
}

// Negar INT32_MIN estoura o int32. A conversao tem de passar por unsigned.
void test_menor_inteiro_possivel_nao_estoura(void) {
  char b[16];
  format_int(-2147483647 - 1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("-2147483648", b);
}

void test_decimais(void) {
  char b[16];
  format_fixed(13.8F, 1, b, sizeof(b));   TEST_ASSERT_EQUAL_STRING("13.8", b);
  format_fixed(83.0F, 0, b, sizeof(b));   TEST_ASSERT_EQUAL_STRING("83", b);
  format_fixed(0.5F, 1, b, sizeof(b));    TEST_ASSERT_EQUAL_STRING("0.5", b);
  format_fixed(-4.25F, 2, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("-4.25", b);
}

// 13.05 nao pode virar "13.5": o zero da fracao precisa aparecer.
void test_zero_a_esquerda_da_fracao(void) {
  char b[16];
  format_fixed(13.05F, 2, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("13.05", b);
  format_fixed(1.007F, 3, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("1.007", b);
}

void test_arredondamento(void) {
  char b[16];
  format_fixed(13.849F, 1, b, sizeof(b)); TEST_ASSERT_EQUAL_STRING("13.8", b);
  format_fixed(13.85F, 1, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("13.9", b);
  format_fixed(82.6F, 0, b, sizeof(b));   TEST_ASSERT_EQUAL_STRING("83", b);
}

// NaN e infinito nao tem representacao util num painel: viram "--".
void test_nan_e_infinito_viram_tracos(void) {
  char b[16];
  const float zero = 0.0F;
  format_fixed(zero / zero, 1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("--", b);
  format_fixed(1.0e30F, 1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("--", b);
  format_fixed(-1.0e30F, 1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("--", b);
}

// ---------------------------------------------------------------------------
//  A GARANTIA QUE MAIS IMPORTA: nunca escrever fora do buffer
// ---------------------------------------------------------------------------

void test_buffer_pequeno_trunca_com_terminador(void) {
  char b[4];
  std::memset(b, 'X', sizeof(b));
  format_int(123456, b, sizeof(b));
  TEST_ASSERT_EQUAL_UINT32(3, std::strlen(b));  // cabe 3 + nulo
  TEST_ASSERT_EQUAL_CHAR('\0', b[3]);
}

void test_buffer_de_um_byte_so_cabe_o_terminador(void) {
  char b[1] = {'X'};
  TEST_ASSERT_EQUAL_UINT32(0, format_int(42, b, sizeof(b)));
  TEST_ASSERT_EQUAL_CHAR('\0', b[0]);
  TEST_ASSERT_EQUAL_UINT32(0, format_fixed(1.5F, 1, b, sizeof(b)));
  TEST_ASSERT_EQUAL_CHAR('\0', b[0]);
}

void test_argumentos_nulos_nao_quebram(void) {
  char b[8];
  TEST_ASSERT_EQUAL_UINT32(0, format_int(1, nullptr, 8));
  TEST_ASSERT_EQUAL_UINT32(0, format_int(1, b, 0));
  TEST_ASSERT_EQUAL_UINT32(0, format_fixed(1.0F, 1, nullptr, 8));
  TEST_ASSERT_EQUAL_UINT32(0, copy_text("x", nullptr, 8));
  copy_text(nullptr, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("", b);
  TEST_ASSERT_EQUAL_UINT32(0, format_row("a", "b", "c", nullptr, 8));
}

// Toda saida termina em nulo, para QUALQUER combinacao. Um driver que faca
// drawString num buffer sem terminador imprime lixo da memoria na tela.
void test_saida_sempre_termina_em_nulo(void) {
  std::uint32_t semente = 0xC0DE;
  for (int it = 0; it < 4000; ++it) {
    semente = semente * 1103515245U + 12345U;
    const std::size_t cap = 1 + ((semente >> 16) % 24);
    char b[32];
    std::memset(b, 'Z', sizeof(b));

    semente = semente * 1103515245U + 12345U;
    const std::int32_t valor = static_cast<std::int32_t>(semente);
    const std::uint8_t casas = static_cast<std::uint8_t>((semente >> 8) % 5);

    format_int(valor, b, cap);
    TEST_ASSERT_LESS_THAN_UINT(cap, std::strlen(b));

    std::memset(b, 'Z', sizeof(b));
    format_fixed(static_cast<float>(valor) / 100.0F, casas, b, cap);
    TEST_ASSERT_LESS_THAN_UINT(cap, std::strlen(b));

    std::memset(b, 'Z', sizeof(b));
    format_row("Rotulo", "1234", "rpm", b, cap);
    TEST_ASSERT_LESS_THAN_UINT(cap, std::strlen(b));
  }
}

// ---------------------------------------------------------------------------
//  ALINHAMENTO
// ---------------------------------------------------------------------------

// Numeros que mudam de largura (999 -> 1000) pulando na tela sao dificeis de
// ler de relance — que e como se le um painel dirigindo. O valor fica sempre
// encostado a direita.
void test_linha_alinha_o_valor_a_direita(void) {
  char b[24];
  format_row("RPM", "1726", "", b, sizeof(b));
  TEST_ASSERT_EQUAL_UINT32(23, std::strlen(b));
  TEST_ASSERT_EQUAL_STRING("1726", b + 19);

  format_row("RPM", "999", "", b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("999", b + 20);  // continua na borda
}

void test_linha_com_unidade(void) {
  char b[24];
  format_row("Agua", "83", "C", b, sizeof(b));
  TEST_ASSERT_EQUAL_UINT32(23, std::strlen(b));
  TEST_ASSERT_TRUE(std::strstr(b, "Agua") == b);
  TEST_ASSERT_EQUAL_STRING("83 C", b + 19);
}

// Quando nao cabe tudo, o VALOR sobrevive: e ele que carrega a informacao.
void test_quando_nao_cabe_o_valor_tem_prioridade(void) {
  char b[10];
  format_row("RotuloMuitoLongo", "1726", "rpm", b, sizeof(b));
  TEST_ASSERT_TRUE(std::strstr(b, "1726") != nullptr);
  TEST_ASSERT_LESS_THAN_UINT(sizeof(b), std::strlen(b));
}

void test_conversoes_de_unidade(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 32.0F,
                           kanri::display::celsius_to_fahrenheit(0.0F));
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 212.0F,
                           kanri::display::celsius_to_fahrenheit(100.0F));
  TEST_ASSERT_FLOAT_WITHIN(0.01F, -40.0F,
                           kanri::display::celsius_to_fahrenheit(-40.0F));
  TEST_ASSERT_FLOAT_WITHIN(0.1F, 62.1F, kanri::display::kmh_to_mph(100.0F));
  TEST_ASSERT_FLOAT_WITHIN(0.1F, 14.7F, kanri::display::kpa_to_psi(101.3F));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_inteiros);
  RUN_TEST(test_menor_inteiro_possivel_nao_estoura);
  RUN_TEST(test_decimais);
  RUN_TEST(test_zero_a_esquerda_da_fracao);
  RUN_TEST(test_arredondamento);
  RUN_TEST(test_nan_e_infinito_viram_tracos);

  RUN_TEST(test_buffer_pequeno_trunca_com_terminador);
  RUN_TEST(test_buffer_de_um_byte_so_cabe_o_terminador);
  RUN_TEST(test_argumentos_nulos_nao_quebram);
  RUN_TEST(test_saida_sempre_termina_em_nulo);

  RUN_TEST(test_linha_alinha_o_valor_a_direita);
  RUN_TEST(test_linha_com_unidade);
  RUN_TEST(test_quando_nao_cabe_o_valor_tem_prioridade);
  RUN_TEST(test_conversoes_de_unidade);
  return UNITY_END();
}
