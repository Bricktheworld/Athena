#pragma once
#include "../types.h"
#include "../memory/memory.h"
#include <cmath>
#include <random>

static constexpr f32 kPI  = 3.1415926535897932f;
static constexpr f32 k2PI = 6.2831853071795864f;

inline f32x4 pass_by_register 
operator+(f32x4 a, f32x4 b)
{
  return _mm_add_ps(a, b);
}

inline f32x4& pass_by_register 
operator+=(f32x4& a, f32x4 b)
{
  a = a + b;
  return a;
}

inline f32x4 pass_by_register 
operator-(f32x4 a, f32x4 b)
{
  return _mm_sub_ps(a, b);
}

inline f32x4& pass_by_register 
operator-(f32x4& a)
{
  a = _mm_setzero_ps() - a;
  return a;
}

inline f32x4& pass_by_register 
operator-=(f32x4& a, f32x4 b)
{
  a = a - b;
  return a;
}

inline f32x4 pass_by_register 
operator/(f32x4 a, f32x4 b)
{
  return _mm_div_ps(a, b);
}

inline f32x4& pass_by_register 
operator/=(f32x4& a, f32x4 b)
{
  a = a / b;
  return a;
}

inline f32x4 pass_by_register 
operator/(f32x4 a, f32 scale)
{
  return a / _mm_set1_ps(scale);
}

inline f32x4& pass_by_register 
operator/=(f32x4& a, f32 scale)
{
  a = a / _mm_set1_ps(scale);
  return a;
}

inline f32x4 pass_by_register 
operator*(f32x4 a, f32 scale)
{
  return _mm_mul_ps(a, _mm_set1_ps(scale));
}

inline f32x4& pass_by_register 
operator*=(f32x4& a, f32 scale)
{
  a = _mm_mul_ps(a, _mm_set1_ps(scale));
  return a;
}

inline f32x4 pass_by_register 
operator*(f32 scale, f32x4 a)
{
  return _mm_mul_ps(_mm_set1_ps(scale), a);
}

inline f32x4 pass_by_register 
hadamard_f32(f32x4 a, f32x4 b)
{
  return _mm_mul_ps(a, b);
}

inline f32 pass_by_register 
dot_f32(f32x4 a, f32x4 b)
{
  f32x4 res = hadamard_f32(a, b);
  f32 vals[4];
  _mm_store_ps(vals, res);

  return vals[0] + vals[1] + vals[2] + vals[3];
}

inline f32 pass_by_register 
length_f32(f32x4 v)
{
  return sqrt(dot_f32(v, v));
}

inline f32x4 pass_by_register
normalize_f32(f32x4 v)
{
  return v / sqrt(dot_f32(v, v));
}

template <typename T>
inline void
dot_f32_arrays_x4(const T* a,
                              const T* b,
                              size_t count,
                              f32* out)
{
  static_assert(sizeof(T) == sizeof(f32x4));

  ASSERT(count % 4 == 0);

  for (size_t i = 0; i < count; i += 4)
  {
    f32x4 a_x = a[i];
    f32x4 a_y = a[i];
    f32x4 a_z = a[i];
    f32x4 a_w = a[i];

    f32x4 b_x = b[i];
    f32x4 b_y = b[i];
    f32x4 b_z = b[i];
    f32x4 b_w = b[i];

    _MM_TRANSPOSE4_PS(a_x, a_y, a_z, a_w);
    _MM_TRANSPOSE4_PS(b_x, b_y, b_z, b_w);

    f32x4 res;
    res = hadamard_f32(a_x, b_x);
    res = res + hadamard_f32(a_y, b_y);
    res = res + hadamard_f32(a_z, a_z);
    res = res + hadamard_f32(a_w, a_w);

    _mm_store_ps(&out[i], res);
  }
}

