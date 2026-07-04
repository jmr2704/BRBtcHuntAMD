# BRBtcHuntAMD — Status

## 🎯 Port: CUDACyclone → AMD ROCm/HIP

**GPU:** AMD Radeon RX 6600 (gfx1032, 14 SMs, 8GB VRAM)  
**ROCm:** 7.1.0  
**Status:** ✅ Funcional e verificado

---

## 🏆 Chaves encontradas

| Endereço | Chave Privada | Range | Modo |
|----------|--------------|-------|------|
| `1HBtApAFA9B2YZw3G2YKSMCtb3dVnjuNe2` (Puzzle 130) | `0x22382FACD0` | `200000000000:3FFFFFFFFFFF` | Test vector |
| `1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv` (Puzzle) | `0xE9AE4933D6` | `8000000000:ffffffffff` | **Chave real encontrada** |

---

## ⚡ Performance (RX 6600)

### Modo Sequencial (`--range A:B`)

| Grid | Velocidade | Config |
|------|-----------|--------|
| 256,8 | **~500 Mkeys/s** | batch=256, batches/SM=8, threads=28672 |
| 256,16 | ~426 Mkeys/s | batch=256, batches/SM=16, threads=57344 |
| 128,8 | ~387 Mkeys/s | batch=128, batches/SM=8, threads=28672 |
| 256,64 | ~328 Mkeys/s | batch=256, batches/SM=64, threads=229376 |

### Modo Aleatório (`--random --slices N`)

| Grid | Velocidade | Observação |
|------|-----------|------------|
| 256,8 | ~60-120 Mkeys/s | chunks menores = overhead de reinicialização |

---

## 🧬 Pipeline verificado

- [x] `fieldAdd` / `fieldSub` — carry chain com Clang builtins
- [x] `fieldMul` — schoolbook __int128 (sem carry chain bugado)
- [x] `fieldSqr` — schoolbook __int128
- [x] `fieldInv` — Exponenciação de Fermat `a^(p-2) mod p`
- [x] `pointDoubleAffine` — 2×G verificado contra Python
- [x] `pointAddAffine` — G+G verificado contra Python
- [x] `scalarMulBaseAffine` — k*G para chave conhecida 0x22382FACD0
- [x] hash160 — SHA256 + RIPEMD160
- [x] Address matching — base58 decode + hash160 compare
- [x] **Chave real encontrada** — 0xE9AE4933D6 para endereço 1EeAxcpr...

---

## 🔧 Próximos passos

1. **GCN inline assembly** — substituir __int128 por `v_mad_u64_u32` + VCC carry chain (est. 1.5-2× mais rápido)
2. **GCD inversion** — reimplementar _ModInv GCD com carry nativo (est. 5-10× sobre Fermat)
3. **Suporte multi-GPU** — verificar split correto de range com `--gpus 0,1`
4. **Range split bug** — hex strings de comprimentos diferentes causam start > end

---

🐊 Jacazul — Julho 2026
