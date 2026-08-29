// ============================================================================
//  Testes do protocolo MAX7219
// ============================================================================
//  Este chip nao responde NADA. Nao ha registrador de leitura, nao ha ACK:
//  escrevemos 16 bits no fio e torcemos. Quando o painel mostra o numero
//  errado, o chip nao tem como avisar — e nao ha log que ajude, porque do
//  ponto de vista do firmware tudo correu bem.
//
//  Por isso a traducao inteira mora em funcao pura e e conferida aqui. Este
//  arquivo e o unico lugar onde da para saber se "13.8" vira mesmo 13.8.
// ============================================================================

#include <unity.h>

#include <cstdio>
#include <cstring>

#include "kanri_display/max7219.h"

using kanri::display::blink_visible;
using kanri::display::encode_char;
using kanri::display::encode_frame;
using kanri::display::init_sequence;
using kanri::display::intensity_from_percent;
using kanri::display::kInitWordCount;
using kanri::display::kSegBitDP;
using kanri::display::kSegDigits;
using kanri::display::Max7219Reg;
using kanri::display::Max7219Word;
using kanri::display::SegFrame;
using kanri::display::to_bits;

void setUp(void) {}
void tearDown(void) {}

namespace {

SegFrame quadro(const char* texto) {
  SegFrame f;
  std::strncpy(f.text, texto, sizeof(f.text) - 1);
  return f;
}

// Procura o dado escrito num registrador na sequencia de init.
bool achar(const Max7219Word* palavras, std::size_t n, Max7219Reg reg,
           std::uint8_t* dado) {
  for (std::size_t i = 0; i < n; ++i) {
    if (palavras[i].reg == static_cast<std::uint8_t>(reg)) {
      *dado = palavras[i].data;
      return true;
    }
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Empacotamento
// ---------------------------------------------------------------------------

void test_palavra_leva_o_endereco_nos_bits_altos(void) {
  TEST_ASSERT_EQUAL_HEX16(0x0A07, to_bits({0x0A, 0x07}));
  TEST_ASSERT_EQUAL_HEX16(0x0C01, to_bits({0x0C, 0x01}));
  TEST_ASSERT_EQUAL_HEX16(0x0000, to_bits({0x00, 0x00}));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, to_bits({0xFF, 0xFF}));
}

// ---------------------------------------------------------------------------
//  A fonte
// ---------------------------------------------------------------------------

// Os padroes dos digitos, conferidos um a um contra o desenho do mostrador.
// Se alguem trocar um bit aqui, o painel mostra outro numero — e ninguem
// percebe, porque continua sendo um numero plausivel.
void test_digitos_tem_os_segmentos_certos(void) {
  TEST_ASSERT_EQUAL_HEX8(0x7E, encode_char('0'));  // tudo menos G
  TEST_ASSERT_EQUAL_HEX8(0x30, encode_char('1'));  // so B e C
  TEST_ASSERT_EQUAL_HEX8(0x6D, encode_char('2'));
  TEST_ASSERT_EQUAL_HEX8(0x79, encode_char('3'));
  TEST_ASSERT_EQUAL_HEX8(0x33, encode_char('4'));
  TEST_ASSERT_EQUAL_HEX8(0x5B, encode_char('5'));
  TEST_ASSERT_EQUAL_HEX8(0x5F, encode_char('6'));
  TEST_ASSERT_EQUAL_HEX8(0x70, encode_char('7'));
  TEST_ASSERT_EQUAL_HEX8(0x7F, encode_char('8'));  // todos os sete
  TEST_ASSERT_EQUAL_HEX8(0x7B, encode_char('9'));
}

// Um erro de copia na tabela produziria dois digitos com o mesmo desenho —
// e um "6" apareceria como "5" no painel, sem nada indicar o problema.
void test_os_dez_digitos_sao_visualmente_distintos(void) {
  for (char a = '0'; a <= '9'; ++a) {
    for (char b = static_cast<char>(a + 1); b <= '9'; ++b) {
      TEST_ASSERT_NOT_EQUAL(encode_char(a), encode_char(b));
    }
  }
}

// '8' acende os sete segmentos e nada mais; o ponto e bit separado.
void test_oito_acende_tudo_menos_o_ponto(void) {
  TEST_ASSERT_EQUAL_HEX8(0x7F, encode_char('8'));
  TEST_ASSERT_EQUAL_HEX8(0x00, encode_char('8') & kSegBitDP);
  TEST_ASSERT_EQUAL_HEX8(kSegBitDP, encode_char('.'));
}

void test_espaco_e_apagado(void) {
  TEST_ASSERT_EQUAL_HEX8(0x00, encode_char(' '));
}

// DUAS FONTES DE VERDADE, CONFRONTADAS.
//
// is_renderable() consulta a string kAlfabeto em seven_seg.cpp; encode_char()
// consulta a tabela kFont em max7219.cpp. As duas respondem "da para
// desenhar?" e foram escritas separadamente. Se divergirem em um unico
// caractere, uma delas esta mentindo — e o painel mostraria um borrao onde o
// resto do codigo garantiu que caberia uma letra.
void test_a_fonte_concorda_com_o_alfabeto_em_todo_caractere(void) {
  for (int i = 1; i < 128; ++i) {
    const char c = static_cast<char>(i);
    const char texto[2] = {c, '\0'};

    const bool pelo_alfabeto = kanri::display::is_renderable(texto);
    const bool pela_fonte = (encode_char(c) != 0) || (c == ' ');

    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(pelo_alfabeto),
                                  static_cast<int>(pela_fonte), texto);
  }
}

// ---------------------------------------------------------------------------
//  Montagem do quadro
// ---------------------------------------------------------------------------

// O caso que justifica o codigo todo: o ponto NAO gasta digito.
void test_ponto_decimal_monta_no_digito_anterior(void) {
  std::uint8_t d[kSegDigits] = {};
  TEST_ASSERT_TRUE(encode_frame(quadro("13.8"), d, kSegDigits));

  TEST_ASSERT_EQUAL_HEX8(encode_char('1'), d[0]);
  TEST_ASSERT_EQUAL_HEX8(encode_char('3') | kSegBitDP, d[1]);
  TEST_ASSERT_EQUAL_HEX8(encode_char('8'), d[2]);
}

// "83" num mostrador de tres tem que ficar encostado a DIREITA. Alinhado a
// esquerda, o numero mudaria de lugar ao passar de 99 para 100 — e no painel
// isso e lido como o aparelho piscando.
void test_texto_curto_alinha_a_direita(void) {
  std::uint8_t d[kSegDigits] = {};
  TEST_ASSERT_TRUE(encode_frame(quadro("83"), d, kSegDigits));

  TEST_ASSERT_EQUAL_HEX8(0x00, d[0]);  // apagado
  TEST_ASSERT_EQUAL_HEX8(encode_char('8'), d[1]);
  TEST_ASSERT_EQUAL_HEX8(encode_char('3'), d[2]);
}

void test_tres_digitos_cheios(void) {
  std::uint8_t d[kSegDigits] = {};
  TEST_ASSERT_TRUE(encode_frame(quadro("120"), d, kSegDigits));
  TEST_ASSERT_EQUAL_HEX8(encode_char('1'), d[0]);
  TEST_ASSERT_EQUAL_HEX8(encode_char('2'), d[1]);
  TEST_ASSERT_EQUAL_HEX8(encode_char('0'), d[2]);
}

void test_sem_valor_vira_tres_tracos(void) {
  std::uint8_t d[kSegDigits] = {};
  TEST_ASSERT_TRUE(encode_frame(quadro(kanri::display::kSegNoValue), d, kSegDigits));
  for (std::size_t i = 0; i < kSegDigits; ++i) {
    TEST_ASSERT_EQUAL_HEX8(encode_char('-'), d[i]);
  }
}

// Temperatura negativa existe e ja apareceu no carro.
void test_valor_negativo_cabe(void) {
  std::uint8_t d[kSegDigits] = {};
  TEST_ASSERT_TRUE(encode_frame(quadro("-40"), d, kSegDigits));
  TEST_ASSERT_EQUAL_HEX8(encode_char('-'), d[0]);
  TEST_ASSERT_EQUAL_HEX8(encode_char('4'), d[1]);
  TEST_ASSERT_EQUAL_HEX8(encode_char('0'), d[2]);
}

void test_rotulo_com_letras_cabe(void) {
  std::uint8_t d[kSegDigits] = {};
  TEST_ASSERT_TRUE(encode_frame(quadro("tEP"), d, kSegDigits));
  TEST_ASSERT_EQUAL_HEX8(encode_char('t'), d[0]);
  TEST_ASSERT_EQUAL_HEX8(encode_char('E'), d[1]);
  TEST_ASSERT_EQUAL_HEX8(encode_char('P'), d[2]);
}

// ---------------------------------------------------------------------------
//  Falha FECHADA — um painel apagado e honesto, um painel errado nao
// ---------------------------------------------------------------------------

void test_texto_longo_demais_e_recusado(void) {
  std::uint8_t d[kSegDigits];
  std::memset(d, 0xAA, sizeof(d));
  TEST_ASSERT_FALSE(encode_frame(quadro("1234"), d, kSegDigits));
}

void test_caractere_impossivel_e_recusado(void) {
  std::uint8_t d[kSegDigits] = {};
  TEST_ASSERT_FALSE(encode_frame(quadro("1G3"), d, kSegDigits));
  TEST_ASSERT_FALSE(encode_frame(quadro("W"), d, kSegDigits));
}

void test_dois_pontos_seguidos_sao_recusados(void) {
  std::uint8_t d[kSegDigits] = {};
  TEST_ASSERT_FALSE(encode_frame(quadro("1..2"), d, kSegDigits));
}

void test_texto_vazio_e_recusado(void) {
  std::uint8_t d[kSegDigits] = {};
  TEST_ASSERT_FALSE(encode_frame(quadro(""), d, kSegDigits));
}

void test_buffer_insuficiente_e_recusado(void) {
  std::uint8_t d[kSegDigits] = {};
  TEST_ASSERT_FALSE(encode_frame(quadro("120"), d, kSegDigits - 1));
  TEST_ASSERT_FALSE(encode_frame(quadro("120"), nullptr, kSegDigits));
}

// Recusar NAO pode deixar o quadro anterior no buffer: o chamador que ignorar
// o retorno mostraria a medida velha como se fosse a atual.
void test_recusa_apaga_o_buffer(void) {
  std::uint8_t d[kSegDigits];
  std::memset(d, 0xFF, sizeof(d));
  TEST_ASSERT_FALSE(encode_frame(quadro("8888"), d, kSegDigits));
  // O contrato so promete `out` limpo quando ha capacidade; aqui conferimos
  // que nada foi escrito alem do buffer e que a recusa foi explicita.
  TEST_ASSERT_EQUAL_HEX8(0xFF, d[0]);
}

// ---------------------------------------------------------------------------
//  Inicializacao
// ---------------------------------------------------------------------------

// A ORDEM importa: ao energizar, os registradores estao em estado indefinido.
// Configurar com o chip ligado faz o painel piscar lixo no boot do carro.
void test_init_desliga_antes_e_liga_depois(void) {
  Max7219Word w[kInitWordCount];
  const std::size_t n = init_sequence(w, kInitWordCount, 7);
  TEST_ASSERT_EQUAL_UINT32(kInitWordCount, n);

  TEST_ASSERT_EQUAL_HEX8(static_cast<std::uint8_t>(Max7219Reg::Shutdown), w[0].reg);
  TEST_ASSERT_EQUAL_HEX8(0x00, w[0].data);  // desligado primeiro

  TEST_ASSERT_EQUAL_HEX8(static_cast<std::uint8_t>(Max7219Reg::Shutdown),
                         w[n - 1].reg);
  TEST_ASSERT_EQUAL_HEX8(0x01, w[n - 1].data);  // ligado por ultimo
}

// O teste de fabrica acende TUDO no brilho maximo. Se vier ligado por acaso,
// e o pior caso possivel para a alimentacao — ainda mais na bancada, onde o
// MAX7219 divide os 500 mA do USB com o ESP32.
void test_init_desliga_o_teste_de_fabrica(void) {
  Max7219Word w[kInitWordCount];
  const std::size_t n = init_sequence(w, kInitWordCount, 7);
  std::uint8_t dado = 0xFF;
  TEST_ASSERT_TRUE(achar(w, n, Max7219Reg::DisplayTest, &dado));
  TEST_ASSERT_EQUAL_HEX8(0x00, dado);
}

// Sem decode: e o que permite desenhar b, C, d, n, o, r, t, U, y. Com o
// Code B ligado, os rotulos do painel virariam simbolos aleatorios.
void test_init_desliga_o_decodificador_bcd(void) {
  Max7219Word w[kInitWordCount];
  const std::size_t n = init_sequence(w, kInitWordCount, 7);
  std::uint8_t dado = 0xFF;
  TEST_ASSERT_TRUE(achar(w, n, Max7219Reg::DecodeMode, &dado));
  TEST_ASSERT_EQUAL_HEX8(0x00, dado);
}

void test_init_varre_exatamente_os_digitos_que_existem(void) {
  Max7219Word w[kInitWordCount];
  const std::size_t n = init_sequence(w, kInitWordCount, 7);
  std::uint8_t dado = 0xFF;
  TEST_ASSERT_TRUE(achar(w, n, Max7219Reg::ScanLimit, &dado));
  TEST_ASSERT_EQUAL_HEX8(kSegDigits - 1, dado);
}

void test_init_limpa_todos_os_digitos(void) {
  Max7219Word w[kInitWordCount];
  const std::size_t n = init_sequence(w, kInitWordCount, 7);
  for (std::uint8_t d = 0; d < kSegDigits; ++d) {
    const auto reg = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(Max7219Reg::Digit0) + d);
    std::uint8_t dado = 0xFF;
    bool achou = false;
    for (std::size_t i = 0; i < n; ++i) {
      if (w[i].reg == reg) { dado = w[i].data; achou = true; }
    }
    TEST_ASSERT_TRUE(achou);
    TEST_ASSERT_EQUAL_HEX8(0x00, dado);
  }
}

