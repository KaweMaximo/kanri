// ============================================================================
//  Testes da decodificacao de PIDs
// ============================================================================
//  Aqui bytes viram numeros que vao para o painel do carro. Duas classes de
//  erro importam:
//
//   1. formula errada -> o numero aparece, parece plausivel, e esta errado.
//      E o pior tipo de bug: ninguem desconfia de um "2.400 rpm" crivel.
//   2. valor impossivel exibido -> ruido eletrico vira "16.000 rpm" na tela.
//
//  Contra (1), conferimos as formulas com valores conhecidos da norma SAE
//  J1979. Contra (2), a faixa fisica do 4B11.
// ============================================================================

#include <unity.h>

#include <cstring>

#include "kanri_obd/pid_decoder.h"

using kanri::obd::decode;
using kanri::obd::DecodeStatus;
using kanri::obd::DecodedValue;
using kanri::obd::parse_response;
using kanri::obd::ParsedFrame;

void setUp(void) {}
void tearDown(void) {}

namespace {

/// Passa uma resposta crua pelo parser e depois pelo decodificador — o mesmo
/// caminho que os bytes fazem no firmware.
DecodedValue decodificar(const char* cru, std::uint8_t modo, std::uint8_t pid) {
  const ParsedFrame f = parse_response(cru, std::strlen(cru), modo, pid);
  return decode(f);
}

void assert_status(DecodeStatus esperado, DecodeStatus obtido) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(esperado), static_cast<int>(obtido));
}

}  // namespace

// ---------------------------------------------------------------------------
//  AS FORMULAS, conferidas com valores conhecidos
// ---------------------------------------------------------------------------

// (0x1A * 256 + 0xF8) / 4 = (26*256 + 248)/4 = 6904/4 = 1726 rpm
void test_rotacao(void) {
  const DecodedValue v = decodificar("41 0C 1A F8", 0x01, 0x0C);
  assert_status(DecodeStatus::Ok, v.status);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 1726.0F, v.value);
  TEST_ASSERT_EQUAL_STRING("rpm", v.unit);
}

void test_rotacao_em_marcha_lenta(void) {
  // 0x0B 0xB8 = 3000 -> 750 rpm, marcha lenta tipica do 4B11
  const DecodedValue v = decodificar("410C0BB8", 0x01, 0x0C);
  assert_status(DecodeStatus::Ok, v.status);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 750.0F, v.value);
}

// O deslocamento de -40 permite representar temperatura negativa num byte
// sem sinal. 0x7B = 123 -> 83 C, temperatura normal de operacao.
void test_temperatura_do_motor(void) {
  const DecodedValue v = decodificar("41 05 7B", 0x01, 0x05);
  assert_status(DecodeStatus::Ok, v.status);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 83.0F, v.value);
  TEST_ASSERT_EQUAL_STRING("C", v.unit);
}

void test_temperatura_negativa(void) {
  // 0x00 -> -40 C. Manha fria em Curitiba.
  const DecodedValue v = decodificar("410500", 0x01, 0x05);
  assert_status(DecodeStatus::Ok, v.status);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, -40.0F, v.value);
}

void test_velocidade(void) {
  const DecodedValue v = decodificar("410D50", 0x01, 0x0D);  // 0x50 = 80
  assert_status(DecodeStatus::Ok, v.status);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 80.0F, v.value);
  TEST_ASSERT_EQUAL_STRING("km/h", v.unit);
}

// A * 100 / 255. 0xFF -> 100%, 0x80 -> 50,2%
void test_borboleta_em_percentual(void) {
  assert_status(DecodeStatus::Ok, decodificar("4111FF", 0x01, 0x11).status);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 100.0F, decodificar("4111FF", 0x01, 0x11).value);
  TEST_ASSERT_FLOAT_WITHIN(0.1F, 50.2F, decodificar("411180", 0x01, 0x11).value);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, decodificar("411100", 0x01, 0x11).value);
}

// (A*256 + B)/1000. 0x35 0xD8 = 13784 -> 13,784 V, alternador carregando.
void test_tensao_do_modulo(void) {
  const DecodedValue v = decodificar("414235D8", 0x01, 0x42);
  assert_status(DecodeStatus::Ok, v.status);
  TEST_ASSERT_FLOAT_WITHIN(0.001F, 13.784F, v.value);
  TEST_ASSERT_EQUAL_STRING("V", v.unit);
}

