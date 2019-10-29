// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/Config/Mapping/WiimoteEmuMotionControlIMU.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"

#include "DolphinQt/Config/ControllerInterface/ControllerInterfaceWindow.h"
#include "DolphinQt/Config/Mapping/MappingWindow.h"

#include "InputCommon/ControllerEmu/ControlGroup/IMUAccelerometer.h"
#include "InputCommon/ControllerEmu/ControlGroup/IMUGyroscope.h"
#include "InputCommon/InputConfig.h"

WiimoteEmuMotionControlIMU::WiimoteEmuMotionControlIMU(MappingWindow* window)
    : MappingWidget(window), m_parent(window)
{
  CreateMainLayout();
}

template <class ControlGroup>
QGroupBox* WiimoteEmuMotionControlIMU::AddResetToDefaultButton(QGroupBox* groupbox,
                                                               ControlGroup* controlgroup)
{
  QFormLayout* layout = static_cast<QFormLayout*>(groupbox->layout());

  // spacer
  layout->addRow(QLatin1String(" "), (QWidget*)nullptr);

  auto* reset_to_default_button = new QPushButton(tr("Reset to Default"));
  layout->addRow(reset_to_default_button);

  connect(reset_to_default_button, &QPushButton::clicked, this, [this, controlgroup] {
    controlgroup->LoadDefaults();
    m_parent->GetController()->UpdateReferences(g_controller_interface);
    m_parent->ConfigChanged();
    m_parent->Save();
  });

  return groupbox;
}

void WiimoteEmuMotionControlIMU::CreateMainLayout()
{
  auto* warning_layout = new QHBoxLayout();
  auto* warning_label =
      new QLabel(tr("WARNING: The controls under Accelerometer and Gyroscope are designed to "
                    "interface directly with motion sensor hardware. They are not intended for "
                    "mapping traditional buttons, triggers or axes. You might need to configure "
                    "alternate input sources before using these controls."));
  warning_label->setWordWrap(true);
  auto* warning_input_sources_button = new QPushButton(tr("Alternate Input Sources"));
  warning_layout->addWidget(warning_label, 1);
  warning_layout->addWidget(warning_input_sources_button, 0, Qt::AlignRight);
  connect(warning_input_sources_button, &QPushButton::clicked, this, [this] {
    ControllerInterfaceWindow* window = new ControllerInterfaceWindow(this);
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    window->setWindowModality(Qt::WindowModality::WindowModal);
    window->show();
  });

  auto* groups_layout = new QHBoxLayout();
  ControllerEmu::IMUCursor* point_group = static_cast<ControllerEmu::IMUCursor*>(
      Wiimote::GetWiimoteGroup(GetPort(), WiimoteEmu::WiimoteGroup::IMUPoint));
  groups_layout->addWidget(CreateGroupBox(point_group));
  ControllerEmu::IMUAccelerometer* accel_group = static_cast<ControllerEmu::IMUAccelerometer*>(
      Wiimote::GetWiimoteGroup(GetPort(), WiimoteEmu::WiimoteGroup::IMUAccelerometer));
  groups_layout->addWidget(AddResetToDefaultButton(CreateGroupBox(accel_group), accel_group));
  ControllerEmu::IMUGyroscope* gyro_group = static_cast<ControllerEmu::IMUGyroscope*>(
      Wiimote::GetWiimoteGroup(GetPort(), WiimoteEmu::WiimoteGroup::IMUGyroscope));
  groups_layout->addWidget(AddResetToDefaultButton(CreateGroupBox(gyro_group), gyro_group));

  m_main_layout = new QVBoxLayout();
  m_main_layout->addLayout(warning_layout);
  m_main_layout->addLayout(groups_layout);

  setLayout(m_main_layout);
}

void WiimoteEmuMotionControlIMU::LoadSettings()
{
  Wiimote::LoadConfig();
}

void WiimoteEmuMotionControlIMU::SaveSettings()
{
  Wiimote::GetConfig()->SaveConfig();
}

InputConfig* WiimoteEmuMotionControlIMU::GetConfig()
{
  return Wiimote::GetConfig();
}
