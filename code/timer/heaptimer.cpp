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
    /* ⚠️ 循环条件必须用 i,不能用 j:j 是 size_t,`j >= 0` 恒为真;而 i == 0 时
       (0-1)/2 会下溢成天文数字 → heap_[j] 变成野指针读(UB),读到的垃圾若判成
       "父亲更小"就会走到 SwapNode_(0, 天文数字) → assert 直接把进程 abort。
       这条路径不罕见:del_(0) 在"堆里恰好 2 个节点"时必然走到
       (被换到根上的节点没有孩子 → siftdown_ 直接返回 false → 转来 siftup_(0))。 */
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
        siftup_(i);   /* 新节点只可能比父亲更早到期(比如先加 5s 的、再加 0s 的),
                         必须上浮,否则堆顶不是最早的 → tick() 会一直看着"还没到期"的
                         堆顶 break,真正到期的那个要等它被弹掉才轮到(超时延迟触发) */
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
    /* 改到期时间有**两个方向**:推后 → 需要下沉;提前 → 需要上浮。
       原版只调 siftdown_ —— 遇到"提前"时节点赖在原地不动,堆序就破了
       (父亲比它晚,而它又不在堆顶 → tick() 看着堆顶 break,它被延迟触发)。
       这里用和 add() 已有分支一致的通用写法:"先试下沉,没沉下去再试上浮"。 */
    if(!siftdown_(i, heap_.size())) {
        siftup_(i);
    }
}

/* 按 id 撤销一个定时器节点(不存在就什么都不做,幂等)。
   用途:连接被关闭时把它的定时器一起撤掉,免得残节点到期后又空跑一次回调。
   ⚠️ 只能在"主线程的非回调代码"里调用:
      1) HeapTimer 全程只由主循环线程碰,worker 线程不许直接调(add/adjust/remove/tick 都不是线程安全的);
      2) 回调是在 tick()/doWork() 内部被调用的,回调返回后它们会按"回调前记下的下标"删节点——
         回调里只要改堆(remove 会调 del_ 交换),下标立刻失效 → 删错节点、ref_ 和 heap_ 失去同步。
      所以 web 层是"worker 只把 fd 投进待处理队列,主循环在 tick 之外统一撤/装"。 */
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
    size_t res = -1;
    if(!heap_.empty()) {
        res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
        if(res < 0) { res = 0; }
    }
    return res;
}