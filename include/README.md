# `include/` — headers públicos do firmware

Esta pasta é uma convenção do PlatformIO: tudo aqui entra no *include path*
do binário do firmware (o que vive em `src/`).

**Hoje ela está praticamente vazia — e isso é de propósito.**

No Kanri, todo header que a lógica precisa mora dentro do módulo dono dele,
em `lib/<modulo>/include/<modulo>/`. Assim o mesmo header é visto tanto pelo
firmware quanto pelos testes que rodam no PC.

A versão do firmware, por exemplo, fica em
`lib/kanri_core/include/kanri_core/version.h`, não aqui — porque a lógica
compartilhada também precisa dela.

Use esta pasta apenas para headers que sejam **exclusivos do binário** e que
nenhum módulo de `lib/` deva enxergar.
