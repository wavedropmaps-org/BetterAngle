#include "shared/Logic.h"
#include "shared/State.h"
#include <atomic>
#include <string>


AngleLogic::AngleLogic(double sensX)
    : m_sensX(sensX), m_isDiving(false), m_accumDx(0), m_baseDx(0),
      m_baseAngle(0.0), m_currentScale(0.00555555 * sensX) {
  BakeScale();
}

void AngleLogic::Update(int dx) { m_accumDx += dx; }

double AngleLogic::GetAngle() const {
  double delta = (double)(m_accumDx.load() - m_baseDx.load());
  return Norm360(m_baseAngle.load() + (delta * m_currentScale.load()));
}

void AngleLogic::SetZero() {
  m_accumDx = 0;
  m_baseDx = 0;
  m_baseAngle = 0.0;
}

void AngleLogic::LoadProfile(double sensX) {
  // Before updating sensitivity, bake in the current angle to prevent jumping
  m_baseAngle = GetAngle();
  m_baseDx = m_accumDx.load();
  m_sensX = sensX;
  BakeScale();
}

void AngleLogic::SetDivingState(bool diving) {
  if (diving == m_isDiving.load())
    return;

  // Bake in the current angle before switching scales
  m_baseAngle = GetAngle();
  m_baseDx = m_accumDx.load();
  m_isDiving = diving;
  BakeScale();
}

void AngleLogic::BakeScale() {
  double sens = m_sensX.load();
  double scale = 0.00555555 * sens;
  if (m_isDiving.load()) {
    scale *= 1.0916;
  }
  m_currentScale = scale;
}

double AngleLogic::Norm360(double a) const {
  while (a >= 360.0)
    a -= 360.0;
  while (a < 0.0)
    a += 360.0;
  return a;
}
