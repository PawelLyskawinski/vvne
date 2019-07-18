#pragma once

#include <SDL2/SDL_stdinc.h>

template <unsigned N> using Vec = float[N];
template <unsigned N> using Mat = Vec<N>[N];

template <unsigned N> void vec_add(Vec<N>& r, const Vec<N>& a, const Vec<N>& b)
{
  for (unsigned i = 0; i < N; ++i)
    r[i] = a[i] + b[i];
}

template <unsigned N> void vec_sub(Vec<N>& r, const Vec<N>& a, const Vec<N>& b)
{
  for (unsigned i = 0; i < N; ++i)
    r[i] = a[i] - b[i];
}

template <unsigned N> void vec_scale(Vec<N>& r, const Vec<N>& v, const float s)
{
  for (unsigned i = 0; i < N; ++i)
    r[i] = v[i] * s;
}

template <unsigned N> float vec_mul_inner(const Vec<N>& a, const Vec<N>& b)
{
  float r = 0.0f;
  for (unsigned i = 0; i < N; ++i)
    r += b[i] * a[i];
  return r;
}

template <unsigned N> float vec_len(const Vec<N>& v) { return SDL_sqrtf(vec_mul_inner(v, v)); }

template <unsigned N> void vec_norm(Vec<N>& r, const Vec<N>& v)
{
  float k = 1.0f / vec_len(v);
  vec_scale(r, v, k);
}

template <unsigned N> void vec_min(Vec<N>& r, const Vec<N>& a, const Vec<N>& b)
{
  for (unsigned i = 0; i < N; ++i)
    r[i] = a[i] < b[i] ? a[i] : b[i];
}

template <unsigned N> void vec_max(Vec<N>& r, const Vec<N>& a, const Vec<N>& b)
{
  for (unsigned i = 0; i < N; ++i)
    r[i] = a[i] > b[i] ? a[i] : b[i];
}

static inline void vec3_mul_cross(Vec<3>& r, const Vec<3>& a, const Vec<3>& b)
{
  r[0] = a[1] * b[2] - a[2] * b[1];
  r[1] = a[2] * b[0] - a[0] * b[2];
  r[2] = a[0] * b[1] - a[1] * b[0];
}

template <unsigned N> void vec_reflect(Vec<N>& r, const Vec<N>& v, const Vec<N>& n)
{
  const float p = 2.f * vec_mul_inner(v, n);
  for (unsigned i = 0; i < N; ++i)
    r[i] = v[i] - p * n[i];
}

static inline void vec4_mul_cross(Vec<4>& r, const Vec<4>& a, const Vec<4>& b)
{
  vec3_mul_cross(*reinterpret_cast<Vec<3>*>(r), *reinterpret_cast<const Vec<3>*>(a),
                 *reinterpret_cast<const Vec<3>*>(b));
  r[3] = 1.f;
}

static inline void mat_identity(Mat<4>& M)
{
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = 0; j < 4; ++j)
      M[i][j] = i == j ? 1.f : 0.f;
}

static inline void mat_row(Vec<4>& r, const Mat<4>& M, unsigned i)
{
  for (unsigned k = 0; k < 4; ++k)
    r[k] = M[k][i];
}

static inline void mat_col(Vec<4>& r, const Mat<4>& M, unsigned i)
{
  for (unsigned k = 0; k < 4; ++k)
    r[k] = M[i][k];
}

static inline void mat_transpose(Mat<4>& M, const Mat<4>& N)
{
  for (unsigned j = 0; j < 4; ++j)
    for (unsigned i = 0; i < 4; ++i)
      M[i][j] = N[j][i];
}

static inline void mat_add(Mat<4>& M, const Mat<4>& a, const Mat<4>& b)
{
  for (unsigned i = 0; i < 4; ++i)
    vec_add(M[i], a[i], b[i]);
}

static inline void mat_sub(Mat<4>& M, const Mat<4>& a, const Mat<4>& b)
{
  for (unsigned i = 0; i < 4; ++i)
    vec_sub(M[i], a[i], b[i]);
}

static inline void mat_scale(Mat<4>& M, const Mat<4>& a, const float k)
{
  for (unsigned i = 0; i < 4; ++i)
    vec_scale(M[i], a[i], k);
}

