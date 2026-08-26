#pragma once
// ============================================================================
//  kanri_obd/i_transport.h — Porta de transporte (o "cano" de bytes)
// ============================================================================
//  Esta interface esconde COMO os bytes vao e voltam. A logica de OBD nao
//  sabe (e nao deve saber) se por tras existe Bluetooth Classic, BLE, USB ou
//  um arquivo de teste.
//
//  Implementacoes:
//    - src/hal/bt_serial_transport.h   -> Bluetooth SPP real no ESP32
//    - test/helpers/fake_transport.h   -> script pre-gravado, usado nos testes
//
//  E graças a isso que da para testar "o que acontece se o adaptador mandar
//  lixo no meio da resposta" sem precisar de carro, adaptador nem viagem.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace kanri::obd {

class ITransport {
 public:
  virtual ~ITransport() = default;

  /// Tenta abrir o canal. Nao bloqueia indefinidamente.
  /// @return true se o canal ficou utilizavel.
  virtual bool connect() = 0;

  /// Fecha o canal. Deve ser seguro chamar mesmo se ja estiver fechado.
  virtual void disconnect() = 0;

  virtual bool is_connected() const = 0;

  /// Envia bytes. @return quantos foram realmente aceitos (pode ser < len).
  virtual std::size_t write(const std::uint8_t* data, std::size_t len) = 0;

  /// Le o que ja chegou, sem esperar. @return quantos bytes foram copiados.
  /// NUNCA escreve mais que `max_len` — o chamador manda o tamanho do buffer.
  virtual std::size_t read(std::uint8_t* out, std::size_t max_len) = 0;

  /// Quantos bytes estao disponiveis para leitura imediata.
  virtual std::size_t available() const = 0;
};

}  // namespace kanri::obd