// This method will produce garbage in the w component
// since the cross product is only defined for 3-component
// vectors.
inline f32x4 pass_by_register
cross_f32(f32x4 a, f32x4 b)
{
  //         [a.y * b.z - a.z * b.y]
  // a x b = [a.z * b.x - a.x * b.z]
  //         [a.x * b.y - a.y * b.x]
  //
  // Notice that we can split this up into two parts:
  // [a.y * b.z - a.z * b.y]   [a.y * b.z] - [a.z * b.y]
  // [a.z * b.x - a.x * b.z] = [a.z * b.x] - [a.x * b.z]
  // [a.x * b.y - a.y * b.x]   [a.x * b.y] - [a.y * b.x]
  //
  //            [a.y]
  // let tmp0 = [a.z]
  //            [a.x]
    // and
  //            [b.z]
  // let tmp1 = [b.x]
  //            [b.y]
  //
  // Then, we can perform some multiplication that, 
  // while not yet in the correct rows, is equivalent
  // to that in our cross product.
  //
    //            [a.y * b.x]
  // let tmp2 = [a.z * b.y] = hadamard_f32(tmp0, b)
  //            [a.x * b.z]
  //
  // This is equivalent to the second component of our
  // cross product subtraction but with the rows re-arranged.
  // 
  // [a.z * b.y]                                 [a.y * b.x]
  // [a.x * b.z] --(Move last row to the top)--> [a.z * b.y]
  // [a.y * b.x]                                 [a.x * b.z]
  // 
  // So we can say:
  // tmp2 = _mm_shuffle_ps(tmp2, tmp2, _MM_SHUFFLE(3, 0, 2, 1))
  //
    //            [a.y * b.z]
  // let tmp3 = [a.z * b.x] = hadamard_f32(tmp0, tmp1)
  //            [a.x * b.y]
  // 
  // This is _exactly_ our first component of the cross product
  // subtraction.
  // So now all we need to do is 
  // let result = tmp3 - tmp2
  f32x4 tmp0 = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));
  f32x4 tmp1 = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 1, 0, 2));
  f32x4 tmp2 = hadamard_f32(tmp0, b);
  f32x4 tmp3 = hadamard_f32(tmp0, tmp1);

  tmp2 = _mm_shuffle_ps(tmp2, tmp2, _MM_SHUFFLE(3, 0, 2, 1));
  return tmp3 - tmp2;
}

struct alignas(16) Vec2
{
  Vec2() : avx(_mm_setzero_ps()) {}
  Vec2(f32 all) : avx(_mm_set_ps(0.0f, 0.0f, all, all)) {}
  Vec2(f32 x, f32 y) : avx(_mm_set_ps(0.0f, 0.0f, y, x)) {}
  Vec2(f32x4 val) : avx(val) {}

  operator f32x4() const
  {
    return avx;
  }

  union
  {
    struct
    {
      union
      {
        f32 x;
        f32 r;
      };
      union
      {
        f32 y;
        f32 g;
      };
    };
    f32x4 avx;
  };
};

inline f32 pass_by_register
dot_f32(Vec2 a, Vec2 b)
{
  Vec2 res = hadamard_f32(a, b);

  return res.x + res.y;
}

inline Vec2 pass_by_register
operator+(Vec2 a, Vec2 b)
{
  return a.avx + b.avx;
}

inline Vec2 pass_by_register
operator-(Vec2 a, Vec2 b)
{
  return a.avx - b.avx;
}

inline Vec2& pass_by_register
operator-(Vec2& a)
{
  a.avx = -a.avx;
  return a;
}

inline Vec2& pass_by_register
operator+=(Vec2& a, Vec2 b)
{
  a.avx = a.avx + b.avx;
  return a;
}

inline Vec2& pass_by_register
operator-=(Vec2& a, Vec2 b)
{
  a.avx = a.avx - b.avx;
  return a;
}

inline Vec2 pass_by_register
operator*(Vec2 a, f32 scale)
{
  return a.avx * scale;
}

inline Vec2& pass_by_register
operator*=(Vec2& a, f32 scale)
{
  a.avx = a.avx * scale;
  return a;
}

inline Vec2 pass_by_register
operator/(Vec2 a, f32 scale)
{
  return a.avx / scale;
}

inline Vec2& pass_by_register
operator/=(Vec2& a, f32 scale)
{
  a.avx = a.avx / scale;
  return a;
}

struct alignas(16) Vec3
{
  Vec3() : avx(_mm_setzero_ps()) {}
  Vec3(f32 all) : avx(_mm_set_ps(0.0f, all, all, all)) {}
  Vec3(f32 x, f32 y, f32 z) : avx(_mm_set_ps(0.0f, z, y, x)) {}
  Vec3(Vec2 v, f32 z = 0.0, f32 w = 0.0) : avx(_mm_set_ps(w, z, v.y, v.x)) {}
  Vec3(f32x4 val) : avx(val) {}