static inline void mat_scale_aniso(Mat<4>& M, const Mat<4>& a, float x, float y, float z)
{
  vec_scale(M[0], a[0], x);
  vec_scale(M[1], a[1], y);
  vec_scale(M[2], a[2], z);
  for (unsigned i = 0; i < 4; ++i)
    M[3][i] = a[3][i];
}

static inline void mat_mul(Mat<4>& M, const Mat<4>& a, const Mat<4>& b)
{
  for (unsigned c = 0; c < 4; ++c)
  {
    for (unsigned r = 0; r < 4; ++r)
    {
      M[c][r] = 0.f;
      for (unsigned k = 0; k < 4; ++k)
        M[c][r] += a[k][r] * b[c][k];
    }
  }
}

static inline void mat_mul_vec4(Vec<4>& r, const Mat<4>& M, const Vec<4>& v)
{
  for (unsigned j = 0; j < 4; ++j)
  {
    r[j] = 0.f;
    for (unsigned i = 0; i < 4; ++i)
    {
      r[j] += M[i][j] * v[i];
    }
  }
}

static inline void mat_translate(Mat<4>& T, float x, float y, float z)
{
  mat_identity(T);
  T[3][0] = x;
  T[3][1] = y;
  T[3][2] = z;
}

static inline void mat_translate_in_place(Mat<4>& M, float x, float y, float z)
{
  Vec<4> t = {x, y, z, 0};
  Vec<4> r = {};
  for (unsigned i = 0; i < 4; ++i)
  {
    mat_row(r, M, i);
    M[3][i] += vec_mul_inner(r, t);
  }
}

static inline void mat_from_vec3_mul_outer(Mat<4>& M, const Vec<3>& a, const Vec<3>& b)
{
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = 0; j < 4; ++j)
      M[i][j] = i < 3 && j < 3 ? a[i] * b[j] : 0.0f;
}

static inline void mat4x4_rotate(Mat<4>& R, const Mat<4>& M, float x, float y, float z, float angle)
{
  float  s = SDL_sinf(angle);
  float  c = SDL_cosf(angle);
  Vec<3> u = {x, y, z};

  if (vec_len(u) > 1e-4)
  {
    vec_norm(u, u);
    Mat<4> T;
    mat_from_vec3_mul_outer(T, u, u);

    Mat<4> S = {{0, u[2], -u[1], 0}, {-u[2], 0, u[0], 0}, {u[1], -u[0], 0, 0}, {0, 0, 0, 0}};
    mat_scale(S, S, s);

    Mat<4> C;
    mat_identity(C);
    mat_sub(C, C, T);

    mat_scale(C, C, c);

    mat_add(T, T, C);
    mat_add(T, T, S);

    T[3][3] = 1.0f;
    mat_mul(R, M, T);
  }
  else
  {
    SDL_memcpy(R, M, sizeof(Mat<4>));
  }
}

static inline void mat_rotate_X(Mat<4>& Q, const Mat<4>& M, float angle)
{
  float  s = SDL_sinf(angle);
  float  c = SDL_cosf(angle);
  Mat<4> R = {{1.f, 0.f, 0.f, 0.f}, {0.f, c, s, 0.f}, {0.f, -s, c, 0.f}, {0.f, 0.f, 0.f, 1.f}};
  mat_mul(Q, M, R);
}

static inline void mat4x4_rotate_Y(Mat<4>& Q, const Mat<4>& M, float angle)
{
  float  s = SDL_sinf(angle);
  float  c = SDL_cosf(angle);
  Mat<4> R = {{c, 0.f, s, 0.f}, {0.f, 1.f, 0.f, 0.f}, {-s, 0.f, c, 0.f}, {0.f, 0.f, 0.f, 1.f}};
  mat_mul(Q, M, R);
}

static inline void mat4x4_rotate_Z(Mat<4>& Q, const Mat<4>& M, float angle)
{
  float   s = SDL_sinf(angle);
  float   c = SDL_cosf(angle);
  Mat<4>& R = {{c, s, 0.f, 0.f}, {-s, c, 0.f, 0.f}, {0.f, 0.f, 1.f, 0.f}, {0.f, 0.f, 0.f, 1.f}};
  mat_mul(Q, M, R);
}

