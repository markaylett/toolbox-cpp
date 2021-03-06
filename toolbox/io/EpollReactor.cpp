// The Reactive C++ Toolbox.
// Copyright (C) 2013-2019 Swirly Cloud Limited
// Copyright (C) 2019 Reactive Markets Limited
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

#include "EpollReactor.hpp"

#include <toolbox/io/TimerFd.hpp>
#include <toolbox/sys/Log.hpp>

namespace toolbox {
inline namespace io {
using namespace std;
namespace {

constexpr size_t MaxEvents{16};

} // namespace

EpollReactor::EpollReactor(size_t size_hint)
: mux_{size_hint}
{
    const auto efd = efd_.fd();
    data_.resize(max<size_t>(efd + 1, size_hint));
    mux_.subscribe(efd, 0, EventIn);
}

EpollReactor::~EpollReactor()
{
    mux_.unsubscribe(efd_.fd());
}

void EpollReactor::do_interrupt() noexcept
{
    // Best effort.
    std::error_code ec;
    efd_.write(1, ec);
}

Reactor::Handle EpollReactor::do_subscribe(int fd, unsigned events, IoSlot slot)
{
    assert(fd >= 0);
    assert(slot);
    if (fd >= static_cast<int>(data_.size())) {
        data_.resize(fd + 1);
    }
    auto& ref = data_[fd];
    mux_.subscribe(fd, ++ref.sid, events);
    ref.events = events;
    ref.slot = slot;
    return {*this, fd, ref.sid};
}

void EpollReactor::do_unsubscribe(int fd, int sid) noexcept
{
    auto& ref = data_[fd];
    if (ref.sid == sid) {
        mux_.unsubscribe(fd);
        ref.events = 0;
        ref.slot.reset();
    }
}

void EpollReactor::do_set_events(int fd, int sid, unsigned events, IoSlot slot,
                                 error_code& ec) noexcept
{
    auto& ref = data_[fd];
    if (ref.sid == sid) {
        if (ref.events != events) {
            mux_.set_events(fd, sid, events, ec);
            if (ec) {
                return;
            }
            ref.events = events;
        }
        ref.slot = slot;
    }
}

void EpollReactor::do_set_events(int fd, int sid, unsigned events, IoSlot slot)
{
    auto& ref = data_[fd];
    if (ref.sid == sid) {
        if (ref.events != events) {
            mux_.set_events(fd, sid, events);
            ref.events = events;
        }
        ref.slot = slot;
    }
}

void EpollReactor::do_set_events(int fd, int sid, unsigned events, error_code& ec) noexcept
{
    auto& ref = data_[fd];
    if (ref.sid == sid && ref.events != events) {
        mux_.set_events(fd, sid, events, ec);
        if (ec) {
            return;
        }
        ref.events = events;
    }
}

void EpollReactor::do_set_events(int fd, int sid, unsigned events)
{
    auto& ref = data_[fd];
    if (ref.sid == sid && ref.events != events) {
        mux_.set_events(fd, sid, events);
        ref.events = events;
    }
}

Timer EpollReactor::do_timer(MonoTime expiry, Duration interval, Priority priority, TimerSlot slot)
{
    return tqs_[static_cast<size_t>(priority)].insert(expiry, interval, slot);
}

Timer EpollReactor::do_timer(MonoTime expiry, Priority priority, TimerSlot slot)
{
    return tqs_[static_cast<size_t>(priority)].insert(expiry, slot);
}

int EpollReactor::do_poll(CyclTime now, Duration timeout)
{
    enum { High = 0, Low = 1 };
    using namespace chrono;

    // If timeout is zero then the wait_until time should also be zero to signify no wait.
    MonoTime wait_until{};
    if (!is_zero(timeout)) {
        const MonoTime next
            = next_expiry(timeout == NoTimeout ? MonoClock::max() : now.mono_time() + timeout);
        if (next > now.mono_time()) {
            wait_until = next;
        }
    }

    int n;
    Event buf[MaxEvents];
    error_code ec;
    if (wait_until == MonoClock::max()) {
        // Block indefinitely.
        n = mux_.wait(buf, MaxEvents, ec);
    } else {
        // The wait function will not block if time is zero.
        n = mux_.wait(buf, MaxEvents, wait_until, ec);
    }
    if (ec) {
        if (ec.value() != EINTR) {
            throw system_error{ec};
        }
        return 0;
    }
    // If the muxer call was a blocking call, then acquire the current time.
    if (!is_zero(wait_until)) {
        now = CyclTime::now();
    }
    n = tqs_[High].dispatch(now) + dispatch(now, buf, n);
    // Low priority timers are only dispatched during empty cycles.
    return n == 0 ? tqs_[Low].dispatch(now) : n;
}

MonoTime EpollReactor::next_expiry(MonoTime next) const
{
    enum { High = 0, Low = 1 };
    using namespace chrono;
    {
        auto& tq = tqs_[High];
        if (!tq.empty()) {
            // Duration until next expiry. Mitigate scheduler latency by preempting the
            // high-priority timer and busy-waiting for 200us ahead of timer expiry.
            next = min(next, tq.front().expiry() - 200us);
        }
    }
    {
        auto& tq = tqs_[Low];
        if (!tq.empty()) {
            // Duration until next expiry.
            next = min(next, tq.front().expiry());
        }
    }
    return next;
}

int EpollReactor::dispatch(CyclTime now, Event* buf, int size)
{
    int work{0};
    for (int i{0}; i < size; ++i) {

        auto& ev = buf[i];
        const auto fd = mux_.fd(ev);
        if (fd == efd_.fd()) {
            efd_.read();
            continue;
        }
        const auto& ref = data_[fd];
        if (!ref.slot) {
            // Ignore timerfd.
            continue;
        }

        const auto sid = mux_.sid(ev);
        // Skip this socket if it was modified after the call to wait().
        if (ref.sid > sid) {
            continue;
        }
        // Apply the interest events to filter-out any events that the user may have removed from
        // the events since the call to wait() was made. This would typically happen via a reentrant
        // call into the reactor from an event-handler. N.B. EventErr and EventHup are always
        // reported if they occur, regardless of whether they are specified in events.
        const auto events = mux_.events(ev) & (ref.events | EventErr | EventHup);
        if (!events) {
            continue;
        }

        try {
            ref.slot(now, fd, events);
        } catch (const std::exception& e) {
            TOOLBOX_ERROR << "error handling io event: " << e.what();
        }
        ++work;
    }
    return work;
}

} // namespace io
} // namespace toolbox
