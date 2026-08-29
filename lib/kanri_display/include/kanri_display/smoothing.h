#pragma once
// ============================================================================
//  kanri_display/smoothing.h — Deixar o painel parecer um instrumento
// ============================================================================
//  Dois problemas diferentes, os dois com a mesma cara de "robotico":
//
//  O VALOR PULA DE SEGUNDO EM SEGUNDO
//  ----------------------------------
//  O rodizio le cinco PIDs, entao cada medida chega a cada ~1 s. Escrevendo
//  direto no mostrador, o numero fica parado um segundo e SALTA. Nao e ruido:
//  e a taxa de atualizacao aparecendo na cara do motorista.
//
//  A correcao nao e ler mais rapido — o barramento OBD tem o ritmo que tem. E
//  ANDAR ate o valor novo, quadro a quadro, no ritmo da TELA (dezenas de Hz).
//  E o que um ponteiro de instrumento faz por inercia mecanica.
//
//  Mas suavizar tudo deixaria o painel LENTO, que e pior que pular: pisar no
//  acelerador e ver a rotacao chegar um segundo depois nao serve. Dai o
//  SALTO: mudanca grande vai direto, mudanca pequena desliza.
//
//  O BRILHO MUDA DE GOLPE
//  ----------------------
//  Girar o potenciometro pula degraus inteiros de intensidade. Caminhar um
//  passo por quadro transforma o degrau em transicao.
//
//  Tudo aqui e funcao pura. Da para ver o painel "andando" no PC.
// ============================================================================

#include <cstdint>

namespace kanri::display {

/// Quanto do caminho ate o valor novo se anda por quadro, em porcentagem.
///
/// 25 % por quadro a 30 Hz chega perto do valor em ~150 ms: rapido o
/// suficiente para nao parecer atrasado, lento o suficiente para o ultimo
/// digito parar de tremer.
constexpr std::uint8_t kSmoothStepPercent = 25;

/// Suaviza um valor que chega em saltos.
class ValueSmoother {
 public:
  /// @param span  a faixa fisica da grandeza (max - min). E dela que sai o
  ///              limiar de salto, porque 100 significa coisas diferentes em
  ///              rpm e em graus: 10% de 8000 rpm sao 800, 10% de 255 graus
  ///              sao 25. Um limiar absoluto serviria a uma medida e
  ///              estragaria as outras.
  explicit ValueSmoother(float span = 100.0F) : span_(span > 0 ? span : 100.0F) {}

  /// Anda um passo em direcao a `alvo` e devolve onde estamos agora.
  float update(float alvo);

  /// Esquece o historico. Chamar quando a leitura fica invalida: sem isso, ao
  /// voltar o valor deslizaria a partir do numero ANTIGO, mostrando por
  /// alguns quadros uma medida que o carro nunca teve.
  void reset();

  bool started() const { return iniciado_; }
  float value() const { return atual_; }

 private:
  float span_;
  float atual_ = 0.0F;
  bool iniciado_ = false;
};

/// Caminha um passo de `atual` para `alvo`, de um em um.
///
/// Existe separada para o brilho, onde o valor e inteiro e pequeno (0..15 no
/// MAX7219) e uma media daria degraus fracionarios que o chip nao tem.
std::uint8_t step_toward(std::uint8_t atual, std::uint8_t alvo);

}  // namespace kanri::display