static inline void mat4x4_invert(Mat<4>& T, const Mat<4>& M)
{
  float s[6];
  float c[6];
  s[0] = M[0][0] * M[1][1] - M[1][0] * M[0][1];
  s[1] = M[0][0] * M[1][2] - M[1][0] * M[0][2];
  s[2] = M[0][0] * M[1][3] - M[1][0] * M[0][3];
  s[3] = M[0][1] * M[1][2] - M[1][1] * M[0][2];
  s[4] = M[0][1] * M[1][3] - M[1][1] * M[0][3];
  s[5] = M[0][2] * M[1][3] - M[1][2] * M[0][3];

  c[0] = M[2][0] * M[3][1] - M[3][0] * M[2][1];
  c[1] = M[2][0] * M[3][2] - M[3][0] * M[2][2];
  c[2] = M[2][0] * M[3][3] - M[3][0] * M[2][3];
  c[3] = M[2][1] * M[3][2] - M[3][1] * M[2][2];
  c[4] = M[2][1] * M[3][3] - M[3][1] * M[2][3];
  c[5] = M[2][2] * M[3][3] - M[3][2] * M[2][3];

  /* Assumes it is invertible */
  float idet = 1.0f / (s[0] * c[5] - s[1] * c[4] + s[2] * c[3] + s[3] * c[2] - s[4] * c[1] + s[5] * c[0]);

  T[0][0] = (M[1][1] * c[5] - M[1][2] * c[4] + M[1][3] * c[3]) * idet;
  T[0][1] = (-M[0][1] * c[5] + M[0][2] * c[4] - M[0][3] * c[3]) * idet;
  T[0][2] = (M[3][1] * s[5] - M[3][2] * s[4] + M[3][3] * s[3]) * idet;
  T[0][3] = (-M[2][1] * s[5] + M[2][2] * s[4] - M[2][3] * s[3]) * idet;

  T[1][0] = (-M[1][0] * c[5] + M[1][2] * c[2] - M[1][3] * c[1]) * idet;
  T[1][1] = (M[0][0] * c[5] - M[0][2] * c[2] + M[0][3] * c[1]) * idet;
  T[1][2] = (-M[3][0] * s[5] + M[3][2] * s[2] - M[3][3] * s[1]) * idet;
  T[1][3] = (M[2][0] * s[5] - M[2][2] * s[2] + M[2][3] * s[1]) * idet;

  T[2][0] = (M[1][0] * c[4] - M[1][1] * c[2] + M[1][3] * c[0]) * idet;
  T[2][1] = (-M[0][0] * c[4] + M[0][1] * c[2] - M[0][3] * c[0]) * idet;
  T[2][2] = (M[3][0] * s[4] - M[3][1] * s[2] + M[3][3] * s[0]) * idet;
  T[2][3] = (-M[2][0] * s[4] + M[2][1] * s[2] - M[2][3] * s[0]) * idet;

  T[3][0] = (-M[1][0] * c[3] + M[1][1] * c[1] - M[1][2] * c[0]) * idet;
  T[3][1] = (M[0][0] * c[3] - M[0][1] * c[1] + M[0][2] * c[0]) * idet;
  T[3][2] = (-M[3][0] * s[3] + M[3][1] * s[1] - M[3][2] * s[0]) * idet;
  T[3][3] = (M[2][0] * s[3] - M[2][1] * s[1] + M[2][2] * s[0]) * idet;
}

static inline void mat_orthonormalize(Mat<4>& R, const Mat<4>& M)
{
  SDL_memcpy(R, M, sizeof(Mat<4>));

  // @todo: rethink this.. all below operations are on vec3 variants
  vec_norm(
          R[2], // as vec3!!
          R[2]  // as vec3!!
          );

  float s = 1.0f;

  Vec<4> h;
  s = vec_mul_inner(R[1], R[2]);
  vec_scale(h, R[2], s);
  h[3] = 0.0f;
  vec_sub(R[1], R[1], h);
  vec_norm(R[2], R[2]);

  s = vec3_mul_inner(R[1], R[2]);
  vec3_scale(h, R[2], s);
  vec3_sub(R[1], R[1], h);
  vec3_norm(R[1], R[1]);

  s = vec3_mul_inner(R[0], R[1]);
  vec3_scale(h, R[1], s);
  vec3_sub(R[0], R[0], h);
  vec3_norm(R[0], R[0]);
}
