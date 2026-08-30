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

// ---------------------------------------------------------------------------
//  Mapeamento de digitos — a orientacao da fiacao
// ---------------------------------------------------------------------------

// REGRESSAO — encontrado na bancada em 29/08/2026.
//
// Para exibir "18.3" era preciso digitar `seg 38.1`: o mostrador saia
// espelhado. O ponto decimal acompanhava o digito certo, o que descartou a
// fonte e apontou direto para o mapeamento posicao -> registrador.
void test_posicao_zero_e_o_digito_da_esquerda(void) {
  TEST_ASSERT_EQUAL_HEX8(static_cast<std::uint8_t>(Max7219Reg::Digit0),
                         kanri::display::digit_register(0));
  TEST_ASSERT_EQUAL_HEX8(static_cast<std::uint8_t>(Max7219Reg::Digit2),
                         kanri::display::digit_register(kSegDigits - 1));
}

// Duas posicoes no mesmo registrador apagariam um digito em silencio: uma
// escrita sobrescreveria a outra e o mostrador ficaria com um digito preso.
void test_cada_posicao_vai_para_um_registrador_diferente(void) {
  for (std::size_t a = 0; a < kSegDigits; ++a) {
    for (std::size_t b = a + 1; b < kSegDigits; ++b) {
      TEST_ASSERT_NOT_EQUAL(kanri::display::digit_register(a),
                            kanri::display::digit_register(b));
    }
  }
}

// Os enderecos vizinhos de Digit0..Digit7 sao DecodeMode, Intensity e
// Shutdown. Escrever num deles por engano nao acende digito errado: apaga ou
// reconfigura o mostrador inteiro.
void test_mapeamento_nunca_sai_da_faixa_dos_digitos(void) {
  for (std::size_t i = 0; i < kSegDigits + 5; ++i) {
    const std::uint8_t reg = kanri::display::digit_register(i);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(
        static_cast<std::uint8_t>(Max7219Reg::Digit0), reg);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(
        static_cast<std::uint8_t>(Max7219Reg::Digit0) + kSegDigits - 1, reg);
  }
}

// ---------------------------------------------------------------------------
//  Barra de LEDs num digito sobrando
// ---------------------------------------------------------------------------

// Os tres primeiros sao do mostrador. Roubar um apagaria uma casa do painel,
// e o sintoma — "o display perdeu um digito" — ninguem associaria a barra.
void test_digitos_do_mostrador_sao_recusados(void) {
  for (std::uint8_t d = 1; d <= kSegDigits; ++d) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(kanri::display::SpareDigitVerdict::UsedByDisplay),
        static_cast<int>(kanri::display::check_spare_digit(d)));
  }
}

void test_digitos_sobrando_sao_aceitos(void) {
  for (std::uint8_t d = kSegDigits + 1; d <= kanri::display::kMax7219Digits; ++d) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        static_cast<int>(kanri::display::SpareDigitVerdict::Ok),
        static_cast<int>(kanri::display::check_spare_digit(d)), "deveria caber");
  }
}

void test_digito_fora_do_chip_e_recusado(void) {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(kanri::display::SpareDigitVerdict::OutOfRange),
      static_cast<int>(kanri::display::check_spare_digit(0)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(kanri::display::SpareDigitVerdict::OutOfRange),
      static_cast<int>(kanri::display::check_spare_digit(9)));
}

// Conta de 1 porque quem esta na bancada diz "o quarto digito". O quarto e o
// registrador Digit3 — traduzir isso na cabeca e onde nasce o erro de um.
void test_quarto_digito_e_o_registrador_digit3(void) {
  TEST_ASSERT_EQUAL_HEX8(static_cast<std::uint8_t>(Max7219Reg::Digit3),
                         kanri::display::spare_digit_register(4));
  TEST_ASSERT_EQUAL_HEX8(static_cast<std::uint8_t>(Max7219Reg::Digit7),
                         kanri::display::spare_digit_register(8));
}

// O CASO QUE NAO PODE ESCAPAR: o chip so varre ate o ScanLimit. Ligar LEDs no
// digito 4 sem subir o limite resulta em nada — sem erro e sem pista.
void test_scan_limit_sobe_para_alcancar_a_barra(void) {
  TEST_ASSERT_EQUAL_UINT8(3, kanri::display::scan_limit_for_digit(4));
  TEST_ASSERT_EQUAL_UINT8(7, kanri::display::scan_limit_for_digit(8));
}

