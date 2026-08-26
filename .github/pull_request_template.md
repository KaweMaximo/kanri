## O que muda

<!-- Uma ou duas frases. O quê e por quê. -->

## Como testar

<!-- Passos para verificar. Se precisa de hardware, diga qual. -->

```bash
pio test -e native
```

## Checklist

- [ ] `pio test -e native` está verde
- [ ] `pio run -e esp32dev` compila
- [ ] Commits seguem Conventional Commits
- [ ] Nada em `lib/` inclui `Arduino.h`

## Checklist de testes

> Política: [docs/TESTING.md](../docs/TESTING.md). O CI exige **100% de
> cobertura de linhas** em `lib/`.

- [ ] Todo código novo em `lib/` tem teste que o executa
- [ ] Existe pelo menos uma **invariante**, não só exemplos
- [ ] Cobertura segue em 100% (`pio test -e native_coverage` + `gcovr`)
- [ ] Nenhuma exclusão `GCOVR_EXCL` nova sem justificativa escrita
- [ ] Nenhum teste depende de tempo real, rede ou hardware
- [ ] Nenhuma semente aleatória sem valor fixo

<!--
Se esta mudança realmente não pede teste (só comentário, só renomear, só
#include), adicione a label `sem-teste-necessario`. A dispensa fica visível
na revisão — de propósito.
-->

## Checklist de segurança

> Ver [docs/SAFETY.md](../docs/SAFETY.md). Marque **todos** os itens.

- [ ] Nenhum modo OBD2 novo além de `0x01` / `0x09`
- [ ] Nenhum comando AT novo sem justificativa escrita em `safety.h`
- [ ] Toda resposta do adaptador passa pelo parser antes de ser usada
- [ ] Nenhum caminho novo que reinicie o firmware por falha de lógica
- [ ] Nenhuma alocação dinâmica no caminho crítico (`String`, `new`, `malloc`)
- [ ] Nenhum `while` sem prazo e sem alimentar o watchdog
- [ ] Todo valor exibido tem `valid == true` verificado
