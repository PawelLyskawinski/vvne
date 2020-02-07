#include "player.hh"
#include "levels/example_level.hh"

namespace {

constexpr uint64_t scancode_to_mask(SDL_Scancode scancode)
{
  switch (scancode)
  {
  case SDL_SCANCODE_W:
    return 1ULL << 0u;
  case SDL_SCANCODE_S:
    return 1ULL << 1u;
  case SDL_SCANCODE_SPACE:
    return 1ULL << 2u;
  case SDL_SCANCODE_A:
    return 1ULL << 3u;
  case SDL_SCANCODE_D:
    return 1ULL << 4u;
  case SDL_SCANCODE_LSHIFT:
    return 1ULL << 5u;
  default:
    return 0ULL;
  }
}

} // namespace

void Player::setup(uint32_t width, uint32_t height)
{
  const VkExtent2D extent = {width, height};
  camera_projection.perspective(extent, to_rad(90.0f), 0.1f, 500.0f);
  camera.angle        = static_cast<float>(M_PI / 2);
  camera.updown_angle = -1.2f;
  position            = Vec3(0.0f, 0.0f, -10.0f);
  freecam_mode        = false;
}

const Camera& Player::get_camera() const
{
  return freecam_mode ? freecam_camera : camera;
}

void Player::process_event(const SDL_Event& event)
{
  switch (event.type)
  {
  case SDL_MOUSEMOTION:
  {
    if (SDL_GetRelativeMouseMode())
    {
      Camera& bound_camera = freecam_mode ? freecam_camera : camera;
      bound_camera.angle   = SDL_fmodf(bound_camera.angle + (0.01f * event.motion.xrel), 2.0f * float(M_PI));
      bound_camera.updown_angle -= (0.005f * event.motion.yrel);
    }
  }
  break;

  case SDL_KEYDOWN:
  {
    internal_key_flags |= scancode_to_mask(event.key.keysym.scancode);
    if (SDL_SCANCODE_Y == event.key.keysym.scancode)
    {
      freecam_mode         = !freecam_mode;
      freecam_camera       = camera;
      freecam_position     = position;
      freecam_velocity     = Vec3(0.0);
      freecam_acceleration = Vec3(0.0);
    }
  }
  break;

  case SDL_KEYUP:
  {
    internal_key_flags &= ~scancode_to_mask(event.key.keysym.scancode);
  }
  break;

  default:
    break;
  }
}

static Vec2 rotation_2D(float angle)
{
  return Vec2(SDL_sinf(angle), SDL_cosf(angle));
}

static Vec3 to_vec3_xz(const Vec2& in)
{
  return Vec3(in.x, 0.0f, in.y);
}

static Vec3 calculate_direction_vector(float angle, float updown_angle)
{
  return Vec3(SDL_cosf(angle), SDL_tanf(updown_angle), -SDL_sinf(angle)).normalize();
}

static Vec3 calculate_direction_vector(float angle)
{
  return Vec3(SDL_cosf(angle), 0.0f, -SDL_sinf(angle)).normalize();
}

void Player::update(const float current_time_sec, const float delta_ms, const ExampleLevel& level)
{
  (void)current_time_sec;

  const float camera_distance    = 3.0f;
  const float friction           = 0.2f;
  const float max_speed          = 3.0f;
  const float acceleration_const = 0.0002f;
  const float boosters_power     = 3.0f;

  if (freecam_mode)
  {
    freecam_position += freecam_velocity.scale(delta_ms);
    freecam_velocity += freecam_acceleration.scale(delta_ms) - freecam_velocity.scale(friction);
    freecam_velocity.clamp(-max_speed, max_speed);
    freecam_acceleration = Vec3(0.0f);

    const Vec3 main_direction_vector  = calculate_direction_vector(freecam_camera.angle, freecam_camera.updown_angle);

    if (scancode_to_mask(SDL_SCANCODE_W) & internal_key_flags)
    {
      freecam_acceleration -= main_direction_vector.scale(acceleration_const);
    }
    else if (scancode_to_mask(SDL_SCANCODE_S) & internal_key_flags)
    {
      freecam_acceleration += main_direction_vector.scale(acceleration_const);
    }

    if (scancode_to_mask(SDL_SCANCODE_A) & internal_key_flags)
    {
      freecam_acceleration +=
          calculate_direction_vector(freecam_camera.angle + to_rad(90.0f)).scale(acceleration_const);
    }
    else if (scancode_to_mask(SDL_SCANCODE_D) & internal_key_flags)
    {
      freecam_acceleration +=
          calculate_direction_vector(freecam_camera.angle - to_rad(90.0f)).scale(acceleration_const);
    }

    if (scancode_to_mask(SDL_SCANCODE_LSHIFT) & internal_key_flags)
    {
      freecam_acceleration = freecam_acceleration.scale(boosters_power);
    }

    freecam_camera.position = freecam_position + main_direction_vector.scale(camera_distance) - Vec3(0.0f, 1.5f, 0.0f);
    camera_view =
        Mat4x4::LookAt(freecam_camera.position, freecam_position - Vec3(0.0f, 1.5f, 0.0f), Vec3(0.0f, -1.0f, 0.0f));
  }
  else
  {

    position += velocity.scale(delta_ms);
    velocity += acceleration.scale(delta_ms) - velocity.scale(friction);
    velocity.clamp(-max_speed, max_speed);
    acceleration = Vec3(0.0f);

    if (scancode_to_mask(SDL_SCANCODE_W) & internal_key_flags)
    {
      acceleration += to_vec3_xz(rotation_2D(camera.angle - float(M_PI_2)).scale(acceleration_const));
    }
    else if (scancode_to_mask(SDL_SCANCODE_S) & internal_key_flags)
    {
      acceleration += to_vec3_xz(rotation_2D(camera.angle + float(M_PI_2)).scale(acceleration_const));
    }

    if (scancode_to_mask(SDL_SCANCODE_A) & internal_key_flags)
    {
      acceleration += to_vec3_xz(rotation_2D(camera.angle + float(M_PI)).scale(acceleration_const));
    }
    else if (scancode_to_mask(SDL_SCANCODE_D) & internal_key_flags)
    {
      acceleration += to_vec3_xz(rotation_2D(camera.angle).scale(acceleration_const));
    }

    if (scancode_to_mask(SDL_SCANCODE_LSHIFT) & internal_key_flags)
    {
      acceleration.x *= boosters_power;
      acceleration.z *= boosters_power;
    }

    // @todo: re-enable jumping
    position.y = level.get_height(position.x, position.z) - 1.5f;

    camera.position =
        position +
        Vec3(SDL_cosf(camera.angle), SDL_sinf(clamp(camera.updown_angle, -1.5f, 1.5f)), -SDL_sinf(camera.angle))
            .scale(camera_distance) -
        Vec3(0.0f, 1.5f, 0.0f);

    camera_view = Mat4x4::LookAt(camera.position, position - Vec3(0.0f, 1.5f, 0.0f), Vec3(0.0f, -1.0f, 0.0f));
  }
}
