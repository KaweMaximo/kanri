#include "kanri_obd/obd_pid.h"

namespace kanri::obd {

const PidDescriptor* find_pid(std::uint8_t mode, std::uint8_t pid) {
  // Busca linear. A tabela tem ~20 entradas: linear e mais rapido que
  // qualquer estrutura sofisticada nesse tamanho, e nao gasta RAM.
  for (std::size_t i = 0; i < kSupportedPidCount; ++i) {
    if (kSupportedPids[i].mode == mode && kSupportedPids[i].pid == pid) {
      return &kSupportedPids[i];
    }
  }
  return nullptr;
}

bool has_expected_length(std::uint8_t mode, std::uint8_t pid,
                         std::uint8_t actual_length) {
  const PidDescriptor* descriptor = find_pid(mode, pid);
  if (descriptor == nullptr) return false;  // fora do catalogo: rejeita
  return descriptor->expected_bytes == actual_length;
}

}  // namespace kanri::obd
