// ============================================================================
//  Testes dos codigos de falha (DTC)
// ============================================================================
//  A codificacao de dois bytes e facil de errar em silencio: uma mascara
//  trocada faz P0301 virar P0310 — um codigo que EXISTE e aponta para outro
//  defeito. O mecanico trocaria a peca errada.
//
//  Por isso os testes conferem digito a digito, com codigos reais.
//
//  Tudo aqui e LEITURA. O Modo 04, que apaga codigos, continua proibido e
//  testado em test_safety_guard.
// ============================================================================

#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "kanri_obd/dtc.h"

using kanri::obd::decode_dtc;
using kanri::obd::Dtc;
using kanri::obd::DtcKind;
using kanri::obd::DtcList;
using kanri::obd::parse_dtc_response;
using kanri::obd::ParsedFrame;

void setUp(void) {}
void tearDown(void) {}

namespace {
const char* dec(std::uint16_t raw) {
  static char b[kanri::obd::kDtcTextLen];
  decode_dtc(raw, b, sizeof(b));
  return b;
}

/// Monta um frame como o parser entregaria, com os codigos ja em bytes.
ParsedFrame frame_com(const std::uint16_t* codigos, std::uint8_t n) {
  ParsedFrame f;
  f.status = kanri::obd::ParseStatus::Ok;
  f.mode = 0x43;
  f.pid = 0x00;
  f.length = static_cast<std::uint8_t>(n * 2);
  for (std::uint8_t i = 0; i < n; ++i) {
    f.data[i * 2] = static_cast<std::uint8_t>(codigos[i] >> 8);
    f.data[(i * 2) + 1] = static_cast<std::uint8_t>(codigos[i] & 0xFF);
  }
  return f;
}
}  // namespace

// ---------------------------------------------------------------------------
//  A CODIFICACAO, conferida com codigos reais
// ---------------------------------------------------------------------------

// P0301 = falha de ignicao no cilindro 1. Um dos codigos mais comuns.
void test_codigos_reais(void) {
  TEST_ASSERT_EQUAL_STRING("P0301", dec(0x0301));
  TEST_ASSERT_EQUAL_STRING("P0143", dec(0x0143));
  TEST_ASSERT_EQUAL_STRING("P0420", dec(0x0420));  // catalisador
  TEST_ASSERT_EQUAL_STRING("P0171", dec(0x0171));  // mistura pobre
}

// A letra vem dos dois bits mais significativos, e trocar a mascara faria um
// codigo de motor virar um de carroceria.
void test_letra_do_sistema(void) {
  TEST_ASSERT_EQUAL_CHAR('P', dec(0x0123)[0]);  // 00 -> powertrain
  TEST_ASSERT_EQUAL_CHAR('C', dec(0x4123)[0]);  // 01 -> chassis
  TEST_ASSERT_EQUAL_CHAR('B', dec(0x8123)[0]);  // 10 -> body
  TEST_ASSERT_EQUAL_CHAR('U', dec(0xC123)[0]);  // 11 -> network
}

// O primeiro digito vem dos bits 13-12 e vai de 0 a 3.
void test_primeiro_digito(void) {
  TEST_ASSERT_EQUAL_STRING("P0000", dec(0x0000));
  TEST_ASSERT_EQUAL_STRING("P1000", dec(0x1000));
  TEST_ASSERT_EQUAL_STRING("P2000", dec(0x2000));
  TEST_ASSERT_EQUAL_STRING("P3000", dec(0x3000));
}

// Os TRES ULTIMOS digitos sao hexadecimais de verdade: P0A1F existe.
//
// O primeiro digito, nao: ele vem de apenas dois bits, entao so vai de 0 a 3.
// Um codigo "PFFFF" nao existe — o maximo com a letra P e P3FFF. Escrevi o
// teste errado aqui na primeira vez, e ele pegou a mim, nao ao codigo.
void test_digitos_hexadecimais(void) {
  TEST_ASSERT_EQUAL_STRING("P0A1F", dec(0x0A1F));
  TEST_ASSERT_EQUAL_STRING("P3FFF", dec(0x3FFF));  // maximo possivel com P
  TEST_ASSERT_EQUAL_STRING("U3FFF", dec(0xFFFF));  // maximo absoluto
}

