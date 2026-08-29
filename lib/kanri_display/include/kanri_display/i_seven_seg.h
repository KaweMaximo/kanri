#pragma once
// ============================================================================
//  kanri_display/i_seven_seg.h — A porta do mostrador de 7 segmentos
// ============================================================================
//  Irma de IDisplay, nao substituta. Sao duas saidas com naturezas
//  diferentes e as duas continuam existindo:
//
//    IDisplay   -> quatro linhas de texto  -> monitor serial e painel web
//    ISevenSeg  -> tres digitos            -> o painel dentro do carro
//
//  O firmware nao conhece MAX7219. Conhece esta porta. Trocar o chip, ou
//  rodar sem mostrador nenhum, nao mexe em uma linha da logica.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "kanri_display/seven_seg.h"

namespace kanri::display {

class ISevenSeg {
 public:
  virtual ~ISevenSeg() = default;

  /// Prepara o mostrador. @return false se ele nao respondeu.
  virtual bool begin() = 0;

  /// Desenha o quadro. Deve ser burro: mostra o que recebeu, sem julgar.
  virtual void render(const SegFrame& frame) = 0;

  /// Desenha bytes de segmento CRUS, um por digito, na ordem de leitura.
  ///
  /// Existe para o autoteste: "acender so o segmento do meio" nao e
  /// expressavel como texto, e e exatamente o que localiza um fio solto.
  virtual void render_raw(const std::uint8_t* digits, std::size_t count) = 0;

  /// Brilho em 0..100 %.
  virtual void set_brightness(std::uint8_t percent) = 0;

  /// Apaga sem desligar o chip.
  virtual void clear() = 0;
};

}  // namespace kanri::display
