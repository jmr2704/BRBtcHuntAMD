/*
 * AMDMath.h — Optimized secp256k1 field arithmetic for AMD HIP (ROCm)
 *
 * Replaces PTX inline assembly with Clang builtins that compile to efficient
 * GCN code on AMD GPUs (gfx1032, gfx90c, etc.).
 *
 * For 64-bit carry arithmetic we use __builtin_addcll / __builtin_subcll
 * which produce v_add_co_u32/v_addc_co_u32 on GCN.
 * For 64×64→128 multiply we use __int128 (Clang lowers to v_mul_lo/hi).
 *
 * Based on JeanLucPons/VanitySearch → CUDACyclone.
 */

#ifndef AMD_MATH_H
#define AMD_MATH_H

#include <cstdint>
#include <cstddef>

#define NBBLOCK 5
#define BIFULLSIZE 40

// ── Carry-chain arithmetic via Clang builtins ────────────────────────────
// __builtin_addcll(a, b, carry_in, &carry_out) → a + b + carry_in
//   carry_out = carry out (0/1)
// __builtin_subcll(a, b, carry_in, &carry_out) → a - b - carry_in
//   carry_out = borrow out (0/1) — 1 means borrow occurred

// ── Carry-chain arithmetic via Clang builtins ────────────────────────────
// __builtin_addcll / __builtin_subcll take unsigned long long* (not uint64_t*)
// We use unsigned long long for the carry variable to match the builtin API.
// On x86_64 Linux, unsigned long long and uint64_t are both 64-bit.

#ifdef __clang__
  #define UADDO(c, a, b)       do { unsigned long long _c; c = __builtin_addcll((a),(b),0ULL,&_c); carry = (uint64_t)_c; } while(0)
  #define UADDC(c, a, b)       do { unsigned long long _c; c = __builtin_addcll((a),(b),carry,&_c); carry = (uint64_t)_c; } while(0)
  #define UADD(c, a, b)        do { c = (a)+(b)+carry; } while(0)

  #define UADDO1(c, a)         do { unsigned long long _c; c = __builtin_addcll((c),(a),0ULL,&_c); carry = (uint64_t)_c; } while(0)
  #define UADDC1(c, a)         do { unsigned long long _c; c = __builtin_addcll((c),(a),carry,&_c); carry = (uint64_t)_c; } while(0)
  #define UADD1(c, a)          do { c = (c)+(a)+carry; } while(0)

  #define USUBO(c, a, b)       do { unsigned long long _c; c = __builtin_subcll((a),(b),0ULL,&_c); carry = (uint64_t)_c; } while(0)
  #define USUBC(c, a, b)       do { unsigned long long _c; c = __builtin_subcll((a),(b),carry,&_c); carry = (uint64_t)_c; } while(0)
  #define USUB(c, a, b)        do { c = (a)-(b)-carry; } while(0)

  #define USUBO1(c, a)         do { unsigned long long _c; c = __builtin_subcll((c),(a),0ULL,&_c); carry = (uint64_t)_c; } while(0)
  #define USUBC1(c, a)         do { unsigned long long _c; c = __builtin_subcll((c),(a),carry,&_c); carry = (uint64_t)_c; } while(0)
  #define USUB1(c, a)          do { c = (c)-(a)-carry; } while(0)
#else
  // Fallback for non-Clang compilers
  #define UADDO(c, a, b)       do { uint64_t _s=(a)+(b); c=_s; carry=(_s<(a))?1ULL:0ULL; } while(0)
  #define UADDC(c, a, b)       do { uint64_t _s=(a)+(b)+carry; c=_s; carry=(_s<(a))?1ULL:0ULL; } while(0)
  #define UADD(c, a, b)        do { c=(a)+(b)+carry; } while(0)
  #define UADDO1(c, a)         do { uint64_t _s=(c)+(a); c=_s; carry=(_s<(c))?1ULL:0ULL; } while(0)
  #define UADDC1(c, a)         do { uint64_t _s=(c)+(a)+carry; c=_s; carry=(_s<(c))?1ULL:0ULL; } while(0)
  #define UADD1(c, a)          do { c=(c)+(a)+carry; } while(0)
  #define USUBO(c, a, b)       do { c=(a)-(b); carry=(c>(a))?1ULL:0ULL; } while(0)
  #define USUBC(c, a, b)       do { c=(a)-(b)-carry; carry=(c>(a))?1ULL:0ULL; } while(0)
  #define USUB(c, a, b)        do { c=(a)-(b)-carry; } while(0)
  #define USUBO1(c, a)         do { c=(c)-(a); carry=(c>=(c))?0ULL:1ULL; } while(0)
  #define USUBC1(c, a)         do { c=(c)-(a)-carry; carry=(c>(c)-(a))?1ULL:0ULL; } while(0)
  #define USUB1(c, a)          do { c=(c)-(a)-carry; } while(0)
#endif

// ── 64×64 multiply: low part ─────────────────────────────────────────────
#define UMULLO(lo, a, b)     do { uint64_t _hi_tmp; mul_lo_hi_u64((a),(b),&(lo),&_hi_tmp); } while(0)
#define UMULHI(hi, a, b)     do { uint64_t _lo_tmp; mul_lo_hi_u64((a),(b),&_lo_tmp,&(hi)); } while(0)

// ── Multiply low / high (64×64 → 128) via __int128 ─────────────────────
// Clang on AMDGPU lowers __int128 mul to v_mul_lo_u32 / v_mul_hi_u32 pairs.

__device__ __forceinline__ void mul_lo_hi_u64(uint64_t a, uint64_t b,
                                               uint64_t *lo, uint64_t *hi) {
    __uint128_t r = (__uint128_t)a * b;
    *lo = (uint64_t)r;
    *hi = (uint64_t)(r >> 64);
}

// ── MADDO / MADDC / MADD: fused multiply-add high with carry chain ─────
//   PTX equivalent:
//     MADDO: mad.hi.cc.u64  r, a, b, c   → r = hi(a*b + c); CC = lo_add_carry
//     MADDC: madc.hi.cc.u64 r, a, b, c   → r = hi(a*b + c + carry); CC = …
//     MADD:  madc.hi.u64    r, a, b, c   → r = hi(a*b + c + carry); no CC
//
//  In GCN this maps to: v_mul_lo/hi + v_add_co_u32 + v_addc_co_u32,
//  which Clang should schedule well from the __int128 decomposition.

#define MADDO(r, a, b, c) do { \
    __uint128_t _p = (__uint128_t)(a) * (b); \
    uint64_t _lo = (uint64_t)_p; \
    uint64_t _hi = (uint64_t)(_p >> 64); \
    uint64_t _sum = _lo + (uint64_t)(c); \
    uint64_t _carry_lo = (_sum < _lo) ? 1ULL : 0ULL; \
    r = _hi + _carry_lo; \
    carry = _carry_lo; \
} while(0)

#define MADDC(r, a, b, c) do { \
    __uint128_t _p = (__uint128_t)(a) * (b); \
    uint64_t _lo = (uint64_t)_p; \
    uint64_t _hi = (uint64_t)(_p >> 64); \
    uint64_t _s1 = _lo + (uint64_t)(c); \
    uint64_t _c1 = (_s1 < _lo) ? 1ULL : 0ULL; \
    uint64_t _s2 = _s1 + carry; \
    if (_s2 < _s1) _c1 = 1ULL; \
    r = _hi + _c1; \
    carry = _c1; \
} while(0)

#define MADD(r, a, b, c) do { \
    __uint128_t _p = (__uint128_t)(a) * (b); \
    uint64_t _lo = (uint64_t)_p; \
    uint64_t _hi = (uint64_t)(_p >> 64); \
    uint64_t _s1 = _lo + (uint64_t)(c); \
    uint64_t _c1 = (_s1 < _lo) ? 1ULL : 0ULL; \
    uint64_t _s2 = _s1 + carry; \
    if (_s2 < _s1) _c1 = 1ULL; \
    r = _hi + _c1; \
} while(0)

#define MADDS(r, a, b, c) do { \
    __int128 _p = (__int128)(a) * (b); \
    uint64_t _lo = (uint64_t)_p; \
    uint64_t _hi = (uint64_t)((uint64_t*)(&_p))[1]; \
    uint64_t _s1 = _lo + (uint64_t)(c); \
    uint64_t _c1 = (_s1 < _lo) ? 1ULL : 0ULL; \
    _s1 += carry; \
    if (_s1 < carry) _c1 = 1ULL; \
    r = _hi + _c1; \
    carry = _c1; \
} while(0)

// ── Shared constants ────────────────────────────────────────────────────
#define HSIZE (GRP_SIZE / 2 - 1)

__device__ __constant__ uint64_t MM64 = 0xD838091DD2253531ULL;
__device__ __constant__ uint64_t MSK62 = 0x3FFFFFFFFFFFFFFFULL;

#define _IsPositive(x) (((int64_t)(x[4]))>=0LL)
#define _IsNegative(x) (((int64_t)(x[4]))<0LL)
#define _IsEqual(a,b)  ((a[4]==b[4])&&(a[3]==b[3])&&(a[2]==b[2])&&(a[1]==b[1])&&(a[0]==b[0]))
#define _IsZero(a)     (((a)[4]|(a)[3]|(a)[2]|(a)[1]|(a)[0])==0ULL)
#define _IsOne(a)      (((a)[4]==0ULL)&&(a)[3]==0ULL&&(a)[2]==0ULL&&(a)[1]==0ULL&&(a)[0]==1ULL)