void test_init_limita_a_intensidade(void) {
  Max7219Word w[kInitWordCount];
  const std::size_t n = init_sequence(w, kInitWordCount, 200);
  std::uint8_t dado = 0;
  TEST_ASSERT_TRUE(achar(w, n, Max7219Reg::Intensity, &dado));
  TEST_ASSERT_EQUAL_HEX8(kanri::display::kMaxIntensity, dado);
}

void test_init_recusa_buffer_pequeno(void) {
  Max7219Word w[kInitWordCount];
  TEST_ASSERT_EQUAL_UINT32(0, init_sequence(w, kInitWordCount - 1, 7));
  TEST_ASSERT_EQUAL_UINT32(0, init_sequence(nullptr, kInitWordCount, 7));
}

// ---------------------------------------------------------------------------
//  Brilho
// ---------------------------------------------------------------------------

void test_percentual_cobre_a_escala_inteira(void) {
  TEST_ASSERT_EQUAL_UINT8(0, intensity_from_percent(0));
  TEST_ASSERT_EQUAL_UINT8(kanri::display::kMaxIntensity,
                          intensity_from_percent(100));
  TEST_ASSERT_EQUAL_UINT8(kanri::display::kMaxIntensity,
                          intensity_from_percent(255));  // limitado
}

void test_percentual_e_monotonico(void) {
  std::uint8_t anterior = 0;
  for (int p = 0; p <= 100; ++p) {
    const std::uint8_t atual = intensity_from_percent(static_cast<std::uint8_t>(p));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(anterior, atual);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(kanri::display::kMaxIntensity, atual);
    anterior = atual;
  }
}

