#include "kanri_core/pin_guard.h"

namespace kanri::core {
namespace {

/// Numeros que simplesmente nao existem no ESP32.
constexpr std::uint8_t kInexistentes[] = {20, 24, 28, 29, 30, 31};

/// Input-only: leem, nunca acionam.
constexpr std::uint8_t kSomenteEntrada[] = {34, 35, 36, 39};

bool contem(const std::uint8_t* lista, std::size_t n, std::uint8_t v) {
  if (lista == nullptr) return false;
  for (std::size_t i = 0; i < n; ++i) {
    if (lista[i] == v) return true;
  }
  return false;
}

template <std::size_t N>
bool contem(const std::uint8_t (&lista)[N], std::uint8_t v) {
  return contem(lista, N, v);
}

}  // namespace

PinVerdict check_output_pin(std::uint8_t pin, const std::uint8_t* reserved,
                            std::size_t count) {
  if (pin > 39) return PinVerdict::OutOfRange;
  if (contem(kInexistentes, pin)) return PinVerdict::DoesNotExist;

  // A flash vem antes de tudo: acionar 6-11 trava o chip na hora, entao nao
  // interessa se o firmware tambem reservou o pino — a razao mais grave e
  // que deve chegar a quem digitou.
  if (pin >= 6 && pin <= 11) return PinVerdict::SpiFlash;

  if (contem(kSomenteEntrada, pin)) return PinVerdict::InputOnly;
  if (pin == 1 || pin == 3) return PinVerdict::UsbSerial;
  if (pin == 12) return PinVerdict::BootStrapping;

  if (contem(reserved, count, pin)) return PinVerdict::Reserved;
  return PinVerdict::Ok;
}

const char* to_string(PinVerdict verdict) {
  switch (verdict) {
    case PinVerdict::Ok:            return "ok";
    case PinVerdict::DoesNotExist:  return "este GPIO nao existe no ESP32";
    case PinVerdict::OutOfRange:    return "fora da faixa (0-39)";
    case PinVerdict::InputOnly:     return "input-only: nao aciona nada";
    case PinVerdict::SpiFlash:      return "flash SPI: acionar trava o chip";
    case PinVerdict::UsbSerial:     return "console USB: derrubaria o painel";
    case PinVerdict::BootStrapping: return "strapping: impediria o boot";
    case PinVerdict::Reserved:      return "ja usado por este firmware";
  }
  return "desconhecido";
}

}  // namespace kanri::core