  operator f32x4() const
  {
    return avx;
  }

  union
  {
    struct
    {
      union
      {
        f32 x;
        f32 r;
      };
      union
      {
        f32 y;
        f32 g;
      };
      union
      {
        f32 z;
        f32 b;
      };
    };
    f32x4 avx;
  };
};

inline Vec3 pass_by_register operator+(Vec3 a, Vec3 b)
{
  return a.avx + b.avx;
}

inline Vec3 pass_by_register operator-(Vec3 a, Vec3 b)
{
  return a.avx - b.avx;
}

inline Vec3& pass_by_register operator-(Vec3& a)
{
  a.avx = -a.avx;
  return a;
}

inline Vec3& pass_by_register operator+=(Vec3& a, Vec3 b)
{
  a.avx = a.avx + b.avx;
  return a;
}

inline Vec3& pass_by_register operator-=(Vec3& a, Vec3 b)
{
  a.avx = a.avx - b.avx;
  return a;
}

inline Vec3 pass_by_register operator*(Vec3 a, f32 scale)
{
  return a.avx * scale;
}

inline Vec3& pass_by_register operator*=(Vec3& a, f32 scale)
{
  a.avx = a.avx * scale;
  return a;
}

inline Vec3 pass_by_register operator/(Vec3 a, f32 scale)
{
  return a.avx / scale;
}

inline Vec3& pass_by_register operator/=(Vec3& a, f32 scale)
{
  a.avx = a.avx / scale;
  return a;
}

struct alignas(16) Vec4
{
  Vec4() : avx(_mm_setzero_ps()) {}
  Vec4(f32 all) : avx(_mm_set_ps(all, all, all, all)) {}
  Vec4(f32 x, f32 y, f32 z, f32 w) : avx(_mm_set_ps(w, z, y, x)) {}
  Vec4(Vec2 v, f32 z = 0.0, f32 w = 1.0) : avx(_mm_set_ps(w, z, v.y, v.x)) {}
  Vec4(Vec3 v, f32 w = 1.0) : avx(_mm_set_ps(w, v.z, v.y, v.x)) {}
  Vec4(f32x4 val) : avx(val) {}

  operator f32x4() const
  {
    return avx;
  }

  union
  {
    struct
    {
      union
      {
        f32 x;
        f32 r;
      };
      union
      {
        f32 y;
        f32 g;
      };
      union
      {
        f32 z;
        f32 b;
      };
      union
      {
        f32 w;
        f32 a;
      };
    };
    f32x4 avx;
  };
};

inline f32 pass_by_register dot_f32(Vec3 a, Vec3 b)
{
  Vec3 res = hadamard_f32(a, b);

  return res.x + res.y + res.z;
}

static_assert(sizeof(Vec4) == sizeof(f32) * 4);

inline Vec4 pass_by_register operator+(Vec4 a, Vec4 b)
{
  return a.avx + b.avx;
}

inline Vec4 pass_by_register operator-(Vec4 a, Vec4 b)
{
  return a.avx - b.avx;
}

inline Vec4& pass_by_register operator-(Vec4& a)
{
  a.avx = -a.avx;
  return a;
}

inline Vec4& pass_by_register operator+=(Vec4& a, Vec4 b)
{
  a.avx = a.avx + b.avx;
  return a;
}

inline Vec4& pass_by_register operator-=(Vec4& a, Vec4 b)
{
  a.avx = a.avx - b.avx;
  return a;
}

inline Vec4 pass_by_register operator*(Vec4 a, f32 scale)
{
  return a.avx * scale;
}

inline Vec4& pass_by_register operator*=(Vec4& a, f32 scale)
{
  a.avx = a.avx * scale;
  return a;
}
inline Vec4 pass_by_register operator/(Vec4 a, f32 scale)
{
  return a.avx / scale;
}

inline Vec4& pass_by_register operator/=(Vec4& a, f32 scale)
{
  a.avx = a.avx / scale;
  return a;
}

// Matrix is stored in column major order.
struct alignas(16) Mat4
{
  Mat4()
  {
    // This looks stupid but it's actually correct.
    // This will default construct the matrix as identity
    cols[0] = _mm_set_ps(0, 0, 0, 1);
    cols[1] = _mm_set_ps(0, 0, 1, 0);
    cols[2] = _mm_set_ps(0, 1, 0, 0);
    cols[3] = _mm_set_ps(1, 0, 0, 0);
  }