#define IDX threadIdx.x

#define __sright128(a,b,n) (((a)>>(n))|((b)<<(64-(n))))
#define __sleft128(a,b,n)  (((b)<<(n))|((a)>>(64-(n))))

// ── Composite macro patterns (unchanged logic, uses the carry macros above) ─

#define AddP(r) { \
  UADDO1(r[0], 0xFFFFFFFEFFFFFC2FULL); \
  UADDC1(r[1], 0xFFFFFFFFFFFFFFFFULL); \
  UADDC1(r[2], 0xFFFFFFFFFFFFFFFFULL); \
  UADDC1(r[3], 0xFFFFFFFFFFFFFFFFULL); \
  UADD1(r[4], 0ULL); }

#define SubP(r) { \
  USUBO1(r[0], 0xFFFFFFFEFFFFFC2FULL); \
  USUBC1(r[1], 0xFFFFFFFFFFFFFFFFULL); \
  USUBC1(r[2], 0xFFFFFFFFFFFFFFFFULL); \
  USUBC1(r[3], 0xFFFFFFFFFFFFFFFFULL); \
  USUB1(r[4], 0ULL); }

#define Sub2(r,a,b) { \
  USUBO(r[0], a[0], b[0]); \
  USUBC(r[1], a[1], b[1]); \
  USUBC(r[2], a[2], b[2]); \
  USUBC(r[3], a[3], b[3]); \
  USUB(r[4], a[4], b[4]); }

#define Sub1(r,a) { \
  USUBO1(r[0], a[0]); \
  USUBC1(r[1], a[1]); \
  USUBC1(r[2], a[2]); \
  USUBC1(r[3], a[3]); \
  USUB1(r[4], a[4]); }

#define Add128(r,a) { \
  UADDO1((r)[0], (a)[0]); \
  UADD1((r)[1], (a)[1]); }

#define Neg(r) { \
  USUBO(r[0],0ULL,r[0]); \
  USUBC(r[1],0ULL,r[1]); \
  USUBC(r[2],0ULL,r[2]); \
  USUBC(r[3],0ULL,r[3]); \
  USUB(r[4],0ULL,r[4]); }

// ── 256×64 → 320-bit multiplication (direct schoolbook, no MADDO chain) ──
// Each a[i]*b is 128-bit. We accumulate into 5 limbs with carry propagation.
#define UMult(r, a, b) do { \
    __uint128_t _p0  = (__uint128_t)(a)[0] * (b); \
    __uint128_t _p1  = (__uint128_t)(a)[1] * (b); \
    __uint128_t _p2  = (__uint128_t)(a)[2] * (b); \
    __uint128_t _p3  = (__uint128_t)(a)[3] * (b); \
    (r)[0] = (uint64_t)_p0; \
    __uint128_t _t1  = (_p0 >> 64) + (uint64_t)_p1; \
    (r)[1] = (uint64_t)_t1; \
    __uint128_t _t2  = (_t1 >> 64) + (_p1 >> 64) + (uint64_t)_p2; \
    (r)[2] = (uint64_t)_t2; \
    __uint128_t _t3  = (_t2 >> 64) + (_p2 >> 64) + (uint64_t)_p3; \
    (r)[3] = (uint64_t)_t3; \
    (r)[4] = (uint64_t)(_t3 >> 64) + (uint64_t)(_p3 >> 64); \
} while(0)

#define Load(r, a) { \
  (r)[0]=(a)[0]; (r)[1]=(a)[1]; (r)[2]=(a)[2]; (r)[3]=(a)[3]; (r)[4]=(a)[4]; }

#define _LoadI64(r, a, _carry) { \
  (r)[0]=a; (r)[1]=a>>63; (r)[2]=(r)[1]; (r)[3]=(r)[1]; (r)[4]=(r)[1]; _carry=(r)[1]; }

#define Load256(r, a) { \
  (r)[0]=(a)[0]; (r)[1]=(a)[1]; (r)[2]=(a)[2]; (r)[3]=(a)[3]; }

// ── Core device functions ─────────────────────────────────────────────

__device__ void ShiftR62(uint64_t r[5]) {
  r[0] = (r[1] << 2) | (r[0] >> 62);
  r[1] = (r[2] << 2) | (r[1] >> 62);
  r[2] = (r[3] << 2) | (r[2] >> 62);
  r[3] = (r[4] << 2) | (r[3] >> 62);
  r[4] = (int64_t)(r[4]) >> 62;
}

__device__ void ModSub256isOdd(uint64_t* a, uint64_t* b, uint8_t* parity) {
    uint64_t carry;
    uint64_t T[4];
    USUBO(T[0], a[0], b[0]);
    USUBC(T[1], a[1], b[1]);
    USUBC(T[2], a[2], b[2]);
    USUBC(T[3], a[3], b[3]);
    USUB(carry, 0ULL, 0ULL);
    *parity = (T[0] & 1) ^ (carry & 1);
}

__device__ void ShiftR62(uint64_t dest[5], uint64_t r[5], uint64_t carry_in) {
  dest[0] = (r[1] << 2) | (r[0] >> 62);
  dest[1] = (r[2] << 2) | (r[1] >> 62);
  dest[2] = (r[3] << 2) | (r[2] >> 62);
  dest[3] = (r[4] << 2) | (r[3] >> 62);
  dest[4] = (carry_in << 2) | (r[4] >> 62);
}

// ── Signed 256×64 → 320-bit multiply (schoolbook, correct) ────────
__device__ void IMult(uint64_t *r, uint64_t *a, int64_t b) {
  uint64_t t[NBBLOCK];
  if(b < 0) {
    b = -b;
    uint64_t carry = 0;
    UADDO(t[0], ~a[0], 1ULL); UADDC(t[1], ~a[1], 0ULL);
    UADDC(t[2], ~a[2], 0ULL); UADDC(t[3], ~a[3], 0ULL); UADD(t[4], ~a[4], 0ULL);
  } else {
    Load(t, a);
  }
  __uint128_t _p0 = (__uint128_t)t[0] * b;
  __uint128_t _p1 = (__uint128_t)t[1] * b;
  __uint128_t _p2 = (__uint128_t)t[2] * b;
  __uint128_t _p3 = (__uint128_t)t[3] * b;
  __uint128_t _p4 = (__uint128_t)t[4] * b;
  r[0] = (uint64_t)_p0;
  __uint128_t _t1 = (_p0 >> 64) + (uint64_t)_p1;
  r[1] = (uint64_t)_t1;
  __uint128_t _t2 = (_t1 >> 64) + (_p1 >> 64) + (uint64_t)_p2;
  r[2] = (uint64_t)_t2;
  __uint128_t _t3 = (_t2 >> 64) + (_p2 >> 64) + (uint64_t)_p3;
  r[3] = (uint64_t)_t3;
  __uint128_t _t4 = (_t3 >> 64) + (_p3 >> 64) + (uint64_t)_p4;
  r[4] = (uint64_t)_t4;
}

__device__ uint64_t IMultC(uint64_t* r, uint64_t* a, int64_t b) {
  // Same as IMult but returns carry-out (hi of last product)
  uint64_t t[NBBLOCK];
  if(b < 0) {
    b = -b;
    uint64_t carry = 0;
    UADDO(t[0], ~a[0], 1ULL); UADDC(t[1], ~a[1], 0ULL);
    UADDC(t[2], ~a[2], 0ULL); UADDC(t[3], ~a[3], 0ULL); UADD(t[4], ~a[4], 0ULL);
  } else {
    Load(t, a);
  }
  __uint128_t _p0 = (__uint128_t)t[0] * b;
  __uint128_t _p1 = (__uint128_t)t[1] * b;
  __uint128_t _p2 = (__uint128_t)t[2] * b;
  __uint128_t _p3 = (__uint128_t)t[3] * b;
  __uint128_t _p4 = (__uint128_t)t[4] * b;
  r[0] = (uint64_t)_p0;
  __uint128_t _t1 = (_p0 >> 64) + (uint64_t)_p1;
  r[1] = (uint64_t)_t1;
  __uint128_t _t2 = (_t1 >> 64) + (_p1 >> 64) + (uint64_t)_p2;
  r[2] = (uint64_t)_t2;
  __uint128_t _t3 = (_t2 >> 64) + (_p2 >> 64) + (uint64_t)_p3;
  r[3] = (uint64_t)_t3;
  __uint128_t _t4 = (_t3 >> 64) + (_p3 >> 64) + (uint64_t)_p4;
  r[4] = (uint64_t)_t4;
  return (uint64_t)(_t4 >> 64) + (uint64_t)(_p4 >> 64);
}

__device__ void MulP(uint64_t *r, uint64_t a) {
  uint64_t ah, al;
  mul_lo_hi_u64(a, 0x1000003D1ULL, &al, &ah);
  uint64_t carry;
  USUBO(r[0], 0ULL, al); USUBC(r[1], 0ULL, ah);
  USUBC(r[2], 0ULL, 0ULL); USUBC(r[3], 0ULL, 0ULL); USUB(r[4], a, 0ULL);
}

