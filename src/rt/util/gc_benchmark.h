#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <region/region_api.h>
#include <unordered_map>
#include <vector>

namespace verona::rt::api
{
  /**
   * Internal collector for gathering GC measurements.
   */
  class TestMeasurementCollector
  {
  private:
    std::vector<std::pair<uint64_t, RegionType>> measurements;
    uint64_t total_duration_ns = 0;
    std::unordered_map<int, uint64_t> duration_by_type;
    std::unordered_map<int, size_t> count_by_type;

  public:
    void record_gc_measurement(uint64_t duration_ns, RegionType region_type)
    {
      measurements.push_back({duration_ns, region_type});
      total_duration_ns += duration_ns;
      duration_by_type[(int)region_type] += duration_ns;
      count_by_type[(int)region_type]++;
    }

    uint64_t get_total_gc_time() const
    {
      return total_duration_ns;
    }

    size_t get_gc_count() const
    {
      return measurements.size();
    }

    size_t get_gc_count_by_type(RegionType type) const
    {
      auto it = count_by_type.find((int)type);
      return it != count_by_type.end() ? it->second : 0;
    }

    uint64_t get_gc_time_by_type(RegionType type) const
    {
      auto it = duration_by_type.find((int)type);
      return it != duration_by_type.end() ? it->second : 0;
    }

    const std::vector<std::pair<uint64_t, RegionType>>& get_measurements() const
    {
      return measurements;
    }

    void reset()
    {
      measurements.clear();
      total_duration_ns = 0;
      duration_by_type.clear();
      count_by_type.clear();
    }
  };

  /**
   * Harness for benchmarking GC performance across multiple runs.
   * Collects metrics and computes statistics (total, average, etc.)
   */
  class GCBenchmark
  {
  public:
    struct Result
    {
      uint64_t total_gc_time_ns;
      size_t gc_call_count;
      uint64_t average_gc_time_ns;
      uint64_t max_gc_time_ns;
    };

  private:
    std::vector<Result> run_results;
    std::vector<uint64_t> all_gc_measurements;
    std::vector<std::pair<uint64_t, RegionType>> all_gc_measurements_with_type;

  public:
    /**
     * Run a test function multiple times and collect GC metrics.
     *
     * @param test_fn Function that runs one iteration of the test
     * @param num_runs Number of times to run the test (default 5)
     * @param warmup_runs Number of warmup iterations before collecting (default
     * 0)
     */
    void run_benchmark(
      std::function<void()> test_fn,
      size_t num_runs = 5,
      size_t warmup_runs = 0);

    /**
     * Print summary statistics
     */
    void print_summary(const char* test_name = "Test") const;

  private:
    inline uint64_t get_average_gc_time() const
    {
      if (run_results.empty())
        return 0;
      uint64_t total = 0;
      for (const auto& result : run_results)
        total += result.total_gc_time_ns;
      return total / run_results.size();
    }

    inline double get_average_gc_calls() const
    {
      if (run_results.empty())
        return 0;
      double total = 0;
      for (const auto& result : run_results)
        total += result.gc_call_count;
      return total / run_results.size();
    }

    inline uint64_t calculate_percentile(
      const std::vector<uint64_t>& sorted_values, double percentile) const
    {
      if (sorted_values.empty())
        return 0;
      size_t idx = (size_t)((percentile / 100.0) * (sorted_values.size() - 1));
      return sorted_values[idx];
    }

    inline double calculate_normalized_jitter(
      const std::vector<uint64_t>& values, uint64_t average) const
    {
      if (values.empty() || average == 0)
        return 0;
      double sum_sq_diff = 0;
      for (uint64_t val : values)
      {
        double diff = (double)val - (double)average;
        sum_sq_diff += diff * diff;
      }
      double variance = sum_sq_diff / values.size();
      double stddev = std::sqrt(variance);
      return stddev / average;
    }
  };

