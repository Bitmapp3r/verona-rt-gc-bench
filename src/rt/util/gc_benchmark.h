// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <region/region_api.h>
#include <sstream>
#include <unordered_map>

#include <vector>
#include <filesystem>

namespace verona::rt::api
{
  /**
   * Internal collector for gathering GC and memory measurements.
   */
  class TestMeasurementCollector
  {
  private:
    std::vector<std::pair<uint64_t, RegionType>> measurements;
    uint64_t total_duration_ns = 0;
    std::unordered_map<int, uint64_t> duration_by_type;
    std::unordered_map<int, size_t> count_by_type;

    // Memory tracking (captured at each GC event)
    std::vector<size_t> memory_samples;
    std::vector<size_t> object_samples;
    size_t peak_memory_bytes = 0;
    size_t peak_object_count = 0;

  public:
    void record_gc_measurement(
      uint64_t duration_ns,
      RegionType region_type,
      size_t memory_before,
      size_t objects_before)
    {
      measurements.push_back({duration_ns, region_type});
      total_duration_ns += duration_ns;
      duration_by_type[(int)region_type] += duration_ns;
      count_by_type[(int)region_type]++;

      // Track memory samples for average calculation
      memory_samples.push_back(memory_before);
      object_samples.push_back(objects_before);

      // Track peak memory (before GC, when it's highest)
      peak_memory_bytes = std::max(peak_memory_bytes, memory_before);
      peak_object_count = std::max(peak_object_count, objects_before);
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

    size_t get_peak_memory() const
    {
      return peak_memory_bytes;
    }

    size_t get_peak_objects() const
    {
      return peak_object_count;
    }

    size_t get_average_memory() const
    {
      if (memory_samples.empty())
        return 0;
      size_t total = 0;
      for (size_t m : memory_samples)
        total += m;
      return total / memory_samples.size();
    }

    size_t get_average_objects() const
    {
      if (object_samples.empty())
        return 0;
      size_t total = 0;
      for (size_t o : object_samples)
        total += o;
      return total / object_samples.size();
    }

    void reset()
    {
      measurements.clear();
      total_duration_ns = 0;
      duration_by_type.clear();
      count_by_type.clear();
      memory_samples.clear();
      object_samples.clear();
      peak_memory_bytes = 0;
      peak_object_count = 0;
    }
  };

  /**
   * Harness for benchmarking GC performance across multiple runs.
   * Collects GC timing and memory metrics.
   */
  class GCBenchmark
  {
  public:
    struct Result
    {
      // GC timing metrics
      uint64_t total_gc_time_ns;
      size_t gc_call_count;
      uint64_t average_gc_time_ns;
      uint64_t max_gc_time_ns;
      // Memory metrics
      size_t peak_memory_bytes;
      size_t peak_object_count;
      size_t avg_memory_bytes;
      size_t avg_object_count;
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
      size_t warmup_runs = 0,
      const char* test_name = "Test");

    /**
     * Print summary statistics
     */
    void print_summary(const char* test_name = "Test") const;

    /**
     * Write raw data to a CSV file for visualization tools.
     * Format:
     * run,gc_time_ns,gc_calls,max_gc_ns,avg_mem_bytes,peak_mem_bytes,peak_objects
     */
    void write_csv(const char* filename) const;

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

    inline size_t get_average_peak_memory() const
    {
      if (run_results.empty())
        return 0;
      size_t total = 0;
      for (const auto& result : run_results)
        total += result.peak_memory_bytes;
      return total / run_results.size();
    }

    inline size_t get_average_peak_objects() const
    {
      if (run_results.empty())
        return 0;
      size_t total = 0;
      for (const auto& result : run_results)
        total += result.peak_object_count;
      return total / run_results.size();
    }