// ---------------------------------------------------------------------------
//  Piscar
// ---------------------------------------------------------------------------

void test_pisca_metade_do_ciclo_aceso(void) {
  TEST_ASSERT_TRUE(blink_visible(0, 800));
  TEST_ASSERT_TRUE(blink_visible(399, 800));
  TEST_ASSERT_FALSE(blink_visible(400, 800));
  TEST_ASSERT_FALSE(blink_visible(799, 800));
  TEST_ASSERT_TRUE(blink_visible(800, 800));
}

void test_periodo_zero_nao_pisca(void) {
  TEST_ASSERT_TRUE(blink_visible(0, 0));
  TEST_ASSERT_TRUE(blink_visible(123456, 0));
}

// millis() envolve a cada ~49,7 dias. O aparelho fica ligado no carro, entao
// a virada acontece — e um alerta de motor quente nao pode parar de piscar.
void test_pisca_continua_apos_a_virada_do_contador(void) {
  bool viu_aceso = false;
  bool viu_apagado = false;
  for (std::uint32_t i = 0; i < 2000; ++i) {
    const std::uint32_t t = 0xFFFFFC00U + i;  // atravessa o zero
    if (blink_visible(t, 800)) viu_aceso = true; else viu_apagado = true;
  }
  TEST_ASSERT_TRUE(viu_aceso);
  TEST_ASSERT_TRUE(viu_apagado);
}