  static Mat4 columns(f32x4 col0, f32x4 col1, f32x4 col2, f32x4 col3)
  {
    Mat4 m;
    m.cols[0] = col0;
    m.cols[1] = col1;
    m.cols[2] = col2;
    m.cols[3] = col3;
    return m;
  }

  static Mat4 rows(f32x4 row0, f32x4 row1, f32x4 row2, f32x4 row3)
  {
    _MM_TRANSPOSE4_PS(row0, row1, row2, row3);
    return columns(row0, row1, row2, row3);
  }

  union
  {
    // To access element -> Mat4.[column][row];
    f32 entries[4][4];
    f32x4 cols[4];
  };
};

static_assert(sizeof(Mat4) == sizeof(f32) * 4 * 4);

inline Mat4 transpose_f32(Mat4 m)
{
  _MM_TRANSPOSE4_PS(m.cols[0], m.cols[1], m.cols[2], m.cols[3]);
  return m;
}

inline f32x4 pass_by_register operator*(Mat4 a, f32x4 v)
{
  // Broadcast components.
  f32x4 v_x = _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 0));
  f32x4 v_y = _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1));
  f32x4 v_z = _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2));
  f32x4 v_w = _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 3, 3, 3));

  return hadamard_f32(v_x, a.cols[0])
       + hadamard_f32(v_y, a.cols[1])
       + hadamard_f32(v_z, a.cols[2])
       + hadamard_f32(v_w, a.cols[3]);
}

// If you want to treat your matrix as row major, then a transpose will be performed
// to transform into col major and then the same calculation will occur.
// NOTE(Brandon): This has a performance implication, so use this sparingly.
inline f32x4 pass_by_register operator*(f32x4 v, Mat4 a)
{
  _MM_TRANSPOSE4_PS(a.cols[0], a.cols[1], a.cols[2], a.cols[3]);
  return a * v;
}

inline Mat4 operator*(Mat4 a, Mat4 b)
{
  Mat4 res;
  // Column-wise matrix multiplication
  res.cols[0] = a * b.cols[0];
  res.cols[1] = a * b.cols[1];
  res.cols[2] = a * b.cols[2];
  res.cols[3] = a * b.cols[3];
  return res;
}

inline Mat4& operator*=(Mat4& a, Mat4 b)
{
  a = a * b;
  return a;
}

inline Mat4 operator*(Mat4 a, f32 scale)
{
  Mat4 res;
  res.cols[0] *= scale;
  res.cols[1] *= scale;
  res.cols[2] *= scale;
  res.cols[3] *= scale;
  return res;
}

inline Mat4& operator*=(Mat4& a, f32 scale)
{
  a = a * scale;
  return a;
}

inline Mat4 operator+(Mat4 a, Mat4 b)
{
  Mat4 res;
  res.cols[0] = a.cols[0] + b.cols[0];
  res.cols[1] = a.cols[1] + b.cols[1];
  res.cols[2] = a.cols[2] + b.cols[2];
  res.cols[3] = a.cols[3] + b.cols[3];
  return res;
}

inline Mat4& operator+=(Mat4& a, Mat4 b)
{
  a = a + b;
  return a;
}

inline Mat4 operator-(Mat4 a, Mat4 b)
{
  Mat4 res;
  res.cols[0] = a.cols[0] - b.cols[0];
  res.cols[1] = a.cols[1] - b.cols[1];
  res.cols[2] = a.cols[2] - b.cols[2];
  res.cols[3] = a.cols[3] - b.cols[3];
  return res;
}

inline Mat4& operator-=(Mat4& a, Mat4 b)
{
  a = a - b;
  return a;
}

inline Mat4 perspective_infinite_reverse_lh(f32 fov_y_rads, f32 aspect_ratio, f32 z_near)
{
  f32 sin_fov = static_cast<f32>(sin(0.5 * fov_y_rads));
  f32 cos_fov = static_cast<f32>(cos(0.5 * fov_y_rads));
  f32 h = cos_fov / sin_fov;
  f32 w = h / aspect_ratio;

  return Mat4::columns(Vec4(w, 0.0, 0.0, 0.0),
                       Vec4(0.0, h, 0.0, 0.0),
                       Vec4(0.0, 0.0, 0.0, 1.0),
                       Vec4(0.0, 0.0, z_near, 0.0));
}