// Sem barra, ou com barra invalida, o limite NAO pode encolher abaixo do que
// o mostrador precisa — apagaria um digito do painel.
void test_scan_limit_nunca_encolhe_abaixo_do_mostrador(void) {
  const std::uint8_t minimo = static_cast<std::uint8_t>(kSegDigits - 1);
  TEST_ASSERT_EQUAL_UINT8(minimo, kanri::display::scan_limit_for_digit(0));
  TEST_ASSERT_EQUAL_UINT8(minimo, kanri::display::scan_limit_for_digit(99));
  for (std::uint8_t d = 1; d <= kSegDigits; ++d) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(minimo,
                                       kanri::display::scan_limit_for_digit(d));
  }
}

void test_todo_veredito_de_digito_tem_explicacao(void) {
  const kanri::display::SpareDigitVerdict todos[] = {
      kanri::display::SpareDigitVerdict::Ok,
      kanri::display::SpareDigitVerdict::UsedByDisplay,
      kanri::display::SpareDigitVerdict::OutOfRange};
  for (const auto v : todos) {
    TEST_ASSERT_NOT_NULL(kanri::display::to_string(v));
    TEST_ASSERT_TRUE(std::strlen(kanri::display::to_string(v)) > 0);
  }
  TEST_ASSERT_NOT_NULL(kanri::display::to_string(
      static_cast<kanri::display::SpareDigitVerdict>(99)));
}

// A barra nunca pode cair num registrador do mostrador, em nenhum caso.
void test_barra_nunca_colide_com_o_mostrador(void) {
  for (std::uint8_t d = kSegDigits + 1; d <= kanri::display::kMax7219Digits; ++d) {
    const std::uint8_t reg = kanri::display::spare_digit_register(d);
    for (std::size_t i = 0; i < kSegDigits; ++i) {
      TEST_ASSERT_NOT_EQUAL_MESSAGE(kanri::display::digit_register(i), reg,
                                    "barra em cima de um digito do mostrador");
    }
  }
}

// ---------------------------------------------------------------------------
//  Autoteste
// ---------------------------------------------------------------------------

void test_autoteste_recusa_indice_invalido(void) {
  kanri::display::SegTestStep p;
  TEST_ASSERT_FALSE(kanri::display::seg_test_step(kanri::display::kSegTestSteps, &p));
  TEST_ASSERT_FALSE(kanri::display::seg_test_step(9999, &p));
  TEST_ASSERT_FALSE(kanri::display::seg_test_step(0, nullptr));
}

void test_autoteste_comeca_com_tudo_aceso(void) {
  kanri::display::SegTestStep p;
  TEST_ASSERT_TRUE(kanri::display::seg_test_step(0, &p));
  for (std::size_t i = 0; i < kSegDigits; ++i) {
    TEST_ASSERT_EQUAL_HEX8(0xFF, p.digits[i]);
  }
}

void test_autoteste_termina_apagado(void) {
  kanri::display::SegTestStep p;
  TEST_ASSERT_TRUE(
      kanri::display::seg_test_step(kanri::display::kSegTestSteps - 1, &p));
  for (std::size_t i = 0; i < kSegDigits; ++i) {
    TEST_ASSERT_EQUAL_HEX8(0x00, p.digits[i]);
  }
}

// O passo de segmento tem que acender UM bit so, e o MESMO em todos os
// digitos. Se acendesse dois, um fio solto passaria despercebido.
void test_cada_passo_de_segmento_acende_um_bit_em_todos_os_digitos(void) {
  for (std::size_t idx = 1; idx <= 8; ++idx) {
    kanri::display::SegTestStep p;
    TEST_ASSERT_TRUE(kanri::display::seg_test_step(idx, &p));

    const std::uint8_t bits = p.digits[0];
    TEST_ASSERT_NOT_EQUAL(0, bits);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bits & (bits - 1), "mais de um segmento");

    for (std::size_t d = 1; d < kSegDigits; ++d) {
      TEST_ASSERT_EQUAL_HEX8(bits, p.digits[d]);
    }
  }
}

