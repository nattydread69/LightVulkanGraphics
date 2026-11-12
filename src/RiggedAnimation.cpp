// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2026 Dr. Nathanael John Inkson
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "RiggedAnimation.h"
#include "RiggedObject.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace lightGraphics
{
	namespace
	{
		// A fixed set of worker threads that run one parallel loop at a time. The calling
		// thread takes part too, so the loop finishes no later than it would serially.
		class AnimationThreadPool
		{
		public:
			static AnimationThreadPool& instance()
			{
				static AnimationThreadPool pool;
				return pool;
			}

			std::size_t workerCount() const { return workers_.size(); }

			// Runs job(i) for every i in [0, count) across the workers and the caller.
			void run(std::size_t count, const std::function<void(std::size_t)>& job)
			{
				{
					std::lock_guard<std::mutex> lock(mutex_);
					job_ = &job;
					count_ = count;
					next_.store(0);
					busy_ = workers_.size();
					++generation_;
				}
				wake_.notify_all();
				work(job, count);

				std::unique_lock<std::mutex> lock(mutex_);
				idle_.wait(lock, [this] { return busy_ == 0; });
				job_ = nullptr;
			}

			~AnimationThreadPool()
			{
				{
					std::lock_guard<std::mutex> lock(mutex_);
					stopping_ = true;
				}
				wake_.notify_all();
				for (std::thread& worker : workers_)
				{
					worker.join();
				}
			}

			AnimationThreadPool(const AnimationThreadPool&) = delete;
			AnimationThreadPool& operator=(const AnimationThreadPool&) = delete;

		private:
			AnimationThreadPool()
			{
				const unsigned hardware = std::thread::hardware_concurrency();
				const std::size_t workers = hardware > 1 ? std::min<std::size_t>(hardware - 1, 8) : 0;
				workers_.reserve(workers);
				for (std::size_t i = 0; i < workers; ++i)
				{
					workers_.emplace_back([this] { workerLoop(); });
				}
			}

			void work(const std::function<void(std::size_t)>& job, std::size_t count)
			{
				for (std::size_t i = next_.fetch_add(1); i < count; i = next_.fetch_add(1))
				{
					job(i);
				}
			}

			void workerLoop()
			{
				std::uint64_t seen = 0;
				for (;;)
				{
					const std::function<void(std::size_t)>* job = nullptr;
					std::size_t count = 0;
					{
						std::unique_lock<std::mutex> lock(mutex_);
						wake_.wait(lock, [&] { return stopping_ || generation_ != seen; });
						if (stopping_)
						{
							return;
						}
						seen = generation_;
						job = job_;
						count = count_;
					}
					work(*job, count);
					{
						std::lock_guard<std::mutex> lock(mutex_);
						--busy_;
					}
					idle_.notify_one();
				}
			}

			std::vector<std::thread> workers_;
			std::mutex mutex_;
			std::condition_variable wake_;
			std::condition_variable idle_;
			const std::function<void(std::size_t)>* job_ = nullptr;
			std::size_t count_ = 0;
			std::atomic<std::size_t> next_{0};
			std::size_t busy_ = 0;
			std::uint64_t generation_ = 0;
			bool stopping_ = false;
		};
	}

	void updateAnimations(const std::vector<std::shared_ptr<RiggedObject>>& objects, float deltaTime)
	{
		constexpr std::size_t kParallelThreshold = 8;
		if (objects.size() < kParallelThreshold || AnimationThreadPool::instance().workerCount() == 0)
		{
			for (const auto& object : objects)
			{
				if (object)
				{
					object->updateAnimation(deltaTime);
				}
			}
			return;
		}

		std::mutex errorMutex;
		std::exception_ptr firstError;
		const std::function<void(std::size_t)> job = [&](std::size_t i)
		{
			try
			{
				if (objects[i])
				{
					objects[i]->updateAnimation(deltaTime);
				}
			}
			catch (...)
			{
				std::lock_guard<std::mutex> lock(errorMutex);
				if (!firstError)
				{
					firstError = std::current_exception();
				}
			}
		};
		AnimationThreadPool::instance().run(objects.size(), job);
		if (firstError)
		{
			std::rethrow_exception(firstError);
		}
	}
}
