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

PinList parse_pin_list(const char* text, const std::uint8_t* reserved,
                       std::size_t reserved_count) {
  PinList lista;
  if (text == nullptr) {
    lista.error = PinListError::Empty;
    return lista;
  }

  std::size_t i = 0;
  while (text[i] != '\0') {
    // Separadores: espaco e virgula. Aceitar os dois porque ninguem lembra
    // qual e o certo, e errar isso nao deveria custar uma tentativa.
    while (text[i] == ' ' || text[i] == ',' || text[i] == '\t') ++i;
    if (text[i] == '\0') break;

    if (text[i] < '0' || text[i] > '9') {
      lista.error = PinListError::NotANumber;
      return lista;
    }

    std::uint32_t valor = 0;
    while (text[i] >= '0' && text[i] <= '9') {
      valor = valor * 10 + static_cast<std::uint32_t>(text[i] - '0');
      if (valor > 255) valor = 255;  // satura; check_output_pin recusa depois
      ++i;
    }

    if (lista.count >= kMaxPinList) {
      lista.error = PinListError::TooMany;
      return lista;
    }

    const std::uint8_t pino = static_cast<std::uint8_t>(valor);

    for (std::size_t j = 0; j < lista.count; ++j) {
      if (lista.pins[j] == pino) {
        lista.error = PinListError::Duplicate;
        lista.offending = pino;
        return lista;
      }
    }

    const PinVerdict v = check_output_pin(pino, reserved, reserved_count);
    if (v != PinVerdict::Ok) {
      lista.error = PinListError::BadPin;
      lista.offending = pino;
      lista.verdict = v;
      return lista;
    }

    lista.pins[lista.count] = pino;
    ++lista.count;
  }

  if (lista.count == 0) lista.error = PinListError::Empty;
  return lista;
}

const char* to_string(PinListError error) {
  switch (error) {
    case PinListError::None:       return "ok";
    case PinListError::Empty:      return "nenhum pino informado";
    case PinListError::TooMany:    return "pinos demais na lista";
    case PinListError::NotANumber: return "havia algo que nao e numero";
    case PinListError::Duplicate:  return "pino repetido na lista";
    case PinListError::BadPin:     return "um dos pinos foi recusado";
  }
  return "desconhecido";
}

std::uint8_t bar_blink_mask(std::uint32_t now_ms, std::size_t count,
                            std::uint32_t period_ms) {
  if (count == 0) return 0;
  if (count > kMaxPinList) count = kMaxPinList;
  if (period_ms == 0) period_ms = 1;

  // Aceso na primeira metade do ciclo. Subtracao sem sinal aqui nao existe:
  // o modulo lida com a virada do millis() sozinho.
  if ((now_ms % period_ms) >= (period_ms / 2)) return 0;

  // count == 8 estouraria o deslocamento de 8 bits, entao a mascara cheia
  // e montada por subtracao em 16 bits.
  return static_cast<std::uint8_t>((1U << count) - 1U);
}

std::size_t bar_test_steps(std::size_t count) {
  if (count == 0) return 0;
  if (count > kMaxPinList) count = kMaxPinList;
  return count + 2;  // um por vez, depois todos, depois apagado
}

std::uint8_t bar_test_mask(std::size_t index, std::size_t count) {
  if (count == 0) return 0;
  if (count > kMaxPinList) count = kMaxPinList;

  if (index < count) {
    return static_cast<std::uint8_t>(1U << index);  // um por vez
  }
  if (index == count) {
    return static_cast<std::uint8_t>((1U << count) - 1U);  // todos
  }
  return 0;  // apagado, e tudo depois disso
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
