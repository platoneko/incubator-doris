// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#pragma once

#include <bthread/countdown_event.h>
#include <fmt/core.h>

#include <future>
#include <optional>
#include <string>

#include "common/logging.h"
#include "common/simple_thread_pool.h"

namespace doris::cloud {

template <typename T>
class SyncExecutor {
public:
    SyncExecutor(
            std::shared_ptr<SimpleThreadPool> pool, std::string name_tag,
            std::function<bool(const T&)> cancel = [](const T& /**/) { return false; })
            : _pool(std::move(pool)), _cancel(std::move(cancel)), _name_tag(std::move(name_tag)) {}

    ~SyncExecutor() {
        if (!_tasks.empty()) {
            bool finished;
            when_all(&finished);
        }
    }

    auto add(std::function<T()> callback) -> SyncExecutor<T>& {
        auto task = std::make_unique<Task>(std::move(callback), _cancel, _count);
        _count.add_count();
        // The actual task logic would be wrapped by one promise and passed to the threadpool.
        // The result would be returned by the future once the task is finished.
        // Or the task would be invalid if the whole task is cancelled.
        int r = _pool->submit([this, t = task.get()]() { (*t)(_stop_token); });
        CHECK_EQ(r, 0);
        _tasks.emplace_back(std::move(task));
        return *this;
    }

    std::vector<T> when_all(bool* finished) {
        timespec current_time;
        auto current_time_second = time(nullptr);
        current_time.tv_sec = current_time_second + 300;
        current_time.tv_nsec = 0;
        while (0 != _count.timed_wait(current_time)) {
            current_time.tv_sec += 300;
            LOG(WARNING) << _name_tag << " has already taken 5 min";
        }
        _count.reset(0);

        std::vector<T> res;
        res.reserve(_tasks.size());
        for (auto&& task : _tasks) {
            auto r = std::move(task->get());
            if (r.has_value()) {
                res.push_back(std::move(r.value()));
            } else {
                break;
            }
        }

        *finished = _tasks.size() == res.size();

        _tasks.clear();
        return res;
    }

    void reset() {
        if (!_tasks.empty()) {
            bool finished;
            when_all(&finished);
        }
        _stop_token = false;
    }

private:
    class Task {
    public:
        Task(std::function<T()> callback, std::function<bool(const T&)> cancel,
             bthread::CountdownEvent& count)
                : _callback(std::move(callback)), _cancel(std::move(cancel)), _count(count) {}
        void operator()(std::atomic_bool& stop_token) {
            std::unique_ptr<int, std::function<void(int*)>> defer((int*)0x01,
                                                                  [&](int*) { _count.signal(); });
            if (stop_token) {
                return;
            }
            T t = _callback();
            // We'll return this task result to user even if this task return error
            // So we don't set _valid to false here
            if (_cancel(t)) {
                stop_token = true;
            }
            _res = std::move(t);
        }
        std::optional<T>& get() { return _res; }

    private:
        // It's guarantted that the valid function can only be called inside SyncExecutor's `when_all()` function
        // and only be called when the _count.timed_wait function returned. So there would be no data race for
        // _valid then it doesn't need to be one atomic bool.
        std::function<T()> _callback;
        std::function<bool(const T&)> _cancel;
        bthread::CountdownEvent& _count;
        std::optional<T> _res;
    };

    std::vector<std::unique_ptr<Task>> _tasks;
    // use CountdownEvent to do periodically log using CountdownEvent::time_wait()
    bthread::CountdownEvent _count {0};
    std::atomic_bool _stop_token {false};
    std::shared_ptr<SimpleThreadPool> _pool;
    std::function<bool(const T&)> _cancel;
    std::string _name_tag;
};
} // namespace doris::cloud