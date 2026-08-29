// ============================================================================
//  Testes do potenciometro de brilho
// ============================================================================
//  O objetivo destes testes e reproduzir, no PC, os tres jeitos de o botao
//  dar errado no carro:
//
//    - o mostrador PULSA com o botao parado          (ruido do ADC)
//    - o nivel OSCILA numa posicao especifica        (fronteira de faixa)
//    - o brilho PASSEIA sem potenciometro ligado     (GPIO 34 flutuando)
//
//  Nenhum deles precisa de hardware para ser exercitado — e nenhum deles seria
//  encontrado por inspecao de codigo.
// ============================================================================

#include <unity.h>

#include "kanri_display/brightness_knob.h"

using kanri::display::BrightnessKnob;
using kanri::display::kAdcMax;
using kanri::display::kKnobConfirmations;
using kanri::display::kKnobDefaultLevel;
using kanri::display::kKnobHysteresis;
using kanri::display::kKnobLevels;
using kanri::display::knob_level_for;
using kanri::display::knob_level_percent;

void setUp(void) {}
void tearDown(void) {}

namespace {

// Alimenta o botao com a mesma leitura ate ele aceitar, e diz se aceitou.
bool assentar(BrightnessKnob& k, std::uint16_t raw) {
  bool mudou = false;
  for (int i = 0; i < kKnobConfirmations + 2; ++i) {
    if (k.update(raw)) mudou = true;
  }
  return mudou;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Percentuais
// ---------------------------------------------------------------------------

void test_niveis_sobem_do_mais_fraco_ao_mais_forte(void) {
  std::uint8_t anterior = 0;
  for (std::uint8_t n = 0; n < kKnobLevels; ++n) {
    const std::uint8_t p = knob_level_percent(n);
    TEST_ASSERT_GREATER_THAN_UINT8(anterior, p);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(100, p);
    anterior = p;
  }
  TEST_ASSERT_EQUAL_UINT8(100, knob_level_percent(kKnobLevels - 1));
}

// O nivel mais fraco NAO pode ser zero: 0 % ainda acende (e o passo 1/32 do
// MAX7219). Um nivel que apaga o painel seria indistinguivel de defeito.
void test_nivel_mais_fraco_ainda_acende(void) {
  TEST_ASSERT_GREATER_THAN_UINT8(0, knob_level_percent(0));
}

// Os passos sao logaritmicos: a diferenca entre os dois primeiros niveis tem
// de ser MENOR que entre os dois ultimos. Passos lineares desperdicariam o
// ajuste justamente na faixa escura, que e onde ele serve.
void test_passos_sao_mais_finos_no_escuro(void) {
  const int primeiro = knob_level_percent(1) - knob_level_percent(0);
  const int ultimo = knob_level_percent(kKnobLevels - 1) -
                     knob_level_percent(kKnobLevels - 2);
  TEST_ASSERT_LESS_THAN_INT(ultimo, primeiro);
}

void test_nivel_fora_da_faixa_nao_estoura(void) {
  TEST_ASSERT_EQUAL_UINT8(knob_level_percent(kKnobLevels - 1),
                          knob_level_percent(200));
}

// ---------------------------------------------------------------------------
//  Quantizacao e histerese
// ---------------------------------------------------------------------------

// As duas pontas do curso precisam ser alcancaveis. Se o nivel maximo exigisse
// mais do que o ADC entrega, o motorista giraria o botao ate o fim e nunca
// veria o brilho maximo — e concluiria que o potenciometro esta com defeito.
void test_as_duas_pontas_do_curso_sao_alcancaveis(void) {
  BrightnessKnob minimo;
  assentar(minimo, 0);
  TEST_ASSERT_EQUAL_UINT8(0, minimo.level());

  BrightnessKnob maximo;
  assentar(maximo, kAdcMax);
  TEST_ASSERT_EQUAL_UINT8(kKnobLevels - 1, maximo.level());
}

void test_girar_devagar_percorre_todos_os_niveis(void) {
  BrightnessKnob k;
  assentar(k, 0);

  std::uint8_t visto[kKnobLevels] = {};
  for (std::uint16_t raw = 0; raw <= kAdcMax; raw += 20) {
    k.update(raw);
    k.update(raw);
    k.update(raw);
    visto[k.level()] = 1;
  }
  for (std::uint8_t n = 0; n < kKnobLevels; ++n) {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, visto[n], "nivel inalcancavel");
  }
}

// O nivel tem que subir junto com a leitura, nunca ao contrario. Um erro de
// sinal aqui faria o botao girar invertido — funcionaria, e estaria errado.
void test_nivel_acompanha_o_sentido_do_giro(void) {
  std::uint8_t anterior = 0;
  for (std::uint16_t raw = 0; raw <= kAdcMax; raw += 50) {
    BrightnessKnob k;
    assentar(k, raw);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(anterior, k.level());
    anterior = k.level();
  }
}

// A ZONA MORTA: dentro da margem, a leitura nao muda de nivel.
void test_histerese_segura_o_nivel_perto_da_fronteira(void) {
  const std::uint16_t fronteira = 819;  // fim da faixa 0
  TEST_ASSERT_EQUAL_UINT8(0, knob_level_for(fronteira + 10, 0));
  TEST_ASSERT_EQUAL_UINT8(0, knob_level_for(fronteira + kKnobHysteresis - 1, 0));
  TEST_ASSERT_EQUAL_UINT8(1, knob_level_for(fronteira + kKnobHysteresis + 1, 0));

  // E o caminho de volta pede a mesma margem, do outro lado.
  TEST_ASSERT_EQUAL_UINT8(1, knob_level_for(fronteira - 10, 1));
  TEST_ASSERT_EQUAL_UINT8(0, knob_level_for(fronteira - kKnobHysteresis - 1, 1));
}

// Um giro rapido pode pular faixas entre duas leituras.
void test_giro_rapido_pula_varias_faixas_de_uma_vez(void) {
  BrightnessKnob k;
  assentar(k, 0);
  assentar(k, kAdcMax);
  TEST_ASSERT_EQUAL_UINT8(kKnobLevels - 1, k.level());
}

void test_leitura_absurda_e_limitada(void) {
  BrightnessKnob k;
  assentar(k, 60000);
  TEST_ASSERT_EQUAL_UINT8(kKnobLevels - 1, k.level());
}

// ---------------------------------------------------------------------------
//  Os tres modos de falha do mundo real
// ---------------------------------------------------------------------------

// RUIDO DO ADC — o mostrador nao pode pulsar com o botao parado.
//
// Simula uma posicao fixa no meio de uma faixa, com o ruido tipico do ESP32
// (dezenas de contagens). Nenhuma mudanca de nivel pode escapar.
void test_botao_parado_nunca_muda_o_brilho(void) {
  BrightnessKnob k;
  assentar(k, 2000);
  const std::uint8_t estavel = k.level();

  std::uint32_t semente = 12345;
  for (int i = 0; i < 5000; ++i) {
    semente = semente * 1103515245U + 12345U;
    const int ruido = static_cast<int>((semente >> 16) % 81) - 40;  // +-40
    TEST_ASSERT_FALSE(k.update(static_cast<std::uint16_t>(2000 + ruido)));
  }
  TEST_ASSERT_EQUAL_UINT8(estavel, k.level());
}

// FRONTEIRA DE FAIXA — o caso que a quantizacao sozinha NAO resolve.
//
// Sem histerese, uma leitura oscilando em cima da fronteira alternaria de
// nivel a cada amostra, e o painel piscaria entre dois brilhos.
void test_ruido_exatamente_na_fronteira_nao_faz_o_painel_piscar(void) {
  BrightnessKnob k;
  assentar(k, 700);
  const std::uint8_t estavel = k.level();

  std::uint32_t semente = 999;
  int mudancas = 0;
  for (int i = 0; i < 5000; ++i) {
    semente = semente * 1103515245U + 12345U;
    const int ruido = static_cast<int>((semente >> 16) % 81) - 40;
    if (k.update(static_cast<std::uint16_t>(819 + ruido))) ++mudancas;
  }
  TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(1, mudancas, "painel piscaria");
  TEST_ASSERT_LESS_OR_EQUAL_UINT8(estavel + 1, k.level());
}

// PINO FLUTUANDO — sem potenciometro ligado, o GPIO 34 vira antena.
//
// O GPIO 34 nao tem pull-up interno: se o fio nao estiver la, le lixo. Sem a
// confirmacao por leituras seguidas, o brilho passearia sozinho num aparelho
// montado sem o botao.
void test_pino_flutuando_nao_mexe_no_brilho(void) {
  BrightnessKnob k;
  const std::uint8_t inicial = k.level();
  TEST_ASSERT_EQUAL_UINT8(kKnobDefaultLevel, inicial);

  std::uint32_t semente = 7;
  for (int i = 0; i < 20000; ++i) {
    semente = semente * 1103515245U + 12345U;
    const std::uint16_t lixo = static_cast<std::uint16_t>((semente >> 8) % (kAdcMax + 1));
    TEST_ASSERT_FALSE_MESSAGE(k.update(lixo), "brilho mudou com o pino solto");
  }
  TEST_ASSERT_EQUAL_UINT8(inicial, k.level());
}

// ---------------------------------------------------------------------------
//  Confirmacao
// ---------------------------------------------------------------------------

// Uma leitura isolada nao muda nada — mesmo sendo do outro extremo do curso.
void test_uma_leitura_isolada_nao_muda_o_nivel(void) {
  BrightnessKnob k;
  assentar(k, 2000);
  const std::uint8_t antes = k.level();
  TEST_ASSERT_FALSE(k.update(0));
  TEST_ASSERT_EQUAL_UINT8(antes, k.level());
}

void test_muda_exatamente_na_enesima_confirmacao(void) {
  BrightnessKnob k;
  assentar(k, 2000);

  for (int i = 1; i < kKnobConfirmations; ++i) {
    TEST_ASSERT_FALSE_MESSAGE(k.update(0), "mudou cedo demais");
  }
  TEST_ASSERT_TRUE_MESSAGE(k.update(0), "nao mudou na confirmacao esperada");
}

// O nivel candidato aparece antes de valer, para o console poder mostrar o
// que o botao esta "tentando" — util ao conferir a fiacao.
void test_candidato_fica_visivel_antes_de_valer(void) {
  BrightnessKnob k;
  assentar(k, 2000);
  k.update(0);
  TEST_ASSERT_EQUAL_UINT8(0, k.pending_level());
  TEST_ASSERT_NOT_EQUAL(0, k.level());
}

// O retorno so pode ser true na mudanca: e ele que evita brigar com o comando
// `brilho` do console, desfazendo-o 5 vezes por segundo.
void test_so_avisa_na_mudanca(void) {
  BrightnessKnob k;
  assentar(k, kAdcMax);
  for (int i = 0; i < 100; ++i) {
    TEST_ASSERT_FALSE(k.update(kAdcMax));
  }
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_niveis_sobem_do_mais_fraco_ao_mais_forte);
  RUN_TEST(test_nivel_mais_fraco_ainda_acende);
  RUN_TEST(test_passos_sao_mais_finos_no_escuro);
  RUN_TEST(test_nivel_fora_da_faixa_nao_estoura);

  RUN_TEST(test_as_duas_pontas_do_curso_sao_alcancaveis);
  RUN_TEST(test_girar_devagar_percorre_todos_os_niveis);
  RUN_TEST(test_nivel_acompanha_o_sentido_do_giro);
  RUN_TEST(test_histerese_segura_o_nivel_perto_da_fronteira);
  RUN_TEST(test_giro_rapido_pula_varias_faixas_de_uma_vez);
  RUN_TEST(test_leitura_absurda_e_limitada);

  RUN_TEST(test_botao_parado_nunca_muda_o_brilho);
  RUN_TEST(test_ruido_exatamente_na_fronteira_nao_faz_o_painel_piscar);
  RUN_TEST(test_pino_flutuando_nao_mexe_no_brilho);

  RUN_TEST(test_uma_leitura_isolada_nao_muda_o_nivel);
  RUN_TEST(test_muda_exatamente_na_enesima_confirmacao);
  RUN_TEST(test_candidato_fica_visivel_antes_de_valer);
  RUN_TEST(test_so_avisa_na_mudanca);
  return UNITY_END();
}
