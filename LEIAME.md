# KI-X360

*[English](README.md) | Português*

O Killer Instinct de arcade (1994) rodando **nativo** no Xbox 360 desbloqueado: velocidade cheia, som do arcade, dois controles, 720p, filtros de imagem e menu de configurações em português e inglês.

Não é um emulador genérico rodando o jogo instrução por instrução. Ele parte do [UltraCombo](https://github.com/thefixinhixon/UltraCombo), um port para PC que traduz o código MIPS do próprio jogo para C. Eu adaptei o UltraCombo para o Xbox 360. O código do jogo vira C no seu PC, a partir dos **seus** arquivos, e é compilado dentro do `.xex` junto com o resto. O hardware em volta (vídeo, disco IDE, placa de som DCS, controles) continua emulado.

> **Aqui não tem jogo nenhum.** Nem ROM, nem disco (CHD), nem o código traduzido, nem `.xex` pronto, nem o SDK da Microsoft. Você precisa ter os seus arquivos do jogo e o SDK instalado para compilar. Não me peça ROM.

---

## O que funciona

- Velocidade cheia nas lutas e nos carregamentos (testado no console de verdade, jogando partidas inteiras)
- Som da placa DCS original, com o processador de som numa thread própria
- Controle 1 e controle 2
- Saída em 720p (escala 3x inteira, 4:3) ou 480 linhas; o console escala para 1080
- Menu de configurações: proporção (4:3, 4:3 com laterais desfocadas, 16:9 com zoom), filtros (nítido, suave, CRT com scanlines), brilho, gama, saturação e idioma
- Recordes e configurações do arcade são salvos de verdade (tela de nomes, modo de teste do operador)
- Mostrador de FPS / velocidade

## O que você precisa

### No console
- Xbox 360 desbloqueado (RGH/JTAG) com Aurora, FreeStyle Dash ou XeXMenu
- Pendrive ou HD com uns 120 MB livres

### No PC (Windows)
Instale nesta ordem:

1. **Visual Studio 2010** com C++. O harness de PC usa o compilador x86 dele, e o SDK do 360 se integra a ele, por isso ele vem primeiro.
2. **Xbox 360 SDK (XDK)**. O instalador cria a variável de ambiente `XEDK`, que o script de compilação usa. Eu usei um XDK cujo compilador é o `cl` 16.00.11886.
3. **Git for Windows** (traz o Git Bash), para baixar o projeto.

Python não é necessário. Só o `ppm2png.py`, opcional, usa Python, para ver as imagens que o harness salva.

### Os arquivos do jogo
O set do MAME **kinst**, do jeito que ele é:

```
kinst.zip          ROMs de boot e de som
kinst\kinst.chd    o disco rígido do arcade
```

Testei com o set atual do MAME (versão 1.5d da ROM de boot) e o CHD padrão. Na inicialização o programa confere os CRCs e o SHA1 do CHD.

## Compilando

Tudo pelo PowerShell, **sempre a partir da raiz do projeto** (a pasta `KI-X360`, onde está este arquivo): os caminhos dos comandos, como `x360\pc\build_pc.ps1`, partem dela. Se aparecer "O argumento ... para o parâmetro -File não existe", é porque o PowerShell está em outra pasta; volte para a raiz com `cd`. Nos exemplos os arquivos do jogo estão em `C:\ki\jogo`; troque pelo seu caminho.

```
C:\ki\jogo\kinst.zip
C:\ki\jogo\kinst\kinst.chd
```

**1. Baixe o projeto** (Git Bash):

```bash
git clone https://github.com/EduDicaseGameplay/KI-X360.git
cd KI-X360
```

**2. Compile o harness de PC.** É o núcleo do emulador sem tela, rodando no Windows. Ele serve para gerar o código traduzido e para testar.

```
powershell -ExecutionPolicy Bypass -File x360\pc\build_pc.ps1
```

**3. Gere o código traduzido a partir dos seus arquivos.** O script roda o jogo 900 frames no PC, tira uma foto da RAM e traduz o código MIPS que está nela para C. Leva um ou dois minutos.

```
powershell -ExecutionPolicy Bypass -File x360\recomp\make_rec.ps1 -Root C:\ki\jogo -Out C:\ki\rec
```

**4. Compile o `.xex` com esse código:**

```
powershell -ExecutionPolicy Bypass -File x360\build360.ps1 -Rec C:\ki\rec -LogExc
```

O resultado sai em `x360\build\kix360.xex`. O `-LogExc` só anota no log as exceções do processador do jogo, o que ajuda se algo der errado.

Sem `-Rec` também compila, mas aí o jogo roda só no interpretador, lento demais no 360. Serve apenas para testar se o SDK está instalado direito.

**(Opcional) Testar no PC antes**, com o mesmo código traduzido:

```
powershell -ExecutionPolicy Bypass -File x360\pc\build_pc.ps1 -Rec C:\ki\rec
x360\pc\build\harness.exe C:\ki\jogo 3000 --ppm-every 1500
```

O harness mostra os ms por frame (o arcade usa 16,96 ms) e salva imagens dos frames 1500 e 3000.

> **Atenção:** a pasta do código traduzido e o `.xex` compilado com `-Rec` contêm o código do jogo. Eles são só para você. Não suba para o GitHub nem compartilhe. O `.gitignore` já deixa tudo isso de fora.

## Instalando no console

Monte uma pasta no pendrive ou no HD assim:

```
KI-X360\
  default.xex        (o x360\build\kix360.xex renomeado)
  kinst.zip
  kinst\kinst.chd
```

Abra o `default.xex` pelo Aurora, FreeStyle ou XeXMenu.

- Na primeira vez, ele pergunta o idioma (português ou inglês).
- Toda vez que abre, aparece "CARREGANDO..." por alguns segundos, enquanto o disco inteiro é lido para a memória. Ler do pendrive aos pedaços durante o jogo é muito lento.
- Logo depois, o disco é descompactado em segundo plano. No meu console isso levou uns 7 segundos, enquanto as telas de abertura rodavam. A partir daí os carregamentos ficam lisos.

O programa cria, ao lado do `.xex`:

| Arquivo | O que é |
|---|---|
| `cache\roms\kinst\` | as ROMs extraídas do zip (só na primeira vez) |
| `saves\kix360.ini` | suas configurações do menu |
| `saves\ki-l15d.settings` | configurações do arcade e recordes |
| `kix360.log` | log da última sessão: velocidade, carregamentos, erros |

## Controles

| Botão | No arcade |
|---|---|
| D-pad / analógico esquerdo | direções |
| X, Y, RB | soco fraco, médio, forte |
| A, B, RT | chute fraco, médio, forte |
| Start | start |
| Back | ficha |
| Back + LB + RB | modo de teste do operador (dificuldade, etc.) |
| Clique do analógico **direito** | menu de configurações (pausa o jogo) |
| Clique do analógico esquerdo | mostra/esconde a velocidade |
| Back + Start (segure 2 s) | sair |

Free play vem ligado. Os textos do próprio jogo estão na ROM e continuam em inglês.

## Problemas comuns

- **`XEDK is not set`**: o SDK do 360 não está instalado, ou o PowerShell foi aberto antes da instalação. Feche e abra de novo.
- **Erro procurando `vcvarsall.bat`**: falta o Visual Studio 2010 (o harness usa a variável `VS100COMNTOOLS` dele).
- **Erro interno do compilador (C1001) numa função `f_XXXXXXXX`**: o compilador do XDK às vezes não aguenta uma função otimizada. Ponha o endereço dela (o `XXXXXXXX`) numa linha nova de `x360\recomp\lista-protegida.txt` e gere o código de novo (passo 3).
- **`the harness did not run the game`**: confira se o `kinst.zip` e o `kinst\kinst.chd` estão na pasta passada em `-Root`.
- **O jogo travou ou ficou lento em algum ponto**: o `kix360.log` mostra a velocidade a cada poucos segundos e o que o jogo estava fazendo.

## Como funciona por dentro (resumo)

- **CPU** (MIPS R4600): o código do jogo é traduzido para C antes, no PC (`src/recomp.c`). Em tempo de execução, `src/rec.c` confere se o código na RAM ainda é o que foi traduzido e chama a função C correspondente. O que não foi traduzido cai no interpretador (`src/cpu.c`).
- **Funções protegidas**: as funções que conversam com o hardware e o driver de disco do jogo são traduzidas sem otimização (`x360/recomp/lista-protegida.txt`). Otimizadas, elas quebravam o protocolo com o disco no 360.
- **Som**: o DSP ADSP-2105 da placa DCS também é traduzido para C e roda numa thread própria, num núcleo separado.
- **Disco**: o CHD é lido inteiro para a memória e descompactado em segundo plano (`src/ide.c`).
- **Frontend do 360** (`x360/main360.cpp`): Direct3D 9, XAudio2 e XInput. A emulação roda numa thread no núcleo 1, e a imagem e o som ficam na thread principal.
- **Harness de PC** (`x360/pc/harness.cpp`): roda o mesmo núcleo sem tela. Calcula hashes da RAM e da CPU em frames fixos, para comparar versões, e tem um perfilador por amostragem (`--sample`).
- O PowerPC do 360 é big-endian e o jogo é little-endian: a memória do jogo fica em little-endian e cada acesso inverte os bytes.

O histórico completo do desenvolvimento, com os problemas que apareceram no console e como foram resolvidos, está em [docs/historico-tecnico.md](docs/historico-tecnico.md).

## Créditos e licença

- **UltraCombo**, de thefixinhixon: o emulador e o tradutor de código em que este port se baseia. Licença MIT.
- **libchdr** (BSD), com **LZMA SDK** (domínio público), **zstd** (BSD) e **miniz** (MIT), para ler o CHD. **stb_image** (domínio público / MIT).
- **MAME**: os arquivos que o programa lê são os do set `kinst` do MAME (`kinst.zip` e `kinst.chd`).

O código deste repositório está sob a licença MIT (veja [LICENSE](LICENSE)). As bibliotecas em `src/third_party` mantêm as licenças delas.

Killer Instinct é marca registrada dos seus donos. Este projeto não tem nenhuma ligação com eles, não contém nada do jogo e só funciona com os arquivos que você já tem.
