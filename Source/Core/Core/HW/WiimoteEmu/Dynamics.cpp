// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/HW/WiimoteEmu/Dynamics.h"

#include <algorithm>
#include <cmath>

#include "Common/MathUtil.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "InputCommon/ControllerEmu/ControlGroup/Buttons.h"
#include "InputCommon/ControllerEmu/ControlGroup/Cursor.h"
#include "InputCommon/ControllerEmu/ControlGroup/Force.h"
#include "InputCommon/ControllerEmu/ControlGroup/IMUAccelerometer.h"
#include "InputCommon/ControllerEmu/ControlGroup/IMUCursor.h"
#include "InputCommon/ControllerEmu/ControlGroup/IMUGyroscope.h"
#include "InputCommon/ControllerEmu/ControlGroup/Tilt.h"

namespace
{
// Given a velocity, acceleration, and maximum jerk value,
// calculate change in position after a stop in the shortest possible time.
// Used to smoothly adjust acceleration and come to complete stops at precise positions.
// Based on equations for motion with constant jerk.
// s = s0 + v0 t + a0 t^2 / 2 + j t^3 / 6
double CalculateStopDistance(double velocity, double acceleration, double max_jerk)
{
  // Math below expects velocity to be non-negative.
  const auto velocity_flip = (velocity < 0 ? -1 : 1);

  const auto v_0 = velocity * velocity_flip;
  const auto a_0 = acceleration * velocity_flip;
  const auto j = max_jerk;

  // Time to reach zero acceleration.
  const auto t_0 = a_0 / j;

  // Distance to reach zero acceleration.
  const auto d_0 = std::pow(a_0, 3) / (3 * j * j) + (a_0 * v_0) / j;

  // Velocity at zero acceleration.
  const auto v_1 = v_0 + a_0 * std::abs(t_0) - std::copysign(j * t_0 * t_0 / 2, t_0);

  // Distance to complete stop.
  const auto d_1 = std::copysign(std::pow(std::abs(v_1), 3.0 / 2), v_1) / std::sqrt(j);

  return (d_0 + d_1) * velocity_flip;
}

double CalculateStopDistance(double velocity, double max_accel)
{
  return velocity * velocity / (2 * std::copysign(max_accel, velocity));
}

}  // namespace

