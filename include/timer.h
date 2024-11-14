#pragma once

#include <chrono>

namespace scvk
{
	class Timer
	{
	public:

		void start() noexcept
		{
			mStarted = true;
			reset();
		}

		void reset() noexcept
		{
			mStartTime = std::chrono::high_resolution_clock::now();
			mLastTime = mStartTime;
		}

		// Get time since last call to Elapsed(), without resetting the timer. Specified in seconds by default
		template<typename Period = std::ratio<1>>
		constexpr float elapsedTime()
		{
			if (!mStarted) {
				return 0.f;
			}
			const auto currentTime = std::chrono::high_resolution_clock::now();
			const auto deltaTime = std::chrono::duration<float, Period>(currentTime - mLastTime).count();
			mLastTime = currentTime;
			return deltaTime;
		}

		// Get the total time elapsed since the timer was started. Specified in seconds by default.
		template<typename Period = std::ratio<1>>
		constexpr float total() const
		{
			const auto current_time = std::chrono::high_resolution_clock::now();
			return std::chrono::duration<float, Period>(current_time - mStartTime).count();
		}

	private:
		std::chrono::time_point<std::chrono::high_resolution_clock> mStartTime;
		std::chrono::time_point<std::chrono::high_resolution_clock> mLastTime;
		bool mStarted{ false };
	};
}