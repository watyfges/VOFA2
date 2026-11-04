#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace vofa::core {

// Single-producer / single-consumer lock-free ring. The producer and consumer
// must be different threads (or externally synchronised); no other thread may
// call push/pop concurrently.
//
// T must be default-constructible and movable/copyable. The capacity is rounded
// up to a power of two. When full, push() drops the newest item and increments
// dropped(); it never blocks and never grows.
template <typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(std::size_t requestedCapacity)
        : capacity_(roundUpPowerOfTwo(requestedCapacity))
        , mask_(capacity_ - 1)
        , buffer_(std::make_unique<T[]>(capacity_))
    {
        if (capacity_ == 0)
            throw std::invalid_argument("SpscRingBuffer capacity must be > 0");
    }

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    [[nodiscard]] bool push(const T& value) noexcept
    {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= capacity_) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        buffer_[head & mask_] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool push(T&& value) noexcept
    {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= capacity_) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        buffer_[head & mask_] = std::move(value);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool pop(T& out) noexcept
    {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto head = head_.load(std::memory_order_acquire);
        if (tail == head)
            return false;
        out = std::move(buffer_[tail & mask_]);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        const auto head = head_.load(std::memory_order_acquire);
        const auto tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::uint64_t dropped() const noexcept
    {
        return dropped_.load(std::memory_order_relaxed);
    }

    // Not thread-safe. Only call when both producer and consumer are stopped.
    void clear() noexcept
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        dropped_.store(0, std::memory_order_relaxed);
    }

private:
    static std::size_t roundUpPowerOfTwo(std::size_t value) noexcept
    {
        if (value <= 1)
            return 1;
        std::size_t result = 1;
        while (result < value)
            result <<= 1;
        return result;
    }

    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<T[]> buffer_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) std::atomic<std::uint64_t> dropped_{0};
};

} // namespace vofa::core
