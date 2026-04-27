#include "AudioSettingsStore.h"
#include <QSettings>

static const char* kGroup = "audio";

AudioSettingsStore::AudioSettingsStore(QObject* parent)
    : QObject(parent)
{
}

AudioSettings AudioSettingsStore::load() const
{
    AudioSettings d;
    QSettings cfg;
    cfg.beginGroup(kGroup);
    d.noiseGateThreshold = cfg.value("noise_gate_threshold", d.noiseGateThreshold).toFloat();
    d.noiseSuppressionEnabled = cfg.value("noise_suppression_enabled", d.noiseSuppressionEnabled).toBool();
    d.vadEnabled = cfg.value("vad_enabled", d.vadEnabled).toBool();
    d.vadOpenThreshold = cfg.value("vad_open_threshold", d.vadOpenThreshold).toFloat();
    d.vadCloseThreshold = cfg.value("vad_close_threshold", d.vadCloseThreshold).toFloat();
    d.vadHangoverMs = cfg.value("vad_hangover_ms", d.vadHangoverMs).toInt();
    d.expectedLossPct = cfg.value("expected_loss_pct", d.expectedLossPct).toInt();
    cfg.endGroup();
    return d;
}

void AudioSettingsStore::save(const AudioSettings& s)
{
    QSettings cfg;
    cfg.beginGroup(kGroup);
    cfg.setValue("noise_gate_threshold", s.noiseGateThreshold);
    cfg.setValue("noise_suppression_enabled", s.noiseSuppressionEnabled);
    cfg.setValue("vad_enabled", s.vadEnabled);
    cfg.setValue("vad_open_threshold", s.vadOpenThreshold);
    cfg.setValue("vad_close_threshold", s.vadCloseThreshold);
    cfg.setValue("vad_hangover_ms", s.vadHangoverMs);
    cfg.setValue("expected_loss_pct", s.expectedLossPct);
    cfg.endGroup();
}
