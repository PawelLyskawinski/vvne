#include "math.hh"
#include <algorithm>

Vec2 Vec2::operator-(const Vec2& rhs) const
{
  return Vec2(x - rhs.x, y - rhs.y);
}

Vec2 Vec2::operator+(const Vec2& rhs) const
{
  return Vec2(x + rhs.x, y + rhs.y);
}

void Vec2::operator+=(const Vec2& rhs)
{
  x += rhs.x;
  y += rhs.y;
}

void Vec2::operator-=(const Vec2& rhs)
{
  x -= rhs.x;
  y -= rhs.y;
}

float Vec2::len() const
{
  return SDL_sqrtf(x * x + y * y);
}

Vec2 Vec2::scale(float s) const
{
  return Vec2(x * s, y * s);
}

Vec2 Vec2::scale(const Vec2& s) const
{
  return Vec2(x * s.x, y * s.y);
}

Vec2 Vec2::normalize() const
{
  return scale(1.0f / len());
}

Vec2 Vec2::invert() const
{
  return Vec2(1.0f / x, 1.0f / y);
}

Vec3::Vec3(float val)
    : x(val)
    , y(val)
    , z(val)
{
}

Vec3::Vec3(const Vec2& vec, float val)
    : x(vec.x)
    , y(vec.y)
    , z(val)
{
}

Vec3::Vec3(float x, float y, float z)
    : x(x)
    , y(y)
    , z(z)
{
}

Vec3 Vec3::operator-(const Vec3& rhs) const
{
  return Vec3(x - rhs.x, y - rhs.y, z - rhs.z);
}

Vec3 Vec3::operator+(const Vec3& rhs) const
{
  return Vec3(x + rhs.x, y + rhs.y, z + rhs.z);
}

Vec3 Vec3::scale(float s) const
{
  return Vec3(x * s, y * s, z * s);
}

void Vec3::operator+=(const Vec3& rhs)
{
  x += rhs.x;
  y += rhs.y;
  z += rhs.z;
}

void Vec3::operator-=(const Vec3& rhs)
{
  x -= rhs.x;
  y -= rhs.y;
  z -= rhs.z;
}

float Vec3::len() const
{
  return SDL_sqrtf(x * x + y * y + z * z);
}

Vec3 Vec3::invert_signs() const
{
  return Vec3(-x, -y, -z);
}

void Vec3::clamp(float min, float max)
{
  x = ::clamp(x, min, max);
  y = ::clamp(y, min, max);
  z = ::clamp(z, min, max);
}

Vec3 Vec3::normalize() const
{
  return scale(1.0f / len());
}

Vec2 Vec3::xz() const
{
  return Vec2(x, z);
}

Vec3 Vec3::mul_cross(const Vec3& rhs) const
{
  return Vec3(y * rhs.z - z * rhs.y, z * rhs.x - x * rhs.z, x * rhs.y - y * rhs.x);
}

float Vec3::mul_inner(const Vec3& rhs) const
{
  return (x * rhs.x) + (y * rhs.y) + (z * rhs.z);
}

Vec3 Vec3::lerp(const Vec3& dst, float t) const
{
  Vec3 r;

  r.x = x + t * (dst.x - x);
  r.y = y + t * (dst.y - y);
  r.z = z + t * (dst.z - z);

  return r;
}

Vec4::Vec4(float x, float y, float z, float w)
    : x(x)
    , y(y)
    , z(z)
    , w(w)
{
}

Vec4::Vec4(const Vec3& v, float w)
    : x(v.x)
    , y(v.y)
    , z(v.z)
    , w(w)
{
}

float Vec4::mul_inner(const Vec4& rhs) const
{
  return (x * rhs.x) + (y * rhs.y) + (z * rhs.z) + (w * rhs.w);
}

Vec4 Vec4::lerp(const Vec4& dst, float t) const
{
  Vec4 r;

  r.x = x + t * (dst.x - x);
  r.y = y + t * (dst.y - y);
  r.z = z + t * (dst.z - z);
  r.w = w + t * (dst.w - w);

  return r;
}

float Vec4::len() const
{
  return SDL_sqrtf(x * x + y * y + z * z + w * w);
}

Vec4 Vec4::normalize() const
{
  return scale(1.0f / len());
}

Quaternion::Quaternion(const float angle, const Vec3& axis)
{
  rotate(angle, axis);
}

void Quaternion::rotate(const float angle, const Vec3& axis)
{
  const float half_angle = 0.5f * angle;
  data.as_vec3()         = axis.scale(SDL_sinf(half_angle));
  data.w                 = SDL_cosf(half_angle);
}

