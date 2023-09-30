#ifndef __ASYNC_REPEATED_TIMER_H_INCLUDED
#define __ASYNC_REPEATED_TIMER_H_INCLUDED

#include <functional>
#include <chrono>
#include <condition_variable>

// Runs a background thread that triggers at regular intervals
class AsyncRepeatedTimer {
public:
	using Duration = std::chrono::duration<double>;

	AsyncRepeatedTimer() : thread_([this](auto&& arg) {thread_function(std::forward<decltype(arg)>(arg)); }) {}

	void set_interval(std::chrono::duration<double> interval) noexcept { // 0-duration = disable
		{
			std::lock_guard guard(mutex_);
			interval_ = interval;
		}
		condition_variable_.notify_all();
	}

	void set_interval(int interval) noexcept { return set_interval(Duration(interval)); }

	void reset_current_timeout() noexcept {
		std::lock_guard guard(mutex_);
		last_triggered_at_ = std::chrono::steady_clock::now();
	};

	void set_trigger_action(std::function<void()> trigger_action) noexcept {
		std::lock_guard guard(mutex_);
		trigger_action_ = std::move(trigger_action);
	}

private:
	void thread_function(std::stop_token stop_token) {
		std::unique_lock<std::mutex> lock(mutex_);
		while (!stop_token.stop_requested()) {
			if (interval_ != Duration(0) && std::chrono::steady_clock::now() > last_triggered_at_ + interval_) {
				trigger_action_();
				last_triggered_at_ = std::chrono::steady_clock::now();
			}

			// Wait for stop_request or interval becoming non-0 (without timeout)
			condition_variable_.wait(lock, stop_token, [&]() { return interval_ != Duration(0); });

			// Wait for interval change, stop request, or timeout with current interval to trigger
			condition_variable_.wait_until(lock, stop_token, last_triggered_at_ + interval_, [&]() {
				return std::chrono::steady_clock::now() > last_triggered_at_ + interval_;
				});
		}
	}

	std::chrono::steady_clock::time_point last_triggered_at_;
	Duration interval_ = Duration(0);

	std::function<void()> trigger_action_;

	std::condition_variable_any condition_variable_;
	std::mutex mutex_;
	std::jthread thread_; // needs to be destroyed first
};

#endif // __ASYNC_REPEATED_TIMER_H_INCLUDED