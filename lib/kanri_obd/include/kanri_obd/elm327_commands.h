#pragma once
// ============================================================================
//  kanri_obd/elm327_commands.h — A conversa de abertura com o ELM327
// ============================================================================
//  Antes de perguntar qualquer coisa ao carro, e preciso configurar o
//  adaptador. Sao seis comandos, e cada um existe por um motivo:
//
//    ATZ    reseta o adaptador. Comeca de um estado conhecido, em vez de
//           herdar a configuracao de quem usou o aparelho antes.
//    ATE0   desliga o ECO. Sem isso, o ELM327 repete cada comando antes de
//           responder, e teriamos de filtrar o eco em toda leitura.
//    ATL0   desliga os linefeeds extras. Menos ruido para o parser.
//    ATS0   desliga os ESPACOS. "410C1AF8" em vez de "41 0C 1A F8": menos
//           bytes no ar, leitura mais rapida.
//    ATH0   desliga os HEADERS. Nao precisamos saber qual modulo respondeu, e
//           o header so aumentaria a resposta.
//    ATSP0  protocolo AUTOMATICO. O ELM327 descobre sozinho que o Lancer usa
//           ISO 15765-4 (CAN 11 bits, 500 kbit/s). Chumbar o protocolo daria
//           uma conexao mais rapida, mas quebraria em qualquer outro carro.
//
//  A sequencia esta aqui como DADO, e nao escondida dentro de uma funcao, por
//  dois motivos: da para ler o que o firmware faz sem seguir codigo, e da
//  para um teste percorrer a lista e conferir que todo comando passa pela
//  allowlist de safety.h.
// ============================================================================

#include <cstddef>

namespace kanri::obd {

/// Um passo da inicializacao.
struct ElmCommand {
  const char* command;   ///< O comando, sem o CR final.
  const char* expected;  ///< Trecho que a resposta DEVE conter, ou "" se
                         ///< qualquer resposta serve.
  bool required;         ///< false = se falhar, seguimos assim mesmo.
};

/// A sequencia, na ordem em que deve ser enviada.
inline constexpr ElmCommand kInitSequence[] = {
    // ATZ reinicia o chip e demora mais que os outros. A resposta traz a
    // versao ("ELM327 v1.5"), mas clones mentem sobre ela — entao nao
    // exigimos conteudo, so que responda alguma coisa.
    {"ATZ", "", true},
    {"ATE0", "OK", true},
    {"ATL0", "OK", true},
    {"ATS0", "OK", true},
    {"ATH0", "OK", true},
    {"ATSP0", "OK", true},
};

inline constexpr std::size_t kInitSequenceLength =
    sizeof(kInitSequence) / sizeof(kInitSequence[0]);

/// O ELM327 termina toda resposta com este caractere. E o sinal de "pode
/// mandar o proximo comando" — ler ate ele e mais confiavel do que esperar
/// um tempo fixo.
constexpr char kPromptChar = '>';

/// Terminador que enviamos ao final de cada comando.
constexpr char kCommandTerminator = '\r';

}  // namespace kanri::obd