__device__ void ModNeg256(uint64_t *r, uint64_t *a) {
  uint64_t t[4], carry;
  USUBO(t[0],0ULL,a[0]); USUBC(t[1],0ULL,a[1]);
  USUBC(t[2],0ULL,a[2]); USUBC(t[3],0ULL,a[3]);
  carry = 0;
  UADDO(r[0],t[0],0xFFFFFFFEFFFFFC2FULL);
  UADDC(r[1],t[1],0xFFFFFFFFFFFFFFFFULL);
  UADDC(r[2],t[2],0xFFFFFFFFFFFFFFFFULL);
  UADD(r[3],t[3],0xFFFFFFFFFFFFFFFFULL);
}

__device__ void ModNeg256(uint64_t *r) {
  uint64_t t[4], carry;
  USUBO(t[0],0ULL,r[0]); USUBC(t[1],0ULL,r[1]);
  USUBC(t[2],0ULL,r[2]); USUBC(t[3],0ULL,r[3]);
  carry = 0;
  UADDO(r[0],t[0],0xFFFFFFFEFFFFFC2FULL);
  UADDC(r[1],t[1],0xFFFFFFFFFFFFFFFFULL);
  UADDC(r[2],t[2],0xFFFFFFFFFFFFFFFFULL);
  UADD(r[3],t[3],0xFFFFFFFFFFFFFFFFULL);
}

__device__ void ModSub256(uint64_t *r, uint64_t *a, uint64_t *b) {
    uint64_t borrow, carry;
    uint64_t p[4] = { 0xFFFFFFFEFFFFFC2FULL, 0xFFFFFFFFFFFFFFFFULL,
                      0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL };
    USUBO(r[0],a[0],b[0]); USUBC(r[1],a[1],b[1]);
    USUBC(r[2],a[2],b[2]); USUBC(r[3],a[3],b[3]);
    USUB(borrow,0ULL,0ULL);
    if(borrow){ carry=0;
      UADDO1(r[0],p[0]); UADDC1(r[1],p[1]);
      UADDC1(r[2],p[2]); UADD1(r[3],p[3]);
    }
}

__device__ void ModSub256(uint64_t* r, uint64_t* b) {
    uint64_t borrow, carry;
    uint64_t p[4]={0xFFFFFFFEFFFFFC2FULL,0xFFFFFFFFFFFFFFFFULL,0xFFFFFFFFFFFFFFFFULL,0xFFFFFFFFFFFFFFFFULL};
    USUBO1(r[0],b[0]); USUBC1(r[1],b[1]);
    USUBC1(r[2],b[2]); USUBC1(r[3],b[3]);
    USUB(borrow,0ULL,0ULL);
    if(borrow){ carry=0;
      UADDO1(r[0],p[0]); UADDC1(r[1],p[1]);
      UADDC1(r[2],p[2]); UADD1(r[3],p[3]);
    }
}

#ifdef USE_SYMMETRY
__device__ bool ModPositive256(uint64_t *r) {
  if(r[3]>0x7FFFFFFFFFFFFFFFULL){ ModNeg256(r); return true; }
  return false;
}
__device__ void ModNeg256Order(uint64_t* r) {
  static __device__ const uint64_t _O[4]={
    0xBFD25E8CD0364141ULL,0xBAAEDCE6AF48A03BULL,
    0xFFFFFFFFFFFFFFFEULL,0xFFFFFFFFFFFFFFFFULL};
  uint64_t t[4],carry;
  USUBO(t[0],0ULL,r[0]); USUBC(t[1],0ULL,r[1]);
  USUBC(t[2],0ULL,r[2]); USUBC(t[3],0ULL,r[3]);
  carry=0;
  UADDO(r[0],t[0],_O[0]); UADDC(r[1],t[1],_O[1]);
  UADDC(r[2],t[2],_O[2]); UADD(r[3],t[3],_O[3]);
}
#endif

// ── ctz via GCN __builtin_ctzll (Clang) ────────────────────────────────
__device__ __forceinline__ uint32_t ctz(uint64_t x) {
  return (uint32_t)__builtin_ctzll(x);
}

#define SWAP(tmp,x,y) do { tmp=x; x=y; y=tmp; } while(0)
#define MSK62 0x3FFFFFFFFFFFFFFFULL

// ── GCD division step ──────────────────────────────────────────────────
__device__ void _DivStep62(uint64_t u[5],uint64_t v[5],
                           int32_t *pos,
                           int64_t *uu,int64_t *uv,
                           int64_t *vu,int64_t *vv) {
  *uu=1;*uv=0;*vu=0;*vv=1;
  uint32_t bitCount=62;
  uint64_t u0=u[0],v0=v[0];
  int64_t uh, vh;  // SIGNED: comparison must detect negative values
  int64_t w,x,y,z;
  while(*pos>0&&(u[*pos]|v[*pos])==0)(*pos)--;
  if(*pos==0){ uh=u[0]; vh=v[0]; }
  else {
    uint32_t s=__clzll(u[*pos]|v[*pos]);
    if(s==0){ uh=u[*pos]; vh=v[*pos]; }
    else{ uh=__sleft128(u[*pos-1],u[*pos],s); vh=__sleft128(v[*pos-1],v[*pos],s); }
  }
  int _div_safe = 0;
  while(1){
    if (++_div_safe > 2000) break;  // safety limit
    uint32_t zeros=ctz(v0|(1ULL<<bitCount));
    v0>>=zeros; vh>>=zeros; *uu<<=zeros; *uv<<=zeros; bitCount-=zeros;
    if(bitCount==0)break;
    if(vh<uh){ SWAP(w,uh,vh); SWAP(x,u0,v0); SWAP(y,*uu,*vu); SWAP(z,*uv,*vv); }
    vh-=uh; v0-=u0; *vv-=*uv; *vu-=*uu;
  }
}

__device__ void MatrixVecMulHalf(uint64_t dest[5],uint64_t u[5],uint64_t v[5],
                                  int64_t _11,int64_t _12,uint64_t* _carry) {
  uint64_t t1[NBBLOCK],t2[NBBLOCK],c1,c2;
  c1=IMultC(t1,u,_11); c2=IMultC(t2,v,_12);
  uint64_t carry=0;
  UADDO(dest[0],t1[0],t2[0]); UADDC(dest[1],t1[1],t2[1]);
  UADDC(dest[2],t1[2],t2[2]); UADDC(dest[3],t1[3],t2[3]);
  UADDC(dest[4],t1[4],t2[4]); UADD(*_carry,c1,c2);
}

__device__ void MatrixVecMul(uint64_t u[5],uint64_t v[5],
                              int64_t _11,int64_t _12,
                              int64_t _21,int64_t _22) {
  uint64_t t1[NBBLOCK],t2[NBBLOCK],t3[NBBLOCK],t4[NBBLOCK],carry;
  IMult(t1,u,_11); IMult(t2,v,_12); IMult(t3,u,_21); IMult(t4,v,_22);
  // NOTE: write v FIRST (uses original u), then u (overwrites u)
  carry=0; UADDO(v[0],t3[0],t4[0]); UADDC(v[1],t3[1],t4[1]);
  UADDC(v[2],t3[2],t4[2]); UADDC(v[3],t3[3],t4[3]); UADD(v[4],t3[4],t4[4]);
  carry=0; UADDO(u[0],t1[0],t2[0]); UADDC(u[1],t1[1],t2[1]);
  UADDC(u[2],t1[2],t2[2]); UADDC(u[3],t1[3],t2[3]); UADD(u[4],t1[4],t2[4]);
}

__device__ uint64_t AddCh(uint64_t r[5],uint64_t a[5],uint64_t carry_in) {
  // r += a + carry_in. Returns final carry out (0 or 1).
  unsigned long long _c;
  r[0] = __builtin_addcll(r[0], a[0], carry_in, &_c);
  uint64_t carry = (uint64_t)_c;
  r[1] = __builtin_addcll(r[1], a[1], carry, &_c); carry = (uint64_t)_c;
  r[2] = __builtin_addcll(r[2], a[2], carry, &_c); carry = (uint64_t)_c;
  r[3] = __builtin_addcll(r[3], a[3], carry, &_c); carry = (uint64_t)_c;
  r[4] = __builtin_addcll(r[4], a[4], carry, &_c); carry = (uint64_t)_c;
  return carry;
}



// ── Special multiply by 0x1000003D1 ─────────────────────────────────────
#define UMultSpecial(r, a) { \
  __uint128_t _p0 = (__uint128_t)(a)[0] * 0x1000003D1ULL; \
  __uint128_t _p1 = (__uint128_t)(a)[1] * 0x1000003D1ULL; \
  __uint128_t _p2 = (__uint128_t)(a)[2] * 0x1000003D1ULL; \
  __uint128_t _p3 = (__uint128_t)(a)[3] * 0x1000003D1ULL; \
  (r)[0] = (uint64_t)_p0; \
  __uint128_t _t1 = (_p0 >> 64) + (uint64_t)_p1; \
  (r)[1] = (uint64_t)_t1; \
  __uint128_t _t2 = (_t1 >> 64) + (_p1 >> 64) + (uint64_t)_p2; \
  (r)[2] = (uint64_t)_t2; \
  __uint128_t _t3 = (_t2 >> 64) + (_p2 >> 64) + (uint64_t)_p3; \
  (r)[3] = (uint64_t)_t3; \
  (r)[4] = (uint64_t)(_t3 >> 64) + (uint64_t)(_p3 >> 64); \
}

