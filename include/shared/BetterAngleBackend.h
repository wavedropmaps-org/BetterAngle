#pragma once
#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>

class BetterAngleBackend : public QObject {
  Q_OBJECT
  Q_INVOKABLE void finishBooting();
  Q_PROPERTY(double sensX READ sensX WRITE setSensX NOTIFY profileChanged)
  Q_PROPERTY(double sensY READ sensY WRITE setSensY NOTIFY profileChanged)
  Q_PROPERTY(
      int tolerance READ tolerance WRITE setTolerance NOTIFY profileChanged)
  Q_PROPERTY(double diveGlideMatch READ diveGlideMatch WRITE setDiveGlideMatch
                 NOTIFY profileChanged)
  Q_PROPERTY(int screenIndex READ screenIndex WRITE setScreenIndex NOTIFY profileChanged)
  Q_PROPERTY(QStringList availableScreens READ availableScreens CONSTANT)

  Q_PROPERTY(bool crosshairOn READ crosshairOn WRITE setCrosshairOn NOTIFY
                 crosshairChanged)
  Q_PROPERTY(float crossThickness READ crossThickness WRITE setCrossThickness
                 NOTIFY crosshairChanged)
  Q_PROPERTY(float crossOffsetX READ crossOffsetX WRITE setCrossOffsetX NOTIFY
                 crosshairChanged)
  Q_PROPERTY(float crossOffsetY READ crossOffsetY WRITE setCrossOffsetY NOTIFY
                 crosshairChanged)
  Q_PROPERTY(bool crossPulse READ crossPulse WRITE setCrossPulse NOTIFY
                 crosshairChanged)
  Q_PROPERTY(QColor crossColor READ crossColor WRITE setCrossColor NOTIFY
                 crosshairChanged)

  Q_PROPERTY(QString versionStr READ versionStr CONSTANT)
  Q_PROPERTY(
      QString latestVersion READ latestVersion NOTIFY updateStatusChanged)
  Q_PROPERTY(
      bool updateAvailable READ updateAvailable NOTIFY updateStatusChanged)
  Q_PROPERTY(
      bool downloadComplete READ downloadComplete NOTIFY updateStatusChanged)
  Q_PROPERTY(
      QString updateHistory READ updateHistory NOTIFY updateStatusChanged)
  Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY updateStatusChanged)
  Q_PROPERTY(bool hasCheckedForUpdates READ hasCheckedForUpdates NOTIFY
                 updateStatusChanged)
  Q_PROPERTY(bool isCheckingForUpdates READ isCheckingForUpdates NOTIFY
                 updateStatusChanged)
  Q_PROPERTY(bool isDownloading READ isDownloading NOTIFY updateStatusChanged)

  // Debug Diagnostics
  Q_PROPERTY(bool fnRunning READ fnRunning NOTIFY debugDataChanged)
  Q_PROPERTY(bool fnFocused READ fnFocused NOTIFY debugDataChanged)
  Q_PROPERTY(bool fnMouseHidden READ fnMouseHidden NOTIFY debugDataChanged)
  Q_PROPERTY(bool showDebugOverlay READ showDebugOverlay WRITE
                 setShowDebugOverlay NOTIFY debugDataChanged)
  Q_PROPERTY(
      long long detectionDelayMs READ detectionDelayMs NOTIFY debugDataChanged)
  Q_PROPERTY(
      int detectionRatioPct READ detectionRatioPct NOTIFY debugDataChanged)
  Q_PROPERTY(bool inputLocked READ inputLocked NOTIFY debugDataChanged)
  Q_PROPERTY(bool isDiving READ isDiving NOTIFY debugDataChanged)
  Q_PROPERTY(QString lockTriggerReason READ lockTriggerReason NOTIFY debugDataChanged)
  Q_PROPERTY(int peakMatchPct READ peakMatchPct NOTIFY debugDataChanged)
  Q_PROPERTY(QString roiDimensions READ roiDimensions NOTIFY debugDataChanged)
  Q_PROPERTY(int scannerCpuPct READ scannerCpuPct NOTIFY debugDataChanged)
  Q_PROPERTY(QString physicalKeyStates READ physicalKeyStates NOTIFY debugDataChanged)
  Q_PROPERTY(bool ghostMismatch READ ghostMismatch NOTIFY debugDataChanged)
  Q_PROPERTY(QString rawWState READ rawWState NOTIFY debugDataChanged)
  Q_PROPERTY(QString inputLockStatus READ inputLockStatus NOTIFY debugDataChanged)
  Q_PROPERTY(QString nitroSyncLog READ nitroSyncLog NOTIFY debugDataChanged)

  // Custom Keybinds
  Q_PROPERTY(
      QString keyToggle READ keyToggle WRITE setKeyToggle NOTIFY hotkeysChanged)
  Q_PROPERTY(QString keyRoi READ keyRoi WRITE setKeyRoi NOTIFY hotkeysChanged)
  Q_PROPERTY(
      QString keyCross READ keyCross WRITE setKeyCross NOTIFY hotkeysChanged)
  Q_PROPERTY(
      QString keyZero READ keyZero WRITE setKeyZero NOTIFY hotkeysChanged)

