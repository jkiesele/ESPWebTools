#include "PersistentWebTimeSeriesBarChart.h"

#include <Preferences.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {

constexpr uint32_t STORAGE_MAGIC = 0x57544243UL;
constexpr uint16_t STORAGE_VERSION = 1;
constexpr const char* STORAGE_KEY = "bins";

struct StorageHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint32_t binWidthSeconds;
    uint32_t binAlignmentOffset;
};

struct StoredBin {
    uint32_t startTimestamp;
    float value;
};

static_assert(sizeof(StorageHeader) == 16, "Unexpected storage header layout");
static_assert(sizeof(StoredBin) == 8, "Unexpected stored bin layout");

}  // namespace

PersistentWebTimeSeriesBarChart::PersistentWebTimeSeriesBarChart(
    const String& nvsNamespace,
    const String& id,
    uint32_t updateIntervalSecs,
    const String& title,
    const String& xLabel,
    const String& yUnit,
    uint32_t binWidthSeconds,
    size_t maxBins,
    uint32_t binAlignmentTimestamp,
    float yAxisMin,
    float yAxisMax)
    : WebTimeSeriesBarChart(id,
                            updateIntervalSecs,
                            title,
                            xLabel,
                            yUnit,
                            binWidthSeconds,
                            maxBins,
                            binAlignmentTimestamp,
                            yAxisMin,
                            yAxisMax),
      nvsNamespace_(nvsNamespace),
      binWidthSecondsForStorage_(binWidthSeconds == 0 ? 1 : binWidthSeconds),
      maxBinsForStorage_(maxBins),
      binAlignmentOffset_(binAlignmentTimestamp % binWidthSecondsForStorage_) {}

bool PersistentWebTimeSeriesBarChart::begin() {
    return load();
}

bool PersistentWebTimeSeriesBarChart::load() {
    Preferences preferences;
    if (!preferences.begin(nvsNamespace_.c_str(), false)) {
        lastPersistenceSucceeded_ = false;
        return false;
    }

    const size_t storedSize = preferences.getBytesLength(STORAGE_KEY);
    if (storedSize == 0) {
        preferences.end();
        WebTimeSeriesBarChart::clear();
        lastPersistenceSucceeded_ = true;
        return true;
    }

    std::vector<uint8_t> data(storedSize);
    const size_t bytesRead = preferences.getBytes(STORAGE_KEY, data.data(), data.size());
    preferences.end();

    if (bytesRead != storedSize || storedSize < sizeof(StorageHeader)) {
        lastPersistenceSucceeded_ = false;
        return false;
    }

    StorageHeader header;
    memcpy(&header, data.data(), sizeof(header));

    const size_t expectedSize =
        sizeof(StorageHeader) + static_cast<size_t>(header.count) * sizeof(StoredBin);

    if (header.magic != STORAGE_MAGIC ||
        header.version != STORAGE_VERSION ||
        header.binWidthSeconds != binWidthSecondsForStorage_ ||
        header.binAlignmentOffset != binAlignmentOffset_ ||
        header.count > maxBinsForStorage_ ||
        storedSize != expectedSize) {
        lastPersistenceSucceeded_ = false;
        return false;
    }

    std::vector<StoredBin> storedBins(header.count);
    if (!storedBins.empty()) {
        memcpy(storedBins.data(),
               data.data() + sizeof(StorageHeader),
               storedBins.size() * sizeof(StoredBin));
    }

    for (size_t i = 0; i < storedBins.size(); ++i) {
        const StoredBin& bin = storedBins[i];
        if (!std::isfinite(bin.value)) {
            lastPersistenceSucceeded_ = false;
            return false;
        }

        const bool aligned =
            (bin.startTimestamp == 0 && binAlignmentOffset_ != 0) ||
            (bin.startTimestamp >= binAlignmentOffset_ &&
             (bin.startTimestamp - binAlignmentOffset_) % binWidthSecondsForStorage_ == 0);
        if (!aligned) {
            lastPersistenceSucceeded_ = false;
            return false;
        }

        if (i > 0) {
            const uint64_t expectedStart =
                static_cast<uint64_t>(storedBins[i - 1].startTimestamp) +
                binWidthSecondsForStorage_;
            if (expectedStart > std::numeric_limits<uint32_t>::max() ||
                bin.startTimestamp != static_cast<uint32_t>(expectedStart)) {
                lastPersistenceSucceeded_ = false;
                return false;
            }
        }
    }

    WebTimeSeriesBarChart::clear();
    for (const StoredBin& bin : storedBins) {
        if (!WebTimeSeriesBarChart::add(bin.startTimestamp, bin.value)) {
            WebTimeSeriesBarChart::clear();
            lastPersistenceSucceeded_ = false;
            return false;
        }
    }

    lastPersistenceSucceeded_ = true;
    return true;
}

bool PersistentWebTimeSeriesBarChart::save() const {
    const std::vector<Bin> snapshot = bins();
    if (snapshot.size() > std::numeric_limits<uint16_t>::max()) {
        lastPersistenceSucceeded_ = false;
        return false;
    }

    StorageHeader header{
        STORAGE_MAGIC,
        STORAGE_VERSION,
        static_cast<uint16_t>(snapshot.size()),
        binWidthSecondsForStorage_,
        binAlignmentOffset_
    };

    const size_t storedSize =
        sizeof(StorageHeader) + snapshot.size() * sizeof(StoredBin);
    std::vector<uint8_t> data(storedSize);
    memcpy(data.data(), &header, sizeof(header));

    for (size_t i = 0; i < snapshot.size(); ++i) {
        const StoredBin storedBin{snapshot[i].startTimestamp, snapshot[i].value};
        memcpy(data.data() + sizeof(header) + i * sizeof(StoredBin),
               &storedBin,
               sizeof(storedBin));
    }

    Preferences preferences;
    if (!preferences.begin(nvsNamespace_.c_str(), false)) {
        lastPersistenceSucceeded_ = false;
        return false;
    }

    const size_t bytesWritten = preferences.putBytes(STORAGE_KEY, data.data(), data.size());
    preferences.end();

    lastPersistenceSucceeded_ = bytesWritten == data.size();
    return lastPersistenceSucceeded_;
}

bool PersistentWebTimeSeriesBarChart::add(uint32_t timestamp, float amount) {
    const bool accepted = WebTimeSeriesBarChart::add(timestamp, amount);
    if (accepted && amount != 0.0f) {
        save();
    }
    return accepted;
}

void PersistentWebTimeSeriesBarChart::clear() {
    WebTimeSeriesBarChart::clear();

    Preferences preferences;
    if (!preferences.begin(nvsNamespace_.c_str(), false)) {
        lastPersistenceSucceeded_ = false;
        return;
    }

    const bool removed = !preferences.isKey(STORAGE_KEY) || preferences.remove(STORAGE_KEY);
    preferences.end();
    lastPersistenceSucceeded_ = removed;
}

bool PersistentWebTimeSeriesBarChart::lastPersistenceSucceeded() const {
    return lastPersistenceSucceeded_;
}
