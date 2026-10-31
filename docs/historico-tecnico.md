# Notas técnicas e histórico do desenvolvimento

Estas são as minhas anotações enquanto fazia o port, versão por versão, do jeito que foram escritas na época (alguns caminhos e nomes de arquivo são de antes de o projeto virar KI-X360). Para compilar, siga o [LEIAME](../LEIAME.md) (em inglês: [README](../README.md)).


Porta do UltraCombo (Killer Instinct arcade) para Xbox 360, gerando um `.xex` nativo.

## Velocidade: código traduzido

O interpretador MIPS sozinho é lento demais para o processador do 360. A velocidade
cheia vem do código do jogo traduzido para C (MIPS→C e ADSP→C) e compilado dentro do
`.xex`. Esse código é gerado **a partir dos seus arquivos**, no seu PC, e um `.xex`
com ele contém código do jogo: **nunca distribua esse `.xex` nem a pasta `rec`.**

## Compilar (passo a passo)

Requer o Xbox 360 SDK (`%XEDK%`) e o Visual Studio 2010.
Coloque os arquivos do jogo numa pasta (ex.: `port\testrun`): `kinst.zip` e `kinst\kinst.chd`.

1. Harness de PC (núcleo do emulador, sem tela):

   ```
   powershell -ExecutionPolicy Bypass -File x360\pc\build_pc.ps1
   ```

2. Rodar o jogo 900 frames no PC e tirar o snapshot da RAM (numa pasta de trabalho):

   ```
   harness.exe C:\caminho\testrun 900 --dump-at 900
   ```

3. Traduzir o código do snapshot para C (pasta `rec`):

   ```
   harness.exe C:\caminho\testrun 0 --recomp ram0.bin ram2.bin C:\caminho\rec
   ```

4. (Opcional) Conferir no PC: `build_pc.ps1 -Rec C:\caminho\rec` e rodar
   `harness.exe C:\caminho\testrun 3000 --ppm-every 1500`.

5. Compilar o `.xex` com o código traduzido:

   ```
   powershell -ExecutionPolicy Bypass -File x360\build360.ps1 -Rec C:\caminho\rec
   ```

Saída: `x360\build\ultracombo.xex`. Sem `-Rec`, sai um `.xex` só com interpretador.

## Arquivos no console

Na mesma pasta do `.xex` (HDD ou USB), no layout do MAME:

```
default.xex
kinst.zip            ROMs de boot e de som (o set do MAME, como está)
kinst\kinst.chd      disco do MAME  (ou kinst.raw ao lado do .xex)
```