public:
  explicit BetterAngleBackend(QObject *parent = nullptr);

  double sensX() const;
  void setSensX(double v);

  double sensY() const;
  void setSensY(double v);

  int tolerance() const;
  void setTolerance(int v);

  double diveGlideMatch() const;
  void setDiveGlideMatch(double v);

  int screenIndex() const;
  void setScreenIndex(int v);
  QStringList availableScreens() const;

  bool crosshairOn() const;
  void setCrosshairOn(bool v);

  float crossThickness() const;
  void setCrossThickness(float v);

  float crossOffsetX() const;
  void setCrossOffsetX(float v);

  float crossOffsetY() const;
  void setCrossOffsetY(float v);

  bool crossPulse() const;
  void setCrossPulse(bool v);

  QColor crossColor() const;
  void setCrossColor(const QColor &c);

  QString versionStr() const;
  QString latestVersion() const;
  bool updateAvailable() const;
  bool isDownloading() const;
  bool downloadComplete() const;
  QString updateHistory() const;

  QString updateStatus() const;
  bool hasCheckedForUpdates() const;
  bool isCheckingForUpdates() const;

  bool fnRunning() const;
  bool fnFocused() const;
  bool fnMouseHidden() const;
  bool showDebugOverlay() const;
  void setShowDebugOverlay(bool v);
  long long detectionDelayMs() const;
  int detectionRatioPct() const;
  bool inputLocked() const;
  bool isDiving() const;
  QString lockTriggerReason() const;
  int peakMatchPct() const;
  QString roiDimensions() const;
  int scannerCpuPct() const;
  QString physicalKeyStates() const;
  bool ghostMismatch() const;
  QString rawWState() const;
  QString inputLockStatus() const;
  QString nitroSyncLog() const;
  Q_INVOKABLE void refreshDebugData();

  Q_INVOKABLE void terminateApp();
  Q_INVOKABLE void checkForUpdates();
  Q_INVOKABLE void downloadUpdate();
  Q_INVOKABLE void requestShowControlPanel();

  // Crosshair preset management
  Q_INVOKABLE QStringList crosshairPresetNames() const;
  Q_INVOKABLE void saveCrosshairPreset(const QString &name);
  Q_INVOKABLE void loadCrosshairPreset(int index);
  Q_INVOKABLE void deleteCrosshairPreset(int index);
  Q_INVOKABLE void resetCrosshairToDefaults();

  // Keybind management
  QString keyToggle() const;
  void setKeyToggle(const QString &s);
  QString keyRoi() const;
  void setKeyRoi(const QString &s);
  QString keyCross() const;
  void setKeyCross(const QString &s);
  QString keyZero() const;
  void setKeyZero(const QString &s);

  Q_INVOKABLE void saveKeybinds();

  // Keybind assignment state management
  Q_INVOKABLE void startKeybindAssignment();
  Q_INVOKABLE void endKeybindAssignment();

  Q_INVOKABLE void setZero();

signals:
  void profileChanged();
  void crosshairChanged();
  void crosshairPresetsChanged();
  void hotkeysChanged();

  void updateStatusChanged();
  void showControlPanelRequested();
  void debugDataChanged();

private:
};
