// ============================================================================
//  Testes da descoberta de PIDs suportados
// ============================================================================
//  A codificacao do mapa de bits e facil de errar de um jeito silencioso: um
//  deslocamento trocado faz o firmware achar que suporta o PID vizinho do
//  certo. O sintoma seria "NO DATA" para um PID que existe, e leitura de um
//  que nao existe — dificil de diagnosticar olhando o log.
//
//  Por isso os testes conferem bit a bit, e nao so "achou alguma coisa".
// ============================================================================

#include <unity.h>

#include "kanri_obd/pid_support.h"

using kanri::obd::kSupportPid0;
using kanri::obd::kSupportPid20;
using kanri::obd::kSupportPid40;
using kanri::obd::next_support_pid;
using kanri::obd::PidSupport;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
//  A CODIFICACAO, conferida bit a bit
// ---------------------------------------------------------------------------

// Bit mais significativo do primeiro byte = PID 0x01.
void test_primeiro_bit_e_o_pid_01(void) {
  PidSupport s;
  const std::uint8_t dados[] = {0x80, 0x00, 0x00, 0x00};
  TEST_ASSERT_TRUE(s.apply_block(kSupportPid0, dados, 4));

  TEST_ASSERT_TRUE(s.supports(0x01));
  TEST_ASSERT_FALSE(s.supports(0x02));
  TEST_ASSERT_FALSE(s.supports(0x00));
  TEST_ASSERT_EQUAL_UINT16(1, s.count());
}

// Bit menos significativo do ultimo byte = PID 0x20.
void test_ultimo_bit_e_o_pid_20(void) {
  PidSupport s;
  const std::uint8_t dados[] = {0x00, 0x00, 0x00, 0x01};
  s.apply_block(kSupportPid0, dados, 4);

  TEST_ASSERT_TRUE(s.supports(0x20));
  TEST_ASSERT_FALSE(s.supports(0x1F));
  TEST_ASSERT_EQUAL_UINT16(1, s.count());
}

// Cada bit de cada byte cai no PID certo — a checagem que pega deslocamento
// trocado.
void test_cada_bit_cai_no_pid_certo(void) {
  for (std::uint8_t byte = 0; byte < 4; ++byte) {
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
      PidSupport s;
      std::uint8_t dados[4] = {0, 0, 0, 0};
      dados[byte] = static_cast<std::uint8_t>(0x80U >> bit);
      s.apply_block(kSupportPid0, dados, 4);

      const std::uint8_t esperado =
          static_cast<std::uint8_t>(1 + (byte * 8) + bit);
      TEST_ASSERT_TRUE(s.supports(esperado));
      TEST_ASSERT_EQUAL_UINT16(1, s.count());
    }
  }
}

// Um mapa realista: o que um carro moderno costuma declarar no primeiro
// bloco. 0xBE 0x3E 0xB8 0x11 e um padrao comum.
void test_mapa_realista(void) {
  PidSupport s;
  const std::uint8_t dados[] = {0xBE, 0x3E, 0xB8, 0x11};
  s.apply_block(kSupportPid0, dados, 4);

  // 0xBE = 1011 1110 -> PIDs 01, 03, 04, 05, 06, 07
  TEST_ASSERT_TRUE(s.supports(0x01));
  TEST_ASSERT_FALSE(s.supports(0x02));
  TEST_ASSERT_TRUE(s.supports(0x03));
  TEST_ASSERT_TRUE(s.supports(0x05));
  TEST_ASSERT_TRUE(s.supports(0x07));
  TEST_ASSERT_FALSE(s.supports(0x08));

  // 0x3E = 0011 1110 -> PIDs 0B, 0C, 0D, 0E, 0F
  TEST_ASSERT_TRUE(s.supports(0x0B));
  TEST_ASSERT_TRUE(s.supports(0x0C));  // rotacao
  TEST_ASSERT_TRUE(s.supports(0x0D));  // velocidade
  TEST_ASSERT_TRUE(s.supports(0x0F));
  TEST_ASSERT_FALSE(s.supports(0x10));
}

