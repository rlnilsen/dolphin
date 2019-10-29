// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "InputCommon/ControllerEmu/ControlGroup/IMUAccelerometer.h"

#include "Common/Common.h"
#include "Common/MathUtil.h"

#include "Core/HW/WiimoteEmu/WiimoteEmu.h"

#include "InputCommon/ControlReference/ControlReference.h"
#include "InputCommon/ControllerEmu/Control/Control.h"
#include "InputCommon/ControllerEmu/Control/Input.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerEmu/Setting/NumericSetting.h"

namespace ControllerEmu
{
IMUAccelerometer::IMUAccelerometer(std::string name, std::string ui_name)
    : ControlGroup(std::move(name), std::move(ui_name), GroupType::IMUAccelerometer)
{
  controls.emplace_back(std::make_unique<Input>(Translate, _trans("Left")));
  controls.emplace_back(std::make_unique<Input>(Translate, _trans("Right")));
  controls.emplace_back(std::make_unique<Input>(Translate, _trans("Forward")));
  controls.emplace_back(std::make_unique<Input>(Translate, _trans("Backward")));
  controls.emplace_back(std::make_unique<Input>(Translate, _trans("Up")));
  controls.emplace_back(std::make_unique<Input>(Translate, _trans("Down")));
}

std::optional<IMUAccelerometer::StateData> IMUAccelerometer::GetState() const
{
  StateData state;
  state.x = (controls[0]->control_ref->State() - controls[1]->control_ref->State());
  state.y = (controls[3]->control_ref->State() - controls[2]->control_ref->State());
  state.z = (controls[4]->control_ref->State() - controls[5]->control_ref->State());

  if (controls[0]->control_ref->BoundCount() != 0)
    return state;
  else
    return std::nullopt;
}

void IMUAccelerometer::LoadDefaults()
{
  SetControlExpression(0, "Accel Left");
  SetControlExpression(1, "Accel Right");
  SetControlExpression(2, "Accel Forward");
  SetControlExpression(3, "Accel Backward");
  SetControlExpression(4, "Accel Up");
  SetControlExpression(5, "Accel Down");
}

}  // namespace ControllerEmu
