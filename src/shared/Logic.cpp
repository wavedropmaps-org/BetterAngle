#include "shared/Logic.h"
#include "shared/State.h"
#include <atomic>
#include <string>


AngleLogic::AngleLogic(double sensX)
    : m_sensX(sensX), m_isDiving(false), m_accumDx(0), m_baseDx(0),
      m_baseAngle(0.0), m_currentScale(0.00555555 * sensX), m_ringHead(0) {
  QueryPerformanceFrequency(&m_qpcFreq);
  BakeScale();
}

void AngleLogic::Update(int dx) {
  m_accumDx += dx;
  // Record into ring buffer for retroactive correction
  int slot = m_ringHead.fetch_add(1, std::memory_order_relaxed) & 511;
  QueryPerformanceCounter(&m_inputRing[slot].qpc);
  m_inputRing[slot].dx = dx;
}

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

void AngleLogic::ApplyRetroCorrection(LARGE_INTEGER frameTime, bool newDiving) {
  // If no frame time (BitBlt fallback), degrade to normal SetDivingState
  if (frameTime.QuadPart == 0) {
    SetDivingState(newDiving);
    return;
  }

  // Walk ring buffer backward from current head, summing dx for all samples after frameTime
  long long corrDx = 0;
  int head = m_ringHead.load(std::memory_order_relaxed);
  for (int i = 1; i <= 512; i++) {
    const auto &s = m_inputRing[(head - i) & 511];
    if (s.qpc.QuadPart == 0 || s.qpc.QuadPart < frameTime.QuadPart) break;
    corrDx += s.dx;
  }

  // Compute scale correction
  double sensX = m_sensX.load();
  double oldScale = m_currentScale.load();
  double newScale = newDiving ? (sensX * 0.00555555 * 1.0916) : (sensX * 0.00555555);
  double correction = (double)corrDx * (newScale - oldScale);

  // Bake correction into angle before the scale flip
  double bakedAngle = Norm360(GetAngle() + correction);
  m_baseAngle.store(bakedAngle);
  m_baseDx.store(m_accumDx.load());

  // Now do the normal state change (scale flip)
  m_isDiving = newDiving;
  BakeScale();
}
