// The Reactive C++ Toolbox.
// Copyright (C) 2013-2019 Swirly Cloud Limited
// Copyright (C) 2024 Reactive Markets Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <toolbox/io.hpp>
#include <toolbox/sys.hpp>
#include <toolbox/util.hpp>

using namespace std;

namespace {

using toolbox::MonoTime;
using toolbox::Duration;
using toolbox::TimerSlot;

class TimerPool;

class TOOLBOX_API Timer {
    friend class TimerPool;

  public:
    struct Impl {

        using AutoUnlinkOption = boost::intrusive::link_mode<boost::intrusive::auto_unlink>;
        boost::intrusive::list_member_hook<AutoUnlinkOption> list_hook;

        Impl() noexcept = default;
        ~Impl() = default;

        // Copy.
        Impl(const Impl&) noexcept = default;
        Impl& operator=(const Impl&) noexcept = default;

        // Move.
        Impl(Impl&&) noexcept = default;
        Impl& operator=(Impl&&) noexcept = default;

        void add_ref() noexcept
        {
            ++ref_count;
        }
        void release() noexcept;
        void cancel() noexcept
        {
            list_hook.unlink();
            release();
        }

        TimerPool* timer_pool{nullptr};
        int ref_count;
        long id;
        MonoTime expiry;
        Duration interval;
        TimerSlot slot;
    };

    explicit Timer(Impl* impl, bool add_ref = true)
    : impl_{impl, add_ref}
    {
    }
    Timer(std::nullptr_t = nullptr) noexcept {} // NOLINT(hicpp-explicit-conversions)
    ~Timer() = default;

    // Copy.
    Timer(const Timer&) = default;
    Timer& operator=(const Timer&) = default;

    // Move.
    Timer(Timer&&) noexcept = default;
    Timer& operator=(Timer&&) noexcept = default;

    bool empty() const noexcept { return !impl_; }
    explicit operator bool() const noexcept { return impl_ != nullptr; }
    long id() const noexcept { return impl_ ? impl_->id : 0; }
    bool pending() const noexcept { return impl_ != nullptr && bool{impl_->slot}; }

    MonoTime expiry() const noexcept { return impl_->expiry; }
    Duration interval() const noexcept { return impl_->interval; }
    /// Setting the interval will not reschedule any pending timer.
    template <typename RepT, typename PeriodT>
    void set_interval(std::chrono::duration<RepT, PeriodT> interval) noexcept
    {
        using namespace std::chrono;
        impl_->interval = duration_cast<Duration>(interval);
    }
    void reset(std::nullptr_t = nullptr) noexcept { impl_.reset(); }
    void swap(Timer& rhs) noexcept { impl_.swap(rhs.impl_); }
    void cancel() noexcept;

    std::partial_ordering operator<=>(const Timer& rhs) const noexcept
    {
        if (!*this || !rhs) {
            return std::partial_ordering::unordered;
        }
        return id() <=> rhs.id();
    }

  private:
    void set_expiry(MonoTime expiry) noexcept { impl_->expiry = expiry; }
    TimerSlot& slot() noexcept { return impl_->slot; }

    boost::intrusive_ptr<Timer::Impl> impl_;
};

inline void intrusive_ptr_add_ref(Timer::Impl* impl) noexcept
{
    impl->add_ref();
}

inline void intrusive_ptr_release(Timer::Impl* impl) noexcept
{
    impl->release();
}

using ConstantTimeSizeOption = boost::intrusive::constant_time_size<false>;
using MemberHookOption = boost::intrusive::member_hook<Timer::Impl, decltype(Timer::Impl::list_hook),
                                                       &Timer::Impl::list_hook>;
using TimerList = boost::intrusive::list<Timer::Impl, ConstantTimeSizeOption, MemberHookOption>;

// Number of entries per 4K slab, assuming that malloc overhead is no more than 32 bytes.
constexpr size_t Overhead = 32;
constexpr size_t PageSize = 4096;
constexpr size_t SlabSize = (PageSize - Overhead) / sizeof(Timer::Impl);

class TOOLBOX_API TimerPool {
    using SlabPtr = std::unique_ptr<Timer::Impl[]>;

  public:
    TimerPool() = default;
    ~TimerPool() noexcept = default;

    // Copy.
    TimerPool(const TimerPool&) = delete;
    TimerPool& operator=(const TimerPool&) = delete;

    // Move.
    TimerPool(TimerPool&&) = delete;
    TimerPool& operator=(TimerPool&&) = delete;

    Timer::Impl* allocate();
    void deallocate(Timer::Impl* impl) noexcept
    {
        assert(impl);
        free_list_.push_back(*impl);
    }

  private:
    std::vector<SlabPtr> slabs_;
    /// Head of free-list.
    TimerList free_list_;
};

void Timer::Impl::release() noexcept
{
    if (--ref_count == 0) {
        TOOLBOX_INFO << "deallocate";
        timer_pool->deallocate(this);
    }
}

void Timer::cancel() noexcept
{
    // If pending, then reset the slot and cancel the timer.
    if (impl_ && impl_->slot) {
        impl_->slot.reset();
        impl_->cancel();
    }
}

Timer::Impl* TimerPool::allocate()
{
    Timer::Impl* impl;

    if (!free_list_.empty()) {

        // Pop next free timer from stack.
        impl = &free_list_.back();
        free_list_.pop_back();

    } else {

        // Add new slab of timers to stack.
        SlabPtr slab{new Timer::Impl[SlabSize]};
        impl = &slab[0];

        for (size_t i{1}; i < SlabSize; ++i) {
            free_list_.push_back(slab[i]);
        }
        slabs_.push_back(std::move(slab));
    }

    return impl;
}

class TimerWheel {
  public:
    explicit TimerWheel(TimerPool& timer_pool)
    : timer_pool_{timer_pool}
    {
    }
    ~TimerWheel()
    {
        timer_list_.clear_and_dispose([](auto* impl) {
            impl->cancel();
        });
    }
    Timer allocate(MonoTime expiry, Duration interval, TimerSlot slot)
    {
        auto* const impl{timer_pool_.allocate()};

        impl->timer_pool = &timer_pool_;
        impl->ref_count = 1;
        impl->id = ++max_id_;
        impl->expiry = expiry;
        impl->interval = interval;
        impl->slot = slot;

        timer_list_.push_back(*impl);
        return Timer{impl};
    }

  private:
    TimerPool& timer_pool_;
    long max_id_{};
    // List of active connections.
    TimerList timer_list_;
};

} // namespace

int main()
{
    int ret = 1;
    try {

        TimerPool timer_pool{};
        TimerWheel timer_wheel{timer_pool};
        Timer tmr{timer_wheel.allocate({}, {}, TimerSlot{})};
        tmr.cancel();
        TOOLBOX_INFO << "HERE";
        ret = 0;

    } catch (const std::exception& e) {
        TOOLBOX_ERROR << "exception on main thread: " << e.what();
    }
    return ret;
}