  // Inline implementations
  inline void GCBenchmark::run_benchmark(
    std::function<void()> test_fn, size_t num_runs, size_t warmup_runs)
  {
    // Warmup phase
    if (warmup_runs > 0)
    {
      std::cout << "=== Warmup Phase (" << warmup_runs << " runs) ===\n";
      for (size_t warmup = 0; warmup < warmup_runs; warmup++)
      {
        TestMeasurementCollector dummy_collector;

        // Create callback that captures the dummy collector
        std::function<void(uint64_t, RegionType)> callback =
          [&dummy_collector](uint64_t duration_ns, RegionType type) {
            dummy_collector.record_gc_measurement(duration_ns, type);
          };

        {
          // Enable callback for warmup
          auto prev = RegionContext::get_gc_callback();
          RegionContext::set_gc_callback(&callback);

          test_fn();

          // Restore previous callback
          RegionContext::set_gc_callback(prev);
        }
        std::cout << "Warmup " << (warmup + 1) << " complete\n";
      }
      std::cout << "\n=== Measurement Phase (" << num_runs << " runs) ===\n\n";
    }

    // Measurement phase
    for (size_t run = 0; run < num_runs; run++)
    {
      std::cout << "\n--- Benchmark Run " << (run + 1) << " of " << num_runs
                << " ---\n";

      TestMeasurementCollector collector;

      // Create callback that captures the collector
      std::function<void(uint64_t, RegionType)> callback =
        [&collector](uint64_t duration_ns, RegionType type) {
          collector.record_gc_measurement(duration_ns, type);
        };

      {
        // Enable callback for this run
        auto prev = RegionContext::get_gc_callback();
        RegionContext::set_gc_callback(&callback);

        test_fn();

        // Restore previous callback
        RegionContext::set_gc_callback(prev);
      }

      // Record all GC measurements
      uint64_t total_time = collector.get_total_gc_time();
      size_t total_calls = collector.get_gc_count();

      uint64_t avg_time = total_calls > 0 ? total_time / total_calls : 0;

      // Track max and collect all measurements for global stats
      uint64_t max_time = 0;
      for (const auto& m : collector.get_measurements())
      {
        all_gc_measurements.push_back(m.first);
        all_gc_measurements_with_type.push_back(m);
        max_time = std::max(max_time, m.first);
      }

      run_results.push_back({total_time, total_calls, avg_time, max_time});

      std::cout << "Run " << (run + 1) << " - Total GC time: " << total_time
                << " ns (" << total_calls << " calls, max: " << max_time
                << " ns)\n";
    }
  }

  inline void GCBenchmark::print_summary(const char* test_name) const
  {
    if (run_results.empty())
    {
      std::cout << "\nNo benchmark results to display.\n";
      return;
    }

    // Sort measurements for percentile calculation
    std::vector<uint64_t> sorted_measurements = all_gc_measurements;
    std::sort(sorted_measurements.begin(), sorted_measurements.end());

    // Collect region type breakdown across all measurements
    std::unordered_map<int, uint64_t> total_by_type;
    std::unordered_map<int, size_t> count_by_type;
    for (const auto& [duration, type] : all_gc_measurements_with_type)
    {
      total_by_type[(int)type] += duration;
      count_by_type[(int)type]++;
    }

    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "GC Benchmark Summary: " << test_name << "\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "Number of runs: " << run_results.size() << "\n\n";
    std::cout << "Per-Run Results:\n";
    std::cout << std::left << std::setw(6) << "Run" << std::setw(18)
              << "Total (ns)" << std::setw(12) << "Calls" << std::setw(14)
              << "Avg (ns)" << std::setw(14) << "Max (ns)\n";
    std::cout << std::string(74, '-') << "\n";

    for (size_t i = 0; i < run_results.size(); i++)
    {
      const auto& result = run_results[i];
      std::cout << std::left << std::setw(6) << (i + 1) << std::setw(18)
                << result.total_gc_time_ns << std::setw(12)
                << result.gc_call_count << std::setw(14)
                << result.average_gc_time_ns << std::setw(14)
                << result.max_gc_time_ns << "\n";
    }

    std::cout << std::string(74, '-') << "\n";
    std::cout << std::left << std::setw(6) << "Avg" << std::setw(18)
              << get_average_gc_time() << std::setw(12)
              << (int)get_average_gc_calls() << std::setw(14)
              << get_average_gc_time() << "\n";
    std::cout << std::string(74, '-') << "\n";

    uint64_t p50 = calculate_percentile(sorted_measurements, 50);
    uint64_t p99 = calculate_percentile(sorted_measurements, 99);
    double jitter = (p50 == 0) ? 0 : (double)(p99 - p50) / p50;

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "P50 (across all GC calls): " << p50 << " ns\n";
    std::cout << "P99 (across all GC calls): " << p99 << " ns\n";
    std::cout << "Normalized Jitter (P99-P50)/P50: " << jitter << "\n";

    // Show per-region-type breakdown if multiple types were used
    if (count_by_type.size() > 1)
    {
      std::cout << "\nPer-Region Type Breakdown:\n";
      std::cout << std::string(50, '-') << "\n";
      const char* type_names[] = {"Trace", "RC", "Arena"};
      for (const auto& [type_id, count] : count_by_type)
      {
        uint64_t total = total_by_type[type_id];
        uint64_t avg = count > 0 ? total / count : 0;
        const char* name =
          (type_id >= 0 && type_id < 3) ? type_names[type_id] : "Unknown";
        std::cout << std::left << std::setw(10) << name
                  << " Calls: " << std::setw(8) << count
                  << " Total: " << std::setw(12) << total << " ns"
                  << " Avg: " << avg << " ns\n";
      }
    }
  }

} // namespace verona::rt::api