// ── Modular multiplication (256-bit field mul) ──────────────────────────
__device__ __forceinline__ void _ModMult(uint64_t *r,uint64_t *a,uint64_t *b) {
  uint64_t r512[8],t[NBBLOCK],carry,ah,al;
  r512[5]=0;r512[6]=0;r512[7]=0;
  carry=0; UMult(r512,a,b[0]);
  carry=0; UMult(t,a,b[1]);
  UADDO1(r512[1],t[0]); UADDC1(r512[2],t[1]); UADDC1(r512[3],t[2]);
  UADDC1(r512[4],t[3]); UADD1(r512[5],t[4]);
  carry=0; UMult(t,a,b[2]);
  UADDO1(r512[2],t[0]); UADDC1(r512[3],t[1]); UADDC1(r512[4],t[2]);
  UADDC1(r512[5],t[3]); UADD1(r512[6],t[4]);
  carry=0; UMult(t,a,b[3]);
  UADDO1(r512[3],t[0]); UADDC1(r512[4],t[1]); UADDC1(r512[5],t[2]);
  UADDC1(r512[6],t[3]); UADD1(r512[7],t[4]);
  carry=0; UMultSpecial(t,(r512+4));
  UADDO1(r512[0],t[0]); UADDC1(r512[1],t[1]); UADDC1(r512[2],t[2]); UADDC1(r512[3],t[3]);
  UADD1(t[4],0ULL);
  mul_lo_hi_u64(t[4],0x1000003D1ULL,&al,&ah);
  carry=0; UADDO(r[0],r512[0],al); UADDC(r[1],r512[1],ah);
  UADDC(r[2],r512[2],0ULL); UADD(r[3],r512[3],0ULL);
  // Final reduction: ensure r < p (p[1..3]=0xFF..F, so check upper limbs then r[0])
  if(r[3]==0xFFFFFFFFFFFFFFFFULL && r[2]==0xFFFFFFFFFFFFFFFFULL &&
     r[1]==0xFFFFFFFFFFFFFFFFULL && r[0]>=0xFFFFFFFEFFFFFC2FULL) {
    carry=0; USUBO1(r[0],0xFFFFFFFEFFFFFC2FULL); USUBC1(r[1],0xFFFFFFFFFFFFFFFFULL);
    USUBC1(r[2],0xFFFFFFFFFFFFFFFFULL); USUB1(r[3],0xFFFFFFFFFFFFFFFFULL);
  }
}

__device__ __forceinline__ void _ModMult(uint64_t *r,uint64_t *a) {
  uint64_t r512[8],t[NBBLOCK],carry,ah,al;
  r512[5]=0;r512[6]=0;r512[7]=0;
  carry=0; UMult(r512,a,r[0]);
  carry=0; UMult(t,a,r[1]);
  UADDO1(r512[1],t[0]); UADDC1(r512[2],t[1]); UADDC1(r512[3],t[2]);
  UADDC1(r512[4],t[3]); UADD1(r512[5],t[4]);
  carry=0; UMult(t,a,r[2]);
  UADDO1(r512[2],t[0]); UADDC1(r512[3],t[1]); UADDC1(r512[4],t[2]);
  UADDC1(r512[5],t[3]); UADD1(r512[6],t[4]);
  carry=0; UMult(t,a,r[3]);
  UADDO1(r512[3],t[0]); UADDC1(r512[4],t[1]); UADDC1(r512[5],t[2]);
  UADDC1(r512[6],t[3]); UADD1(r512[7],t[4]);
  carry=0; UMultSpecial(t,(r512+4));
  UADDO1(r512[0],t[0]); UADDC1(r512[1],t[1]); UADDC1(r512[2],t[2]); UADDC1(r512[3],t[3]);
  UADD1(t[4],0ULL);
  mul_lo_hi_u64(t[4],0x1000003D1ULL,&al,&ah);
  carry=0; UADDO(r[0],r512[0],al); UADDC(r[1],r512[1],ah);
  UADDC(r[2],r512[2],0ULL); UADD(r[3],r512[3],0ULL);
  // Final reduction: ensure r < p
  if(r[3]==0xFFFFFFFFFFFFFFFFULL && r[2]==0xFFFFFFFFFFFFFFFFULL &&
     r[1]==0xFFFFFFFFFFFFFFFFULL && r[0]>=0xFFFFFFFEFFFFFC2FULL) {
    carry=0; USUBO1(r[0],0xFFFFFFFEFFFFFC2FULL); USUBC1(r[1],0xFFFFFFFFFFFFFFFFULL);
    USUBC1(r[2],0xFFFFFFFFFFFFFFFFULL); USUB1(r[3],0xFFFFFFFFFFFFFFFFULL);
  }
}

__device__ __forceinline__ void _ModSqr(uint64_t *rp,const uint64_t *up) {
  uint64_t r512[8],SL,SH,r01L,r01H,r02L,r02H,r03L,r03H,carry;
  mul_lo_hi_u64(up[0],up[0],&SL,&SH);
  mul_lo_hi_u64(up[0],up[1],&r01L,&r01H);
  mul_lo_hi_u64(up[0],up[2],&r02L,&r02H);
  mul_lo_hi_u64(up[0],up[3],&r03L,&r03H);
  r512[0]=SL;r512[1]=r01L;r512[2]=r02L;r512[3]=r03L;
  carry=0; UADDO1(r512[1],SH); UADDC1(r512[2],r01H);
  UADDC1(r512[3],r02H); UADD(r512[4],r03H,0ULL);
  uint64_t r12L,r12H,r13L,r13H;
  mul_lo_hi_u64(up[1],up[1],&SL,&SH);
  mul_lo_hi_u64(up[1],up[2],&r12L,&r12H);
  mul_lo_hi_u64(up[1],up[3],&r13L,&r13H);
  carry=0; UADDO1(r512[1],r01L); UADDC1(r512[2],SL); UADDC1(r512[3],r12L);
  UADDC1(r512[4],r13L); UADD(r512[5],r13H,0ULL);
  carry=0; UADDO1(r512[2],r01H); UADDC1(r512[3],SH); UADDC1(r512[4],r12H); UADD1(r512[5],0ULL);
  uint64_t r23L,r23H;
  mul_lo_hi_u64(up[2],up[2],&SL,&SH);
  mul_lo_hi_u64(up[2],up[3],&r23L,&r23H);
  carry=0; UADDO1(r512[2],r02L); UADDC1(r512[3],r12L); UADDC1(r512[4],SL);
  UADDC1(r512[5],r23L); UADD(r512[6],r23H,0ULL);
  carry=0; UADDO1(r512[3],r02H); UADDC1(r512[4],r12H); UADDC1(r512[5],SH); UADD1(r512[6],0ULL);
  mul_lo_hi_u64(up[3],up[3],&SL,&SH);
  carry=0; UADDO1(r512[3],r03L); UADDC1(r512[4],r13L); UADDC1(r512[5],r23L);
  UADDC1(r512[6],SL); UADD(r512[7],SH,0ULL);
  carry=0; UADDO1(r512[4],r03H); UADDC1(r512[5],r13H); UADDC1(r512[6],r23H); UADD1(r512[7],0ULL);
  uint64_t t[NBBLOCK];
  carry=0; UMult(t,(r512+4),0x1000003D1ULL);
  carry=0; UADDO1(r512[0],t[0]); UADDC1(r512[1],t[1]);
  UADDC1(r512[2],t[2]); UADDC1(r512[3],t[3]);
  UADD1(t[4],0ULL);
  mul_lo_hi_u64(t[4],0x1000003D1ULL,&SL,&SH);
  carry=0; UADDO(rp[0],r512[0],SL); UADDC(rp[1],r512[1],SH);
  UADDC(rp[2],r512[2],0ULL); UADD(rp[3],r512[3],0ULL);
  // Final reduction: ensure rp < p
  if(rp[3]==0xFFFFFFFFFFFFFFFFULL && rp[2]==0xFFFFFFFFFFFFFFFFULL &&
     rp[1]==0xFFFFFFFFFFFFFFFFULL && rp[0]>=0xFFFFFFFEFFFFFC2FULL) {
    carry=0; USUBO1(rp[0],0xFFFFFFFEFFFFFC2FULL); USUBC1(rp[1],0xFFFFFFFFFFFFFFFFULL);
    USUBC1(rp[2],0xFFFFFFFFFFFFFFFFULL); USUB1(rp[3],0xFFFFFFFFFFFFFFFFULL);
  }
}

// ── 4×64-bit LE ↔ 5×62-bit signed conversion ─────────────────────────
// _DivStep62 operates on 5-limb signed values where each limb has at
// most 62 significant bits (MSK62 = 0x3FFFFFFFFFFFFFFF). The total
// range is 310 bits signed, giving 54 extra bits for arithmetic.