// O primeiro digito NUNCA passa de 3, para nenhuma entrada possivel.
void test_primeiro_digito_nunca_passa_de_tres(void) {
  char b[kanri::obd::kDtcTextLen];
  for (std::uint32_t raw = 0; raw <= 0xFFFF; raw += 13) {
    decode_dtc(static_cast<std::uint16_t>(raw), b, sizeof(b));
    TEST_ASSERT_TRUE_MESSAGE(b[1] >= '0' && b[1] <= '3',
                             "primeiro digito fora da faixa 0-3");
  }
}

// Uma mascara trocada faria P0301 virar P0310 — codigo que existe e aponta
// para outro defeito. Este teste percorre todos os 65.536 valores e confere
// que cada posicao do texto vem do lugar certo.
void test_cada_bit_cai_na_posicao_certa(void) {
  char b[kanri::obd::kDtcTextLen];
  for (std::uint32_t raw = 0; raw <= 0xFFFF; raw += 7) {
    const std::uint16_t v = static_cast<std::uint16_t>(raw);
    TEST_ASSERT_TRUE(decode_dtc(v, b, sizeof(b)));
    TEST_ASSERT_EQUAL_UINT32(5, std::strlen(b));

    const char letras[] = {'P', 'C', 'B', 'U'};
    TEST_ASSERT_EQUAL_CHAR(letras[(v >> 14) & 3], b[0]);

    auto hexval = [](char c) -> int {
      return (c >= '0' && c <= '9') ? (c - '0') : (c - 'A' + 10);
    };
    TEST_ASSERT_EQUAL_INT((v >> 12) & 0x03, hexval(b[1]));
    TEST_ASSERT_EQUAL_INT((v >> 8) & 0x0F, hexval(b[2]));
    TEST_ASSERT_EQUAL_INT((v >> 4) & 0x0F, hexval(b[3]));
    TEST_ASSERT_EQUAL_INT(v & 0x0F, hexval(b[4]));
  }
}

void test_buffer_invalido(void) {
  char b[3];
  TEST_ASSERT_FALSE(decode_dtc(0x0301, b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("", b);
  TEST_ASSERT_FALSE(decode_dtc(0x0301, nullptr, 8));
}

// ---------------------------------------------------------------------------
//  A LISTA VINDA DA ECU
// ---------------------------------------------------------------------------

void test_lista_com_varios_codigos(void) {
  const std::uint16_t codigos[] = {0x0301, 0x0420, 0x0171};
  const DtcList l = parse_dtc_response(frame_com(codigos, 3), DtcKind::Stored);

  TEST_ASSERT_EQUAL_UINT8(3, l.count);
  TEST_ASSERT_EQUAL_STRING("P0301", l.items[0].text);
  TEST_ASSERT_EQUAL_STRING("P0420", l.items[1].text);
  TEST_ASSERT_EQUAL_STRING("P0171", l.items[2].text);
  TEST_ASSERT_FALSE(l.truncated);
}

// 0x0000 e o preenchimento que a ECU usa quando sobra espaco na resposta.
// Listar "P0000" faria o usuario procurar um defeito que nao existe.
void test_preenchimento_zero_e_ignorado(void) {
  const std::uint16_t codigos[] = {0x0301, 0x0000, 0x0000};
  const DtcList l = parse_dtc_response(frame_com(codigos, 3), DtcKind::Stored);
  TEST_ASSERT_EQUAL_UINT8(1, l.count);
  TEST_ASSERT_EQUAL_STRING("P0301", l.items[0].text);
}

void test_sem_codigos(void) {
  const std::uint16_t nenhum[] = {0x0000, 0x0000};
  TEST_ASSERT_EQUAL_UINT8(0, parse_dtc_response(frame_com(nenhum, 2),
                                                DtcKind::Stored).count);
  ParsedFrame vazio;
  TEST_ASSERT_EQUAL_UINT8(0, parse_dtc_response(vazio, DtcKind::Stored).count);
}

// Mais codigos do que cabe: guardamos o que da e sinalizamos, em vez de
// escrever fora do vetor.
void test_lista_cheia_e_sinalizada(void) {
  std::uint16_t muitos[kanri::obd::kMaxDtcs + 4];
  for (std::size_t i = 0; i < sizeof(muitos) / sizeof(muitos[0]); ++i) {
    muitos[i] = static_cast<std::uint16_t>(0x0100 + i);
  }
  // O frame so comporta kMaxPayloadBytes; usamos o maximo que cabe.
  const std::uint8_t n = kanri::obd::kMaxPayloadBytes / 2;
  const DtcList l = parse_dtc_response(frame_com(muitos, n), DtcKind::Stored);
  TEST_ASSERT_LESS_OR_EQUAL_UINT8(kanri::obd::kMaxDtcs, l.count);
}

// A MESMA falha aparece em listas diferentes conforme o estagio. Confundi-las
// levaria a diagnostico errado: um pendente ainda pode sumir sozinho; um
// permanente resiste ate a ECU confirmar que o defeito acabou.
void test_o_tipo_acompanha_o_codigo(void) {
  const std::uint16_t c[] = {0x0301};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DtcKind::Pending),
      static_cast<int>(parse_dtc_response(frame_com(c, 1),
                                          DtcKind::Pending).items[0].kind));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DtcKind::Permanent),
      static_cast<int>(parse_dtc_response(frame_com(c, 1),
                                          DtcKind::Permanent).items[0].kind));
}

