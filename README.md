# ⚡ BRBtcHuntAMD — GPU Satoshi Puzzle Solver

<div align="center">

![ROCm](https://img.shields.io/badge/ROCm-HIP-red?logo=amd)
![Language](https://img.shields.io/badge/C%2B%2B-17-blue)
![GPU](https://img.shields.io/badge/GPU-RX%206600-green)
![License](https://img.shields.io/badge/license-MIT-yellow)
[![Donate](https://img.shields.io/badge/BTC-donate-orange?logo=bitcoin)](https://blockchair.com/bitcoin/address/bc1q7s4m9cwlq8xtx2nz74mquh6ax0jqwsmkkd56s3)

**A high‑performance Bitcoin puzzle solver for AMD GPUs (ROCm/HIP)**  
**Quebrador de puzzles Bitcoin de alta performance para GPUs AMD (ROCm/HIP)**

Brute‑force P2PKH addresses at **~1 billion keys/second** on a single RX 6600.

</div>

---

<div align="center">

### 🌐 Escolha o idioma — Choose language

[🇧🇷 **Português**](#português) &nbsp;·&nbsp; [🇬🇧 **English**](#english)

</div>

---

## 🚀 In a nutshell — Resumo

```bash
./BRBtcHuntAMD --range 200000000:3FFFFFFFF --address 1HBtApAFA9B2YZw3G2YKSMCtb3dVnjuNe2 \
                --grid 2048,32 --slices 64 --gpus 0
```

<br>

---

# 🇧🇷 Português

<div align="center">

[⬆ Voltar ao topo](#-brbcthuntamd--gpu-satoshi-puzzle-solver) &nbsp;·&nbsp; [🇬🇧 Ir para English](#english)

</div>

---

## 📸 Preview da execução

```
======== Pre-Fase: Info GPU (1 GPU) ===
  GPU 0 : AMD Radeon RX 6600  |  14 SMs  |  7.98 GB
=======================================================

======== Fase-1: Forca Bruta (1 GPU) =====
======== GPU 0 : AMD Radeon RX 6600 (compute 10.3) ========
SM                   : 14
Threads/Bloq         : 256
Blocos               : 448
Total threads        : 114688
Lote pontos          : 2048
Lotes/SM             : 32
Lotes/lanc.          : 64 (por thread)
Uso memoria          : 2.4% (192.0 MB / 7.98 GB)
-------------------------------------------------------
Tempo: 1.0 s | Veloc.: 1.12 Gkeys/s | Total: 1147142144 | Prog.: 0.20 % | Vaidade: 0
Tempo: 2.0 s | Veloc.: 1.03 Gkeys/s | Total: 2206203904 | Prog.: 0.40 % | Vaidade: 0
Tempo: 3.0 s | Veloc.: 1.07 Gkeys/s | Total: 3303014400 | Prog.: 0.60 % | Vaidade: 0
Tempo: 4.0 s | Veloc.: 1.02 Gkeys/s | Total: 4378853376 | Prog.: 0.79 % | Vaidade: 0

--- Resumo ---
Total chaves  : 4696473600
Tempo         : 4.00 s
Veloc. med.   : 1.03 Gkeys/s

======== CHAVE ENCONTRADA! =================================
Chave Privada : 00000000000000000000000000000000000000000000000000000022382FACD0
Chave Publica : 03C060E1E3771CBECCB38E119C2414702F3F5181A89652538851D2E3886BDD70C6
```

---

## 📋 Índice

- [Funcionalidades](#-funcionalidades)
- [Requisitos](#️-requisitos)
- [Instalação](#️-instalação)
- [Uso](#-uso)
  - [Parâmetros](#parâmetros)
  - [Exemplos](#exemplos)
- [Modo Vanity](#-modo-vanity)
- [Suporte a Idioma](#-suporte-a-idioma)
- [Performance](#-performance)
- [Doação](#-doação)
- [Licença](#-licença)

---

## ✨ Funcionalidades

- **Performance extrema** — ~1 Gkeys/s em uma única RX 6600, totalmente otimizado para RDNA 2.
- **Hash de prefixo duplo com paridade diferida** — Elimina a multiplicação modular cara para paridade de chave pública, substituindo por duas chamadas SHA‑256 + RIPEMD‑160 (~+59 % de velocidade).
- **Inversão em lote Fermat** — Mais rápida que GCD estendido ao processar 1024+ pontos de uma vez.
- **Modo `--random`** — Saltos aleatórios estilo loteria pelo espaço de busca.
- **Modo `--vanity`** — Salva toda chave cujo hash160 comece com os mesmos N caracteres hex do alvo.
- **`--lang pt|en`** — Interface completa em Português e Inglês, persistida em `lang.cfg`.
- **Multi‑GPU** — Detecção automática de GPUs, use `--gpus` para selecionar dispositivos.

---

## 🖥️ Requisitos

| Componente | Requisito |
|-----------|-----------|
| **GPU** | AMD com ROCm 6+ (RX 5000/6000/7000, Vega, série MI) |
| **SO**  | Linux (Ubuntu 22.04+, Fedora 38+, Arch, etc.) |
| **ROCm**| rocm-hip-sdk, rocm-llvm, hipcc |
| **RAM** | 8 GB+ de memória do sistema |
| **VRAM**| 4 GB+ (testado em 8 GB) |

> **Nota:** GPUs NVIDIA **não** são suportadas. Para CUDA use o [CUDACyclone](https://github.com/kanhavishva/CUDACyclone) original.

---

## 🛠️ Instalação

### 1. Instalar ROCm

```bash
# Ubuntu 22.04
sudo apt update
sudo apt install rocm-hip-sdk rocm-llvm rocm-dev

# Verificar
hipcc --version
rocminfo | grep gfx
```

### 2. Clonar e compilar

```bash
git clone https://github.com/jmr2704/BRBtcHuntAMD.git
cd BRBtcHuntAMD

# Detecção automática da arquitetura da GPU (recomendado)
GPU_ARCHS=$(rocminfo | grep -oP 'gfx\w+' | sort -u | tr '\n' ' ')
make GPU_ARCHS="$GPU_ARCHS" -j$(nproc)

# Ou especificar a GPU diretamente
make GPU_ARCHS="gfx1032" -j$(nproc)

# Compilação limpa
make clean && make -j$(nproc)
```

### 3. Teste rápido

```bash
# Encontrar a chave conhecida do puzzle (range = 1 chave)
./BRBtcHuntAMD --range 22382FACD0:22382FACD0 \
               --address 1HBtApAFA9B2YZw3G2YKSMCtb3dVnjuNe2 \
               --grid 2048,32 --slices 64 --gpus 0
```

---

## 📖 Uso

```
BRBtcHuntAMD — Quebrador de Puzzle Bitcoin (ROCm/HIP)

Uso: ./BRBtcHuntAMD --range <inicio_hex>:<fim_hex> --address <base58>
       [--grid A,B] [--slices N] [--gpus all|0|0,1] [--random]
       [--vanity N] [--lang pt|en]

Obrigatório:
  --range <inicio:fim>        Intervalo em hex (ex: 2000000000:3FFFFFFFFF)
  --address <base58>          Endereço P2PKH para buscar
  --target-hash160 <hex>      Alternativa ao --address (hash160 raw)

Opções:
  --grid <P,T>                Pontos por lote, threads por bloco (ex: 512,256)
  --slices <N>                Lotes por thread por execução do kernel
  --gpus <all|0|0,1>          Seleciona GPUs (padrão: all)
  --random                    Modo loteria: saltos aleatórios pelo intervalo
  --vanity <N>                Salva chaves cujo hash160 começa c/ N chars hex iguais ao alvo
  --lang pt|en                Idioma (padrão: en)
  -h, --help                  Mostra esta ajuda
```

### Parâmetros

| Parâmetro | Descrição |
|-----------|-----------|
| `--range <inicio:fim>` | **Obrigatório.** Intervalo hex para busca. Ex: `2000000000:3FFFFFFFFF` |
| `--address <base58>` | **Obrigatório** (ou `--target-hash160`). Endereço P2PKH tipo `1HBtAp...` |
| `--target-hash160 <hex>` | Alternativa ao `--address`. Hash160 raw de 20 bytes em hex. |
| `--grid <P,T>` | Ajuste. `P` = pontos por lote (par, ≤2048). `T` = threads por bloco (128‑512). Padrão: `2048,256` |
| `--slices <N>` | Quantos lotes cada thread executa por lançamento de kernel. Maior = menos overhead. Padrão: `64` |
| `--gpus <all\|0\|0,1>` | Seleciona GPUs. `all` usa todos dispositivos compatíveis. Padrão: `all` |
| `--random` | Modo loteria — cada GPU salta para posição aleatória a cada chunk. |
| `--vanity <N>` | Modo vaidade — salva toda chave verificada cujo hash160 comece com os mesmos N chars hex que o hash160 alvo. Resultados em `vanity_results.txt`. |
| `--lang pt\|en` | Troca o idioma da interface. Salvo em `lang.cfg`. |
| `-h, --help` | Mostra o texto de ajuda. |

---

## 🧪 Exemplos

```bash
# 1) Busca básica
./BRBtcHuntAMD --range 200000000:3FFFFFFFF --address 1HBtAp... --gpus 0

# 2) Com ajuste fino
./BRBtcHuntAMD --range 200000000:3FFFFFFFF --address 1HBtAp... \
               --grid 2048,32 --slices 64 --gpus 0

# 3) Modo aleatório / loteria
./BRBtcHuntAMD --random --range 80000000:FFFFFFFF --address 1HBtAp... \
               --grid 2048,32 --slices 64 --gpus 0

# 4) Multi‑GPU
./BRBtcHuntAMD --range 200000000:3FFFFFFFF --address 1HBtAp... \
               --gpus 0,1

# 5) Vanity (salvar chaves que combinam primeiros 4 chars hex do alvo)
./BRBtcHuntAMD --range 200000000:3FFFFFFFF --address 1HBtAp... \
               --grid 2048,32 --slices 64 --gpus 0 --vanity 4

# 6) Interface em Português
./BRBtcHuntAMD --lang pt --range 200000000:3FFFFFFFF --address 1HBtAp...

# 7) Validar chave conhecida (puzzle 66)
./BRBtcHuntAMD --range 22382FACD0:22382FACD0 \
               --address 1HBtApAFA9B2YZw3G2YKSMCtb3dVnjuNe2 \
               --grid 2048,32 --slices 64 --gpus 0
```

---

## 🎭 Modo Vanity

Com `--vanity <N>`, toda chave verificada cujo hash160 **comece com os mesmos N caracteres hex** que seu hash160 alvo é salva.

```
# Salvar chaves cujo hash160 combine os primeiros 4 chars hex do alvo
./BRBtcHuntAMD --range 80000000:FFFFFFFF --address 1HBtAp... --vanity 4
```

- Resultados são escritos em **`vanity_results.txt`** ao final da execução.
- O display de progresso mostra um contador **`Vaidade: N`** ao vivo.
- Sem flood no terminal — apenas o contador aparece durante a execução.

---

## 🌐 Suporte a Idioma

Troque entre **Português** e **Inglês**:

```bash
# Português (uma vez)
./BRBtcHuntAMD --range ... --address ... --lang pt

# A configuração é salva em lang.cfg. Execuções futuras lembram.
./BRBtcHuntAMD --range ... --address ...   # ← agora em PT

# Voltar para Inglês
./BRBtcHuntAMD --range ... --address ... --lang en
```

| Label | PT | EN |
|-------|----|----|
| Tempo / Velocidade | `Tempo`, `Veloc.` | `Time`, `Speed` |
| Total / Progresso | `Total`, `Prog.` | `Count`, `Progress` |
| Vaidade | `Vaidade` | `Vanity` |
| Saltos | `Saltos` | `Chunks` |
| CHAVE ENCONTRADA | `CHAVE ENCONTRADA!` | `FOUND MATCH!` |
| Resumo | `Total chaves`, `Veloc. med.` | `Total keys`, `Avg speed` |
| Labels de config | `SM`, `Blocos`,... | `SM`, `Blocks`,... |

---

## ⚡ Performance

Medido em uma única **AMD Radeon RX 6600** (RDNA 2, 14 CUs, 8 GB VRAM).

| Modo | Throughput |
|------|-----------|
| Sequencial, grid 2048×32, slices 64 | **~1.16 Gkeys/s** |
| Modo aleatório | **~1.05 Gkeys/s** |
| Com `--vanity 4` | **~950 Mkeys/s** |

**Principais otimizações:**
- Hash de prefixo duplo (paridade diferida) → +59 % sobre abordagem de paridade modular.
- Inversão em lote Fermat (árvore de produto) → lote rápido de 1024 pontos.
- Aritmética modular inline (`__forceinline__` em `_ModMult`, `_ModSqr`).
- Limites de lançamento ajustados manualmente (`__launch_bounds__(256, 2)`).

---

<br>

# 🇬🇧 English

<div align="center">

[⬆ Back to top](#-brbcthuntamd--gpu-satoshi-puzzle-solver) &nbsp;·&nbsp; [🇧🇷 Ir para Português](#português)

</div>

---

## 📸 Execution preview

```
======== PrePhase: GPU Information (1 GPU) ===
  GPU 0 : AMD Radeon RX 6600  |  14 SMs  |  7.98 GB
=======================================================

======== Phase-1: BruteForce (1 GPU) =====
======== GPU 0 : AMD Radeon RX 6600 (compute 10.3) ========
SM                   : 14
ThreadsPerBlock      : 256
Blocks               : 448
Total threads        : 114688
Points batch size    : 2048
Batches/SM           : 32
Batches/launch       : 64 (per thread)
Memory utilization   : 2.4% (192.0 MB / 7.98 GB)
-------------------------------------------------------
Time: 1.0 s | Speed: 1.12 Gkeys/s | Count: 1147142144 | Progress: 0.20 % | Vanity: 0
Time: 2.0 s | Speed: 1.03 Gkeys/s | Count: 2206203904 | Progress: 0.40 % | Vanity: 0
Time: 3.0 s | Speed: 1.07 Gkeys/s | Count: 3303014400 | Progress: 0.60 % | Vanity: 0
Time: 4.0 s | Speed: 1.02 Gkeys/s | Count: 4378853376 | Progress: 0.79 % | Vanity: 0

--- Summary ---
Total keys  : 4696473600
Time        : 4.00 s
Avg speed   : 1.03 Gkeys/s

======== FOUND MATCH! =================================
Private Key   : 00000000000000000000000000000000000000000000000000000022382FACD0
Public Key    : 03C060E1E3771CBECCB38E119C2414702F3F5181A89652538851D2E3886BDD70C6
```

---

## 📋 Table of Contents

- [Features](#-features-1)
- [Requirements](#️-requirements-1)
- [Installation](#️-installation-1)
- [Usage](#-usage-1)
  - [Parameters](#parameters-1)
  - [Examples](#examples-1)
- [Vanity Mode](#-vanity-mode-1)
- [Language Support](#-language-support-1)
- [Performance](#-performance-1)
- [Donate](#-donate)
- [License](#-license)

---

## ✨ Features

- **Extreme performance** — ~1 Gkeys/s on a single RX 6600, fully optimised for RDNA 2.
- **Deferred‑parity / dual‑prefix hash** — Eliminates expensive modular multiplication for public‑key parity, replacing it with two SHA‑256 + RIPEMD‑160 calls (~+59 % speed).
- **Fermat batch inversion** — Faster than extended‑GCD when processing 1024 + points at once.
- **`--random` mode** — Lottery‑style random jumps across the search space.
- **`--vanity` mode** — Log every key whose hash160 starts with the same N hex chars as your target.
- **`--lang pt|en`** — Full Portuguese / English interface. Persisted in `lang.cfg`.
- **Multi‑GPU** — Automatic GPU detection, `--gpus` to select devices.

---

## 🖥️ Requirements

| Component | Requirement |
|-----------|------------|
| **GPU** | AMD with ROCm 6+ (RX 5000/6000/7000, Vega, MI series) |
| **OS**  | Linux (Ubuntu 22.04+, Fedora 38+, Arch, etc.) |
| **ROCm**| rocm-hip-sdk, rocm-llvm, hipcc |
| **RAM** | 8 GB+ system memory |
| **VRAM**| 4 GB+ (tested on 8 GB) |

> **Note:** NVIDIA GPUs are **not** supported. For CUDA the original [CUDACyclone](https://github.com/kanhavishva/CUDACyclone) can be used.

---

## 🛠️ Installation

### 1. Install ROCm

```bash
# Ubuntu 22.04
sudo apt update
sudo apt install rocm-hip-sdk rocm-llvm rocm-dev

# Verify
hipcc --version
rocminfo | grep gfx
```

### 2. Clone & build

```bash
git clone https://github.com/jmr2704/BRBtcHuntAMD.git
cd BRBtcHuntAMD

# Automatic GPU‑arch detection (recommended)
GPU_ARCHS=$(rocminfo | grep -oP 'gfx\w+' | sort -u | tr '\n' ' ')
make GPU_ARCHS="$GPU_ARCHS" -j$(nproc)

# Or specify your GPU directly
make GPU_ARCHS="gfx1032" -j$(nproc)

# Clean build
make clean && make -j$(nproc)
```

### 3. Quick test

```bash
# Find the known puzzle key (range = 1 key)
./BRBtcHuntAMD --range 22382FACD0:22382FACD0 \
               --address 1HBtApAFA9B2YZw3G2YKSMCtb3dVnjuNe2 \
               --grid 2048,32 --slices 64 --gpus 0
```

---

## 📖 Usage

```
BRBtcHuntAMD — GPU Satoshi Puzzle Solver (ROCm/HIP)

Usage: ./BRBtcHuntAMD --range <start_hex>:<end_hex> --address <base58>
       [--grid A,B] [--slices N] [--gpus all|0|0,1] [--random]
       [--vanity N] [--lang pt|en]

Required:
  --range <start:end>        Search range in hex (e.g. 2000000000:3FFFFFFFFF)
  --address <base58>         P2PKH address to search for
  --target-hash160 <hex>     Alternative to --address (raw hash160)

Options:
  --grid <P,T>               Points per batch, threads per block (e.g. 512,256)
  --slices <N>               Batches per thread per kernel launch
  --gpus <all|0|0,1>         Select which GPUs to use (default: all)
  --random                   Lottery mode: random jumps across the range
  --vanity <N>               Save keys whose first N hex chars of hash160 match the target
  --lang pt|en               Language: Portuguese / English (default: en)
  -h, --help                 Show this help
```

### Parameters

| Parameter | Description |
|-----------|-------------|
| `--range <start:end>` | **Required.** Hex range to search. Example: `2000000000:3FFFFFFFFF` |
| `--address <base58>` | **Required** (or `--target-hash160`). P2PKH address like `1HBtAp...` |
| `--target-hash160 <hex>` | Alternative to `--address`. Raw 20‑byte hash160 in hex. |
| `--grid <P,T>` | Tuning. `P` = points per batch (must be even, ≤2048). `T` = threads per block (128‑512). Default: `2048,256` |
| `--slices <N>` | How many batches each thread runs per kernel launch. Higher = less overhead, less frequent chunk switches. Default: `64` |
| `--gpus <all\|0\|0,1>` | Select GPUs. `all` uses every compatible device. Default: `all` |
| `--random` | Lottery mode — each GPU jumps to a random position after every chunk. Good for sparse competitions. |
| `--vanity <N>` | Vanity mode — log every checked key whose hash160 begins with the same N hex chars as the target hash160. Results saved to `vanity_results.txt`. |
| `--lang pt\|en` | Switch interface language. Saved to `lang.cfg` for persistence. |
| `-h, --help` | Show the help text. |

---

## 🧪 Examples

```bash
# 1) Basic search
./BRBtcHuntAMD --range 200000000:3FFFFFFFF --address 1HBtAp... --gpus 0

# 2) With tuning
./BRBtcHuntAMD --range 200000000:3FFFFFFFF --address 1HBtAp... \
               --grid 2048,32 --slices 64 --gpus 0

# 3) Random / lottery mode
./BRBtcHuntAMD --random --range 80000000:FFFFFFFF --address 1HBtAp... \
               --grid 2048,32 --slices 64 --gpus 0

# 4) Multi‑GPU
./BRBtcHuntAMD --range 200000000:3FFFFFFFF --address 1HBtAp... \
               --gpus 0,1

# 5) Vanity (save keys matching first 4 hex chars of target hash160)
./BRBtcHuntAMD --range 200000000:3FFFFFFFF --address 1HBtAp... \
               --grid 2048,32 --slices 64 --gpus 0 --vanity 4

# 6) Portuguese interface
./BRBtcHuntAMD --range 200000000:3FFFFFFFF --address 1HBtAp... --lang pt

# 7) Validate a known key (puzzle 66)
./BRBtcHuntAMD --range 22382FACD0:22382FACD0 \
               --address 1HBtApAFA9B2YZw3G2YKSMCtb3dVnjuNe2 \
               --grid 2048,32 --slices 64 --gpus 0
```

---

## 🎭 Vanity Mode

When you pass `--vanity <N>`, every checked key whose hash160 **starts with the same N hex characters** as your target hash160 is logged.

```
# Save keys whose hash160 matches first 4 hex chars of the target
./BRBtcHuntAMD --range 80000000:FFFFFFFF --address 1HBtAp... --vanity 4
```

- Results are written to **`vanity_results.txt`** when the worker finishes.
- The progress display shows a live **`Vanity: N`** counter.
- No console flood — only the count appears during execution.

---

## 🌐 Language Support

Switch between **English** and **Portuguese**:

```bash
# Portuguese (one‑time)
./BRBtcHuntAMD --range ... --address ... --lang pt

# The setting is saved to lang.cfg. Next runs remember it.
./BRBtcHuntAMD --range ... --address ...   # ← now in PT

# Back to English
./BRBtcHuntAMD --range ... --address ... --lang en
```

| Label | EN | PT |
|-------|----|----|
| Time / Speed | `Time`, `Speed` | `Tempo`, `Veloc.` |
| Count / Progress | `Count`, `Progress` | `Total`, `Prog.` |
| Vanity | `Vanity` | `Vaidade` |
| Chunks | `Chunks` | `Saltos` |
| FOUND MATCH | `FOUND MATCH!` | `CHAVE ENCONTRADA!` |
| Summary | `Total keys`, `Avg speed` | `Total chaves`, `Veloc. med.` |
| Config labels | `SM`, `Blocks`,... | `SM`, `Blocos`,... |

---

## ⚡ Performance

Measured on a single **AMD Radeon RX 6600** (RDNA 2, 14 CUs, 8 GB VRAM).

| Mode | Throughput |
|------|-----------|
| Sequential, grid 2048×32, slices 64 | **~1.16 Gkeys/s** |
| Random mode | **~1.05 Gkeys/s** |
| With `--vanity 4` | **~950 Mkeys/s** |

**Key optimisations:**
- Dual‑prefix hash (deferred parity) → +59 % over modular‑parity approach.
- Fermat batch inversion (product‑tree) → fast 1024‑point batch.
- Inline modular arithmetic (`__forceinline__` on `_ModMult`, `_ModSqr`).
- Hand‑tuned launch bounds (`__launch_bounds__(256, 2)`).

---

<br>

# 💛 Donate — Doação

If this tool helps you find a puzzle key, a coffee (or a beer) is always welcome!  
Se esta ferramenta te ajudar a encontrar uma chave de puzzle, um café (ou uma cerveja) é sempre bem-vindo!

```
bc1q7s4m9cwlq8xtx2nz74mquh6ax0jqwsmkkd56s3
```

[![Bitcoin](https://img.shields.io/badge/BTC-donate-f7931a?logo=bitcoin)](https://blockchair.com/bitcoin/address/bc1q7s4m9cwlq8xtx2nz74mquh6ax0jqwsmkkd56s3)

---

# 📄 License — Licença

This project is licensed under the **MIT License**.  
Este projeto é licenciado sob a **Licença MIT**.

Based on the original [CUDACyclone](https://github.com/kanhavishva/CUDACyclone) by kanhavishva, ported to AMD ROCm/HIP.  
Baseado no [CUDACyclone](https://github.com/kanhavishva/CUDACyclone) original por kanhavishva, portado para AMD ROCm/HIP.

---

<div align="center">
  <sub>Made with ❤️ and 🐊 | Feito com ❤️ e 🐊</sub>
  <br>
  <sub>🚀 BRBtcHuntAMD — breaking puzzles, one hash at a time.</sub>
</div>