    static std::string format_bytes(size_t bytes)
    {
      const char* units[] = {"B", "KB", "MB", "GB"};
      int unit_idx = 0;
      double value = static_cast<double>(bytes);
      while (value >= 1024.0 && unit_idx < 3)
      {
        value /= 1024.0;
        unit_idx++;
      }
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(2) << value << " "
          << units[unit_idx];
      return oss.str();
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
    std::function<void()> test_fn, size_t num_runs, size_t warmup_runs, const char* test_name)
  {
    // Warmup phase
    if (warmup_runs > 0)
    {
      std::cout << "=== Warmup Phase (" << warmup_runs << " runs) ===\n";
      for (size_t warmup = 0; warmup < warmup_runs; warmup++)
      {
        TestMeasurementCollector dummy_collector;

        // Create callback that captures the dummy collector
        std::function<void(uint64_t, RegionType, size_t, size_t)> callback =
          [&dummy_collector](
            uint64_t duration_ns, RegionType type, size_t mem, size_t obj) {
            dummy_collector.record_gc_measurement(duration_ns, type, mem, obj);
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
      std::function<void(uint64_t, RegionType, size_t, size_t)> callback =
        [&collector](
          uint64_t duration_ns, RegionType type, size_t mem, size_t obj) {
          collector.record_gc_measurement(duration_ns, type, mem, obj);
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

      run_results.push_back(
        {total_time,
         total_calls,
         avg_time,
         max_time,
         collector.get_peak_memory(),
         collector.get_peak_objects(),
         collector.get_average_memory(),
         collector.get_average_objects()});

      std::cout << "Run " << (run + 1) << " - GC: " << total_time << " ns ("
                << total_calls << " calls) | Avg Mem: "
                << format_bytes(collector.get_average_memory())
                << " | Peak: " << format_bytes(collector.get_peak_memory())
                << " (" << collector.get_peak_objects() << " obj)\n";
    }
    std::string path = test_name;
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos)
      path = path.substr(pos + 1);
    path.resize(path.size() - 4);
    print_summary(path.c_str());
  }

  inline void GCBenchmark::print_summary(const char* test_name) const
  {
    if (run_results.empty())
    {
      std::cout << "\nNo benchmark results to display.\n";
      return;
    }
    write_csv(test_name);

    // Auto-write CSV file (convert test name to filename: spaces->underscores,
    // lowercase, append region type if available)

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

    std::cout << "\n" << std::string(90, '=') << "\n";
    std::cout << "Benchmark Summary: " << test_name << "\n";
    std::cout << std::string(90, '=') << "\n";
    std::cout << "Number of runs: " << run_results.size() << "\n\n";
    std::cout << "Per-Run Results:\n";
    std::cout << std::left << std::setw(5) << "Run" << std::setw(15)
              << "GC Time(ns)" << std::setw(8) << "Calls" << std::setw(12)
              << "Max(ns)" << std::setw(14) << "Avg Mem" << std::setw(14)
              << "Peak Mem" << std::setw(10) << "Peak Obj\n";
    std::cout << std::string(78, '-') << "\n";

    for (size_t i = 0; i < run_results.size(); i++)
    {
      const auto& r = run_results[i];
      std::cout << std::left << std::setw(5) << (i + 1) << std::setw(15)
                << r.total_gc_time_ns << std::setw(8) << r.gc_call_count
                << std::setw(12) << r.max_gc_time_ns << std::setw(14)
                << format_bytes(r.avg_memory_bytes) << std::setw(14)
                << format_bytes(r.peak_memory_bytes) << std::setw(10)
                << r.peak_object_count << "\n";
    }

    std::cout << std::string(78, '-') << "\n";

    // Calculate overall averages for average and peak memory
    size_t total_avg_mem = 0, total_peak_mem = 0, total_peak_obj = 0;
    for (const auto& r : run_results)
    {
      total_avg_mem += r.avg_memory_bytes;
      total_peak_mem += r.peak_memory_bytes;
      total_peak_obj += r.peak_object_count;
    }
    size_t overall_avg_mem = total_avg_mem / run_results.size();
    size_t overall_peak_mem = total_peak_mem / run_results.size();
    size_t overall_peak_obj = total_peak_obj / run_results.size();

    std::cout << std::left << std::setw(5) << "Avg" << std::setw(15)
              << get_average_gc_time() << std::setw(8)
              << (int)get_average_gc_calls() << std::setw(12) << "-"
              << std::setw(14) << format_bytes(overall_avg_mem) << std::setw(14)
              << format_bytes(overall_peak_mem) << std::setw(10)
              << overall_peak_obj << "\n";
    std::cout << std::string(78, '-') << "\n";

    uint64_t p50 = calculate_percentile(sorted_measurements, 50);
    uint64_t p99 = calculate_percentile(sorted_measurements, 99);
    double jitter = (p50 == 0) ? 0 : (double)(p99 - p50) / p50;

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "\nGC Timing:\n";
    std::cout << "  P50: " << p50 << " ns | P99: " << p99 << " ns\n";
    std::cout << "  Jitter (P99-P50)/P50: " << jitter << "\n";

    std::cout << "\nMemory:\n";
    std::cout << "  Average Live Memory: " << format_bytes(overall_avg_mem)
              << " (avg memory at GC events - explains GC frequency)\n";
    std::cout << "  Average Peak Memory: " << format_bytes(overall_peak_mem)
              << " (avg of per-run peaks - ensures GC not unbounded)\n";

    // Show per-region-type breakdown if multiple types were used
    if (count_by_type.size() > 1)
    {
      std::cout << "\nPer-Region Type:\n";
      const char* type_names[] = {"Trace", "Rc", "Arena"};
      for (const auto& [type_id, count] : count_by_type)
      {
        uint64_t total = total_by_type[type_id];
        uint64_t avg = count > 0 ? total / count : 0;
        const char* name =
          (type_id >= 0 && type_id < 3) ? type_names[type_id] : "Unknown";
        std::cout << "  " << std::left << std::setw(6) << name << " - " << count
                  << " calls, " << total << " ns total, " << avg << " ns avg\n";
      }
    }
    std::cout << std::string(90, '=') << "\n";
  }

  inline void GCBenchmark::write_csv(const char* filename) const
  {
    std::string filename_str = filename;
    std::string csv_filename = filename;
    for (char& c : csv_filename)
    {
    if (c == ' ' || c == '-')
        c = '_';
    else
        c = std::tolower(c);
    }
      // Determine region type from measurements (if available)
      std::string region_type_str;
      if (!all_gc_measurements_with_type.empty())
      {
        int region_type = (int)all_gc_measurements_with_type[0].second;
        const char* type_names[] = {"trace", "arena", "rc"};
        if (region_type >= 0 && region_type < 3)
          region_type_str = std::string("_") + type_names[region_type];
        else
          region_type_str = "_unknown";
      }



    // Ensure CSVs directory exists (platform-independent)
    // Always use repo root for CSVs directory, 3 parents up from this file
    std::filesystem::path this_file = __FILE__;

    std::filesystem::path repo_root = this_file.parent_path().parent_path().parent_path().parent_path();
    std::string dir = (repo_root / "CSVs" / filename_str).string();
    std::filesystem::create_directory(dir);
    csv_filename += region_type_str + ".csv";
    std::string base_filename = std::filesystem::path(csv_filename).filename().string();
    std::string fullpath = dir + "/" + base_filename;
    std::ofstream file(fullpath);
    if (!file.is_open())
    {
      std::cerr << "Error: Could not open file " << fullpath
                << " for writing\n";
      return;
    }

    if (run_results.empty())
    {
      file << "# No benchmark results\n";
      return;
    }

    // Calculate P50, P99, jitter
    std::vector<uint64_t> sorted_measurements = all_gc_measurements;
    std::sort(sorted_measurements.begin(), sorted_measurements.end());
    uint64_t p50 = calculate_percentile(sorted_measurements, 50.0);
    uint64_t p99 = calculate_percentile(sorted_measurements, 99.0);
    double jitter = p50 > 0 ? static_cast<double>(p99 - p50) / p50 : 0.0;

    // Calculate overall averages
    uint64_t overall_avg_mem = 0, overall_peak_mem = 0;
    for (const auto& r : run_results)
    {
      overall_avg_mem += r.avg_memory_bytes;
      overall_peak_mem += r.peak_memory_bytes;
    }
    overall_avg_mem /= run_results.size();
    overall_peak_mem /= run_results.size();

    // CSV header
    file << "run,gc_time_ns,gc_calls,max_gc_ns,avg_mem_bytes,peak_mem_bytes,"
            "peak_objects\n";

    // Per-run data
    for (size_t i = 0; i < run_results.size(); ++i)
    {
      const auto& r = run_results[i];
      file << (i + 1) << "," << r.total_gc_time_ns << "," << r.gc_call_count
           << "," << r.max_gc_time_ns << "," << r.avg_memory_bytes << ","
           << r.peak_memory_bytes << "," << r.peak_object_count << "\n";
    }

    // Summary row
    file << "#p50_ns=" << p50 << ",p99_ns=" << p99 << ",jitter=" << std::fixed
         << std::setprecision(4) << jitter << ",avg_mem=" << overall_avg_mem
         << ",peak_mem=" << overall_peak_mem << "\n";
  }

} // namespace verona::rt::api
