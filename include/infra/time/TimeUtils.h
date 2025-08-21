#pragma once

#include <chrono>

namespace driver
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
}