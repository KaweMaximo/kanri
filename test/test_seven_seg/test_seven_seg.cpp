// ============================================================================
//  Testes do mostrador de 7 segmentos
// ============================================================================
//  Tres digitos e pouco espaco, e a decisao de quantas casas decimais usar
//  muda o que o motorista le. "13.8 V" e util; "13 V" joga fora a precisao
//  onde ela importa; "1 V" seria errado.
//
//  Aqui verificamos exatamente o que apareceria no painel — sem ter o
//  mostrador montado.
// ============================================================================

#include <unity.h>

#include <cstring>

#include "kanri_display/seven_seg.h"

using kanri::core::TelemetrySnapshot;
using kanri::display::build_seg_frame;
using kanri::display::format_segments;
using kanri::display::SegFrame;

void setUp(void) {}
void tearDown(void) {}

namespace {
const char* fmt(float v) {
  static char buf[kanri::display::kSegTextLen];
  format_segments(v, buf, sizeof(buf));
  return buf;
}
}  // namespace

// ---------------------------------------------------------------------------
//  A ESCOLHA DAS CASAS DECIMAIS
// ---------------------------------------------------------------------------

// Preferir sempre a maior precisao que couber: e o que faz a tensao ser util.
void test_usa_a_maior_precisao_que_cabe(void) {
  TEST_ASSERT_EQUAL_STRING("9.52", fmt(9.52F));    // duas casas cabem
  TEST_ASSERT_EQUAL_STRING("13.8", fmt(13.8F));    // uma casa cabe
  TEST_ASSERT_EQUAL_STRING("120", fmt(120.0F));    // tres digitos cheios
  TEST_ASSERT_EQUAL_STRING("83.0", fmt(83.0F));    // sobra espaco para a casa
}

void test_tensao_da_bateria(void) {
  // A faixa que importa: 12,1 V parado, 14,4 V carregando. A decima de volt
  // e o que distingue bateria fraca de alternador trabalhando.
  TEST_ASSERT_EQUAL_STRING("12.1", fmt(12.1F));
  TEST_ASSERT_EQUAL_STRING("14.4", fmt(14.4F));
  TEST_ASSERT_EQUAL_STRING("11.8", fmt(11.8F));
}

void test_temperatura(void) {
  TEST_ASSERT_EQUAL_STRING("83.0", fmt(83.0F));
  TEST_ASSERT_EQUAL_STRING("105", fmt(105.0F));   // tres digitos, sem casa
  TEST_ASSERT_EQUAL_STRING("9.00", fmt(9.0F));
}

// Rotacao nao cabe em digitos inteiros: vai em milhares, como num tacometro.
void test_rotacao_vai_em_milhares(void) {
  bool escalado = false;
  char buf[kanri::display::kSegTextLen];

  format_segments(1726.0F, buf, sizeof(buf), &escalado);
  TEST_ASSERT_TRUE(escalado);
  TEST_ASSERT_EQUAL_STRING("1.73", buf);   // 1,73 mil

  format_segments(6500.0F, buf, sizeof(buf), &escalado);
  TEST_ASSERT_TRUE(escalado);
  TEST_ASSERT_EQUAL_STRING("6.50", buf);

  // Marcha lenta ainda cabe inteira, e nao deve ser escalada.
  format_segments(950.0F, buf, sizeof(buf), &escalado);
  TEST_ASSERT_FALSE(escalado);
  TEST_ASSERT_EQUAL_STRING("950", buf);
}

void test_zero_e_negativos(void) {
  TEST_ASSERT_EQUAL_STRING("0.00", fmt(0.0F));
  // O sinal ocupa um digito, entao sobra menos para o numero.
  TEST_ASSERT_EQUAL_STRING("-40", fmt(-40.0F));
  TEST_ASSERT_EQUAL_STRING("-5.0", fmt(-5.0F));
}

// ---------------------------------------------------------------------------
//  RECUSAS
// ---------------------------------------------------------------------------

void test_valores_impossiveis_viram_tracos(void) {
  const float zero = 0.0F;
  TEST_ASSERT_EQUAL_STRING("---", fmt(zero / zero));      // NaN
  TEST_ASSERT_EQUAL_STRING("---", fmt(1.0e9F));            // nem em milhares cabe
}