inline Mat4 pass_by_register look_at_lh(Vec3 eye, Vec3 dir, Vec3 up)
{
  Vec3 z = normalize_f32(dir);
  Vec3 x = normalize_f32(cross_f32(up, z));
  Vec3 y = cross_f32(z, x);
  return Mat4::columns(Vec4(x.x, y.x, z.x, 0.0f),
                       Vec4(x.y, y.y, z.y, 0.0f),
                       Vec4(x.z, y.z, z.z, 0.0f),
                       Vec4(-dot_f32(x, eye), -dot_f32(y, eye), -dot_f32(z, eye), 1.0f));
}

inline Mat4 transform_inverse_no_scale(Mat4 in)
{
  Mat4 ret;
  f32x4 t0 = _mm_movelh_ps(in.cols[0], in.cols[1]);
  f32x4 t1 = _mm_movehl_ps(in.cols[1], in.cols[0]);
#define MAKE_SHUFFLE_MASK(x,y,z,w)           (x | (y<<2) | (z<<4) | (w<<6))
#define VEC_SHUFFLE(vec1, vec2, x,y,z,w)    _mm_shuffle_ps(vec1, vec2, MAKE_SHUFFLE_MASK(x,y,z,w))

  ret.cols[0] = VEC_SHUFFLE(t0, in.cols[2], 0, 2, 0, 3);
  ret.cols[1] = VEC_SHUFFLE(t0, in.cols[2], 1, 3, 1, 3);
  ret.cols[2] = VEC_SHUFFLE(t0, in.cols[2], 0, 2, 2, 3);

#define VEC_SWIZZLE_MASK(vec, mask)          _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(vec), mask))
#define VEC_SWIZZLE(vec, x, y, z, w)        VEC_SWIZZLE_MASK(vec, MAKE_SHUFFLE_MASK(x,y,z,w))
#define VEC_SWIZZLE_1(vec, x)                VEC_SWIZZLE_MASK(vec, MAKE_SHUFFLE_MASK(x,x,x,x))

  ret.cols[3] = hadamard_f32(ret.cols[0], VEC_SWIZZLE_1(in.cols[3], 0));
  ret.cols[3] += hadamard_f32(ret.cols[1], VEC_SWIZZLE_1(in.cols[3], 1));
  ret.cols[3] += hadamard_f32(ret.cols[2], VEC_SWIZZLE_1(in.cols[3], 2));
  ret.cols[3] = _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f) - ret.cols[3];

  return ret;
}

// for column major matrix
// we use __m128 to represent 2x2 matrix as A = | A0  A2 |
//                                              | A1  A3 |
// 2x2 column major Matrix multiply A*B
__forceinline __m128 Mat2Mul(__m128 vec1, __m128 vec2)
{
  return
    _mm_add_ps(_mm_mul_ps(                     vec1, VEC_SWIZZLE(vec2, 0,0,3,3)),
               _mm_mul_ps(VEC_SWIZZLE(vec1, 2,3,0,1), VEC_SWIZZLE(vec2, 1,1,2,2)));
}
// 2x2 column major Matrix adjugate multiply (A#)*B
__forceinline __m128 Mat2AdjMul(__m128 vec1, __m128 vec2)
{
  return
    _mm_sub_ps(_mm_mul_ps(VEC_SWIZZLE(vec1, 3,0,3,0), vec2),
               _mm_mul_ps(VEC_SWIZZLE(vec1, 2,1,2,1), VEC_SWIZZLE(vec2, 1,0,3,2)));

}
// 2x2 column major Matrix multiply adjugate A*(B#)
__forceinline __m128 Mat2MulAdj(__m128 vec1, __m128 vec2)
{
  return
    _mm_sub_ps(_mm_mul_ps(                     vec1, VEC_SWIZZLE(vec2, 3,3,0,0)),
               _mm_mul_ps(VEC_SWIZZLE(vec1, 2,3,0,1), VEC_SWIZZLE(vec2, 1,1,2,2)));
}

