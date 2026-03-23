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

#include "benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

void benchmark_timer_start(BenchmarkTimer* timer)
{
    clock_gettime(CLOCK_MONOTONIC, &timer->start);
}

double benchmark_timer_stop(BenchmarkTimer* timer)
{
    clock_gettime(CLOCK_MONOTONIC, &timer->end);
    return benchmark_timespec_diff_ms(&timer->start, &timer->end);
}

double benchmark_timespec_diff_ms(struct timespec* start, struct timespec* end)
{
    double sec_diff = (double)(end->tv_sec - start->tv_sec);
    double nsec_diff = (double)(end->tv_nsec - start->tv_nsec);
    return (sec_diff * 1000.0) + (nsec_diff / 1000000.0);
}

void benchmark_print_result(const BenchmarkResult* result)
{
    printf("\n=== %s ===\n", result->name);
    printf("Iterations:    %lu\n", result->iterations);
    printf("Total time:    %.3f ms\n", result->total_time_ms);
    printf("Average time:  %.3f ms\n", result->avg_time_ms);
    printf("Min time:      %.3f ms\n", result->min_time_ms);
    printf("Max time:      %.3f ms\n", result->max_time_ms);

    if (result->bytes_processed > 0) {
        printf("Bytes:         %lu\n", result->bytes_processed);
        printf("Throughput:    %.2f MB/s\n", result->throughput_mbps);
    }
}

void benchmark_print_result_json(const BenchmarkResult* result, int is_last)
{
    printf("  {\n");
    printf("    \"name\": \"%s\",\n", result->name);
    printf("    \"iterations\": %lu,\n", result->iterations);
    printf("    \"total_time_ms\": %.3f,\n", result->total_time_ms);
    printf("    \"avg_time_ms\": %.3f,\n", result->avg_time_ms);
    printf("    \"min_time_ms\": %.3f,\n", result->min_time_ms);
    printf("    \"max_time_ms\": %.3f", result->max_time_ms);

    if (result->bytes_processed > 0) {
        printf(",\n");
        printf("    \"bytes_processed\": %lu,\n", result->bytes_processed);
        printf("    \"throughput_mbps\": %.2f\n", result->throughput_mbps);
    } else {
        printf("\n");
    }

    printf("  }%s\n", is_last ? "" : ",");
}

void benchmark_print_result_csv(const BenchmarkResult* result, int print_header)
{
    if (print_header) {
        printf("name,iterations,total_time_ms,avg_time_ms,min_time_ms,max_time_ms,bytes_processed,throughput_mbps\n");
    }
    printf("%s,%lu,%.3f,%.3f,%.3f,%.3f,%lu,%.2f\n",
           result->name,
           result->iterations,
           result->total_time_ms,
           result->avg_time_ms,
           result->min_time_ms,
           result->max_time_ms,
           result->bytes_processed,
           result->throughput_mbps);
}

double benchmark_calculate_throughput(uint64_t bytes, double time_ms)
{
    if (time_ms <= 0.0) {
        return 0.0;
    }
    double time_sec = time_ms / 1000.0;
    double mb = (double)bytes / (1024.0 * 1024.0);
    return mb / time_sec;
}

BenchmarkResult benchmark_run(
    const char* name,
    uint64_t iterations,
    int (*setup_fn)(void* user_data),
    int (*benchmark_fn)(void* user_data),
    void (*teardown_fn)(void* user_data),
    void* user_data)
{
    BenchmarkResult result;
    BenchmarkTimer timer;
    double* iteration_times;
    uint64_t i;
    int status;

    memset(&result, 0, sizeof(result));
    result.name = name;
    result.iterations = iterations;
    result.min_time_ms = DBL_MAX;
    result.max_time_ms = 0.0;

    // Allocate array to store individual iteration times
    iteration_times = (double*)malloc(sizeof(double) * iterations);
    if (!iteration_times) {
        fprintf(stderr, "Failed to allocate memory for iteration times\n");
        return result;
    }

    // Run setup
    if (setup_fn) {
        status = setup_fn(user_data);
        if (status != 0) {
            fprintf(stderr, "Benchmark setup failed for %s\n", name);
            free(iteration_times);
            return result;
        }
    }

    // Run benchmark iterations
    for (i = 0; i < iterations; i++) {
        benchmark_timer_start(&timer);
        status = benchmark_fn(user_data);
        iteration_times[i] = benchmark_timer_stop(&timer);

        if (status != 0) {
            fprintf(stderr, "Benchmark iteration %lu failed for %s\n", i, name);
            break;
        }

        result.total_time_ms += iteration_times[i];
        if (iteration_times[i] < result.min_time_ms) {
            result.min_time_ms = iteration_times[i];
        }
        if (iteration_times[i] > result.max_time_ms) {
            result.max_time_ms = iteration_times[i];
        }
    }

    // Calculate average
    if (iterations > 0) {
        result.avg_time_ms = result.total_time_ms / (double)iterations;
    }

    // Run teardown
    if (teardown_fn) {
        teardown_fn(user_data);
    }

    free(iteration_times);
    return result;
}