void test_pressao_do_coletor(void) {
  const DecodedValue v = decodificar("410B65", 0x01, 0x0B);  // 101 kPa
  assert_status(DecodeStatus::Ok, v.status);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 101.0F, v.value);
  TEST_ASSERT_EQUAL_STRING("kPa", v.unit);
}

// A/2 - 64. 0x80 = 128 -> 0 graus.
void test_avanco_de_ignicao(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, decodificar("410E80", 0x01, 0x0E).value);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, -64.0F, decodificar("410E00", 0x01, 0x0E).value);
}

void test_fluxo_de_ar(void) {
  // (A*256+B)/100. 0x07 0xD0 = 2000 -> 20,00 g/s
  const DecodedValue v = decodificar("4110 07 D0", 0x01, 0x10);
  assert_status(DecodeStatus::Ok, v.status);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 20.0F, v.value);
}

void test_tempo_de_motor_ligado(void) {
  const DecodedValue v = decodificar("411F0E10", 0x01, 0x1F);  // 3600 s
  assert_status(DecodeStatus::Ok, v.status);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 3600.0F, v.value);
  TEST_ASSERT_EQUAL_STRING("s", v.unit);
}

// ---------------------------------------------------------------------------
//  A SEGUNDA BARREIRA: valor possivel pela formula, impossivel no motor
// ---------------------------------------------------------------------------

// 0xFF 0xFF pela formula da 16.383 rpm. Nenhum 4B11 chega perto: e ruido
// eletrico, nao medida. Exibir isso no painel seria pior do que nao exibir.
void test_rotacao_impossivel_e_recusada(void) {
  const DecodedValue v = decodificar("410CFFFF", 0x01, 0x0C);
  assert_status(DecodeStatus::OutOfRange, v.status);
  TEST_ASSERT_FALSE(v.ok());
}

// 0xFF -> 215 C. O motor teria fundido muito antes.
void test_temperatura_impossivel_e_recusada(void) {
  assert_status(DecodeStatus::OutOfRange, decodificar("4105FF", 0x01, 0x05).status);
}

// Rede de 12 V nao chega a 65 V. Acima de 30 ja e load dump, nao leitura.
void test_tensao_impossivel_e_recusada(void) {
  assert_status(DecodeStatus::OutOfRange,
                decodificar("4142FFFF", 0x01, 0x42).status);
}

// O limite tem de deixar passar o que e legitimo: 6.500 rpm no corte.
void test_faixa_aceita_o_extremo_legitimo(void) {
  // 6500 * 4 = 26000 = 0x6590
  const DecodedValue v = decodificar("410C6590", 0x01, 0x0C);
  assert_status(DecodeStatus::Ok, v.status);
  TEST_ASSERT_FLOAT_WITHIN(1.0F, 6500.0F, v.value);
}

// ---------------------------------------------------------------------------
//  RECUSAS
// ---------------------------------------------------------------------------

void test_frame_invalido_nao_decodifica(void) {
  assert_status(DecodeStatus::FrameNotOk, decodificar("NO DATA", 0x01, 0x0C).status);
  assert_status(DecodeStatus::FrameNotOk, decodificar("", 0x01, 0x0C).status);
  ParsedFrame vazio;
  assert_status(DecodeStatus::FrameNotOk, decode(vazio).status);
}

void test_pid_sem_formula(void) {
  // 0x00 e mapa de suporte, nao grandeza: nao ha o que decodificar.
  assert_status(DecodeStatus::NotDecodable,
                decodificar("410000000000", 0x01, 0x00).status);
  TEST_ASSERT_FALSE(kanri::obd::is_decodable(0x01, 0x00));
  TEST_ASSERT_FALSE(kanri::obd::is_decodable(0x09, 0x02));  // VIN e texto
  TEST_ASSERT_TRUE(kanri::obd::is_decodable(0x01, 0x0C));
}

