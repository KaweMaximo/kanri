#include "kanri_obd/pid_support.h"

namespace kanri::obd {
namespace {

/// Indice do bloco (0, 1 ou 2) para uma base valida; 0xFF se invalida.
std::uint8_t indice_do_bloco(std::uint8_t base_pid) {
  switch (base_pid) {
    case kSupportPid0:  return 0;
    case kSupportPid20: return 1;
    case kSupportPid40: return 2;
    default:            return 0xFF;
  }
}

}  // namespace

void PidSupport::reset() {
  for (std::size_t i = 0; i < sizeof(bits_); ++i) bits_[i] = 0;
  blocos_ = 0;
}

bool PidSupport::apply_block(std::uint8_t base_pid, const std::uint8_t* data,
                             std::uint8_t len) {
  if (data == nullptr || len != kSupportPayloadBytes) return false;
  const std::uint8_t bloco = indice_do_bloco(base_pid);
  if (bloco == 0xFF) return false;

  // O bit mais significativo do primeiro byte corresponde ao PID
  // (base + 1); o menos significativo do ultimo, ao PID (base + 32).
  for (std::uint8_t byte = 0; byte < kSupportPayloadBytes; ++byte) {
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
      const bool ligado = (data[byte] & (0x80U >> bit)) != 0;
      if (!ligado) continue;

      const std::uint16_t pid =
          static_cast<std::uint16_t>(base_pid) + 1U + (byte * 8U) + bit;
      // O bloco 0x40 termina no PID 0x60; nada aqui passa de 255, mas a
      // checagem deixa isso explicito em vez de depender da aritmetica.
      if (pid > 0xFF) continue;
      bits_[pid >> 3] |= static_cast<std::uint8_t>(1U << (pid & 7U));
    }
  }

  blocos_ |= static_cast<std::uint8_t>(1U << bloco);
  return true;
}

bool PidSupport::supports(std::uint8_t pid) const {
  return (bits_[pid >> 3] & (1U << (pid & 7U))) != 0;
}

bool PidSupport::has_next_block(std::uint8_t base_pid) const {
  const std::uint8_t proximo = next_support_pid(base_pid);
  if (proximo == 0) return false;
  // O bit do proximo PID de mapa e justamente o "tem mais depois disto".
  return supports(proximo);
}

std::uint16_t PidSupport::count() const {
  std::uint16_t total = 0;
  for (std::size_t i = 0; i < sizeof(bits_); ++i) {
    std::uint8_t b = bits_[i];
    while (b != 0) {
      total += (b & 1U);
      b = static_cast<std::uint8_t>(b >> 1);
    }
  }
  return total;
}

std::uint8_t next_support_pid(std::uint8_t base_pid) {
  switch (base_pid) {
    case kSupportPid0:  return kSupportPid20;
    case kSupportPid20: return kSupportPid40;
    default:            return 0;
  }
}

}  // namespace kanri::obd
