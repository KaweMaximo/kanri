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
- [ ] Lógica nova em `lib/` tem teste
- [ ] Nada em `lib/` inclui `Arduino.h`

## Checklist de segurança

> Ver [docs/SAFETY.md](../docs/SAFETY.md). Marque **todos** os itens.

- [ ] Nenhum modo OBD2 novo além de `0x01` / `0x09`
- [ ] Nenhum comando AT novo sem justificativa escrita em `safety.h`
- [ ] Toda resposta do adaptador passa pelo parser antes de ser usada
- [ ] Nenhum caminho novo que reinicie o firmware por falha de lógica
- [ ] Nenhuma alocação dinâmica no caminho crítico (`String`, `new`, `malloc`)
- [ ] Nenhum `while` sem prazo e sem alimentar o watchdog
- [ ] Todo valor exibido tem `valid == true` verificado
