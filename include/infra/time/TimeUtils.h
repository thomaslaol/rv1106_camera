#pragma once

#include <chrono>

namespace infra
{
    // 获取当前时间戳（微秒）
    inline uint64_t now_us()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // 获取当前时间戳（毫秒）
    inline uint64_t now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // 获取当前时间戳（秒）
    inline double now_seconds()
    {
        return std::chrono::duration_cast<std::chrono::duration<double>>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    inline int64_t TEST_COMM_GetNowUs()
    {
        struct timespec time = {0, 0};
        clock_gettime(CLOCK_MONOTONIC, &time);
        return (int64_t)time.tv_sec * 1000000 + (int64_t)time.tv_nsec / 1000; /* microseconds */
    }
}