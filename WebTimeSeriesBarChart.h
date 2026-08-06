#pragma once

#include <Arduino.h>
#include <WebDisplay.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <vector>

class WebTimeSeriesBarChart : public WebDisplayBase {
public:
    struct Bin {
        uint32_t startTimestamp;
        float value;
    };

    WebTimeSeriesBarChart(const String& id,
                          uint32_t updateIntervalSecs,
                          const String& title,
                          const String& xLabel,
                          const String& yUnit,
                          uint32_t binWidthSeconds,
                          size_t maxBins = 52,
                          uint32_t binAlignmentTimestamp = 0,
                          float yAxisMin = 1.0f,
                          float yAxisMax = 0.0f);

    ~WebTimeSeriesBarChart() override;

    WebTimeSeriesBarChart(const WebTimeSeriesBarChart&) = delete;
    WebTimeSeriesBarChart& operator=(const WebTimeSeriesBarChart&) = delete;

    virtual bool add(uint32_t timestamp, float amount);
    virtual void clear();

    size_t size() const;
    std::vector<Bin> bins() const;

    String routeText() const override;
    String createHtmlFragment() const override;

private:
    void lock_() const;
    void unlock_() const;

    uint32_t alignedBinStart_(uint32_t timestamp) const;
    bool hasFixedYAxisUnlocked_() const;

    static uint32_t saturatingAdd_(uint32_t value, uint32_t increment);
    static uint32_t ceilToStep_(uint32_t value, uint32_t step);
    static uint32_t floorToStep_(uint32_t value, uint32_t step);
    static int countTicksForStep_(uint32_t xMin, uint32_t xMax, uint32_t step);
    static std::vector<uint32_t> computeNiceXTicks_(uint32_t xMin, uint32_t xMax);

    static uint8_t decimalsForStep_(float step);
    static float niceYStepCandidate_(float range, int exponentOffset, uint8_t baseIndex);
    static int countYTicksForStep_(float yMin, float yMax, float step);
    static std::vector<float> computeNiceYTicks_(float yMin,
                                                  float yMax,
                                                  uint8_t& decimals);

    bool computeEffectiveYAxis_(const std::vector<Bin>& bins,
                                float& yMin,
                                float& yMax) const;

    static String htmlEscape_(const String& in);
    static String jsStringLiteral_(const String& in);

    String title_;
    String xLabel_;
    String yUnit_;

    const uint32_t binWidthSeconds_;
    const size_t maxBins_;
    const uint32_t binAlignmentTimestamp_;

    std::vector<Bin> bins_;

    float yAxisMin_;
    float yAxisMax_;

    mutable SemaphoreHandle_t accessMutex_{nullptr};
};
