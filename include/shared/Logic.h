#ifndef LOGIC_H
#define LOGIC_H

#include <atomic>
#include <windows.h>
#include <string>



class AngleLogic {
public:
    AngleLogic(double sensX);
    void Update(int dx);
    double GetAngle() const;
    long long GetAccumDx() const { return m_accumDx.load(); }
    void SetZero();
    void LoadProfile(double sensX);
    void SetDivingState(bool diving);

private:
    std::atomic<double> m_sensX;
    std::atomic<bool>   m_isDiving;
    
    std::atomic<long long> m_accumDx;
    std::atomic<long long> m_baseDx;
    std::atomic<double>    m_baseAngle;
    std::atomic<double>    m_currentScale;
    
    double Norm360(double a) const;
    void BakeScale();
};

#endif // LOGIC_H
