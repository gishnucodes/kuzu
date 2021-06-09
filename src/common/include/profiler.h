#pragma once

#include <memory>
#include <unordered_map>

#include "src/common/include/metric.h"

namespace graphflow {
namespace common {

class Profiler {

public:
    TimeMetric* registerTimeMetric(const string& key);

    NumericMetric* registerNumericMetric(const string& key);

    double sumAllTimeMetricsWithKey(const string& key);

    uint64_t sumAllNumericMetricsWithKey(const string& key);

    void reset();

private:
    void addMetric(const string& key, unique_ptr<Metric> metric);

public:
    bool enabled;
    unordered_map<string, vector<unique_ptr<Metric>>> metrics;
};

} // namespace common
} // namespace graphflow
