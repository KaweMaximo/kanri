# `tools/` — ferramentas de desenvolvimento

Ferramentas que **rodam no seu PC** para apoiar o desenvolvimento. Nada aqui
vai para o ESP32.

| Ferramenta | O que faz |
|---|---|
| [`kanri-console/`](kanri-console/) | Painel web local: estado do ESP32, log serial ao vivo, gravar firmware, reiniciar, rodar testes |

## Por que não é um Artifact / site publicado

O painel precisa **abrir a porta serial** (`/dev/ttyUSB0`) e **executar
comandos** (`pio run -t upload`). Uma página publicada roda no navegador, em
sandbox, sem acesso a nenhuma das duas coisas.

Por isso é um servidor Python local, que serve a interface para o seu próprio
navegador — e que escuta **apenas em `127.0.0.1`**. Ver a nota de segurança no
[README do console](kanri-console/README.md#segurança).