Quaternion Quaternion::operator*(const Quaternion& rhs) const
{
  Quaternion r;

  const Vec3& a = data.as_vec3();
  const Vec3& b = rhs.data.as_vec3();
  Vec3&       c = r.data.as_vec3();

  c = a.mul_cross(b);
  c += a.scale(rhs.data.w);
  c += b.scale(data.w);
  r.data.w = data.w * rhs.data.w - a.mul_inner(b);

  return r;
}

Mat4x4::Mat4x4(const float* data)
{
  SDL_memcpy(&columns[0].x, data, 16 * sizeof(float));
}

Mat4x4::Mat4x4(const Quaternion& q)
{
  const float a  = q.data.w;
  const float b  = q.data.x;
  const float c  = q.data.y;
  const float d  = q.data.z;
  const float a2 = a * a;
  const float b2 = b * b;
  const float c2 = c * c;
  const float d2 = d * d;

  columns[0]   = Vec4(a2 + b2 - c2 - d2, 2.0f * (b * c + a * d), 2.0f * (b * d - a * c), 0.0f);
  columns[1]   = Vec4(2.0f * (b * c - a * d), a2 - b2 + c2 - d2, 2.0f * (c * d + a * b), 0.0f);
  columns[2]   = Vec4(2.0f * (b * d + a * c), 2.0f * (c * d - a * b), a2 - b2 - c2 + d2, 0.0f);
  columns[3].w = 1.0f;
}

void Mat4x4::translate(const Vec3 v)
{
  identity();
  columns[3].x = v.x;
  columns[3].y = v.y;
  columns[3].z = v.z;
}

void Mat4x4::set_diagonal(const Vec3& v)
{
  columns[0].x = v.x;
  columns[1].y = v.y;
  columns[2].z = v.z;
  columns[3].w = 1.0f;
}

void Mat4x4::identity()
{
  std::fill(columns, &columns[4], Vec4());
  for (uint32_t i = 0; i < 4; ++i)
    columns[i][i] = 1.0f;
}

void Mat4x4::scale(const Vec3 s)
{
  set_diagonal(s);
}

void Mat4x4::transpose()
{
  //
  // a1 b1 c1 d1     a1 a2 a3 a4
  // a2 b2 c2 d2     b1 b2 b3 b4
  // a3 b3 c3 d3 --> c1 c2 c3 c4
  // a4 b4 c4 d4     d1 d2 d3 d4
  //
  // We only need to iterate elements on one side of diagonal (exluding diag line).
  //
  // x 0 1 3
  // x x 2 4
  // x x x 5
  // x x x x
  //
  // (1, 0) -> (0, 1)
  // (2, 0) -> (0, 2)
  // (2, 1) -> (1, 2)
  // (3, 0) -> (0, 3)
  // (3, 1) -> (1, 3)
  // (3, 2) -> (2, 3)
  //
  for (uint32_t c = 1; c < 4; ++c)
    for (uint32_t idx = 0; idx < c; ++idx)
      columns[idx][c] = columns[c][idx];
}

Mat4x4 Mat4x4::operator*(const Mat4x4& rhs) const
{
  Mat4x4 result;

  //
  //   A               B              C
  //   a1 b1 c1 d1     e1 f1 g1 h1    i1 j1 k1 l1
  //   a2 b2 c2 d2     e2 f2 g2 h2    i2 j2 k2 l2
  //   a3 b3 c3 d3 mul e3 f3 g3 h3 -> i3 j3 k3 l3
  //   a4 b4 c4 d4     e4 f4 g4 h4    i4 j4 k4 l4
  //
  //   i1 = (a1 * e1) + (b1 * e2) + (c1 * e3) + (d1 * e4)
  //   i2 = (a2 * e1) + (b2 * e2) + (c2 * e3) + (d2 * e4)
  //   ...
  //
  //   For each column (c) in C
  //       For each row (r) in C
  //           Use full length (k) of row in A and column in B to calculate C[c][r]
  //

  for (uint32_t c = 0; c < 4; ++c)
    for (uint32_t r = 0; r < 4; ++r)
      for (uint32_t k = 0; k < 4; ++k)
        result.columns[c][r] += columns[k][r] * rhs.columns[c][k];

  return result;
}

Vec4 Mat4x4::operator*(const Vec4& rhs) const
{
  Vec4 result;

  //
  // Simplified version of algorithm above. Only difference is B and C matrices are 1-column versions
  //

  for (uint32_t r = 0; r < 4; ++r)
    for (uint32_t k = 0; k < 4; ++k)
      result[r] += columns[k][r] * rhs[k];

  return result;
}

void Mat4x4::perspective(const uint32_t width, const uint32_t height, float fov_rads, float n, float f)
{
  perspective(static_cast<float>(width) / static_cast<float>(height), fov_rads, n, f);
}