void test_blocos_seguintes_cobrem_as_faixas_certas(void) {
  PidSupport s;
  const std::uint8_t um[] = {0x80, 0x00, 0x00, 0x00};

  s.apply_block(kSupportPid20, um, 4);
  TEST_ASSERT_TRUE(s.supports(0x21));   // base 0x20 + 1
  TEST_ASSERT_FALSE(s.supports(0x01));

  s.apply_block(kSupportPid40, um, 4);
  TEST_ASSERT_TRUE(s.supports(0x41));   // base 0x40 + 1
  TEST_ASSERT_EQUAL_UINT16(2, s.count());
}

void test_blocos_se_acumulam(void) {
  PidSupport s;
  const std::uint8_t bloco1[] = {0x00, 0x18, 0x00, 0x00};  // 0x0C e 0x0D
  const std::uint8_t bloco2[] = {0x00, 0x00, 0x00, 0x02};  // 0x3F
  s.apply_block(kSupportPid0, bloco1, 4);
  s.apply_block(kSupportPid20, bloco2, 4);

  TEST_ASSERT_TRUE(s.supports(0x0C));
  TEST_ASSERT_TRUE(s.supports(0x0D));
  TEST_ASSERT_TRUE(s.supports(0x3F));
  TEST_ASSERT_EQUAL_UINT16(3, s.count());
}

// ---------------------------------------------------------------------------
//  O BIT DE CONTINUACAO
// ---------------------------------------------------------------------------

// O ultimo bit de cada bloco diz se vale perguntar o proximo. Parar quando
// ele esta desligado economiza duas consultas ao barramento em toda partida.
void test_bit_de_continuacao(void) {
  PidSupport com;
  const std::uint8_t tem_mais[] = {0x00, 0x00, 0x00, 0x01};  // bit do 0x20
  com.apply_block(kSupportPid0, tem_mais, 4);
  TEST_ASSERT_TRUE(com.has_next_block(kSupportPid0));

  PidSupport sem;
  const std::uint8_t nao_tem[] = {0xFF, 0xFF, 0xFF, 0xFE};  // tudo menos o 0x20
  sem.apply_block(kSupportPid0, nao_tem, 4);
  TEST_ASSERT_FALSE(sem.has_next_block(kSupportPid0));
}

void test_o_ultimo_bloco_nunca_tem_proximo(void) {
  PidSupport s;
  const std::uint8_t tudo[] = {0xFF, 0xFF, 0xFF, 0xFF};
  s.apply_block(kSupportPid40, tudo, 4);
  TEST_ASSERT_FALSE(s.has_next_block(kSupportPid40));
  TEST_ASSERT_EQUAL_UINT8(0, next_support_pid(kSupportPid40));
}

void test_sequencia_dos_blocos(void) {
  TEST_ASSERT_EQUAL_UINT8(kSupportPid20, next_support_pid(kSupportPid0));
  TEST_ASSERT_EQUAL_UINT8(kSupportPid40, next_support_pid(kSupportPid20));
  TEST_ASSERT_EQUAL_UINT8(0, next_support_pid(kSupportPid40));
  TEST_ASSERT_EQUAL_UINT8(0, next_support_pid(0x0C));  // nao e PID de mapa
}

void test_reconhece_os_pids_de_mapa(void) {
  TEST_ASSERT_TRUE(kanri::obd::is_support_pid(0x00));
  TEST_ASSERT_TRUE(kanri::obd::is_support_pid(0x20));
  TEST_ASSERT_TRUE(kanri::obd::is_support_pid(0x40));
  TEST_ASSERT_FALSE(kanri::obd::is_support_pid(0x0C));
  TEST_ASSERT_FALSE(kanri::obd::is_support_pid(0x60));
}

// ---------------------------------------------------------------------------
//  RECUSAS E ESTADO INICIAL
// ---------------------------------------------------------------------------

// Antes de qualquer bloco, nada e suportado. "Na duvida, nao suportado" e a
// resposta segura: pedir a mais custa banda do barramento; deixar de pedir
// custa uma medida.
void test_sem_blocos_nada_e_suportado(void) {
  PidSupport s;
  TEST_ASSERT_FALSE(s.any_block_applied());
  TEST_ASSERT_EQUAL_UINT16(0, s.count());
  for (int pid = 0; pid <= 0xFF; ++pid) {
    TEST_ASSERT_FALSE(s.supports(static_cast<std::uint8_t>(pid)));
  }
}

