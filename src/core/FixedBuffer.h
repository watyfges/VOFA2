#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>

namespace vofa::core {

// Fixed-capacity byte buffer that never allocates after construction. Parsers
// use it so that hostile input can never trigger unbounded growth or repeated
// heap allocation in the hot path.
class FixedBuffer {
public:
    explicit FixedBuffer(std::size_t capacity)
        : storage_(std::make_unique<std::byte[]>(capacity))
        , capacity_(capacity)
    {
    }

    FixedBuffer(const FixedBuffer&) = delete;
    FixedBuffer& operator=(const FixedBuffer&) = delete;

    [[nodiscard]] std::size_t size() const noexcept { return end_ - start_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return start_ == end_; }

    [[nodiscard]] std::span<const std::byte> data() const noexcept
    {
        return std::span<const std::byte>(storage_.get() + start_, end_ - start_);
    }

    [[nodiscard]] std::span<std::byte> mutableData() noexcept
    {
        return std::span<std::byte>(storage_.get() + start_, end_ - start_);
    }

    // Appends bytes. Returns false if the data does not fit even after
    // compaction. The caller decides how to resynchronise.
    [[nodiscard]] bool append(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.empty())
            return true;
        if (bytes.size() > capacity_)
            return false;
        if (end_ + bytes.size() > capacity_)
            compact();
        if (end_ + bytes.size() > capacity_)
            return false;
        std::memcpy(storage_.get() + end_, bytes.data(), bytes.size());
        end_ += bytes.size();
        return true;
    }

    // Drops the first n bytes, compacting when the consumed prefix grows.
    void consume(std::size_t n) noexcept
    {
        n = std::min(n, size());
        start_ += n;
        if (start_ == end_) {
            start_ = 0;
            end_ = 0;
        } else if (start_ > capacity_ / 2) {
            compact();
        }
    }

    void clear() noexcept
    {
        start_ = 0;
        end_ = 0;
    }

    // Drops n bytes from the front without any bounds checks beyond clamping.
    void dropPrefix(std::size_t n) noexcept { consume(n); }

private:
    void compact() noexcept
    {
        if (start_ == 0)
            return;
        const auto remaining = end_ - start_;
        if (remaining > 0)
            std::memmove(storage_.get(), storage_.get() + start_, remaining);
        start_ = 0;
        end_ = remaining;
    }

    std::unique_ptr<std::byte[]> storage_;
    std::size_t capacity_;
    std::size_t start_ = 0;
    std::size_t end_ = 0;
};

} // namespace vofa::core