Na inicialização o disco inteiro é lido para a memória (~90 MB, alguns segundos com
a tela "LOADING..."): ler setor por setor do USB/HDD durante o boot do jogo é lento.
As ROMs são extraídas uma vez para `cache\roms\kinst\`. Configurações e recordes
ficam em `saves\`, e o log em `ultracombo.log`.

## Controles (controle 1 e 2)

| Botão | Arcade |
|---|---|
| D-pad / analógico esquerdo | direções |
| X, Y, RB | soco fraco, médio, forte |
| A, B, RT | chute fraco, médio, forte |
| Start | start |
| Back | ficha |
| Back + LB + RB | modo de teste do operador |
| Clique do analógico esquerdo | mostra a velocidade |
| Back + Start (2 s) | sair |

Free play vem ligado; sangue alto; finishing moves ligados.

## Vídeo

720p (3x inteiro, 4:3) ou 480 linhas (2x); o console escala para 1080. Um frame que
perde o vblank por pouco é mostrado na hora; se a emulação passar de um quadro de
tela, frames são pulados na exibição para o jogo manter a velocidade.

## Versões e base oficial

- **Versão atual: v8** (31/10/2026). Igual à base v4 abaixo, mais: `KI2_RECOMP_FAST=1`
  (acessos à RAM com caminho rápido, constantes em locais, r0 constante) e `KI2_RECOMP_INLINE=1`
  (sub-rotinas folha pequenas copiadas para dentro de quem chama), lista protegida
  `rec8_plain_list.txt` (hardware, driver de disco e 6 funções que derrubam o compilador do
  XDK), som sem espera no fim do frame, DSP no thread 5, gamma, overlay com FPS:

  ```
  set KI2_RECOMP_PLAIN=C:\caminho\work\rec8_plain_list.txt
  set KI2_RECOMP_FAST=1
  set KI2_RECOMP_INLINE=1
  harness.exe C:\caminho\testrun 0 --recomp ram0.bin ram2.bin C:\caminho\work\rec8
  powershell -ExecutionPolicy Bypass -File x360\build360.ps1 -Rec C:\caminho\work\rec8 -LogExc
  ```

  Se o compilador do XDK der erro interno (C1001) numa função, ponha o endereço dela na lista
  protegida e gere de novo.

- **Candidato V9** (em teste): v8 com o mesmo `rec8`, mais atalhos de E/S no núcleo para os
  carregamentos (`rec_load` manda leituras KSEG1 de E/S direto a `io_read`; `io_read32` testa
  o disco primeiro; `is_irq_poll_loop` guarda a última resposta "não"). Idêntico à v8 no PC.

- **Candidato V10** (em teste): V9 mais invalidação por função em `rec.c`. O intervalo de uma
  função traduzida inclui os dados que vêm logo depois do código dela (ex.: variáveis em
  `0x88019cbc`–`0x88019d4c`, dentro de `f_88019c3c`). Antes, qualquer gravação ali invalidava
  todo o código traduzido e o interpretador assumia por até 1M ciclos; no cenário do Cinder
  isso acontecia 2 vezes por frame. Agora `rec_note_write` marca só as funções que contêm o
  endereço, e `rec_lookup_slow` reconfere só essas.

- **Versão principal desde o teste no console: V10** (`releases\2026-10-31_v10_BOA_cenario-sem-engasgo`).

- **Candidato V11** (em teste): V10 mais duas coisas. (1) Código traduzido `rec9`: o recipe da v8
  mais `set KI2_RECOMP_FPU=1`, que traduz as operações de ponto flutuante mais usadas (helpers em
  `rec_gen.h` com as mesmas expressões de `cpu.c`; antes ~18 mil por frame iam para `rec_op` no
  cenário do Cinder). (2) `ide_unpack_begin`/`ide_unpack_step`: o CHD inteiro descompactado numa
  thread em segundo plano (hw thread 1), em vez de LZMA a cada bloco lido nos carregamentos.
  No PC, 174 checkpoints e o hash final idênticos à V10. Build: `build360.ps1 -Rec work\rec9 -LogExc`.

- **Base oficial (desde 31/10/2026): v4.** Frontend `main360.cpp`, DSP de som num núcleo
  próprio (padrão do build; `-NoDcsThread` desliga) e código traduzido `rec6`: otimizado
  (chamadas diretas + registradores em locais), **exceto** as funções que falam com o hardware
  e o driver de disco, traduzidas sem otimização (lista `port\work\rec6_plain_list.txt`):

  ```
  set KI2_RECOMP_PLAIN=C:\caminho\work\rec6_plain_list.txt
  harness.exe C:\caminho\testrun 0 --recomp ram0.bin ram2.bin C:\caminho\work\rec6
  powershell -ExecutionPolicy Bypass -File x360\build360.ps1 -Rec C:\caminho\work\rec6 -LogExc
  ```

  Regra aprendida: no 360, o código otimizado quebra o protocolo com o disco (o jogo nunca vê
  o disco pronto após gravar). Toda função que acessa o hardware (base `0xB0000000`) ou que
  fica no driver de disco (`0x8802D000`–`0x8802E400`) tem de ficar na lista sem otimização.

- Cada versão entregue fica em `x360\releases\<data>_<nome>\` (o `.xex`, o código-fonte e uma
  nota de como foi feita). O `dist\KI-X360\default.xex` só recebe versões comprovadas no console;
  as outras vão para `dist\KI-X360-TESTES`.
- O código traduzido otimizado (`rec3`/`rec4`: chamadas diretas e registradores em locais) é
  idêntico no PC, mas no 360 deixa o `$gp` do jogo zerado numa `syscall` (tela do nome, boot com
  configurações originais) e trava numa tempestade de exceções TLBL lendo `0xFFFFE000`. Não usar
  até entender o porquê.

## Diagnóstico: checkpoints

Só no build de diagnóstico: `build360.ps1 -Rec ... -Diag`. Ele liga o overlay desde o início,
mostra o log do disco na tela, usa os setores de configuração originais do disco e compara
checkpoints com a referência do PC. O build normal não tem nada disso.

O `.xex` e o harness calculam, em frames fixos, hashes da RAM0, da RAM2, da CPU e do DSP de
som (`checkpoint N: ram0 ram2 cpu dsp pc=...`). A RAM guarda os bytes em little-endian nos dois
hosts e os registradores entram como valores, então a mesma emulação dá os mesmos hashes no PC
e no 360. A referência do PC fica embutida no `.xex` (`x360\reference.h`, só hashes, sem dados
do jogo), e a terceira linha do overlay mostra o primeiro frame diferente e quais partes
diferem. Depois de gerar o código traduzido de novo, refaça a referência:

```
powershell -ExecutionPolicy Bypass -File x360\pc\make_reference.ps1 -Root C:\caminho\testrun
```

A linha `disk id` do log confere a descompressão do CHD.

## Medições no PC (harness, código traduzido)

~2 ms por frame (o real é 16,96 ms); interpretado 4–6% das instruções; DSP de som
~0,1 ms por frame. No 360 ainda não medido.
