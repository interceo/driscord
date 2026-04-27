#pragma once
#include <QObject>

struct AudioSettings {
    float noiseGateThreshold = 0.0f;
    bool noiseSuppressionEnabled = false;
    bool vadEnabled = false;
    float vadOpenThreshold = 0.6f;
    float vadCloseThreshold = 0.3f;
    int vadHangoverMs = 200;
    int expectedLossPct = 10;
};

class AudioSettingsStore : public QObject {
    Q_OBJECT
public:
    explicit AudioSettingsStore(QObject* parent = nullptr);

    AudioSettings load() const;
    void save(const AudioSettings& s);
};
