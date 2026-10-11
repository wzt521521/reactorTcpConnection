#pragma once

#include <cstdint>      // uint64_t
#include <chrono>       // std::chrono::steady_clock / time_point / duration
#include <functional>   // std::function
#include <map>          // std::multimap
#include <unordered_map>

#include "EventLoop.hpp" // EventLoop* 成员、Channel 值成员（间接含 Channel.hpp）

using Clock = std::chrono::steady_clock; // 单调时钟
using TimerId = uint64_t;                // 定时器唯一ID

struct Timer {
	using TimeOnCallback = std::function<void()>;
	TimerId Id = 0;
	TimeOnCallback cb;
	Clock::time_point expiration;  // 到期时刻
	Clock::duration intervals{ 0 };  // 周期间隔，0 表示一次性
	bool repeat = false;          // 是否周期运行
};

class TimerQueue {
public:
	TimerQueue(EventLoop* loop);
	~TimerQueue();

	// delay 首次延迟；interval 周期间隔，默认 0（一次性）；返回 TimerId
	TimerId addTimer(std::function<void()> cb,
		Clock::duration delay,
		Clock::duration interval = Clock::duration::zero());
	// 按 id 取消
	void cancelTimer(TimerId id);

private:
	void setSchedule_(); // timerfd 对准最早到期点
	void handleRead();   // 到期事件处理（两阶段）

	EventLoop* loop_;
	int timerfd;
	Channel ch;
	std::multimap<Clock::time_point, TimerId> schedule_; // 按时间排序
	std::unordered_map<TimerId, Timer> timer_;          // 按 id 索引
	TimerId TimeId{ 0 };
};
