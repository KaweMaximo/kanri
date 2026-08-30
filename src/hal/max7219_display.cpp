#include "hal/max7219_display.h"

#include <Arduino.h>

namespace kanri::hal {
namespace {

// 1 MHz, e nao os 10 MHz que o chip aceita. Na protoboard, com jumper solto e
// um 74HCT125 no caminho, a borda rapida toca e o dado chega corrompido — sem
// erro nenhum, so um numero errado no painel. O tempo nao importa aqui: a
// inicializacao inteira sao 9 palavras, 144 bits, menos de 0,2 ms.
constexpr std::uint32_t kSpiHz = 1000000;

// Brilho inicial deliberadamente BAIXO.
//
// Na bancada o MAX7219 costuma ser alimentado pelo mesmo 5 V do USB que
// alimenta o ESP32. Um mostrador em brilho maximo puxa perto de 300 mA, e o
// ESP32 com Bluetooth ativo ja usa boa parte dos 500 mA da porta. Se a linha
// cair, quem reinicia e o ESP32 — e o log vai dizer BROWNOUT.
//
// O registrador de intensidade e duty cycle, entao este numero corta a
// corrente media de verdade, nao so o brilho percebido. Subir e um comando de
// console; descer depois de queimar a fonte nao e.
constexpr std::uint8_t kBrilhoInicialPct = 30;

}  // namespace

bool Max7219Display::begin() {
  pinMode(load_, OUTPUT);
  digitalWrite(load_, HIGH);  // repouso alto: o dado engata na SUBIDA

  // MISO = -1: o MAX7219 tem DOUT, mas so para encadear chips. Nao lemos
  // nada dele — e por isso nao ha como confirmar que ele esta vivo. begin()
  // devolve true querendo dizer "o barramento foi configurado", nao "o chip
  // respondeu". Nao ha protocolo de leitura para prometer mais que isso.
  SPI.begin(clk_, -1, din_, load_);

  kanri::display::Max7219Word init[kanri::display::kInitWordCount];
  const std::size_t n = kanri::display::init_sequence(
      init, kanri::display::kInitWordCount,
      kanri::display::intensity_from_percent(kBrilhoInicialPct));
  if (n == 0) return false;

  pronto_ = true;
  for (std::size_t i = 0; i < n; ++i) escrever(init[i]);

  Serial.printf("[7seg] MAX7219 em DIN=%u CLK=%u LOAD=%u, brilho %u%%\n",
                static_cast<unsigned>(din_), static_cast<unsigned>(clk_),
                static_cast<unsigned>(load_),
                static_cast<unsigned>(kBrilhoInicialPct));
  return true;
}

void Max7219Display::render(const kanri::display::SegFrame& frame) {
  if (!pronto_) return;

  std::uint8_t digitos[kanri::display::kSegDigits];
  if (!kanri::display::encode_frame(frame, digitos,
                                    kanri::display::kSegDigits)) {
    // Recusa do tradutor: apagamos. Um painel apagado e honesto; um painel
    // com resto do quadro anterior mente sobre o estado do carro.
    clear();
    return;
  }

  // digitos[0] e o da ESQUERDA na leitura. Qual registrador do chip aciona
  // esse digito depende da fiacao, e a resposta mora em digit_register() —
  // funcao pura, com teste. Ver kanri_display/max7219.h.
  for (std::size_t i = 0; i < kanri::display::kSegDigits; ++i) {
    escrever({kanri::display::digit_register(i), digitos[i]});
  }
}

void Max7219Display::render_raw(const std::uint8_t* digits, std::size_t count) {
  if (!pronto_ || digits == nullptr) return;
  if (count > kanri::display::kSegDigits) count = kanri::display::kSegDigits;

  // Mesmo mapeamento de render(), pela mesma funcao — nao por uma copia da
  // conta, que e como uma das duas fica para tras numa correcao.
  for (std::size_t i = 0; i < count; ++i) {
    escrever({kanri::display::digit_register(i), digits[i]});
  }
}

void Max7219Display::set_intensity(std::uint8_t intensity) {
  if (!pronto_) return;
  if (intensity > kanri::display::kMaxIntensity) {
    intensity = kanri::display::kMaxIntensity;
  }
  escrever({static_cast<std::uint8_t>(kanri::display::Max7219Reg::Intensity),
            intensity});
}

void Max7219Display::set_digit_raw(std::uint8_t reg, std::uint8_t bits) {
  if (!pronto_) return;
  escrever({reg, bits});
}

void Max7219Display::set_scan_limit(std::uint8_t last_index) {
  if (!pronto_) return;
  if (last_index > kanri::display::kMax7219Digits - 1) {
    last_index = kanri::display::kMax7219Digits - 1;
  }
  escrever({static_cast<std::uint8_t>(kanri::display::Max7219Reg::ScanLimit),
            last_index});
}

void Max7219Display::set_brightness(std::uint8_t percent) {
  if (!pronto_) return;
  escrever({static_cast<std::uint8_t>(kanri::display::Max7219Reg::Intensity),
            kanri::display::intensity_from_percent(percent)});
}

void Max7219Display::clear() {
  if (!pronto_) return;
  for (std::uint8_t d = 0; d < kanri::display::kSegDigits; ++d) {
    escrever({static_cast<std::uint8_t>(
                  static_cast<std::uint8_t>(
                      kanri::display::Max7219Reg::Digit0) + d),
              0x00});
  }
}

void Max7219Display::escrever(const kanri::display::Max7219Word& palavra) {
  SPI.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(load_, LOW);
  SPI.transfer16(kanri::display::to_bits(palavra));
  digitalWrite(load_, HIGH);  // a subida do LOAD e o que engata os 16 bits
  SPI.endTransaction();
}

}  // namespace kanri::hal