void test_bloco_invalido_e_recusado_sem_efeito(void) {
  PidSupport s;
  const std::uint8_t dados[] = {0xFF, 0xFF, 0xFF, 0xFF};

  TEST_ASSERT_FALSE(s.apply_block(0x0C, dados, 4));       // base invalida
  TEST_ASSERT_FALSE(s.apply_block(kSupportPid0, dados, 3));  // curto
  TEST_ASSERT_FALSE(s.apply_block(kSupportPid0, dados, 5));  // longo
  TEST_ASSERT_FALSE(s.apply_block(kSupportPid0, nullptr, 4));

  // Nenhuma recusa pode ter registrado nada.
  TEST_ASSERT_EQUAL_UINT16(0, s.count());
  TEST_ASSERT_FALSE(s.any_block_applied());
}

void test_reset_esquece_tudo(void) {
  PidSupport s;
  const std::uint8_t tudo[] = {0xFF, 0xFF, 0xFF, 0xFF};
  s.apply_block(kSupportPid0, tudo, 4);
  TEST_ASSERT_EQUAL_UINT16(32, s.count());

  // Reconectar pode ser outro carro: esquecer e obrigatorio.
  s.reset();
  TEST_ASSERT_EQUAL_UINT16(0, s.count());
  TEST_ASSERT_FALSE(s.any_block_applied());
  TEST_ASSERT_FALSE(s.supports(0x0C));
}

// ---------------------------------------------------------------------------
//  INVARIANTES
// ---------------------------------------------------------------------------

// Um bloco marca no maximo 32 PIDs, e todos dentro da faixa do bloco. Se
// marcasse fora, o firmware pediria PIDs de outra faixa.
void test_bloco_so_marca_a_propria_faixa(void) {
  const std::uint8_t bases[] = {kSupportPid0, kSupportPid20, kSupportPid40};
  for (const std::uint8_t base : bases) {
    PidSupport s;
    const std::uint8_t tudo[] = {0xFF, 0xFF, 0xFF, 0xFF};
    s.apply_block(base, tudo, 4);

    TEST_ASSERT_EQUAL_UINT16(32, s.count());
    for (int pid = 0; pid <= 0xFF; ++pid) {
      const bool dentro = pid > base && pid <= (base + 32);
      TEST_ASSERT_EQUAL_INT_MESSAGE(
          dentro, s.supports(static_cast<std::uint8_t>(pid)),
          "bloco marcou PID fora da propria faixa");
    }
  }
}

// Qualquer payload de 4 bytes entra sem quebrar, e a contagem bate com os
// bits ligados.
void test_fuzz_de_payloads(void) {
  std::uint32_t semente = 0xA11CE;
  for (int it = 0; it < 3000; ++it) {
    PidSupport s;
    std::uint8_t dados[4];
    int bits_ligados = 0;
    for (std::uint8_t i = 0; i < 4; ++i) {
      semente = semente * 1103515245U + 12345U;
      dados[i] = static_cast<std::uint8_t>((semente >> 16) & 0xFF);
      for (int b = 0; b < 8; ++b) {
        if (dados[i] & (1U << b)) ++bits_ligados;
      }
    }
    TEST_ASSERT_TRUE(s.apply_block(kSupportPid0, dados, 4));
    TEST_ASSERT_EQUAL_UINT16(bits_ligados, s.count());
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_primeiro_bit_e_o_pid_01);
  RUN_TEST(test_ultimo_bit_e_o_pid_20);
  RUN_TEST(test_cada_bit_cai_no_pid_certo);
  RUN_TEST(test_mapa_realista);
  RUN_TEST(test_blocos_seguintes_cobrem_as_faixas_certas);
  RUN_TEST(test_blocos_se_acumulam);

  RUN_TEST(test_bit_de_continuacao);
  RUN_TEST(test_o_ultimo_bloco_nunca_tem_proximo);
  RUN_TEST(test_sequencia_dos_blocos);
  RUN_TEST(test_reconhece_os_pids_de_mapa);

  RUN_TEST(test_sem_blocos_nada_e_suportado);
  RUN_TEST(test_bloco_invalido_e_recusado_sem_efeito);
  RUN_TEST(test_reset_esquece_tudo);

  RUN_TEST(test_bloco_so_marca_a_propria_faixa);
  RUN_TEST(test_fuzz_de_payloads);
  return UNITY_END();
}
