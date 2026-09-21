#include "heaptimer.h"

void HeapTimer::SwapNode_(size_t i, size_t j) {
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    std::swap(heap_[i], heap_[j]);
    ref_[heap_[i].id] = i;
    ref_[heap_[j].id] = j;
}

void HeapTimer::siftup_(size_t i) {
    assert(i < heap_.size());
    /* 循环条件用 i 而不是 (i-1)/2 得到的 j:j 为 size_t,`j >= 0` 恒真,
       i == 0 时 (0-1)/2 下溢成巨大值 → heap_[j] 越界访问。 */
    while(i > 0) {
        size_t j = (i - 1) / 2;
        if(heap_[j] < heap_[i]) { break; }
        SwapNode_(i, j);
        i = j;
    }
}

bool HeapTimer::siftdown_(size_t index, size_t n) {
    assert(index >= 0 && index < heap_.size());
    assert(n >= 0 && n <= heap_.size());
    size_t i = index;
    size_t j = i * 2 + 1;
    while(j < n) {
        if(j + 1 < n && heap_[j + 1] < heap_[j]) j++;
        if(heap_[i] < heap_[j]) break;
        SwapNode_(i, j);
        i = j;
        j = i * 2 + 1;
    }
    return i > index;
}

void HeapTimer::add(int id, int timeout, const TimeoutCallBack& cb) {
    assert(id >= 0);
    size_t i;
    if(ref_.count(id) == 0) {
        i = heap_.size();
        ref_[id] = i;
        heap_.push_back({id, Clock::now() + MS(timeout), cb});
        siftup_(i);   /* 新节点可能比父亲更早到期,上浮以维持"堆顶最早"的不变式 */
    }
    else {
        i = ref_[id];
        heap_[i].expires = Clock::now() + MS(timeout);
        heap_[i].cb = cb;
        if(!siftdown_(i, heap_.size())) {
            siftup_(i);
        }
    }
}

void HeapTimer::doWork(int id) {
    if(heap_.empty() || ref_.count(id) == 0) {
        return;
    }
    size_t i = ref_[id];
    TimerNode node = heap_[i];
    node.cb();
    del_(i);
}

void HeapTimer::del_(size_t index) {
    assert(!heap_.empty() && index >= 0 && index < heap_.size());
    size_t i = index;
    size_t n = heap_.size() - 1;
    assert(i <= n);
    if(i < n) {
        SwapNode_(i, n);
        if(!siftdown_(i, n)) {
            siftup_(i);
        }
    }
    ref_.erase(heap_.back().id);
    heap_.pop_back();
}

void HeapTimer::adjust(int id, int timeout) {
    assert(!heap_.empty() && ref_.count(id) > 0);
    size_t i = ref_[id];
    heap_[i].expires = Clock::now() + MS(timeout);
    /* 到期时间可能推后(需下沉)也可能提前(需上浮):先试下沉,没沉下去说明该上浮 */
    if(!siftdown_(i, heap_.size())) {
        siftup_(i);
    }
}

/* 按 id 撤销一个定时器节点(不存在则什么都不做,幂等)。
   线程约束:HeapTimer 不是线程安全的,只允许主循环线程在回调之外调用。 */
void HeapTimer::remove(int id) {
    std::unordered_map<int, size_t>::iterator it = ref_.find(id);
    if(it == ref_.end()) { return; }
    del_(it->second);
}

void HeapTimer::tick() {
    if(heap_.empty()) {
        return;
    }
    while(!heap_.empty()) {
        TimerNode node = heap_.front();
        if(std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0) { 
            break; 
        }
        node.cb();
        pop();
    }
}

void HeapTimer::pop() {
    assert(!heap_.empty());
    del_(0);
}

void HeapTimer::clear() {
    ref_.clear();
    heap_.clear();
}

int HeapTimer::GetNextTick() {
    tick();
    int res = -1;
    if(!heap_.empty()) {
        res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
        if(res < 0) { res = 0; }
    }
    return res;
}