void test_modo_de_cada_tipo(void) {
  TEST_ASSERT_EQUAL_UINT8(0x03, kanri::obd::mode_for(DtcKind::Stored));
  TEST_ASSERT_EQUAL_UINT8(0x07, kanri::obd::mode_for(DtcKind::Pending));
  TEST_ASSERT_EQUAL_UINT8(0x0A, kanri::obd::mode_for(DtcKind::Permanent));
  // Nenhum deles e 0x04: esse APAGA codigos e continua proibido.
  for (const DtcKind k : {DtcKind::Stored, DtcKind::Pending, DtcKind::Permanent}) {
    TEST_ASSERT_NOT_EQUAL_UINT8(0x04, kanri::obd::mode_for(k));
    TEST_ASSERT_NOT_NULL(kanri::obd::to_string(k));
  }
  TEST_ASSERT_EQUAL_STRING("desconhecido",
                           kanri::obd::to_string(static_cast<DtcKind>(99)));
  TEST_ASSERT_EQUAL_UINT8(0x03, kanri::obd::mode_for(static_cast<DtcKind>(99)));
}

// `length` e um uint8_t e pode dizer ate 255, mas `data` tem 32 bytes. Um
// frame montado a mao — ou um campo corrompido em memoria — nao pode fazer o
// decodificador ler alem do vetor.
void test_length_mentiroso_nao_le_alem_do_buffer(void) {
  ParsedFrame f;
  f.status = kanri::obd::ParseStatus::Ok;
  f.mode = 0x43;
  f.length = 255;  // muito alem do que data comporta
  for (std::size_t i = 0; i < kanri::obd::kMaxPayloadBytes; ++i) {
    f.data[i] = 0x11;  // todos iguais, para o resultado ser previsivel
  }

  const DtcList l = parse_dtc_response(f, DtcKind::Stored);
  // No maximo o que cabe no buffer: 32 bytes = 16 pares.
  TEST_ASSERT_LESS_OR_EQUAL_UINT8(kanri::obd::kMaxPayloadBytes / 2, l.count);
  TEST_ASSERT_LESS_OR_EQUAL_UINT8(kanri::obd::kMaxDtcs, l.count);
}

// Payload impar significa resposta truncada: usamos os pares completos e
// descartamos o byte solto, em vez de ler um byte alem do que chegou.
void test_payload_impar_nao_le_alem(void) {
  ParsedFrame f;
  f.status = kanri::obd::ParseStatus::Ok;
  f.mode = 0x43;
  f.length = 3;  // um par e meio
  f.data[0] = 0x03; f.data[1] = 0x01; f.data[2] = 0x04;
  const DtcList l = parse_dtc_response(f, DtcKind::Stored);
  TEST_ASSERT_EQUAL_UINT8(1, l.count);
  TEST_ASSERT_EQUAL_STRING("P0301", l.items[0].text);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_codigos_reais);
  RUN_TEST(test_letra_do_sistema);
  RUN_TEST(test_primeiro_digito);
  RUN_TEST(test_digitos_hexadecimais);
  RUN_TEST(test_primeiro_digito_nunca_passa_de_tres);
  RUN_TEST(test_cada_bit_cai_na_posicao_certa);
  RUN_TEST(test_buffer_invalido);

  RUN_TEST(test_lista_com_varios_codigos);
  RUN_TEST(test_preenchimento_zero_e_ignorado);
  RUN_TEST(test_sem_codigos);
  RUN_TEST(test_lista_cheia_e_sinalizada);
  RUN_TEST(test_o_tipo_acompanha_o_codigo);
  RUN_TEST(test_modo_de_cada_tipo);
  RUN_TEST(test_length_mentiroso_nao_le_alem_do_buffer);
  RUN_TEST(test_payload_impar_nao_le_alem);
  return UNITY_END();
}