void Mat4x4::perspective(const VkExtent2D& extent, float fov_rads, float n, float f)
{
  perspective(static_cast<float>(extent.width) / static_cast<float>(extent.height), fov_rads, n, f);
}

void Mat4x4::perspective(float aspect_ratio, float fov_rads, float n, float f)
{
  const float a = 1.0f / SDL_tanf(0.5f * fov_rads);

  columns[0].x = a / aspect_ratio;
  columns[1].y = -a;
  columns[2].z = -((f + n) / (f - n));
  columns[2].w = -1.0f;
  columns[3].z = -((2.f * f * n) / (f - n));
}

Mat4x4 Mat4x4::LookAt(const Vec3& eye, const Vec3& center, const Vec3& up)
{
  Mat4x4 r;

  const Vec3 f = (center - eye).normalize();
  const Vec3 s = f.mul_cross(up).normalize(); // up should be already normalized
  const Vec3 t = s.mul_cross(f);

  r.columns[0] = Vec4(s.x, t.x, -f.x, 0.0f);
  r.columns[1] = Vec4(s.y, t.y, -f.y, 0.0f);
  r.columns[2] = Vec4(s.z, t.z, -f.z, 0.0f);
  r.translate_in_place(eye.invert_signs());
  r.columns[3].w = 1.0f;

  return r;
}

Vec4 Mat4x4::row(uint32_t i) const
{
  return {columns[0][i], columns[1][i], columns[2][i], columns[3][i]};
}

void Mat4x4::translate_in_place(const Vec3& v)
{
  const Vec4 t(v, 0.0f);
  for (uint32_t i = 0; i < 4; ++i)
    columns[3][i] = row(i).mul_inner(t);
}

void Mat4x4::ortho(float l, float r, float b, float t, float n, float f)
{
  std::fill(columns, &columns[4], Vec4());

  columns[0].x = 2.0f / (r - l);
  columns[1].y = 2.0f / (t - b);
  columns[2].z = -2.0f / (f - n);
  columns[3].x = -(r + l) / (r - l);
  columns[3].y = -(t + b) / (t - b);
  columns[3].z = -(f + n) / (f - n);
  columns[3].w = 1.0f;
}

Mat4x4 Mat4x4::invert() const
{
  Mat4x4 r;

  float s[6];
  float c[6];

  s[0] = columns[0][0] * columns[1][1] - columns[1][0] * columns[0][1];
  s[1] = columns[0][0] * columns[1][2] - columns[1][0] * columns[0][2];
  s[2] = columns[0][0] * columns[1][3] - columns[1][0] * columns[0][3];
  s[3] = columns[0][1] * columns[1][2] - columns[1][1] * columns[0][2];
  s[4] = columns[0][1] * columns[1][3] - columns[1][1] * columns[0][3];
  s[5] = columns[0][2] * columns[1][3] - columns[1][2] * columns[0][3];

  c[0] = columns[2][0] * columns[3][1] - columns[3][0] * columns[2][1];
  c[1] = columns[2][0] * columns[3][2] - columns[3][0] * columns[2][2];
  c[2] = columns[2][0] * columns[3][3] - columns[3][0] * columns[2][3];
  c[3] = columns[2][1] * columns[3][2] - columns[3][1] * columns[2][2];
  c[4] = columns[2][1] * columns[3][3] - columns[3][1] * columns[2][3];
  c[5] = columns[2][2] * columns[3][3] - columns[3][2] * columns[2][3];

  // clang-format off
  r.columns[0][0] =  columns[1][1] * c[5] - columns[1][2] * c[4] + columns[1][3] * c[3];
  r.columns[0][1] = -columns[0][1] * c[5] + columns[0][2] * c[4] - columns[0][3] * c[3];
  r.columns[0][2] =  columns[3][1] * s[5] - columns[3][2] * s[4] + columns[3][3] * s[3];
  r.columns[0][3] = -columns[2][1] * s[5] + columns[2][2] * s[4] - columns[2][3] * s[3];
  r.columns[1][0] = -columns[1][0] * c[5] + columns[1][2] * c[2] - columns[1][3] * c[1];
  r.columns[1][1] =  columns[0][0] * c[5] - columns[0][2] * c[2] + columns[0][3] * c[1];
  r.columns[1][2] = -columns[3][0] * s[5] + columns[3][2] * s[2] - columns[3][3] * s[1];
  r.columns[1][3] =  columns[2][0] * s[5] - columns[2][2] * s[2] + columns[2][3] * s[1];
  r.columns[2][0] =  columns[1][0] * c[4] - columns[1][1] * c[2] + columns[1][3] * c[0];
  r.columns[2][1] = -columns[0][0] * c[4] + columns[0][1] * c[2] - columns[0][3] * c[0];
  r.columns[2][2] =  columns[3][0] * s[4] - columns[3][1] * s[2] + columns[3][3] * s[0];
  r.columns[2][3] = -columns[2][0] * s[4] + columns[2][1] * s[2] - columns[2][3] * s[0];
  r.columns[3][0] = -columns[1][0] * c[3] + columns[1][1] * c[1] - columns[1][2] * c[0];
  r.columns[3][1] =  columns[0][0] * c[3] - columns[0][1] * c[1] + columns[0][2] * c[0];
  r.columns[3][2] = -columns[3][0] * s[3] + columns[3][1] * s[1] - columns[3][2] * s[0];
  r.columns[3][3] =  columns[2][0] * s[3] - columns[2][1] * s[1] + columns[2][2] * s[0];
  // clang-format on

  const float idet = 1.0f / (s[0] * c[5] - s[1] * c[4] + s[2] * c[3] + s[3] * c[2] - s[4] * c[1] + s[5] * c[0]);
  for (Vec4& v : r.columns)
    for (uint32_t i = 0; i < 4; ++i)
      v[i] *= idet;

  return r;
}