namespace WiimoteEmu
{
void EmulateShake(PositionalState* state, ControllerEmu::Shake* const shake_group,
                  float time_elapsed)
{
  auto target_position = shake_group->GetState() * float(shake_group->GetIntensity() / 2);
  for (std::size_t i = 0; i != target_position.data.size(); ++i)
  {
    if (state->velocity.data[i] * std::copysign(1.f, target_position.data[i]) < 0 ||
        state->position.data[i] / target_position.data[i] > 0.5)
    {
      target_position.data[i] *= -1;
    }
  }

  // Time from "top" to "bottom" of one shake.
  const auto travel_time = 1 / shake_group->GetFrequency() / 2;

  Common::Vec3 jerk;
  for (std::size_t i = 0; i != target_position.data.size(); ++i)
  {
    const auto half_distance =
        std::max(std::abs(target_position.data[i]), std::abs(state->position.data[i]));

    jerk.data[i] = half_distance / std::pow(travel_time / 2, 3);
  }

  ApproachPositionWithJerk(state, target_position, jerk, time_elapsed);
}

void EmulateTilt(RotationalState* state, ControllerEmu::Tilt* const tilt_group, float time_elapsed)
{
  const auto target = tilt_group->GetState();

  // 180 degrees is currently the max tilt value.
  const ControlState roll = target.x * MathUtil::PI;
  const ControlState pitch = target.y * MathUtil::PI;

  const auto max_accel = std::pow(tilt_group->GetMaxRotationalVelocity(), 2) / MathUtil::TAU;

  ApproachAngleWithAccel(state, Common::Vec3(pitch, -roll, 0), max_accel, time_elapsed);
}

void EmulateSwing(MotionState* state, ControllerEmu::Force* swing_group, float time_elapsed)
{
  const auto input_state = swing_group->GetState();
  const float max_distance = swing_group->GetMaxDistance();
  const float max_angle = swing_group->GetTwistAngle();

  // Note: Y/Z swapped because X/Y axis to the swing_group is X/Z to the wiimote.
  // X is negated because Wiimote X+ is to the left.
  const auto target_position = Common::Vec3{-input_state.x, -input_state.z, input_state.y};

  // Jerk is scaled based on input distance from center.
  // X and Z scale is connected for sane movement about the circle.
  const auto xz_target_dist = Common::Vec2{target_position.x, target_position.z}.Length();
  const auto y_target_dist = std::abs(target_position.y);
  const auto target_dist = Common::Vec3{xz_target_dist, y_target_dist, xz_target_dist};
  const auto speed = MathUtil::Lerp(Common::Vec3{1, 1, 1} * float(swing_group->GetReturnSpeed()),
                                    Common::Vec3{1, 1, 1} * float(swing_group->GetSpeed()),
                                    target_dist / max_distance);

  // Convert our m/s "speed" to the jerk required to reach this speed when traveling 1 meter.
  const auto max_jerk = speed * speed * speed * 4;

  // Rotational acceleration to approximately match the completion time of our swing.
  const auto max_accel = max_angle * speed.x * speed.x;

  // Apply rotation based on amount of swing.
  const auto target_angle =
      Common::Vec3{-target_position.z, 0, target_position.x} / max_distance * max_angle;

  // Angular acceleration * 2 seems to reduce "spurious stabs" in ZSS.
  // TODO: Fix properly.
  ApproachAngleWithAccel(state, target_angle, max_accel * 2, time_elapsed);

  // Clamp X and Z rotation.
  for (const int c : {0, 2})
  {
    if (std::abs(state->angle.data[c] / max_angle) > 1 &&
        MathUtil::Sign(state->angular_velocity.data[c]) == MathUtil::Sign(state->angle.data[c]))
    {
      state->angular_velocity.data[c] = 0;
    }
  }

  // Adjust target position backwards based on swing progress and max angle
  // to simulate a swing with an outstretched arm.
  const auto backwards_angle = std::max(std::abs(state->angle.x), std::abs(state->angle.z));
  const auto backwards_movement = (1 - std::cos(backwards_angle)) * max_distance;

  // TODO: Backswing jerk should be based on x/z speed.

  ApproachPositionWithJerk(state, target_position + Common::Vec3{0, backwards_movement, 0},
                           max_jerk, time_elapsed);

  // Clamp Left/Right/Up/Down movement within the configured circle.
  const auto xz_progress =
      Common::Vec2{state->position.x, state->position.z}.Length() / max_distance;
  if (xz_progress > 1)
  {
    state->position.x /= xz_progress;
    state->position.z /= xz_progress;

    state->acceleration.x = state->acceleration.z = 0;
    state->velocity.x = state->velocity.z = 0;
  }

  // Clamp Forward/Backward movement within the configured distance.
  // We allow additional backwards movement for the back swing.
  const auto y_progress = state->position.y / max_distance;
  const auto max_y_progress = 2 - std::cos(max_angle);
  if (y_progress > max_y_progress || y_progress < -1)
  {
    state->position.y =
        std::clamp(state->position.y, -1.f * max_distance, max_y_progress * max_distance);
    state->velocity.y = 0;
    state->acceleration.y = 0;
  }
}

WiimoteCommon::DataReportBuilder::AccelData ConvertAccelData(const Common::Vec3& accel, u16 zero_g,
                                                             u16 one_g)
{
  const auto scaled_accel = accel * (one_g - zero_g) / float(GRAVITY_ACCELERATION);

  // 10-bit integers.
  constexpr long MAX_VALUE = (1 << 10) - 1;

  return {u16(std::clamp(std::lround(scaled_accel.x + zero_g), 0l, MAX_VALUE)),
          u16(std::clamp(std::lround(scaled_accel.y + zero_g), 0l, MAX_VALUE)),
          u16(std::clamp(std::lround(scaled_accel.z + zero_g), 0l, MAX_VALUE))};
}

void EmulateCursor(MotionState* state, ControllerEmu::Cursor* ir_group, float time_elapsed)
{
  const auto cursor = ir_group->GetState(true);

  if (!cursor.IsVisible())
  {
    // Move the wiimote a kilometer forward so the sensor bar is always behind it.
    *state = {};
    state->position = {0, -1000, 0};
    return;
  }

  // Nintendo recommends a distance of 1-3 meters.
  constexpr float NEUTRAL_DISTANCE = 2.f;

  // When the sensor bar position is on bottom, apply the "offset" setting negatively.
  // This is kinda odd but it does seem to maintain consistent cursor behavior.
  const bool sensor_bar_on_top = Config::Get(Config::SYSCONF_SENSOR_BAR_POSITION) != 0;

  const float height = ir_group->GetVerticalOffset() * (sensor_bar_on_top ? 1 : -1);

  const float yaw_scale = ir_group->GetTotalYaw() / 2;
  const float pitch_scale = ir_group->GetTotalPitch() / 2;

  // Just jump to the target position.
  state->position = {0, NEUTRAL_DISTANCE, -height};
  state->velocity = {};
  state->acceleration = {};

  const auto target_angle = Common::Vec3(pitch_scale * -cursor.y, 0, yaw_scale * -cursor.x);

  // If cursor was hidden, jump to the target angle immediately.
  if (state->position.y < 0)
  {
    state->angle = target_angle;
    state->angular_velocity = {};

    return;
  }

  // Higher values will be more responsive but increase rate of M+ "desync".
  // I'd rather not expose this value in the UI if not needed.
  // At this value, sync is very good and responsiveness still appears instant.
  constexpr auto MAX_ACCEL = float(MathUtil::TAU * 8);

  ApproachAngleWithAccel(state, target_angle, MAX_ACCEL, time_elapsed);
}

void ApproachAngleWithAccel(RotationalState* state, const Common::Vec3& angle_target,
                            float max_accel, float time_elapsed)
{
  const auto stop_distance =
      Common::Vec3(CalculateStopDistance(state->angular_velocity.x, max_accel),
                   CalculateStopDistance(state->angular_velocity.y, max_accel),
                   CalculateStopDistance(state->angular_velocity.z, max_accel));

  const auto offset = angle_target - state->angle;
  const auto stop_offset = offset - stop_distance;
  const auto accel = MathUtil::Sign(stop_offset) * max_accel;

  state->angular_velocity += accel * time_elapsed;

  const auto change_in_angle =
      state->angular_velocity * time_elapsed + accel * time_elapsed * time_elapsed / 2;

  for (std::size_t i = 0; i != offset.data.size(); ++i)
  {
    // If new angle will overshoot stop right on target.
    if (std::abs(offset.data[i]) < 0.0001 || (change_in_angle.data[i] / offset.data[i] > 1.0))
    {
      state->angular_velocity.data[i] =
          (angle_target.data[i] - state->angle.data[i]) / time_elapsed;
      state->angle.data[i] = angle_target.data[i];
    }
    else
    {
      state->angle.data[i] += change_in_angle.data[i];
    }
  }
}

static Common::Vec3 NormalizeAngle(Common::Vec3 angle)
{
  // TODO: There must be a more elegant way to do this
  angle.x = fmod(angle.x, float(MathUtil::TAU));
  angle.y = fmod(angle.y, float(MathUtil::TAU));
  angle.z = fmod(angle.z, float(MathUtil::TAU));
  angle.x += angle.x < 0 ? float(MathUtil::TAU) : 0;
  angle.y += angle.y < 0 ? float(MathUtil::TAU) : 0;
  angle.z += angle.z < 0 ? float(MathUtil::TAU) : 0;
  return angle;
}

static Common::Vec3 ComplementaryFilter(const Common::Vec3& angle,
                                        const Common::Vec3& accelerometer,
                                        const Common::Vec3& gyroscope, float time_elapsed)
{
  Common::Vec3 gyroangle = angle + gyroscope * time_elapsed;
  gyroangle = NormalizeAngle(gyroangle);

  // Calculate accelerometer tilt angles
  Common::Vec3 accangle = gyroangle;
  if ((accelerometer.x != 0 && accelerometer.y != 0) || accelerometer.z != 0)
  {
    float accpitch = -atan2(accelerometer.y, -accelerometer.z) + float(MathUtil::PI);
    float accroll = atan2(accelerometer.x, -accelerometer.z) + float(MathUtil::PI);
    accangle = {accpitch, accroll, gyroangle.z};
  }

  // Massage accelerometer and gyroscope angle values so that averaging them works when they are on
  // opposite sides of TAU / zero (which both represent the same angle)
  // TODO: There must be a more elegant way to do this
  constexpr float DEG360 = float(MathUtil::TAU);
  constexpr float DEG270 = DEG360 * 0.75f;
  constexpr float DEG90 = DEG360 * 0.25f;
  if (accangle.x < DEG90 && gyroangle.x > DEG270)
    accangle.x += DEG360;
  else if (gyroangle.x < DEG90 && accangle.x > DEG270)
    gyroangle.x += DEG360;
  if (accangle.y < DEG90 && gyroangle.y > DEG270)
    accangle.y += DEG360;
  else if (gyroangle.y < DEG90 && accangle.y > DEG270)
    gyroangle.y += DEG360;

  // Combine accelerometer and gyroscope angles
  return NormalizeAngle((gyroangle * 0.98f) + (accangle * 0.02f));
}

void EmulateIMUCursor(std::optional<RotationalState>* state, ControllerEmu::IMUCursor* imu_ir_group,
                      ControllerEmu::IMUAccelerometer* imu_accelerometer_group,
                      ControllerEmu::IMUGyroscope* imu_gyroscope_group, float time_elapsed)
{
  // Avoid having to double dereference
  auto& st = *state;

  if (!imu_ir_group->enabled)
  {
    st = std::nullopt;
    return;
  }

  auto accel = imu_accelerometer_group->GetState();
  auto ang_vel = imu_gyroscope_group->GetState();

  // The IMU Cursor requires both an accelerometer and a gyroscope to function correctly.
  if (!(accel.has_value() && ang_vel.has_value()))
  {
    st = std::nullopt;
    return;
  }

  if (!st.has_value())
    st = RotationalState{};

  st->angle = ComplementaryFilter(st->angle, accel.value(), ang_vel.value(), time_elapsed);

  // Reset camera yaw angle
  constexpr ControlState BUTTON_THRESHOLD = 0.5;
  if (imu_ir_group->controls[0]->control_ref->State() > BUTTON_THRESHOLD)
    st->angle.z = 0;

  // Limit camera yaw angle
  float totalyaw = float(imu_ir_group->GetTotalYaw());
  float yawmax = totalyaw / 2;
  float yawmin = float(MathUtil::TAU) - totalyaw / 2;
  if (st->angle.z > yawmax && st->angle.z <= float(MathUtil::PI))
    st->angle.z = yawmax;
  if (st->angle.z < yawmin && st->angle.z > float(MathUtil::PI))
    st->angle.z = yawmin;
}

void ApproachPositionWithJerk(PositionalState* state, const Common::Vec3& position_target,
                              const Common::Vec3& max_jerk, float time_elapsed)
{
  const auto stop_distance =
      Common::Vec3(CalculateStopDistance(state->velocity.x, state->acceleration.x, max_jerk.x),
                   CalculateStopDistance(state->velocity.y, state->acceleration.y, max_jerk.y),
                   CalculateStopDistance(state->velocity.z, state->acceleration.z, max_jerk.z));

  const auto offset = position_target - state->position;
  const auto stop_offset = offset - stop_distance;
  const auto jerk = MathUtil::Sign(stop_offset) * max_jerk;

  state->acceleration += jerk * time_elapsed;

  state->velocity += state->acceleration * time_elapsed + jerk * time_elapsed * time_elapsed / 2;

  const auto change_in_position = state->velocity * time_elapsed +
                                  state->acceleration * time_elapsed * time_elapsed / 2 +
                                  jerk * time_elapsed * time_elapsed * time_elapsed / 6;

  for (std::size_t i = 0; i != offset.data.size(); ++i)
  {
    // If new velocity will overshoot assume we would have stopped right on target.
    // TODO: Improve check to see if less jerk would have caused undershoot.
    if ((change_in_position.data[i] / offset.data[i]) > 1.0)
    {
      state->acceleration.data[i] = 0;
      state->velocity.data[i] = 0;
      state->position.data[i] = position_target.data[i];
    }
    else
    {
      state->position.data[i] += change_in_position.data[i];
    }
  }
}

Common::Matrix33 GetRotationalMatrix(const Common::Vec3& angle)
{
  return Common::Matrix33::RotateZ(angle.z) * Common::Matrix33::RotateY(angle.y) *
         Common::Matrix33::RotateX(angle.x);
}

}  // namespace WiimoteEmu
