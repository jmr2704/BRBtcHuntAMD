# BRBtcHuntAMD — Plano de Otimização

## Missão
Recuperar a performance do CUDACyclone original (~2 Gkeys/s em NVIDIA) no AMD ROCm/HIP.  
Atualmente: ~500 Mkeys/s com Fermat inversion.  
Alvo: ~2-3 Gkeys/s com GCD inversion + micro-otimizações.

---

## Fase 0: Bug Fix — AddCh carry duplicado ✅

### Problema
A função `AddCh` retornava `2*carry` em vez de `carry`:
```c
// ANTES (bug):
UADD(carryOut, carry, 0ULL);  // carryOut = carry + 0 + carry_flag = 2*carry

// DEPOIS (fix):
return carry;  // carry já é o carry_out final
```

Esse bug afetava TANTO o Fermat inversion quanto o GCD, corrompendo
todos os resultados de inversão modular.

### Impacto
- ❌ `_ModInv` (Fermat e GCD) produziam inversos incorretos
- ❌ `pointDoubleAffine`, `scalarMulBaseAffine` corrompidos
- ❌ Programa não encontrava chaves
- ✅ APÓS FIX: ~500-580 Mkeys/s, chaves encontradas corretamente

---

## Fase 1: GCD Inversion (🔜 PRÓXIMA)

### Problema
`_ModInv` atual usa exponenciação de Fermat `a^(p-2) mod p`:
- ~256 squarings + ~128 multiplications = ~384 field mults por inversão
- ~90% do tempo de GPU gasto aqui

### Solução
Ativar o algoritmo GCD original do CUDACyclone (já implementado em `AMDMath.h`):
- `_DivStep62` — divisão 62-bit do GCD estendido
- `MatrixVecMul` / `MatrixVecMulHalf` — transformação linear
- Usa limbs de 62 bits (5 limbs de 62 = 310 bits)
- ~5-10 iterações vs ~256 squarings do Fermat

### O que já foi corrigido (e atrapalhou)
- `AddCh` — ✅ CORRIGIDO (fev de todo carry chain bug)
- `IMult` / `IMultC` — ✅ schoolbook __int128 (correto)
- `UADDO` / `UADDC` — ✅ Clang builtins (corretos)
- Binary GCD caseiro — ❌ implementado mas descartado (bug no signed arithmetic)

### O que fazer (GCD original CUDACyclone)
1. ✅ Funções auxiliares já existem em `AMDMath.h`
2. 🔲 Ativar `_DivStep62` + `MatrixVecMul` no `_ModInv`
3. 🔲 Testar com `fieldInv(2) * 2 mod p == 1`
4. 🔲 Testar `scalarMulBaseAffine` com chave conhecida
5. 🔲 Benchmark: Fermat (~500 Mkeys/s) vs GCD (estimado ~2-3 Gkeys/s)

---

## Fase 2: Montgomery Multiplication (ADMT)

### Problema
`_ModMult` atual usa schoolbook (UMult) + redução por `0x1000003D1`:
- 4× UMult (cada um faz 4 mul 64×64 = 16 mul)
- + redução especial
- Total: ~20 mul 64×64 por field mult

### Solução
Montgomery multiplication:
- Pré-calcular `mu = -p^-1 mod 2^256`
- 4× mul 64×64 (parcial) + redução Montgomery
- Total: ~16 mul 64×64 por field mult (similar ao atual)
- Mas com pipeline mais eficiente

### Prioridade
Média — ganho estimado < 20% sobre o schoolbook atual.

---

## Fase 3: GCN Inline Assembly (Micro-op)

### Problema
Benchmark mostrou que GCN inline asm manual é 3× MAIS LENTO que `__int128`.
O compilador Clang já gera código GCN eficiente.

### Decisão
**NÃO USAR** inline assembly manual. O compilador sabe melhor.

---

## Fase 4: Kernel Launch & Occupancy Tuning

### Oportunidades
- `__launch_bounds__(256, 2)` — limita a 2 blocks/SM (muito conservador)
- RX 6600 tem 64KB shared memory + 128 registers/thread
- Podemos tentar `__launch_bounds__(256, 4)` ou `(128, 4)`

### Prioridade
Baixa — ganho marginal depois do GCD.

---

## Cronograma

| Fase | Estimativa | Ganho | Status |
|------|-----------|-------|--------|
| 0. AddCh Bug Fix | 4h debug | ESSENCIAL | ✅ CONCLUÍDO |
| 1. GCD Inversion (62-bit CUDACyclone) | 2-3h | 5-10× | ⏳ PRÓXIMO |
| 2. Montgomery Multiplication | 2-3h | 1.2× | ⏳ |
| 3. GCN Inline Assembly | — | 0.33× (pior) | ❌ CANCELADO |
| 4. Occupancy Tuning | 1h | 1.1× | ⏳ |

**Alvo final:** ~2.5-5 Gkeys/s (vs 500 Mkeys/s atual)

## Lições Aprendidas

1. **GCN inline asm é PIOR que __int128**: O compilador Clang gera código
   GCN melhor que inline asm manual. Não tentar otimizar micro-operações.

2. **AddCh era o bug central**: Carry chain duplicado corrompia TODAS as
   operações de inversão modular. Nada funcionava até consertar isso.

3. **Binary GCD caseiro é traiçoeiro**: Signed arithmetic em 256-bit sem
   espaço pra carry extra (257o bit) leva a bugs sutis. O algoritmo do
   CUDACyclone usa 5 limbs (320 bits) justamente pra evitar isso.

---

🐊 Jacazul — Julho 2026
