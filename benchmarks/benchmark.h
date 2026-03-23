/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * VaFS Benchmark Framework
 */

#ifndef __VAFS_BENCHMARK_H__
#define __VAFS_BENCHMARK_H__

#include <stdint.h>
#include <time.h>

/**
 * @brief Timing utilities for benchmarks
 */
typedef struct {
    struct timespec start;
    struct timespec end;
} BenchmarkTimer;

/**
 * @brief Benchmark result structure
 */
typedef struct {
    const char*  name;
    uint64_t     iterations;
    double       total_time_ms;
    double       avg_time_ms;
    double       min_time_ms;
    double       max_time_ms;
    double       throughput_mbps;  // For I/O benchmarks
    uint64_t     bytes_processed;  // For I/O benchmarks
} BenchmarkResult;

/**
 * @brief Start a benchmark timer
 */
void benchmark_timer_start(BenchmarkTimer* timer);

/**
 * @brief Stop a benchmark timer and return elapsed time in milliseconds
 */
double benchmark_timer_stop(BenchmarkTimer* timer);

/**
 * @brief Get elapsed time in milliseconds between two timespec values
 */
double benchmark_timespec_diff_ms(struct timespec* start, struct timespec* end);

/**
 * @brief Print benchmark result in human-readable format
 */
void benchmark_print_result(const BenchmarkResult* result);

/**
 * @brief Print benchmark result in JSON format
 */
void benchmark_print_result_json(const BenchmarkResult* result, int is_last);

/**
 * @brief Print benchmark result in CSV format
 */
void benchmark_print_result_csv(const BenchmarkResult* result, int print_header);

/**
 * @brief Calculate throughput in MB/s
 */
double benchmark_calculate_throughput(uint64_t bytes, double time_ms);

/**
 * @brief Run a benchmark function multiple times and collect statistics
 *
 * @param name Benchmark name
 * @param iterations Number of iterations to run
 * @param setup_fn Setup function called once before all iterations (can be NULL)
 * @param benchmark_fn Benchmark function to time (returns 0 on success, -1 on error)
 * @param teardown_fn Teardown function called once after all iterations (can be NULL)
 * @param user_data User data passed to all functions
 * @return BenchmarkResult with timing statistics
 */
BenchmarkResult benchmark_run(
    const char* name,
    uint64_t iterations,
    int (*setup_fn)(void* user_data),
    int (*benchmark_fn)(void* user_data),
    void (*teardown_fn)(void* user_data),
    void* user_data
);

#endif // __VAFS_BENCHMARK_H__
