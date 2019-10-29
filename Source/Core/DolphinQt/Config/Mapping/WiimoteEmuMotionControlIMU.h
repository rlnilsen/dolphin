// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "DolphinQt/Config/Mapping/MappingWidget.h"

class QCheckBox;
class QFormLayout;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

class WiimoteEmuMotionControlIMU final : public MappingWidget
{
  Q_OBJECT
public:
  explicit WiimoteEmuMotionControlIMU(MappingWindow* window);

  InputConfig* GetConfig() override;

private:
  void LoadSettings() override;
  void SaveSettings() override;
  void CreateMainLayout();
  template <class ControlGroup>
  QGroupBox* AddResetToDefaultButton(QGroupBox* groupbox, ControlGroup* controlgroup);
  MappingWindow* m_parent;
  // Main
  QVBoxLayout* m_main_layout;
};