// Convert 4×64-bit LE (256-bit unsigned) to 5×62-bit signed
__device__ __forceinline__ void to62(const uint64_t src[4], uint64_t dst[5]) {
    dst[0] = src[0] & MSK62;
    dst[1] = ((src[0] >> 62) | (src[1] << 2)) & MSK62;
    dst[2] = ((src[1] >> 60) | (src[2] << 4)) & MSK62;
    dst[3] = ((src[2] >> 58) | (src[3] << 6)) & MSK62;
    // Top 8 bits (248-255) in dst[4], signed extend
    dst[4] = (src[3] >> 56) & 0xFFULL;
    if (dst[4] & 0x80ULL) dst[4] |= 0xFFFFFFFFFFFFFF00ULL;  // sign extend bit 7
}

// Convert 5×62-bit signed to 4×64-bit LE (truncating high bits).
// If src[4] is negative, the 256-bit value is in [2^256 - offset, 2^256).
__device__ __forceinline__ void from62(const uint64_t src[5], uint64_t dst[4]) {
    dst[0] =  (src[0]       | (src[1] << 62));
    dst[1] = ((src[1] >> 2) | (src[2] << 60));
    dst[2] = ((src[2] >> 4) | (src[3] << 58));
    dst[3] = ((src[3] >> 6) | ((uint64_t)((int64_t)src[4] << 56)));
}

// ── 256-bit helper functions for binary GCD ──────────────────────────
__device__ __forceinline__ bool is_even_256(const uint64_t r[4]) {
    return (r[0] & 1ULL) == 0;
}
__device__ __forceinline__ bool is_one_256(const uint64_t r[4]) {
    return r[0]==1 && r[1]==0 && r[2]==0 && r[3]==0;
}
__device__ __forceinline__ bool is_zero_256(const uint64_t r[4]) {
    return (r[0]|r[1]|r[2]|r[3]) == 0ULL;
}
__device__ __forceinline__ bool cmp_ge_256(const uint64_t a[4], const uint64_t b[4]) {
    if (a[3] != b[3]) return a[3] > b[3];
    if (a[2] != b[2]) return a[2] > b[2];
    if (a[1] != b[1]) return a[1] > b[1];
    return a[0] >= b[0];
}
__device__ __forceinline__ void shr1_256(uint64_t r[4]) {
    uint64_t t0=r[0], t1=r[1], t2=r[2], t3=r[3];
    r[0] = (t0 >> 1) | (t1 << 63);
    r[1] = (t1 >> 1) | (t2 << 63);
    r[2] = (t2 >> 1) | (t3 << 63);
    r[3] = t3 >> 1;
}
// r = (r + secp256k1_p) >> 1  (modular halving, assumes r is odd)
__device__ __forceinline__ void add_p_then_shr1(uint64_t r[4]) {
    unsigned long long _c;
    uint64_t carry;
    r[0] = __builtin_addcll(r[0], 0xFFFFFFFEFFFFFC2FULL, 0ULL, &_c); carry = (uint64_t)_c;
    r[1] = __builtin_addcll(r[1], 0xFFFFFFFFFFFFFFFFULL, carry, &_c); carry = (uint64_t)_c;
    r[2] = __builtin_addcll(r[2], 0xFFFFFFFFFFFFFFFFULL, carry, &_c); carry = (uint64_t)_c;
    r[3] = __builtin_addcll(r[3], 0xFFFFFFFFFFFFFFFFULL, carry, &_c); carry = (uint64_t)_c;
    // r = (r + p) >> 1, propagate 257th bit (carry) into r[3] bit 63
    uint64_t t0=r[0], t1=r[1], t2=r[2], t3=r[3];
    r[0] = (t0 >> 1) | (t1 << 63);
    r[1] = (t1 >> 1) | (t2 << 63);
    r[2] = (t2 >> 1) | (t3 << 63);
    r[3] = (t3 >> 1) | (carry << 63);
}

// ── Modular inverse — Binary GCD (extended, 256-bit) ─────────────────
// Standard extended binary GCD on 4×64-bit LE unsigned values.
// Converges in ~256 iterations for 256-bit inputs.
// Uses only shifts, subtractions, and modular field operations.
__device__ __noinline__ void _ModInvGCD(uint64_t R[5]) {
    if (is_zero_256(R)) { R[4]=0; return; }

    uint64_t u[4], v[4];
    uint64_t x1[4] = {1,0,0,0};  // Bezout: x1 * a_orig ≡ ? (mod p)
    uint64_t x2[4] = {0,0,0,0};  // Bezout: x2 * a_orig ≡ ? (mod p)
    uint64_t carry;

    // u = R (input), v = secp256k1 prime p
    u[0]=R[0]; u[1]=R[1]; u[2]=R[2]; u[3]=R[3];
    v[0]=0xFFFFFFFEFFFFFC2FULL; v[1]=0xFFFFFFFFFFFFFFFFULL;
    v[2]=0xFFFFFFFFFFFFFFFFULL; v[3]=0xFFFFFFFFFFFFFFFFULL;

    // Binary GCD extended loop
    // Converges in ~256 iters for 256-bit. Safety limit at 10000.
    int _safe = 0;
    while (!is_one_256(u) && !is_one_256(v) && ++_safe < 10000) {
        // Remove factors of 2 from u
        while (is_even_256(u)) {
            shr1_256(u);
            if (is_even_256(x1)) {
                shr1_256(x1);
            } else {
                add_p_then_shr1(x1);  // x1 = (x1 + p) >> 1
            }
        }
        // Remove factors of 2 from v
        while (is_even_256(v)) {
            shr1_256(v);
            if (is_even_256(x2)) {
                shr1_256(x2);
            } else {
                add_p_then_shr1(x2);  // x2 = (x2 + p) >> 1
            }
        }
        // Subtract: ensure u >= v, then u = u - v
        if (cmp_ge_256(u, v)) {
            carry = 0;
            USUBO(u[0], u[0], v[0]); USUBC(u[1], u[1], v[1]);
            USUBC(u[2], u[2], v[2]); USUBC(u[3], u[3], v[3]);
            // x1 = x1 - x2 (mod p) — inline modular subtraction
            carry = 0;
            USUBO(x1[0], x1[0], x2[0]); USUBC(x1[1], x1[1], x2[1]);
            USUBC(x1[2], x1[2], x2[2]); USUBC(x1[3], x1[3], x2[3]);
            if (carry) {  // borrow: add p back
                unsigned long long _c;
                carry = 0;
                x1[0] = __builtin_addcll(x1[0], 0xFFFFFFFEFFFFFC2FULL, 0ULL, &_c); carry = (uint64_t)_c;
                x1[1] = __builtin_addcll(x1[1], 0xFFFFFFFFFFFFFFFFULL, carry, &_c); carry = (uint64_t)_c;
                x1[2] = __builtin_addcll(x1[2], 0xFFFFFFFFFFFFFFFFULL, carry, &_c); carry = (uint64_t)_c;
                x1[3] = __builtin_addcll(x1[3], 0xFFFFFFFFFFFFFFFFULL, carry, &_c);
            }
        } else {
            carry = 0;
            USUBO(v[0], v[0], u[0]); USUBC(v[1], v[1], u[1]);
            USUBC(v[2], v[2], u[2]); USUBC(v[3], v[3], u[3]);
            // x2 = x2 - x1 (mod p) — inline modular subtraction
            carry = 0;
            USUBO(x2[0], x2[0], x1[0]); USUBC(x2[1], x2[1], x1[1]);
            USUBC(x2[2], x2[2], x1[2]); USUBC(x2[3], x2[3], x1[3]);
            if (carry) {  // borrow: add p back
                unsigned long long _c;
                carry = 0;
                x2[0] = __builtin_addcll(x2[0], 0xFFFFFFFEFFFFFC2FULL, 0ULL, &_c); carry = (uint64_t)_c;
                x2[1] = __builtin_addcll(x2[1], 0xFFFFFFFFFFFFFFFFULL, carry, &_c); carry = (uint64_t)_c;
                x2[2] = __builtin_addcll(x2[2], 0xFFFFFFFFFFFFFFFFULL, carry, &_c); carry = (uint64_t)_c;
                x2[3] = __builtin_addcll(x2[3], 0xFFFFFFFFFFFFFFFFULL, carry, &_c);
            }
        }
    }

    // Result is x1 (if u==1) or x2 (if v==1)
    uint64_t *res = is_one_256(u) ? x1 : x2;
    R[0]=res[0]; R[1]=res[1]; R[2]=res[2]; R[3]=res[3]; R[4]=0;
}

// ── Modular inverse (Fermat exponentiation) ─────────────────────────
// Default. ~500 Mkeys/s on RX 6600. Binary GCD (_ModInvGCD) is also
// available in this file but is slower in practice (1.6× on GPU).
__device__ __noinline__ void _ModInv(uint64_t* R) {
    if (R[0]==0 && R[1]==0 && R[2]==0 && R[3]==0) return;
    uint64_t res[NBBLOCK] = {1,0,0,0,0};
    uint64_t base[NBBLOCK] = {R[0],R[1],R[2],R[3],0};
    uint64_t tmp[NBBLOCK];
    uint64_t exp[4] = {0xFFFFFFFEFFFFFC2DULL,0xFFFFFFFFFFFFFFFFULL,
                       0xFFFFFFFFFFFFFFFFULL,0xFFFFFFFFFFFFFFFFULL};
    bool started = false;
    for (int limb = 3; limb >= 0; limb--) {
        for (int bit = 63; bit >= 0; bit--) {
            bool bit_set = (exp[limb] >> bit) & 1ULL;
            if (!bit_set && !started) continue;
            if (!started) {
                res[0] = base[0]; res[1] = base[1];
                res[2] = base[2]; res[3] = base[3];
                started = true;
                continue;
            }
            _ModSqr(tmp, res);
            res[0]=tmp[0]; res[1]=tmp[1]; res[2]=tmp[2]; res[3]=tmp[3];
            if (bit_set) {
                _ModMult(tmp, res, base);
                res[0]=tmp[0]; res[1]=tmp[1]; res[2]=tmp[2]; res[3]=tmp[3];
            }
        }
    }
    R[0]=res[0]; R[1]=res[1]; R[2]=res[2]; R[3]=res[3]; R[4]=0;
}