// Inverse function is the same no matter column major or row major
// this version treats it as column major
inline Mat4 inverse_mat4(const Mat4& inM)
{
  // use block matrix method
  // A is a matrix, then i(A) or iA means inverse of A, A# (or A_ in code) means adjugate of A, |A| (or detA in code) is determinant, tr(A) is trace

  // sub matrices
#define VecShuffle_0101(vec1, vec2)        _mm_movelh_ps(vec1, vec2)
#define VecShuffle_2323(vec1, vec2)        _mm_movehl_ps(vec2, vec1)
  __m128 A = VecShuffle_0101(inM.cols[0], inM.cols[1]);
  __m128 C = VecShuffle_2323(inM.cols[0], inM.cols[1]);
  __m128 B = VecShuffle_0101(inM.cols[2], inM.cols[3]);
  __m128 D = VecShuffle_2323(inM.cols[2], inM.cols[3]);

  // determinant as (|A| |C| |B| |D|)
  __m128 det_sub = _mm_sub_ps(
                  _mm_mul_ps(VEC_SHUFFLE(inM.cols[0], inM.cols[2], 0,2,0,2), VEC_SHUFFLE(inM.cols[1], inM.cols[3], 1,3,1,3)),
    _mm_mul_ps(VEC_SHUFFLE(inM.cols[0], inM.cols[2], 1,3,1,3), VEC_SHUFFLE(inM.cols[1], inM.cols[3], 0,2,0,2))
                  );
  __m128 detA = VEC_SWIZZLE_1(det_sub, 0);
  __m128 detC = VEC_SWIZZLE_1(det_sub, 1);
  __m128 detB = VEC_SWIZZLE_1(det_sub, 2);
  __m128 detD = VEC_SWIZZLE_1(det_sub, 3);

  // let iM = 1/|M| * | X  Y |
  //                  | Z  W |

  // D#C
  __m128 D_C = Mat2AdjMul(D, C);
  // A#B
  __m128 A_B = Mat2AdjMul(A, B);
  // X# = |D|A - B(D#C)
  __m128 X_ = _mm_sub_ps(_mm_mul_ps(detD, A), Mat2Mul(B, D_C));
  // W# = |A|D - C(A#B)
  __m128 W_ = _mm_sub_ps(_mm_mul_ps(detA, D), Mat2Mul(C, A_B));

  // |M| = |A|*|D| + ... (continue later)
  __m128 det_m = _mm_mul_ps(detA, detD);

  // Y# = |B|C - D(A#B)#
  __m128 Y_ = _mm_sub_ps(_mm_mul_ps(detB, C), Mat2MulAdj(D, A_B));
  // Z# = |C|B - A(D#C)#
  __m128 Z_ = _mm_sub_ps(_mm_mul_ps(detC, B), Mat2MulAdj(A, D_C));

  // |M| = |A|*|D| + |B|*|C| ... (continue later)
  det_m = _mm_add_ps(det_m, _mm_mul_ps(detB, detC));

  // tr((A#B)(D#C))
  __m128 tr = _mm_mul_ps(A_B, VEC_SWIZZLE(D_C, 0,2,1,3));
  tr = _mm_hadd_ps(tr, tr);
  tr = _mm_hadd_ps(tr, tr);
  // |M| = |A|*|D| + |B|*|C| - tr((A#B)(D#C))
  det_m = _mm_sub_ps(det_m, tr);

  const __m128 adj_sign_mask = _mm_setr_ps(1.f, -1.f, -1.f, 1.f);
  // (1/|M|, -1/|M|, -1/|M|, 1/|M|)
  __m128 rDetM = _mm_div_ps(adj_sign_mask, det_m);

  X_ = _mm_mul_ps(X_, rDetM);
  Y_ = _mm_mul_ps(Y_, rDetM);
  Z_ = _mm_mul_ps(Z_, rDetM);
  W_ = _mm_mul_ps(W_, rDetM);

  Mat4 r;
  
  // apply adjugate and store, here we combine adjugate shuffle and store shuffle
  r.cols[0] = VEC_SHUFFLE(X_, Z_, 3,1,3,1);
  r.cols[1] = VEC_SHUFFLE(X_, Z_, 2,0,2,0);
  r.cols[2] = VEC_SHUFFLE(Y_, W_, 3,1,3,1);
  r.cols[3] = VEC_SHUFFLE(Y_, W_, 2,0,2,0);
  
  return r;
}

struct alignas(16) Quat
{
  Quat() : avx(_mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f)) {}
  Quat(f32 w, f32 x, f32 y, f32 z) : avx(_mm_set_ps(z, y, x, w)) {}
  Quat(f32x4 q) : avx(q) {}

  operator f32x4() const
  {
    return avx;
  }

  union
  {
    struct
    {
      f32 w;
      f32 x;
      f32 y;
      f32 z;
    };
    f32x4 avx;
  };
};