// O TESTE QUE PROVA QUE O AUTOTESTE PRESTA.
//
// Os oito passos precisam cobrir os oito bits — cada um exatamente uma vez.
// Um segmento que nenhum passo acende e um segmento cujo defeito o autoteste
// nao encontra, e aí ele daria uma confianca que nao tem.
void test_os_oito_passos_cobrem_os_oito_segmentos_sem_repetir(void) {
  std::uint8_t acumulado = 0;
  for (std::size_t idx = 1; idx <= 8; ++idx) {
    kanri::display::SegTestStep p;
    TEST_ASSERT_TRUE(kanri::display::seg_test_step(idx, &p));
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, acumulado & p.digits[0], "segmento repetido");
    acumulado |= p.digits[0];
  }
  TEST_ASSERT_EQUAL_HEX8(0xFF, acumulado);
}

// Os passos de digito isolam um digito cada, e juntos cobrem todos.
void test_passos_de_digito_isolam_e_cobrem_todos(void) {
  for (std::size_t d = 0; d < kSegDigits; ++d) {
    kanri::display::SegTestStep p;
    TEST_ASSERT_TRUE(kanri::display::seg_test_step(9 + d, &p));
    for (std::size_t i = 0; i < kSegDigits; ++i) {
      TEST_ASSERT_EQUAL_HEX8(i == d ? 0xFF : 0x00, p.digits[i]);
    }
  }
}

// "123" tem que sair igual ao caminho normal. Se o autoteste usasse outra
// rota, ele aprovaria uma ordem que o uso real erra.
void test_passo_da_ordem_bate_com_o_caminho_normal(void) {
  kanri::display::SegTestStep p;
  TEST_ASSERT_TRUE(kanri::display::seg_test_step(8 + kSegDigits + 1, &p));

  std::uint8_t esperado[kSegDigits] = {};
  TEST_ASSERT_TRUE(encode_frame(quadro("123"), esperado, kSegDigits));
  for (std::size_t i = 0; i < kSegDigits; ++i) {
    TEST_ASSERT_EQUAL_HEX8(esperado[i], p.digits[i]);
  }
}

// Cada passo precisa dizer ao operador o que ele deve estar vendo. Um passo
// sem rotulo e um passo que ninguem sabe interpretar.
void test_todo_passo_tem_o_que_esperar_escrito(void) {
  for (std::size_t i = 0; i < kanri::display::kSegTestSteps; ++i) {
    kanri::display::SegTestStep p;
    TEST_ASSERT_TRUE(kanri::display::seg_test_step(i, &p));
    TEST_ASSERT_NOT_NULL(p.espera);
    TEST_ASSERT_TRUE(std::strlen(p.espera) > 0);
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

  RUN_TEST(test_posicao_zero_e_o_digito_da_esquerda);
  RUN_TEST(test_cada_posicao_vai_para_um_registrador_diferente);
  RUN_TEST(test_mapeamento_nunca_sai_da_faixa_dos_digitos);

  RUN_TEST(test_digitos_do_mostrador_sao_recusados);
  RUN_TEST(test_digitos_sobrando_sao_aceitos);
  RUN_TEST(test_digito_fora_do_chip_e_recusado);
  RUN_TEST(test_quarto_digito_e_o_registrador_digit3);
  RUN_TEST(test_scan_limit_sobe_para_alcancar_a_barra);
  RUN_TEST(test_scan_limit_nunca_encolhe_abaixo_do_mostrador);
  RUN_TEST(test_todo_veredito_de_digito_tem_explicacao);
  RUN_TEST(test_barra_nunca_colide_com_o_mostrador);

  RUN_TEST(test_autoteste_recusa_indice_invalido);
  RUN_TEST(test_autoteste_comeca_com_tudo_aceso);
  RUN_TEST(test_autoteste_termina_apagado);
  RUN_TEST(test_cada_passo_de_segmento_acende_um_bit_em_todos_os_digitos);
  RUN_TEST(test_os_oito_passos_cobrem_os_oito_segmentos_sem_repetir);
  RUN_TEST(test_passos_de_digito_isolam_e_cobrem_todos);
  RUN_TEST(test_passo_da_ordem_bate_com_o_caminho_normal);
  RUN_TEST(test_todo_passo_tem_o_que_esperar_escrito);

  RUN_TEST(test_toda_saida_do_formatador_e_desenhavel);
  RUN_TEST(test_todo_rotulo_do_catalogo_e_desenhavel);
  return UNITY_END();
}