// ── Modular inverse (Bernstein-Yang Algorithm 1) ─────────────────────
// Uses eprint 2019/266 Algorithm 1 with Bezout coefficients.
// Tracks u,v,q,r as signed 5-limb values. ~550 iterations worst-case.
// Invariant: f = u*a + v*P, g = q*a + r*P
// When a coefficient is odd and must be divided by 2, we add P to
// the 'a'-coefficient or subtract a from the 'P'-coefficient,
// preserving the invariant since P*a - a*P = 0.
// ── P constant in device memory ──
static __device__ __constant__ const uint64_t BY_P[4] = {
    0xFFFFFFFEFFFFFC2FULL,0xFFFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL,0xFFFFFFFFFFFFFFFFULL
};

__device__ void _ModInvBY(uint64_t* R) {
    if ((R[0]|R[1]|R[2]|R[3])==0ULL) return;

    // Even inputs: inv(a) = P - inv(P-a). P-a is odd, so BY applies.
    bool was_even = false;
    if ((R[0] & 1ULL) == 0ULL) {
        was_even = true;
        uint64_t carry = 0;
        uint64_t n0, n1, n2, n3;
        USUBO(n0, 0xFFFFFFFEFFFFFC2FULL, R[0]); USUBC(n1, 0xFFFFFFFFFFFFFFFFULL, R[1]);
        USUBC(n2, 0xFFFFFFFFFFFFFFFFULL, R[2]); USUBC(n3, 0xFFFFFFFFFFFFFFFFULL, R[3]);
        R[0]=n0; R[1]=n1; R[2]=n2; R[3]=n3; R[4]=0;
    }

    uint64_t a_work[4] = {R[0],R[1],R[2],R[3]};

    // ── State (5-limb signed) ──
    uint64_t f[5] = {R[0],R[1],R[2],R[3],0ULL};
    uint64_t g[5] = {0xFFFFFFFEFFFFFC2FULL,0xFFFFFFFFFFFFFFFFULL,
                     0xFFFFFFFFFFFFFFFFULL,0xFFFFFFFFFFFFFFFFULL,0ULL};
    uint64_t u[5] = {1ULL,0,0,0,0};
    uint64_t v[5] = {0,0,0,0,0};
    uint64_t qv[5] = {0,0,0,0,0};
    uint64_t r[5] = {1ULL,0,0,0,0};

    int32_t delta = 1;

    // Using BY_P[] constant memory

    for (int step = 0; step < 2000; step++) {
        // Check termination: g==0 or f==±1
        uint64_t gz = g[0]|g[1]|g[2]|g[3]|g[4];
        uint64_t f1 = (f[0]==1ULL && (f[1]|f[2]|f[3]|f[4])==0ULL) ? 1ULL : 0ULL;
        uint64_t fm1 = ((f[0] & f[1] & f[2] & f[3] & f[4]) == 0xFFFFFFFFFFFFFFFFULL) ? 1ULL : 0ULL;
        if (gz==0ULL || f1 || fm1) break;

        uint64_t carry;

        if ((g[0] & 1ULL) == 0ULL) {
            // ── g even ──
            // g >>= 1
            g[0] = (g[0]>>1ULL) | (g[1]<<63ULL);
            g[1] = (g[1]>>1ULL) | (g[2]<<63ULL);
            g[2] = (g[2]>>1ULL) | (g[3]<<63ULL);
            g[3] = (g[3]>>1ULL) | (g[4]<<63ULL);
            g[4] = ((int64_t)g[4]) >> 1;

            if (qv[0] & 1ULL) {
                // q = (q + P) >> 1
                UADDO1(qv[0], BY_P[0]); UADDC1(qv[1], BY_P[1]);
                UADDC1(qv[2], BY_P[2]); UADDC1(qv[3], BY_P[3]); UADD1(qv[4], 0ULL);
                qv[0] = (qv[0]>>1ULL) | (qv[1]<<63ULL);
                qv[1] = (qv[1]>>1ULL) | (qv[2]<<63ULL);
                qv[2] = (qv[2]>>1ULL) | (qv[3]<<63ULL);
                qv[3] = (qv[3]>>1ULL) | (qv[4]<<63ULL);
                qv[4] = ((int64_t)qv[4]) >> 1;
                // r = (r - a) >> 1
                USUBO1(r[0], a_work[0]); USUBC1(r[1], a_work[1]);
                USUBC1(r[2], a_work[2]); USUBC1(r[3], a_work[3]); USUB1(r[4], 0ULL);
                r[0] = (r[0]>>1ULL) | (r[1]<<63ULL);
                r[1] = (r[1]>>1ULL) | (r[2]<<63ULL);
                r[2] = (r[2]>>1ULL) | (r[3]<<63ULL);
                r[3] = (r[3]>>1ULL) | (r[4]<<63ULL);
                r[4] = ((int64_t)r[4]) >> 1;
            } else {
                qv[0] = (qv[0]>>1ULL) | (qv[1]<<63ULL);
                qv[1] = (qv[1]>>1ULL) | (qv[2]<<63ULL);
                qv[2] = (qv[2]>>1ULL) | (qv[3]<<63ULL);
                qv[3] = (qv[3]>>1ULL) | (qv[4]<<63ULL);
                qv[4] = ((int64_t)qv[4]) >> 1;
                r[0] = (r[0]>>1ULL) | (r[1]<<63ULL);
                r[1] = (r[1]>>1ULL) | (r[2]<<63ULL);
                r[2] = (r[2]>>1ULL) | (r[3]<<63ULL);
                r[3] = (r[3]>>1ULL) | (r[4]<<63ULL);
                r[4] = ((int64_t)r[4]) >> 1;
            }
            delta++;
        } else if (delta > 0) {
            // ── swap (in-place, 3 temps instead of 6) ──
            // Save old_f, old_u, old_v before overwriting
            uint64_t tf[5], tu[5], tv[5];
            tf[0]=f[0];tf[1]=f[1];tf[2]=f[2];tf[3]=f[3];tf[4]=f[4];
            tu[0]=u[0];tu[1]=u[1];tu[2]=u[2];tu[3]=u[3];tu[4]=u[4];
            tv[0]=v[0];tv[1]=v[1];tv[2]=v[2];tv[3]=v[3];tv[4]=v[4];
            // new_f = old_g, new_u = old_q, new_v = old_r
            f[0]=g[0];f[1]=g[1];f[2]=g[2];f[3]=g[3];f[4]=g[4];
            u[0]=qv[0];u[1]=qv[1];u[2]=qv[2];u[3]=qv[3];u[4]=qv[4];
            v[0]=r[0];v[1]=r[1];v[2]=r[2];v[3]=r[3];v[4]=r[4];
            // new_g = (old_g - old_f) / 2  ← in-place on g
            USUBO(g[0], g[0], tf[0]); USUBC(g[1], g[1], tf[1]);
            USUBC(g[2], g[2], tf[2]); USUBC(g[3], g[3], tf[3]);
            USUBC(g[4], g[4], tf[4]);
            g[0]=(g[0]>>1ULL)|(g[1]<<63ULL);g[1]=(g[1]>>1ULL)|(g[2]<<63ULL);
            g[2]=(g[2]>>1ULL)|(g[3]<<63ULL);g[3]=(g[3]>>1ULL)|(g[4]<<63ULL);
            g[4]=((int64_t)g[4])>>1;
            // new_q = (old_q - old_u) / 2  ← in-place on qv
            USUBO(qv[0], qv[0], tu[0]); USUBC(qv[1], qv[1], tu[1]);
            USUBC(qv[2], qv[2], tu[2]); USUBC(qv[3], qv[3], tu[3]);
            USUBC(qv[4], qv[4], tu[4]);
            // new_r = (old_r - old_v) / 2  ← in-place on r
            USUBO(r[0], r[0], tv[0]); USUBC(r[1], r[1], tv[1]);
            USUBC(r[2], r[2], tv[2]); USUBC(r[3], r[3], tv[3]);
            USUBC(r[4], r[4], tv[4]);
            if (qv[0] & 1ULL) {
                carry=0; UADDO1(qv[0], BY_P[0]); UADDC1(qv[1], BY_P[1]);
                UADDC1(qv[2], BY_P[2]); UADDC1(qv[3], BY_P[3]); UADD1(qv[4], 0ULL);
                qv[0]=(qv[0]>>1ULL)|(qv[1]<<63ULL);qv[1]=(qv[1]>>1ULL)|(qv[2]<<63ULL);
                qv[2]=(qv[2]>>1ULL)|(qv[3]<<63ULL);qv[3]=(qv[3]>>1ULL)|(qv[4]<<63ULL);
                qv[4]=((int64_t)qv[4])>>1;
                carry=0; USUBO1(r[0], a_work[0]); USUBC1(r[1], a_work[1]);
                USUBC1(r[2], a_work[2]); USUBC1(r[3], a_work[3]); USUB1(r[4], 0ULL);
                r[0]=(r[0]>>1ULL)|(r[1]<<63ULL);r[1]=(r[1]>>1ULL)|(r[2]<<63ULL);
                r[2]=(r[2]>>1ULL)|(r[3]<<63ULL);r[3]=(r[3]>>1ULL)|(r[4]<<63ULL);
                r[4]=((int64_t)r[4])>>1;
            } else {
                qv[0]=(qv[0]>>1ULL)|(qv[1]<<63ULL);qv[1]=(qv[1]>>1ULL)|(qv[2]<<63ULL);
                qv[2]=(qv[2]>>1ULL)|(qv[3]<<63ULL);qv[3]=(qv[3]>>1ULL)|(qv[4]<<63ULL);
                qv[4]=((int64_t)qv[4])>>1;
                r[0]=(r[0]>>1ULL)|(r[1]<<63ULL);r[1]=(r[1]>>1ULL)|(r[2]<<63ULL);
                r[2]=(r[2]>>1ULL)|(r[3]<<63ULL);r[3]=(r[3]>>1ULL)|(r[4]<<63ULL);
                r[4]=((int64_t)r[4])>>1;
            }
            delta = 1 - delta;
        } else {
            // ── no-swap (in-place, no temporaries) ──
            // g = (g+f)/2
            UADDO(g[0], g[0], f[0]); UADDC(g[1], g[1], f[1]);
            UADDC(g[2], g[2], f[2]); UADDC(g[3], g[3], f[3]);
            UADDC(g[4], g[4], f[4]);
            g[0]=(g[0]>>1ULL)|(g[1]<<63ULL);g[1]=(g[1]>>1ULL)|(g[2]<<63ULL);
            g[2]=(g[2]>>1ULL)|(g[3]<<63ULL);g[3]=(g[3]>>1ULL)|(g[4]<<63ULL);
            g[4]=((int64_t)g[4])>>1;
            // q = (q+u)/2  (in-place)
            UADDO(qv[0], qv[0], u[0]); UADDC(qv[1], qv[1], u[1]);
            UADDC(qv[2], qv[2], u[2]); UADDC(qv[3], qv[3], u[3]);
            UADDC(qv[4], qv[4], u[4]);
            // r = (r+v)/2  (in-place)
            UADDO(r[0], r[0], v[0]); UADDC(r[1], r[1], v[1]);
            UADDC(r[2], r[2], v[2]); UADDC(r[3], r[3], v[3]);
            UADDC(r[4], r[4], v[4]);
            if (qv[0] & 1ULL) {
                carry=0; UADDO1(qv[0], BY_P[0]); UADDC1(qv[1], BY_P[1]);
                UADDC1(qv[2], BY_P[2]); UADDC1(qv[3], BY_P[3]); UADD1(qv[4], 0ULL);
                qv[0]=(qv[0]>>1ULL)|(qv[1]<<63ULL);qv[1]=(qv[1]>>1ULL)|(qv[2]<<63ULL);
                qv[2]=(qv[2]>>1ULL)|(qv[3]<<63ULL);qv[3]=(qv[3]>>1ULL)|(qv[4]<<63ULL);
                qv[4]=((int64_t)qv[4])>>1;
                carry=0; USUBO1(r[0], a_work[0]); USUBC1(r[1], a_work[1]);
                USUBC1(r[2], a_work[2]); USUBC1(r[3], a_work[3]); USUB1(r[4], 0ULL);
                r[0]=(r[0]>>1ULL)|(r[1]<<63ULL);r[1]=(r[1]>>1ULL)|(r[2]<<63ULL);
                r[2]=(r[2]>>1ULL)|(r[3]<<63ULL);r[3]=(r[3]>>1ULL)|(r[4]<<63ULL);
                r[4]=((int64_t)r[4])>>1;
            } else {
                qv[0]=(qv[0]>>1ULL)|(qv[1]<<63ULL);qv[1]=(qv[1]>>1ULL)|(qv[2]<<63ULL);
                qv[2]=(qv[2]>>1ULL)|(qv[3]<<63ULL);qv[3]=(qv[3]>>1ULL)|(qv[4]<<63ULL);
                qv[4]=((int64_t)qv[4])>>1;
                r[0]=(r[0]>>1ULL)|(r[1]<<63ULL);r[1]=(r[1]>>1ULL)|(r[2]<<63ULL);
                r[2]=(r[2]>>1ULL)|(r[3]<<63ULL);r[3]=(r[3]>>1ULL)|(r[4]<<63ULL);
                r[4]=((int64_t)r[4])>>1;
            }
            delta++;
        }
    }

    // ── Extract inverse ──
    // If f = 1: inverse = u (mod P)
    // If f = -1: inverse = -u (mod P)
    // If g = 1: inverse = q (mod P)
    // If g = -1: inverse = -q (mod P)

    uint64_t inv[5];
    uint64_t f_is_one = (f[0]==1ULL && (f[1]|f[2]|f[3]|f[4])==0ULL) ? 1ULL : 0ULL;
    uint64_t f_is_mone = ((f[0] & f[1] & f[2] & f[3] & f[4]) == 0xFFFFFFFFFFFFFFFFULL) ? 1ULL : 0ULL;
    uint64_t g_is_one = (g[0]==1ULL && (g[1]|g[2]|g[3]|g[4])==0ULL) ? 1ULL : 0ULL;
    uint64_t g_is_mone = ((g[0] & g[1] & g[2] & g[3] & g[4]) == 0xFFFFFFFFFFFFFFFFULL) ? 1ULL : 0ULL;

    if (f_is_one) {
        inv[0]=u[0];inv[1]=u[1];inv[2]=u[2];inv[3]=u[3];inv[4]=u[4];
    } else if (f_is_mone) {
        uint64_t carry;
        USUBO(inv[0],0ULL,u[0]);USUBC(inv[1],0ULL,u[1]);
        USUBC(inv[2],0ULL,u[2]);USUBC(inv[3],0ULL,u[3]);USUBC(inv[4],0ULL,u[4]);
    } else if (g_is_one) {
        inv[0]=qv[0];inv[1]=qv[1];inv[2]=qv[2];inv[3]=qv[3];inv[4]=qv[4];
    } else {
        uint64_t carry;
        USUBO(inv[0],0ULL,qv[0]);USUBC(inv[1],0ULL,qv[1]);
        USUBC(inv[2],0ULL,qv[2]);USUBC(inv[3],0ULL,qv[3]);USUBC(inv[4],0ULL,qv[4]);
    }

    // Reduce 5-limb inverse to 4-limb (mod P)
    // ARMADILLO: inv mod P = inv[0..3] + signed(inv[4]) * C256
    uint64_t carry;
    int64_t sinv4 = (int64_t)inv[4];
    if (sinv4 >= 0) {
        // Positive: ADD sinv4 * C256 (unsigned)
        __uint128_t p = (__uint128_t)sinv4 * 0x1000003D1ULL;
        uint64_t plo = (uint64_t)p, phi = (uint64_t)(p>>64);
        UADDO(R[0], inv[0], plo); UADDC(R[1], inv[1], phi);
        UADDC(R[2], inv[2], 0ULL); UADDC(R[3], inv[3], 0ULL);
        for (int k=0; k<3 && carry; k++) {
            p = (__uint128_t)carry * 0x1000003D1ULL;
            plo = (uint64_t)p; phi = (uint64_t)(p>>64);
            carry=0;
            UADDO1(R[0], plo); UADDC1(R[1], phi);
            UADDC1(R[2], 0ULL); UADD1(R[3], 0ULL);
        }
    } else {
        // Negative: SUBTRACT |sinv4| * C256
        uint64_t absv = (uint64_t)(-sinv4);
        __uint128_t p = (__uint128_t)absv * 0x1000003D1ULL;
        uint64_t plo = (uint64_t)p, phi = (uint64_t)(p>>64);
        USUBO(R[0], inv[0], plo); USUBC(R[1], inv[1], phi);
        USUBC(R[2], inv[2], 0ULL); USUBC(R[3], inv[3], 0ULL);
    }
    // Subtract P if >= P
    uint64_t t[4];
    USUBO(t[0], R[0], BY_P[0]); USUBC(t[1], R[1], BY_P[1]);
    USUBC(t[2], R[2], BY_P[2]); USUBC(t[3], R[3], BY_P[3]);
    uint64_t borrow = carry;
    if (borrow==0ULL) { R[0]=t[0];R[1]=t[1];R[2]=t[2];R[3]=t[3]; }

    // If input was even, result so far is inv(P-a). Final: inv(a) = P - inv(P-a).
    if (was_even) {
        uint64_t carry = 0;
        uint64_t n0, n1, n2, n3;
        USUBO(n0, 0xFFFFFFFEFFFFFC2FULL, R[0]); USUBC(n1, 0xFFFFFFFFFFFFFFFFULL, R[1]);
        USUBC(n2, 0xFFFFFFFFFFFFFFFFULL, R[2]); USUBC(n3, 0xFFFFFFFFFFFFFFFFULL, R[3]);
        R[0]=n0; R[1]=n1; R[2]=n2; R[3]=n3;
    }

    R[4] = 0ULL;
}