inline Quat
quat_from_rotation_x(f32 x)
{
  f32 s = sinf(x * 0.5f);
  f32 c = cosf(x * 0.5f);
  return Quat(c, s, 0, 0);
}

inline Quat
quat_from_rotation_y(f32 y)
{
  f32 s = sinf(y * 0.5f);
  f32 c = cosf(y * 0.5f);
  return Quat(c, 0, s, 0);
}

inline Quat
quat_from_rotation_z(f32 z)
{
  f32 s = sinf(z * 0.5f);
  f32 c = cosf(z * 0.5f);
  return Quat(c, 0, 0, s);
}

inline Quat pass_by_register
quat_mul(Quat lhs, Quat rhs)
{
  return Quat((lhs.w, rhs.w) - (lhs.x * rhs.x) - (lhs.y * rhs.y) - (lhs.z * rhs.z),
              (lhs.w * rhs.x) + (lhs.x * rhs.w) + (lhs.y * rhs.z) - (lhs.z * rhs.y),
              (lhs.w * rhs.y) - (lhs.x * rhs.z) + (lhs.y * rhs.w) + (lhs.z * rhs.x),
              (lhs.w * rhs.z) + (lhs.x * rhs.y) - (lhs.y * rhs.x) + (lhs.z * rhs.w));
}

inline Quat pass_by_register
operator*(Quat lhs, Quat rhs)
{
  return quat_mul(lhs, rhs);
}

inline Quat& pass_by_register
operator*=(Quat& lhs, Quat rhs)
{
  lhs = quat_mul(lhs, rhs);
  return lhs;
}

inline Quat
quat_from_euler_xyz(f32 x, f32 y, f32 z)
{
  return quat_from_rotation_x(x) * quat_from_rotation_y(y) * quat_from_rotation_z(z);
}

inline Quat
quat_from_euler_yxz(f32 y, f32 x, f32 z)
{
  return quat_from_rotation_y(y) * quat_from_rotation_x(x) * quat_from_rotation_z(z);
}

inline Quat pass_by_register
quat_conjugate(Quat quat)
{
  return Quat(quat.w, -quat.x, -quat.y, -quat.z);
}

inline Vec3 pass_by_register
rotate_vec3_by_quat(Vec3 vec, Quat q)
{
  Quat p = Quat(0, vec.x, vec.y, vec.z);
  Quat c = quat_conjugate(q);
  Quat res = q * p * c;

  return Vec3(res.x, res.y, res.z);
}

inline Mat4
generate_random_rotation()
{
  static std::uniform_real_distribution<f32> s_Distribution(0.f, 1.f);
  static std::mt19937 s_Rng;

  f32 u1 = k2PI * s_Distribution(s_Rng);
  f32 cos1 = cosf(u1);
  f32 sin1 = sinf(u1);

  f32 u2 = k2PI * s_Distribution(s_Rng);
  f32 cos2 = cosf(u2);
  f32 sin2 = sinf(u2);

  f32 u3 = s_Distribution(s_Rng);
  f32 sq3 = 2.0f * sqrtf(u3 * (1.0f - u3));

  f32 s2 = 2.0f * u3 * sin2 * sin2 - 1.0f;
  f32 c2 = 2.0f * u3 * cos2 * cos2 - 1.0f;
  f32 sc = 2.0f * u3 * sin2 * cos2;

  f32 _11 = cos1 * c2 - sin1 * sc;
  f32 _12 = sin1 * c2 + cos1 * sc;
  f32 _13 = sq3 * cos2;

  f32 _21 = cos1 * sc - sin1 * s2;
  f32 _22 = sin1 * sc + cos1 * s2;
  f32 _23 = sq3 * sin2;

  f32 _31 = cos1 * (sq3 * cos2) - sin1 * (sq3 * sin2);
  f32 _32 = sin1 * (sq3 * cos2) + cos1 * (sq3 * sin2);
  f32 _33 = 1.0f - 2.0f * u3;

  return Mat4::columns(Vec4(_11, _21, _31, 0.0f),
                       Vec4(_12, _22, _32, 0.0f),
                       Vec4(_13, _23, _33, 0.0f),
                       Vec4(0.0f, 0.0f, 0.0f, 1.0f));
}
