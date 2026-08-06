#pragma once

#include <WebTimeSeriesBarChart.h>

class PersistentWebTimeSeriesBarChart : public WebTimeSeriesBarChart {
public:
    PersistentWebTimeSeriesBarChart(const String& nvsNamespace,
                                    const String& id,
                                    uint32_t updateIntervalSecs,
                                    const String& title,
                                    const String& xLabel,
                                    const String& yUnit,
                                    uint32_t binWidthSeconds,
                                    size_t maxBins = 52,
                                    uint32_t binAlignmentTimestamp = 0,
                                    float yAxisMin = 1.0f,
                                    float yAxisMax = 0.0f);

    bool begin();
    bool load();
    bool save() const;

    bool add(uint32_t timestamp, float amount) override;
    void clear() override;

    bool lastPersistenceSucceeded() const;

private:
    String nvsNamespace_;
    uint32_t binWidthSecondsForStorage_;
    size_t maxBinsForStorage_;
    uint32_t binAlignmentOffset_;
    mutable bool lastPersistenceSucceeded_{true};
};
