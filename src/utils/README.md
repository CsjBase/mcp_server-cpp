# utils — 基础工具库

被其余模块共享的底层组件：线程池、无锁/阻塞队列、环形缓冲、加解密（OpenSSL AES-GCM）与压缩（zlib）、系统接口。除 OpenSSL/ZLIB 外全部基于标准库与系统调用自实现。

## 目录结构

```
utils/
├── thread_pool.h             # 通用线程池（有界队列 + 多种入队策略）
├── mpmc_bounded_queue.h      # MPMC 无锁有界队列（Vyukov 算法）
├── mpmc_blocking_queue.h     # 阻塞式 MPMC 队列（条件变量，可选覆盖最旧）
├── circular_q.h              # 单生产者单消费者环形缓冲
├── cached_clock.h / .cc      # 后台时钟缓存（1ms 粒度原子时间戳）
├── null_mutex.h              # 空互斥量（模板化开关锁）
├── crypt.h / aes_crypt.h     # 加解密接口 + AES-256-GCM 实现（OpenSSL）
├── compress.h / zlib_compress.h # 压缩接口 + zlib 实现
└── os.h                      # 系统接口：thread_id、sleep 等
```

## ThreadPool — 通用线程池

固定线程数 + 有界任务队列，支持四种入队策略与优雅/立即两种停止方式。

```cpp
#include "utils/thread_pool.h"

utils::ThreadPool pool(4, 1024);   // 4 线程，队列上限 1024

pool.submit([] { do_work(); });                    // 队列满：阻塞等待
pool.submit_overwrite([] { do_work(); });          // 队列满：覆盖最旧任务
bool ok = pool.try_submit([] { do_work(); });      // 队列满：立即返回 false
bool ok2 = pool.submit_for(100ms, [] { do_work(); }); // 带超时的阻塞提交

pool.pending_count();    // 待执行任务数
pool.completed_count();  // 已完成任务数（统计）

pool.stop_gracefully();  // 排空队列后退出（不再接收新任务）
// pool.stop_immediately();  // 丢弃剩余任务立即退出
```

被 [logger](../logger/README.md)（异步落盘）、[db](../db/README.md)（异步建连）等模块复用。线程池停止后提交的任务将不执行（返回 false）。

## 无锁 / 阻塞队列

### mpmc_bounded_queue — MPMC 无锁有界队列

Vyukov 环形数组 + 槽位序号（seq）算法，多生产者多消费者，**入队/出队全程无锁**：

```cpp
utils::mpmc_bounded_queue<int> q(1024);  // 内部容量向上取整为 2 的幂
q.enqueue(42);
int v;
bool ok = q.dequeue(v);   // 空返回 false
q.capacity();
```

`pos` 为 `size_t`，溢出回绕是算法设计内行为。适合对锁竞争敏感的热路径（如 [db 连接池](../db/README.md)的空闲连接队列）。

### mpmc_blocking_queue — 阻塞式 MPMC 队列

基于条件变量，支持阻塞出队与「覆盖最旧」入队：

```cpp
utils::mpmc_blocking_queue<Task> q(1024);

q.enqueue(task);                    // 队列满：阻塞等待
q.enqueue_for(task, 100ms);         // 带超时入队
q.enqueue_overwrite(task);          // 队列满：丢弃最旧，压入新任务

auto item = q.dequeue();            // 阻塞出队
auto item2 = q.try_dequeue();       // 非阻塞，返回 std::optional<T>

q.overrun_counter();  // 覆盖丢弃计数
q.discard_counter();  // 显式丢弃计数
```

### circular_q — SPSC 环形缓冲

单生产者单消费者，零锁开销；容量加一槽位用于区分 empty/full。

## 加解密与压缩

### AES-256-GCM（OpenSSL）

```cpp
#include "utils/aes_crypt.h"

utils::AesCrypt aes("32-byte-key............");
// 每次加密自动生成随机 IV，输出格式：[12B IV][密文][16B GCM Tag]

size_t bound = aes.encrypt_bound(plain_len);   // 输出缓冲上限
std::vector<unsigned char> out(bound);
size_t out_len = aes.encrypt(plain.data(), plain.size(), out.data(), out.size());

std::vector<unsigned char> dec(plain_len);
aes.decrypt(out.data(), out_len, dec.data(), dec.size());
```

`Crypt` 为加解密抽象接口，可替换其他算法实现。

### zlib 压缩

```cpp
#include "utils/zlib_compress.h"

utils::ZlibCompress z;
size_t bound = z.compressed_bound(len);
std::vector<unsigned char> buf(bound);
size_t n = z.compress(data, len, buf.data(), bound);

std::string original = z.decompress(buf.data(), n);
```

`Compress` 为压缩抽象接口。二者均被 [logger 轮转处理器](../logger/README.md)（`CompressHandler` / `EncryptHandler`）复用。

### cached_clock — 后台时钟缓存

独立后台线程每 ~1ms 刷新原子墙钟/单调钟时间戳，读路径仅一次无锁原子加载：

```cpp
#include "utils/cached_clock.h"

auto t1 = utils::cached_wall_now();    // system_clock 语义, ~1ms 粒度
auto t2 = utils::cached_steady_now();  // steady_clock 语义, ~1ms 粒度
```

动机：`clock_gettime` 在部分虚拟化环境（VM 拦截，缺 vDSO 快路径）单次可达 ~20µs，对每条日志/每次借还都取时间的热路径是灾难性的。本类把真实取时收敛到唯一后台线程，热点线程零系统调用 —— [logger](../logger/README.md) 日志时间戳、[db 连接池](../db/README.md) 秒级超时判定均使用它。仅适用于毫秒级及以上精度场景；精确计时请用真实时钟。

### os.h — 系统接口

`utils::thread_id()`（跨平台线程 id）、`utils::sleep_for_millis(ms)` 等小工具，供 [net EventLoop](../net/README.md) 线程判定等处使用。

## 测试与示例

- 单元测试：[tests/utils](../../tests/utils/)
- 使用示例：[examples/utils](../../examples/utils/)（`aes_crypt_example`、`zlib_compress_example`）