// ── Field inversion (Bernstein-Yang Algorithm 1) ──────────────────────
__device__ void fieldInv(const uint64_t in[4], uint64_t out[4]) {
    uint64_t t[5]={in[0],in[1],in[2],in[3],0};
    _ModInvBY(t);
    out[0]=t[0];out[1]=t[1];out[2]=t[2];out[3]=t[3];
}

// ── Secp256k1 constants ────────────────────────────────────────────────
static __device__ const uint64_t SECP_P_LE[4] = {
    0xFFFFFFFEFFFFFC2FULL,0xFFFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL,0xFFFFFFFFFFFFFFFFULL};
static __device__ const uint64_t SECP_GX_LE[4] = {
    0x59f2815b16f81798ULL,0x029bfcdb2dce28d9ULL,
    0x55a06295ce870b07ULL,0x79be667ef9dcbbacULL};
static __device__ const uint64_t SECP_GY_LE[4] = {
    0x9c47d08ffb10d4b8ULL,0xfd17b448a6855419ULL,
    0x5da4fbfc0e1108a8ULL,0x483ada7726a3c465ULL};

// ── Field ops ───────────────────────────────────────────────────────────
__device__ __forceinline__ void fieldCopy(const uint64_t a[4],uint64_t out[4]) {
    out[0]=a[0];out[1]=a[1];out[2]=a[2];out[3]=a[3];}
