#pragma once
// ============================================================================
//  kanri_display/max7219.h — O protocolo do MAX7219, em funcao pura
// ============================================================================
//  Este arquivo NAO fala com hardware. Ele traduz "quero mostrar 13.8" nos
//  bytes exatos que o chip espera, e nada mais. O SPI mora em
//  src/hal/max7219_display.cpp.
//
//  A divisao existe porque quase tudo que da errado num display de 7
//  segmentos e erro de TRADUCAO, nao de fio: o ponto decimal na casa errada,
//  o numero alinhado ao contrario, uma letra que vira borrao. Nada disso
//  precisa do chip para ser testado — e testar no PC e a diferenca entre ver
//  o erro em 300 ms e desmontar a protoboard para procurar.
//
//  DECISAO: MODO SEM DECODE
//  ------------------------
//  O MAX7219 tem um decodificador BCD embutido (Code B) que desenha digitos
//  sozinho. Nao usamos: ele so sabe 0-9, "-", E, H, L, P e branco. Os rotulos
//  do painel precisam de b, C, d, n, o, r, t, U, y — que o Code B nao tem.
//
//  Sem decode, cada bit do registrador e um segmento, e nos controlamos tudo.
//  O custo e ter a fonte aqui embaixo; o ganho e o painel poder dizer "tEP"
//  em vez de um numero sem contexto.
//
//  LAYOUT DOS BITS (datasheet, tabela 6)
//  -------------------------------------
//      bit  7   6   5   4   3   2   1   0
//           DP  A   B   C   D   E   F   G
//
//           AAA
//          F   B
//          F   B
//           GGG
//          E   C
//          E   C
//           DDD  DP
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "kanri_display/seven_seg.h"

namespace kanri::display {

/// Registradores do MAX7219 (datasheet, tabela 2).
enum class Max7219Reg : std::uint8_t {
  NoOp = 0x00,
  Digit0 = 0x01,
  Digit1 = 0x02,
  Digit2 = 0x03,
  Digit3 = 0x04,
  Digit4 = 0x05,
  Digit5 = 0x06,
  Digit6 = 0x07,
  Digit7 = 0x08,
  DecodeMode = 0x09,
  Intensity = 0x0A,
  ScanLimit = 0x0B,
  Shutdown = 0x0C,
  DisplayTest = 0x0F,
};

/// Uma escrita no chip: 16 bits, endereco em cima e dado embaixo.
struct Max7219Word {
  std::uint8_t reg = 0;
  std::uint8_t data = 0;
};

/// Empacota a palavra do jeito que sai no fio: endereco nos 8 bits altos.
constexpr std::uint16_t to_bits(const Max7219Word& w) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(w.reg) << 8) |
                                    w.data);
}

/// Bit do ponto decimal. Ele nao ocupa digito: monta no digito anterior.
constexpr std::uint8_t kSegBitDP = 0x80;

/// Quantas palavras a sequencia de inicializacao gera.
constexpr std::size_t kInitWordCount = 5 + kSegDigits + 1;

/// Intensidade maxima aceita pelo chip (registrador de 4 bits).
constexpr std::uint8_t kMaxIntensity = 15;

/// O padrao de segmentos de um caractere.
///
/// @return 0 quando o caractere nao tem forma reconhecivel em 7 segmentos.
///         Zero e "apagado", que e exatamente o que queremos mostrar em vez
///         de um borrao — mas o chamador deve tratar isso como recusa, nao
///         como sucesso. Ver encode_frame(), que falha fechado.
std::uint8_t encode_char(char c);

/// Traduz o quadro nos bytes dos digitos, JA ALINHADOS A DIREITA.
///
/// `out[0]` e o digito da ESQUERDA na leitura, `out[kSegDigits-1]` o da
/// direita. O mapeamento para os registradores Digit0..DigitN do chip depende
/// de como o mostrador foi soldado, e por isso mora no HAL, nao aqui.
///
/// O ponto decimal NAO consome posicao: ele acende o bit DP do digito que
/// vem antes dele. "13.8" ocupa tres digitos, nao quatro.
///
/// Falha FECHADO: se algum caractere for irrepresentavel, ou se o texto nao
/// couber, devolve false e apaga `out`. Um painel apagado e honesto; um
/// painel com o numero errado nao.
///
/// @return false em recusa; nesse caso `out` fica todo zerado.
bool encode_frame(const SegFrame& frame, std::uint8_t* out, std::size_t cap);

/// A sequencia que deixa o chip pronto para desenhar.
///
/// Escreve `kInitWordCount` palavras. A ordem importa: o chip e desligado
/// (Shutdown) ANTES de ser configurado e religado no fim, para que o lixo que
/// os registradores tem ao energizar nunca chegue a aparecer no painel.
///
/// @return quantas palavras foram escritas; 0 se `cap` for insuficiente.
std::size_t init_sequence(Max7219Word* out, std::size_t cap,
                          std::uint8_t intensity);

/// Converte 0..100 % na escala de 0..15 do chip, limitando a entrada.
///
/// Note que 0 % NAO apaga: e o passo mais fraco (1/32 do ciclo), ainda
/// visivel. Apagar de verdade e o registrador Shutdown.
std::uint8_t intensity_from_percent(std::uint8_t percent);