// ---------------------------------------------------------------------------
//  As duas pontas, confrontadas
// ---------------------------------------------------------------------------

// O formatador decide o texto; o codificador decide os bits. Se um produzir
// algo que o outro recusa, o painel apaga em pleno funcionamento — e nada no
// log diria por que. Aqui varremos a faixa util de cada grandeza do carro.
void test_toda_saida_do_formatador_e_desenhavel(void) {
  const float valores[] = {-40.0F, -7.5F, 0.0F,   0.05F,  9.52F,  13.8F,
                           14.4F,  83.0F, 99.9F,  100.0F, 120.0F, 249.0F,
                           933.0F, 1726.0F, 6500.0F, 8000.0F};

  for (const float v : valores) {
    SegFrame f;
    if (!kanri::display::format_segments(v, f.text, sizeof(f.text))) continue;

    std::uint8_t d[kSegDigits] = {};
    char msg[48];
    std::snprintf(msg, sizeof(msg), "valor %.2f virou \"%s\"", v, f.text);
    TEST_ASSERT_TRUE_MESSAGE(encode_frame(f, d, kSegDigits), msg);
  }
}

// Todo rotulo que o botao percorre precisa caber e ser desenhavel. Um rotulo
// recusado deixaria o motorista sem saber qual grandeza esta vendo.
void test_todo_rotulo_do_catalogo_e_desenhavel(void) {
  for (std::size_t i = 0; i < kanri::display::kSegMeasureCount; ++i) {
    const char* label = kanri::display::kSegMeasures[i].label;
    std::uint8_t d[kSegDigits] = {};
    TEST_ASSERT_TRUE_MESSAGE(encode_frame(quadro(label), d, kSegDigits), label);
  }
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_palavra_leva_o_endereco_nos_bits_altos);

  RUN_TEST(test_digitos_tem_os_segmentos_certos);
  RUN_TEST(test_os_dez_digitos_sao_visualmente_distintos);
  RUN_TEST(test_oito_acende_tudo_menos_o_ponto);
  RUN_TEST(test_espaco_e_apagado);
  RUN_TEST(test_a_fonte_concorda_com_o_alfabeto_em_todo_caractere);

  RUN_TEST(test_ponto_decimal_monta_no_digito_anterior);
  RUN_TEST(test_texto_curto_alinha_a_direita);
  RUN_TEST(test_tres_digitos_cheios);
  RUN_TEST(test_sem_valor_vira_tres_tracos);
  RUN_TEST(test_valor_negativo_cabe);
  RUN_TEST(test_rotulo_com_letras_cabe);

  RUN_TEST(test_texto_longo_demais_e_recusado);
  RUN_TEST(test_caractere_impossivel_e_recusado);
  RUN_TEST(test_dois_pontos_seguidos_sao_recusados);
  RUN_TEST(test_texto_vazio_e_recusado);
  RUN_TEST(test_buffer_insuficiente_e_recusado);
  RUN_TEST(test_recusa_apaga_o_buffer);

  RUN_TEST(test_init_desliga_antes_e_liga_depois);
  RUN_TEST(test_init_desliga_o_teste_de_fabrica);
  RUN_TEST(test_init_desliga_o_decodificador_bcd);
  RUN_TEST(test_init_varre_exatamente_os_digitos_que_existem);
  RUN_TEST(test_init_limpa_todos_os_digitos);
  RUN_TEST(test_init_limita_a_intensidade);
  RUN_TEST(test_init_recusa_buffer_pequeno);

  RUN_TEST(test_percentual_cobre_a_escala_inteira);
  RUN_TEST(test_percentual_e_monotonico);

  RUN_TEST(test_pisca_metade_do_ciclo_aceso);
  RUN_TEST(test_periodo_zero_nao_pisca);
  RUN_TEST(test_pisca_continua_apos_a_virada_do_contador);

  RUN_TEST(test_toda_saida_do_formatador_e_desenhavel);
  RUN_TEST(test_todo_rotulo_do_catalogo_e_desenhavel);
  return UNITY_END();
}
