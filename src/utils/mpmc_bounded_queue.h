#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace utils
{

    /// 有界无锁 MPMC 队列 (Vyukov / 1024cores 算法)
    ///
    /// 核心思想: 用固定槽位 + 每槽单调递增的序号(sequence)代替指针链,
    /// 生产/消费双方通过对共享游标 pos 的 CAS 抢占槽位, 全程无锁,
    /// 且天然免疫指针 ABA 问题(槽位永不释放, 序号单调递增)。
    ///
    /// 序号语义: 槽 i 初始 seq=i; 入队发布后 seq=i+1(有数据);
    /// 出队消费后 seq=i+capacity(槽位归还, 可供下一轮入队)。
    /// 空/满/竞态全部由 seq 与 pos 的差值判断:
    ///   enqueue: dif==0 空闲可抢占; dif<0 队列满; dif>0 他人抢占未发布 → 重试
    ///   dequeue: dif==0 数据就绪可抢占; dif<0 队列空; dif>0 他人消费中 → 重试
    ///
    /// 特点:
    /// - enqueue/dequeue 无锁, 多生产者多消费者安全
    /// - 容量向上取整为 2 的幂(最小 2)
    /// - 队列满时 enqueue 返回 false; 队列空时 dequeue 返回 false
    /// - 入队/出队游标各自独立 → 生产与消费互不阻塞
    /// - pos 为 size_t, 溢出回绕是算法设计内行为
    template <typename T>
    class mpmc_bounded_queue
    {
    public:
        /// @param capacity 期望容量, 内部向上取整为 2 的幂
        explicit mpmc_bounded_queue(size_t capacity)
        {
            size_t pow2 = 2;
            while (pow2 < capacity)
                pow2 <<= 1;
            mask_ = pow2 - 1;
            // Cell 含原子成员不可移动, 用原始数组原地构造(槽位地址终身不变, 这是 ABA 免疫的前提)
            buffer_ = std::make_unique<Cell[]>(pow2);
            for (size_t i = 0; i < pow2; ++i)
                buffer_[i].seq_.store(i, std::memory_order_relaxed);
        }

        mpmc_bounded_queue(const mpmc_bounded_queue &) = delete;
        mpmc_bounded_queue &operator=(const mpmc_bounded_queue &) = delete;

        /// 入队; 队列满返回 false
        bool enqueue(const T &item)
        {
            size_t pos = enqueuePos_.load(std::memory_order_relaxed);
            Cell *cell = nullptr;
            for (;;)
            {
                cell = &buffer_[pos & mask_];
                size_t seq = cell->seq_.load(std::memory_order_acquire);
                intptr_t dif = (intptr_t)seq - (intptr_t)pos;
                if (dif == 0)
                {
                    // 槽空闲: CAS 抢占入队游标(弱 CAS 允许假失败, 失败后重载游标)
                    if (enqueuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                        break;
                    pos = enqueuePos_.load(std::memory_order_relaxed);
                }
                else if (dif < 0)
                {
                    return false; // 槽仍被上一轮数据占用 → 队列满
                }
                else
                {
                    // 其他生产者已抢占但未发布 → 换下一个槽
                    pos = enqueuePos_.load(std::memory_order_relaxed);
                }
            }
            cell->data_ = item;
            cell->seq_.store(pos + 1, std::memory_order_release); // 发布数据
            return true;
        }

        /// 出队; 队列空返回 false
        bool dequeue(T &out)
        {
            size_t pos = dequeuePos_.load(std::memory_order_relaxed);
            Cell *cell = nullptr;
            for (;;)
            {
                cell = &buffer_[pos & mask_];
                size_t seq = cell->seq_.load(std::memory_order_acquire);
                intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);
                if (dif == 0)
                {
                    // 数据就绪: CAS 抢占出队游标
                    if (dequeuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                        break;
                    pos = dequeuePos_.load(std::memory_order_relaxed);
                }
                else if (dif < 0)
                {
                    return false; // 队列空
                }
                else
                {
                    // 其他消费者已抢占但未归还槽位 → 换下一个槽
                    pos = dequeuePos_.load(std::memory_order_relaxed);
                }
            }
            out = cell->data_;
            cell->seq_.store(pos + mask_ + 1, std::memory_order_release); // 归还槽位
            return true;
        }

        /// 实际容量(向上取整后的 2 的幂)
        size_t capacity() const { return mask_ + 1; }

    private:
        struct Cell
        {
            std::atomic<size_t> seq_;
            T data_;
        };

        std::unique_ptr<Cell[]> buffer_;
        size_t mask_ = 0;
        // 入队/出队游标各自独立 → 生产与消费互不阻塞
        std::atomic<size_t> enqueuePos_{0};
        std::atomic<size_t> dequeuePos_{0};
    };

} // namespace utils