void test_buffer_pequeno_nao_estoura(void) {
  char b[3];
  std::memset(b, 'X', sizeof(b));
  format_segments(13.8F, b, sizeof(b));
  TEST_ASSERT_LESS_THAN_UINT(sizeof(b), std::strlen(b));
  TEST_ASSERT_EQUAL_UINT32(0, format_segments(1.0F, nullptr, 8));
  TEST_ASSERT_EQUAL_UINT32(0, format_segments(1.0F, b, 0));
}

// ---------------------------------------------------------------------------
//  O QUE O MOSTRADOR CONSEGUE DESENHAR
// ---------------------------------------------------------------------------

// Um display de 7 segmentos so desenha algumas letras. Um rotulo com letra
// impossivel viraria um borrao, e o motorista leria a grandeza errada.
void test_todos_os_rotulos_sao_desenhaveis(void) {
  for (std::size_t i = 0; i < kanri::display::kSegMeasureCount; ++i) {
    const char* rotulo = kanri::display::kSegMeasures[i].label;
    TEST_ASSERT_NOT_NULL(rotulo);
    TEST_ASSERT_TRUE_MESSAGE(kanri::display::is_renderable(rotulo), rotulo);
    // Tem de caber nos digitos disponiveis.
    TEST_ASSERT_LESS_OR_EQUAL_UINT(kanri::display::kSegDigits,
                                   std::strlen(rotulo));
  }
}

void test_alfabeto_recusa_o_impossivel(void) {
  TEST_ASSERT_TRUE(kanri::display::is_renderable("83.0"));
  TEST_ASSERT_TRUE(kanri::display::is_renderable("bAt"));
  TEST_ASSERT_TRUE(kanri::display::is_renderable("---"));
  // K, M, W, X, Z nao tem forma reconhecivel em 7 segmentos.
  TEST_ASSERT_FALSE(kanri::display::is_renderable("KM"));
  TEST_ASSERT_FALSE(kanri::display::is_renderable("MAX"));
  TEST_ASSERT_FALSE(kanri::display::is_renderable("W"));
  TEST_ASSERT_FALSE(kanri::display::is_renderable(nullptr));
}

// Todo numero formatado tem de ser desenhavel — senao apareceria lixo.
void test_toda_saida_formatada_e_desenhavel(void) {
  std::uint32_t semente = 0xF00D;
  for (int i = 0; i < 3000; ++i) {
    semente = semente * 1103515245U + 12345U;
    const float v = static_cast<float>(static_cast<int>(semente % 200000)) / 100.0F
                    - 500.0F;
    TEST_ASSERT_TRUE_MESSAGE(kanri::display::is_renderable(fmt(v)), fmt(v));
  }
}

// ---------------------------------------------------------------------------
//  O FRAME COMPLETO
// ---------------------------------------------------------------------------

void test_frame_mostra_a_medida_escolhida(void) {
  TelemetrySnapshot t;
  t.coolant_temp_c = {83.0F, true, 1000};
  t.engine_rpm = {1726.0F, true, 1000};

  // Indice 0 e a temperatura: e a grandeza que estraga motor sem ninguem ver.
  TEST_ASSERT_EQUAL_STRING("tEP", kanri::display::kSegMeasures[0].label);
  TEST_ASSERT_EQUAL_STRING("83.0", build_seg_frame(t, 0, 1000).text);
  TEST_ASSERT_EQUAL_STRING("1.73", build_seg_frame(t, 1, 1000).text);
}

// Mesma regra do resto do projeto: sem leitura confiavel, nao ha numero.
void test_medida_ausente_ou_velha_vira_tracos(void) {
  TelemetrySnapshot vazia;
  TEST_ASSERT_EQUAL_STRING("---", build_seg_frame(vazia, 0, 1000).text);

  TelemetrySnapshot velha;
  velha.coolant_temp_c = {83.0F, true, 1000};
  TEST_ASSERT_EQUAL_STRING("---", build_seg_frame(velha, 0, 9000).text);
}