// Formula de 2 bytes com so 1 byte: recusa em vez de ler lixo do vizinho.
void test_bytes_insuficientes(void) {
  ParsedFrame f;
  f.status = kanri::obd::ParseStatus::Ok;
  f.mode = 0x41;
  f.pid = 0x0C;   // rotacao precisa de 2 bytes
  f.length = 1;
  f.data[0] = 0x1A;
  assert_status(DecodeStatus::WrongLength, decode(f).status);
}

// ---------------------------------------------------------------------------
//  INVARIANTES
// ---------------------------------------------------------------------------

// Para QUALQUER par de bytes, em QUALQUER PID decodificavel: ou o valor sai
// dentro da faixa fisica, ou e recusado. Nunca um numero impossivel com
// status Ok — que e o que apareceria no painel.
void test_nenhum_valor_aceito_esta_fora_da_faixa(void) {
  const std::uint8_t pids[] = {0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                               0x10, 0x11, 0x1F, 0x2F, 0x42, 0x43, 0x46, 0x5C};
  for (const std::uint8_t pid : pids) {
    for (int a = 0; a <= 0xFF; a += 7) {
      for (int b = 0; b <= 0xFF; b += 29) {
        ParsedFrame f;
        f.status = kanri::obd::ParseStatus::Ok;
        f.mode = 0x41;
        f.pid = pid;
        f.length = 2;
        f.data[0] = static_cast<std::uint8_t>(a);
        f.data[1] = static_cast<std::uint8_t>(b);

        const DecodedValue v = decode(f);
        TEST_ASSERT_NOT_NULL(kanri::obd::to_string(v.status));
        if (!v.ok()) continue;

        // Um valor aceito nunca pode ser absurdo.
        TEST_ASSERT_TRUE_MESSAGE(v.value > -1000.0F && v.value < 70000.0F,
                                 "valor aceito fora de qualquer realidade");
        TEST_ASSERT_NOT_NULL(v.unit);
      }
    }
  }
}

// Decodificar duas vezes o mesmo frame da o mesmo resultado.
void test_decodificacao_e_deterministica(void) {
  const char* cru = "410C1AF8";
  for (int i = 0; i < 50; ++i) {
    const DecodedValue a = decodificar(cru, 0x01, 0x0C);
    const DecodedValue b = decodificar(cru, 0x01, 0x0C);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(a.status), static_cast<int>(b.status));
    TEST_ASSERT_EQUAL_FLOAT(a.value, b.value);
  }
}

void test_to_string_cobre_todos_os_status(void) {
  const DecodeStatus todos[] = {
      DecodeStatus::Ok, DecodeStatus::NotDecodable, DecodeStatus::WrongLength,
      DecodeStatus::OutOfRange, DecodeStatus::FrameNotOk,
  };
  for (const DecodeStatus s : todos) {
    TEST_ASSERT_NOT_NULL(kanri::obd::to_string(s));
    TEST_ASSERT_TRUE(kanri::obd::to_string(s)[0] != '\0');
  }
  TEST_ASSERT_EQUAL_STRING("Unknown",
                           kanri::obd::to_string(static_cast<DecodeStatus>(99)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rotacao);
  RUN_TEST(test_rotacao_em_marcha_lenta);
  RUN_TEST(test_temperatura_do_motor);
  RUN_TEST(test_temperatura_negativa);
  RUN_TEST(test_velocidade);
  RUN_TEST(test_borboleta_em_percentual);
  RUN_TEST(test_tensao_do_modulo);
  RUN_TEST(test_pressao_do_coletor);
  RUN_TEST(test_avanco_de_ignicao);
  RUN_TEST(test_fluxo_de_ar);
  RUN_TEST(test_tempo_de_motor_ligado);

  RUN_TEST(test_rotacao_impossivel_e_recusada);
  RUN_TEST(test_temperatura_impossivel_e_recusada);
  RUN_TEST(test_tensao_impossivel_e_recusada);
  RUN_TEST(test_faixa_aceita_o_extremo_legitimo);

  RUN_TEST(test_frame_invalido_nao_decodifica);
  RUN_TEST(test_pid_sem_formula);
  RUN_TEST(test_bytes_insuficientes);

  RUN_TEST(test_nenhum_valor_aceito_esta_fora_da_faixa);
  RUN_TEST(test_decodificacao_e_deterministica);
  RUN_TEST(test_to_string_cobre_todos_os_status);
  return UNITY_END();
}