Mat4x4 Mat4x4::RotationX(float radians)
{
  const float s = SDL_sinf(radians);
  const float c = SDL_cosf(radians);

  Mat4x4 r;
  r.columns[0].x = 1.0f;
  r.columns[1].y = c;
  r.columns[1].z = s;
  r.columns[2].y = -s;
  r.columns[2].z = c;
  r.columns[3].w = 1.0f;

  return r;
}

Mat4x4 Mat4x4::RotationY(float radians)
{
  const float s = SDL_sinf(radians);
  const float c = SDL_cosf(radians);

  Mat4x4 r;
  r.columns[0].x = c;
  r.columns[0].z = s;
  r.columns[1].y = 1.0f;
  r.columns[2].x = -s;
  r.columns[2].z = c;
  r.columns[3].w = 1.0f;

  return r;
}

Mat4x4 Mat4x4::RotationZ(float radians)
{
  const float s = SDL_sinf(radians);
  const float c = SDL_cosf(radians);

  Mat4x4 r;
  r.columns[0].x = c;
  r.columns[0].y = s;
  r.columns[1].x = -s;
  r.columns[1].y = c;
  r.columns[2].z = 1.0f;
  r.columns[3].w = 1.0f;

  return r;
}

Mat4x4 Mat4x4::Translation(const Vec3& t)
{
  Mat4x4 r;
  r.identity();
  r.translate(t);
  return r;
}

Mat4x4 Mat4x4::Scale(const Vec3& s)
{
  Mat4x4 r;
  r.identity();
  r.scale(s);
  return r;
}

void Mat4x4::generate_frustum_planes(Vec4 planes[6]) const
{
  enum
  {
    LEFT   = 0,
    RIGHT  = 1,
    TOP    = 2,
    BOTTOM = 3,
    BACK   = 4,
    FRONT  = 5
  };

  planes[LEFT].x   = at(3, 0) + at(0, 0);
  planes[LEFT].y   = at(3, 1) + at(0, 1);
  planes[LEFT].z   = at(3, 2) + at(0, 2);
  planes[LEFT].w   = at(3, 3) + at(0, 3);
  planes[RIGHT].x  = at(3, 0) - at(0, 0);
  planes[RIGHT].y  = at(3, 1) - at(0, 1);
  planes[RIGHT].z  = at(3, 2) - at(0, 2);
  planes[RIGHT].w  = at(3, 3) - at(0, 3);
  planes[TOP].x    = at(3, 0) - at(1, 0);
  planes[TOP].y    = at(3, 1) - at(1, 1);
  planes[TOP].z    = at(3, 2) - at(1, 2);
  planes[TOP].w    = at(3, 3) - at(1, 3);
  planes[BOTTOM].x = at(3, 0) + at(1, 0);
  planes[BOTTOM].y = at(3, 1) + at(1, 1);
  planes[BOTTOM].z = at(3, 2) + at(1, 2);
  planes[BOTTOM].w = at(3, 3) + at(1, 3);
  planes[BACK].x   = at(3, 0) + at(2, 0);
  planes[BACK].y   = at(3, 1) + at(2, 1);
  planes[BACK].z   = at(3, 2) + at(2, 2);
  planes[BACK].w   = at(3, 3) + at(2, 3);
  planes[FRONT].x  = at(3, 0) - at(2, 0);
  planes[FRONT].y  = at(3, 1) - at(2, 1);
  planes[FRONT].z  = at(3, 2) - at(2, 2);
  planes[FRONT].w  = at(3, 3) - at(2, 3);

  for (auto i = 0; i < 6; i++)
  {
    const float length = planes[i].as_vec3().len();
    planes[i]          = planes[i].scale(1.0f / length);
  }
}