// Num mostrador de tres digitos nao ha espaco para escrever um aviso — entao
// o piscar E o aviso.
void test_motor_quente_pisca(void) {
  TelemetrySnapshot t;
  t.coolant_temp_c = {108.0F, true, 1000};
  TEST_ASSERT_TRUE(build_seg_frame(t, 0, 1000).blink);

  t.coolant_temp_c = {90.0F, true, 1000};
  TEST_ASSERT_FALSE(build_seg_frame(t, 0, 1000).blink);
}

void test_indice_invalido_nao_quebra(void) {
  TelemetrySnapshot t;
  t.coolant_temp_c = {83.0F, true, 1000};
  TEST_ASSERT_EQUAL_STRING("---", build_seg_frame(t, 99, 1000).text);
  TEST_ASSERT_EQUAL_STRING("---",
      build_seg_frame(t, kanri::display::kSegMeasureCount, 1000).text);
}

// O botao percorre esta lista; ela nao pode ter buraco nem duplicata.
void test_catalogo_de_medidas_e_coerente(void) {
  TEST_ASSERT_GREATER_THAN_UINT(1, kanri::display::kSegMeasureCount);
  for (std::size_t i = 0; i < kanri::display::kSegMeasureCount; ++i) {
    TEST_ASSERT_NOT_NULL(kanri::display::kSegMeasures[i].key);
    for (std::size_t j = i + 1; j < kanri::display::kSegMeasureCount; ++j) {
      TEST_ASSERT_FALSE_MESSAGE(
          kanri::display::kSegMeasures[i].field ==
              kanri::display::kSegMeasures[j].field,
          "duas entradas apontam para a mesma grandeza");
      TEST_ASSERT_FALSE_MESSAGE(
          std::strcmp(kanri::display::kSegMeasures[i].label,
                      kanri::display::kSegMeasures[j].label) == 0,
          "dois rotulos iguais: o motorista nao saberia qual esta vendo");
    }
  }
}

// O vetor de suavizadores no main.cpp e dimensionado por kSegMeasureMax. Se
// o catalogo passar do teto, a escrita sairia do vetor — e o compilador nao
// tem como avisar, porque a contagem real e de tempo de execucao.
void test_catalogo_cabe_no_teto_de_compilacao(void) {
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(kanri::display::kSegMeasureMax,
                                   kanri::display::kSegMeasureCount);
}

// Toda medida precisa de uma faixa fisica positiva: e dela que sai o limiar
// de salto da suavizacao. Faixa zero faria tudo virar "mudanca grande", e o
// painel voltaria a saltar.
void test_toda_medida_tem_faixa_fisica_utilizavel(void) {
  for (std::size_t i = 0; i < kanri::display::kSegMeasureCount; ++i) {
    TEST_ASSERT_GREATER_THAN_FLOAT_MESSAGE(
        0.0F, kanri::display::kSegMeasures[i].span,
        kanri::display::kSegMeasures[i].key);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_usa_a_maior_precisao_que_cabe);
  RUN_TEST(test_tensao_da_bateria);
  RUN_TEST(test_temperatura);
  RUN_TEST(test_rotacao_vai_em_milhares);
  RUN_TEST(test_zero_e_negativos);

  RUN_TEST(test_valores_impossiveis_viram_tracos);
  RUN_TEST(test_buffer_pequeno_nao_estoura);

  RUN_TEST(test_todos_os_rotulos_sao_desenhaveis);
  RUN_TEST(test_alfabeto_recusa_o_impossivel);
  RUN_TEST(test_toda_saida_formatada_e_desenhavel);

  RUN_TEST(test_frame_mostra_a_medida_escolhida);
  RUN_TEST(test_medida_ausente_ou_velha_vira_tracos);
  RUN_TEST(test_motor_quente_pisca);
  RUN_TEST(test_indice_invalido_nao_quebra);
  RUN_TEST(test_catalogo_de_medidas_e_coerente);
  RUN_TEST(test_catalogo_cabe_no_teto_de_compilacao);
  RUN_TEST(test_toda_medida_tem_faixa_fisica_utilizavel);
  return UNITY_END();
}