__device__ __forceinline__ bool fieldIsZero(const uint64_t a[4]) {
    return ((a[0]|a[1]|a[2]|a[3])==0ULL);}

__device__ void fieldAdd(const uint64_t a[4],const uint64_t b[4],uint64_t out[4]) {
    uint64_t carry=0;
    for(int i=0;i<4;++i){
        uint64_t s=a[i]+b[i];uint64_t c=(s<a[i])?1ULL:0ULL;
        s+=carry;if(s<carry)c=1ULL;out[i]=s;carry=c;}
    if(carry||out[3]>SECP_P_LE[3]||
      (out[3]==SECP_P_LE[3]&&out[2]>SECP_P_LE[2])||
      (out[3]==SECP_P_LE[3]&&out[2]==SECP_P_LE[2]&&out[1]>SECP_P_LE[1])||
      (out[3]==SECP_P_LE[3]&&out[2]==SECP_P_LE[2]&&out[1]==SECP_P_LE[1]&&out[0]>=SECP_P_LE[0])){
        uint64_t borrow=0;
        for(int i=0;i<4;++i){
            uint64_t d=out[i]-borrow;uint64_t nb=(d>out[i])?1ULL:0ULL;
            uint64_t d2=d-SECP_P_LE[i];if(d2>d)nb=1ULL;out[i]=d2;borrow=nb;}}
}

__device__ void fieldSub(const uint64_t a[4],const uint64_t b[4],uint64_t out[4]) {
    uint64_t borrow=0;
    for(int i=0;i<4;++i){
        uint64_t d=a[i]-borrow;uint64_t nb=(d>a[i])?1ULL:0ULL;
        uint64_t d2=d-b[i];if(d2>d)nb=1ULL;out[i]=d2;borrow=nb;}
    if(borrow){
        uint64_t carry=0;
        for(int i=0;i<4;++i){
            uint64_t s=out[i]+SECP_P_LE[i];uint64_t c=(s<out[i])?1ULL:0ULL;
            s+=carry;if(s<carry)c=1ULL;out[i]=s;carry=c;}}
}

__device__ void fieldNeg(const uint64_t a[4],uint64_t out[4]) {
    if(fieldIsZero(a)){out[0]=out[1]=out[2]=out[3]=0ULL;return;}
    fieldSub(SECP_P_LE,a,out);}
__device__ __forceinline__ void fieldMul(const uint64_t a[4],const uint64_t b[4],uint64_t out[4]){
    _ModMult(out,(uint64_t*)a,(uint64_t*)b);}
__device__ __forceinline__ void fieldSqr(const uint64_t a[4],uint64_t out[4]){
    _ModSqr(out,a);}

// ── EC point affine ────────────────────────────────────────────────────
struct ECPointA { uint64_t X[4],Y[4]; bool infinity; };

__device__ __forceinline__ void pointSetInfinity(ECPointA&P){
    P.infinity=true;P.X[0]=P.X[1]=P.X[2]=P.X[3]=0ULL;
    P.Y[0]=P.Y[1]=P.Y[2]=P.Y[3]=0ULL;}
__device__ __forceinline__ void pointSetG(ECPointA&P){
    P.infinity=false;
    P.X[0]=SECP_GX_LE[0];P.X[1]=SECP_GX_LE[1];P.X[2]=SECP_GX_LE[2];P.X[3]=SECP_GX_LE[3];
    P.Y[0]=SECP_GY_LE[0];P.Y[1]=SECP_GY_LE[1];P.Y[2]=SECP_GY_LE[2];P.Y[3]=SECP_GY_LE[3];}

__device__ void pointDoubleAffine(const ECPointA&P,ECPointA&R){
    if(P.infinity){pointSetInfinity(R);return;}
    uint64_t x2[4],two_x2[4],three_x2[4],denom[4],invDen[4],lambda[4];
    fieldSqr(P.X,x2); fieldAdd(x2,x2,two_x2); fieldAdd(two_x2,x2,three_x2);
    fieldAdd(P.Y,P.Y,denom); fieldInv(denom,invDen); fieldMul(three_x2,invDen,lambda);
    uint64_t lambda2[4],twoX[4],newX[4];
    fieldSqr(lambda,lambda2); fieldAdd(P.X,P.X,twoX); fieldSub(lambda2,twoX,newX);
    uint64_t tmp[4],prod[4],newY[4];
    fieldSub(P.X,newX,tmp); fieldMul(lambda,tmp,prod); fieldSub(prod,P.Y,newY);
    fieldCopy(newX,R.X); fieldCopy(newY,R.Y); R.infinity=false;}

__device__ void pointAddAffine(const ECPointA&P,const ECPointA&Q,ECPointA&R){
    if(P.infinity){R=Q;return;} if(Q.infinity){R=P;return;}
    bool sameX=(P.X[0]==Q.X[0]&&P.X[1]==Q.X[1]&&P.X[2]==Q.X[2]&&P.X[3]==Q.X[3]);
    bool sameY=(P.Y[0]==Q.Y[0]&&P.Y[1]==Q.Y[1]&&P.Y[2]==Q.Y[2]&&P.Y[3]==Q.Y[3]);
    if(sameX&&sameY){pointDoubleAffine(P,R);return;}
    if(sameX&&!sameY){pointSetInfinity(R);return;}
    uint64_t dx[4],dy[4],invdx[4],lambda[4],lambda2[4],tmp1[4],prod[4],newX[4],newY[4];
    fieldSub(Q.X,P.X,dx); fieldSub(Q.Y,P.Y,dy); fieldInv(dx,invdx); fieldMul(dy,invdx,lambda);
    fieldSqr(lambda,lambda2); fieldSub(lambda2,P.X,tmp1); fieldSub(tmp1,Q.X,newX);
    fieldSub(P.X,newX,tmp1); fieldMul(lambda,tmp1,prod); fieldSub(prod,P.Y,newY);
    fieldCopy(newX,R.X); fieldCopy(newY,R.Y); R.infinity=false;}

__device__ void scalarMulBaseAffine(const uint64_t scalar_le[4],uint64_t outX[4],uint64_t outY[4]){
    ECPointA R; pointSetInfinity(R);
    int msb=-1;
    for(int limb=3;limb>=0;--limb){
        uint64_t v=scalar_le[limb];
        if(v!=0){msb=limb*64+63-__clzll(v);break;}}
    if(msb==-1){outX[0]=outX[1]=outX[2]=outX[3]=0ULL;outY[0]=outY[1]=outY[2]=outY[3]=0ULL;return;}
    for(int bi=msb;bi>=0;--bi){
        if(!R.infinity){ECPointA t;pointDoubleAffine(R,t);R=t;}
        int limb=bi>>6;int shift=bi&63;
        uint64_t bit=(scalar_le[limb]>>shift)&1ULL;
        if(bit){ECPointA Gp;pointSetG(Gp);
          if(R.infinity)R=Gp;else{ECPointA t;pointAddAffine(R,Gp,t);R=t;}}}
    if(R.infinity){outX[0]=outX[1]=outX[2]=outX[3]=0ULL;outY[0]=outY[1]=outY[2]=outY[3]=0ULL;}
    else{fieldCopy(R.X,outX);fieldCopy(R.Y,outY);}}

__global__ void scalarMulKernelBase(const uint64_t* scalars_in,uint64_t* outX,uint64_t* outY,int N){
    int idx=blockIdx.x*blockDim.x+threadIdx.x;
    if(idx>=N)return;
    const uint64_t* scalar=scalars_in+idx*4;
    uint64_t* outx=outX+idx*4;uint64_t* outy=outY+idx*4;
    scalarMulBaseAffine(scalar,outx,outy);}

#endif // AMD_MATH_H
