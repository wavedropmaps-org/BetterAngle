#ifndef LOGIC_H
#define LOGIC_H

#include <atomic>
#include <array>
#include <windows.h>
#include <string>

struct InputSample {
  LARGE_INTEGER qpc;
  int dx;
};

class AngleLogic {
public:
    AngleLogic(double sensX);
    void Update(int dx);
    double GetAngle() const;
    long long GetAccumDx() const { return m_accumDx.load(); }
    void SetZero();
    void LoadProfile(double sensX);
    void SetDivingState(bool diving);
    void ApplyRetroCorrection(LARGE_INTEGER frameTime, bool newDiving);

private:
    std::atomic<double> m_sensX;
    std::atomic<bool>   m_isDiving;

    std::atomic<long long> m_accumDx;
    std::atomic<long long> m_baseDx;
    std::atomic<double>    m_baseAngle;
    std::atomic<double>    m_currentScale;

    std::array<InputSample, 512> m_inputRing;
    std::atomic<int> m_ringHead{0};
    LARGE_INTEGER m_qpcFreq;

    double Norm360(double a) const;
    void BakeScale();
};

#endif // LOGIC_H
