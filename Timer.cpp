#include "Timer.hpp"

#include <utility>       // std::move
#include <vector>
#include <sys/timerfd.h> // timerfd_create / timerfd_settime / struct itimerspec
#include <unistd.h>      // ::read / ::close

TimerQueue::TimerQueue(EventLoop* loop)
	: loop_(loop),
	timerfd(::timerfd_create(CLOCK_MONOTONIC,
		TFD_NONBLOCK | TFD_CLOEXEC)),
	ch(timerfd) {
	// timerfd 也走 Channel 统一事件分发，和网络 fd 完全一致
	ch.setReadCallback([this] { handleRead(); });
	ch.enableRead();
	loop_->updateChannel(&ch);
}

TimerQueue::~TimerQueue() {
	::close(timerfd);
}

TimerId TimerQueue::addTimer(std::function<void()> cb,
	Clock::duration delay,
	Clock::duration interval) {
	Timer t;
	t.Id = ++TimeId;
	t.cb = std::move(cb);
	t.expiration = Clock::now() + delay;
	t.repeat = interval.count() > 0;
	t.intervals = interval;

	// 新定时器是否比当前最早到期的还早
	bool earliestChanged =
		schedule_.empty() || t.expiration < schedule_.begin()->first;
	TimerId id = t.Id;
	timer_[id] = std::move(t);                       // id 索引表
	schedule_.emplace(timer_[id].expiration, id);    // 时间排序表
	if (earliestChanged) setSchedule_();             // 最早点变了，重设 timerfd
	return id;
}

void TimerQueue::cancelTimer(TimerId id) {
	auto it = timer_.find(id);
	if (it == timer_.end()) return;
	// 同一时间可能有多个，按 id 精确匹配
	auto range = schedule_.equal_range(it->second.expiration);
	for (auto q = range.first; q != range.second; ++q) {
		if (q->second == id) { schedule_.erase(q); break; }
	}
	timer_.erase(it);
	setSchedule_();
}

// 重新校准对齐 timerfd：对准 schedule_ 中最早到期点
void TimerQueue::setSchedule_() {
	struct itimerspec its {}; // 全 0 表示解除定时器
	if (!schedule_.empty()) {
		auto now = Clock::now();
		auto when = schedule_.begin()->first;
		if (when <= now) when = now + std::chrono::nanoseconds(1); // 已过期则 1ns 后触发
		auto d = when - now;
		auto secs = std::chrono::duration_cast<std::chrono::seconds>(d);
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(d - secs);
		its.it_value.tv_sec = secs.count();
		its.it_value.tv_nsec = ns.count();
	}
	// it_interval 为 0 不重复，周期由自己在 handleRead 里管理
	::timerfd_settime(timerfd, 0, &its, nullptr);
}

void TimerQueue::handleRead() {
	// 阶段零：read 清事件，否则 LT 下会一直触发可读
	uint64_t expireCount = 0;
	ssize_t n = ::read(timerfd, &expireCount, sizeof(expireCount));
	(void)n;
	auto now = Clock::now();

	// 阶段一：纯摘，不执行任何回调（一次性/周期都先摘到 fired）
	std::vector<Timer> fired;
	auto it = schedule_.begin();
	while (it != schedule_.end() && it->first <= now) {
		auto tit = timer_.find(it->second);
		if (tit != timer_.end()) {
			fired.push_back(std::move(tit->second)); // 移到临时数组
			timer_.erase(tit);
		}
		it = schedule_.erase(it); // 从时间表移除，erase 返回下一个迭代器
	}

	// 阶段二：统一执行 cb；周期任务算好下一次到期点重新入队
	for (auto& t : fired) {
		if (t.cb) t.cb();
		if (t.repeat) {
			auto next = t.expiration + t.intervals;
			if (next <= now) next = now + t.intervals; // 落后太多则从现在重排，防雪崩
			t.expiration = next;
			TimerId id = t.Id;
			timer_[id] = std::move(t);
			schedule_.emplace(next, id);
		}
	}

	setSchedule_(); // 重新对准剩余最早点
}