// ---------------------------------------------------------------------------
//  Autoteste do mostrador
// ---------------------------------------------------------------------------
//  Serve para responder, sem o carro e sem multimetro, tres perguntas que a
//  fiacao de um mostrador de 7 segmentos sempre levanta:
//
//    1. Todo segmento acende?     -> um passo por segmento, isolado
//    2. Todo digito acende?       -> um passo por digito, isolado
//    3. A ORDEM dos digitos esta certa?  -> mostra "123"
//
//  A terceira e a que ninguem lembra de conferir. Se o mostrador exibir
//  "321", os fios estao todos bons e o painel mente em silencio — e a correcao
//  e uma linha no HAL, nao um fio.

// ---------------------------------------------------------------------------
//  Orientacao da fiacao
// ---------------------------------------------------------------------------
//  Qual registrador Digit do chip corresponde ao digito da ESQUERDA depende
//  de como o mostrador foi ligado, e nao ha como o firmware descobrir isso
//  sozinho: o MAX7219 nao responde nada.
//
//  Confirmado na bancada em 29/08/2026, na montagem do Kanri: `seg 38.1`
//  aparecia como "18.3". Ordem espelhada, com o ponto decimal acompanhando
//  o digito certo — ou seja, um puro erro de mapeamento, nao de fonte.
//
//  Esta constante e a unica coisa a mudar se a proxima montagem for ao
//  contrario.

/// true quando o Digit0 do chip aciona o digito da ESQUERDA.
constexpr bool kDigit0IsLeftmost = true;

// ---------------------------------------------------------------------------
//  Digitos sobrando do MAX7219
// ---------------------------------------------------------------------------
//  O MAX7219 tem OITO saidas de digito e o mostrador usa tres. As outras
//  cinco estao livres, e cada uma comanda oito LEDs — as proprias linhas de
//  segmento. Sao 40 posicoes de LED sem gastar um GPIO do ESP32, e sem
//  resistor nenhum, porque o ISET ja limita a corrente de todos.
//
//  ⚠️ O DETALHE QUE MORDE: o chip so varre ate o ScanLimit. Ligar LEDs no
//  digito 4 e NAO subir o ScanLimit resulta em... nada. O chip nunca aciona
//  aquela linha. Sem erro, sem pista — o mesmo tipo de falha silenciosa do
//  GPIO 28.
//
//  ⚠️ E O PRECO: o ScanLimit divide o ciclo entre os digitos varridos. Passar
//  de 3 para 4 faz cada um receber 1/4 em vez de 1/3 — o mostrador inteiro
//  fica ~25% mais fraco. Nao e defeito, e multiplexacao; da para compensar no
//  registrador de intensidade ou baixando o ISET.

/// Quantos digitos o MAX7219 tem ao todo.
constexpr std::uint8_t kMax7219Digits = 8;

/// Por que um digito nao pode receber LEDs externos.
enum class SpareDigitVerdict : std::uint8_t {
  Ok = 0,
  UsedByDisplay,  ///< E um dos digitos do mostrador.
  OutOfRange,     ///< O chip so tem 8 digitos.
};

/// A posicao `human` (contando de 1, como uma pessoa conta) serve de barra?
///
/// Conta de 1 de proposito: quem esta na bancada diz "o quarto digito", e
/// traduzir para indice na cabeca e onde nasce o erro de um.
SpareDigitVerdict check_spare_digit(std::uint8_t human);

const char* to_string(SpareDigitVerdict verdict);

/// Registrador do chip para a posicao `human` (1..8).
std::uint8_t spare_digit_register(std::uint8_t human);

/// Quantos digitos o chip precisa varrer para incluir a barra em `human`.
std::uint8_t scan_limit_for_digit(std::uint8_t human);

/// Traduz a posicao de LEITURA (0 = esquerda) no registrador do chip.
///
/// Mora aqui, e nao no HAL, porque estava duplicada em render() e
/// render_raw() — duas copias da mesma conta e o lugar classico onde uma e
/// corrigida e a outra nao.
std::uint8_t digit_register(std::size_t reading_position);

/// Um passo do autoteste.
struct SegTestStep {
  std::uint8_t digits[kSegDigits] = {};  ///< Bytes crus, na ordem de LEITURA.
  const char* espera = "";               ///< O que o operador deve estar vendo.
};

/// Quantos passos o autoteste tem.
constexpr std::size_t kSegTestSteps = 1 + 8 + kSegDigits + 2;

/// Monta o passo `index` do autoteste.
/// @return false se o indice nao existe.
bool seg_test_step(std::size_t index, SegTestStep* out);

/// Num quadro que pisca, o conteudo aparece na primeira metade do ciclo.
///
/// Fica aqui, e nao no HAL, porque "esta visivel agora?" e uma conta com o
/// relogio — e conta com relogio e onde nascem os erros de virada de contador.
bool blink_visible(std::uint32_t now_ms, std::uint32_t period_ms = 800);

}  // namespace kanri::display
