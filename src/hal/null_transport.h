#pragma once
// ============================================================================
//  NullTransport — o transporte da v0.1
// ============================================================================
//  Nao existe Bluetooth no Kanri ainda (chega na v0.2). Em vez de deixar um
//  ponteiro nulo por ai, usamos o padrao "Null Object": um objeto que cumpre
//  a interface inteira e nao faz nada.
//
//  Vantagem pratica: o firmware da v0.1 ja EXERCITA o caminho de falha de
//  verdade. Ele tenta conectar, falha, degrada, mostra o erro na tela e
//  retenta com backoff — exatamente o que docs/SAFETY.md exige. Ou seja, o
//  comportamento fail-safe esta testado no hardware desde o primeiro dia,
//  antes de existir uma unica linha de Bluetooth.
// ============================================================================

#include "kanri_obd/i_transport.h"

namespace kanri::hal {

class NullTransport final : public obd::ITransport {
 public:
  bool connect() override { return false; }
  void disconnect() override {}
  bool is_connected() const override { return false; }

  std::size_t write(const std::uint8_t* data, std::size_t len) override {
    (void)data;
    (void)len;
    return 0;  // NADA vai para o barramento. Seguro por construcao.
  }

  std::size_t read(std::uint8_t* out, std::size_t max_len) override {
    (void)out;
    (void)max_len;
    return 0;
  }

  std::size_t available() const override { return 0; }
};

}  // namespace kanri::hal
