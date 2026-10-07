# logger — 高性能日志库

仿 spdlog API 风格、从零实现的异步日志库。支持多 Sink 输出、格式化 pattern、按大小/按时间文件轮转（可叠加压缩、加密处理）、异步落盘与周期性 flush。依赖 fmt 与 nlohmann/json，不使用任何 spdlog 代码。

## 目录结构

```
logger/
├── Logger.h / Logger.cc        # 日志器基类：级别过滤、格式化、分发到 Sink
├── AsyncLogger.h / .cc         # 异步日志器：借助线程池在后台落盘
├── SynchronousFactory.h        # 同步日志器工厂
├── AsynchronousFactory.h       # 异步日志器工厂
├── LoggerManager.h / .cc       # 全局日志器注册表（单例），周期 flush
├── LogEvent.h / .cc            # 日志事件（级别、时间戳、源位置、消息）
├── LogEventBuffer.h / .cc      # 日志事件缓冲（前端打包，后端批量处理）
├── LogFormatter.h / .cc        # pattern 编译与格式化
├── LogConfig.h / .cc           # 配置结构（可配合 config 模块从 JSON 加载）
├── PeriodicWorker.h / .cc      # 周期任务线程（周期 flush）
├── public.h / log.h            # LogLevel 枚举、日志宏
├── sinks/                      # 输出端：控制台、文件、按大小/按天轮转
└── rotater/                    # 轮转策略与轮转后处理（压缩/加密）
```

## 核心概念

```
Logger（一个名字 → 一组 Sink + 一个 Formatter）
   │  level 过滤 → 格式化 → sink_it_()
   ├── Logger        ：同步，直接在调用线程写各 Sink
   └── AsyncLogger   ：异步，事件打包进 LogEventBuffer，投递到
                         utils::ThreadPool 的后台任务中写各 Sink

Sink（输出端）
   ├── StdoutLogSink / StderrLogSink      控制台（mt/st 两种锁策略）
   ├── ColorLogSink                       带颜色的控制台输出
   ├── BasicFileLogSink                   单文件追加
   ├── RotatingFileLogSink                按大小轮转（SizeBasedRotation）
   └── DailyFileLogSink                   按时间轮转（TimeBasedRotation）

Rotater（轮转管道）
   RotationStrategy（何时轮转） → RotatedFileHandler 链（轮转后做什么：
   CompressHandler 压缩 / EncryptHandler 加密，可组合）
```

- **Sink 独立过滤**：Sink 自身也有 level 门槛，可实现「Error 写文件 + 全量打控制台」这类组合。
- **`_mt` / `_st`**：多线程版 Sink 内部加锁（`ConsoleMutex`），单线程版无锁（`ConsoleNullMutex`），与 spdlog 命名一致。

## 快速上手

```cpp
#include "logger/log.h"

// 方式一：直接用全局默认日志器
LOG_INFO(LOGGER_DEFAULT(), "hello {}", "world");
LOG_WARN(LOGGER_NAME("net"), "connection reset, fd={}", fd);

// 方式二：手工构造 Logger + Sink
auto console = std::make_shared<logger::StdoutLogSink>();
console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");

auto logger = std::make_shared<logger::Logger>("app", console);
logger->set_level(logger::LogLevel::Debug);

logger->info("server listening on {}", 8080);
logger->error("fatal: {}", errmsg);
logger->flush();
```

日志宏（[log.h](log.h)）：`LOG_TRACE / LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERROR / LOG_FATAL`，自动附带文件与行号（`SRC_LOCATION`）。

## 异步日志

```cpp
#include "logger/AsyncLogger.h"
#include "utils/thread_pool.h"

// 后台线程池负责落盘（推荐每个进程共享一个）
auto tp = std::make_shared<utils::ThreadPool>(1, 65536);

auto sink = std::make_shared<logger::RotatingFileLogSink<std::mutex>>(
    "logs/server.log", 10 * 1024 * 1024, 10);

auto async = std::make_shared<logger::AsyncLogger>(
    "async", sink, tp,
    logger::AsyncOverflowStrategy::Block,  // 队列满时阻塞提交
    /*batch_size=*/256,                    // 批大小上限
    /*flush_interval=*/std::chrono::milliseconds(1)); // 时间兜底提交间隔

async->info("this line is written by background thread");
async->flush();  // 换出残余批 + 提交 flush 屏障，保证此前事件全部落盘
```

**前端攒批**：事件先拷入当前批（`LogEventBuffer` 250B 栈内缓冲，短消息零堆分配），仅在以下任一条件触发时把整批作为一个任务投递线程池，队列互斥与唤醒系统调用等"投递税"被摊薄到每条事件约几纳秒：

| 触发条件 | 说明 |
|---|---|
| 批满（`batch_size`，默认 256） | 吞吐优先，凑满一批再走 |
| 时间兜底（`flush_interval`，默认 1ms） | 距本批首条超过间隔后，**下一条日志到达时**提交本批；0 表示禁用 |
| 事件级别 ≥ `flush_level` | error 等紧急日志即时提交（与同步 Logger 语义一致） |

完全静默期（无新事件）由 `flush()`、析构或 `LoggerManager::flush_every()` 周期 flush 兜底。注意：共享线程池为多 worker 时批任务并发执行，不保证跨批全局顺序；需要严格顺序请用单 worker 池。

溢出策略（`AsyncOverflowStrategy`）在**批提交时**生效：

| 策略 | 队列满时行为 | 适用场景 |
|---|---|---|
| `Block` | 阻塞等待队列有空位；线程池已停止等提交失败时**同步写入兜底**（调用线程直接落盘并 flush），保证不丢日志 | 不丢日志，延迟敏感 |
| `DiscardNew` | 丢弃新日志 | 高吞吐、允许少量丢失 |
| `DiscardOldest` | 覆盖最旧日志 | 保留最新上下文 |

析构时残余批会尽力提交（不丢事件）；通过 `LoggerManager::instance().set_thread_pool(pool)` 设置全局线程池后，`AsynchronousFactory` 创建的异步日志器会自动使用。

## 日志轮转

### 按大小轮转

```cpp
// 单文件超过 10MB 轮转，最多保留 5 个历史文件
auto sink = std::make_shared<logger::RotatingFileLogSink<std::mutex>>(
    "logs/server.log", 10 * 1024 * 1024, 5);
sink->rotate_now();   // 手动触发一次轮转
```

### 按时间轮转（DailyFileLogSink）

按天（或自定义间隔）滚动文件名，例如 `server_2026-10-05.log`。

### 轮转后处理链

轮转产生的旧文件可依次经过处理器链，例如压缩后加密：

```cpp
// RotatedFileHandler 链：先压缩再加密
auto compress = std::make_shared<logger::CompressHandler>();
auto encrypt  = std::make_shared<logger::EncryptHandler>("aes-key");
// 处理器间可组合（CompositeHandler），轮转后按顺序执行
```

## Pattern 格式

支持 spdlog 风格 pattern，常用项：

| 项 | 含义 | 项 | 含义 |
|---|---|---|---|
| `%n` | 日志器名 | `%l` | 级别（`%^..%$` 包裹着色） |
| `%v` | 日志消息 | `%d{...}` | 时间戳（strftime 格式） |
| `%t` | 线程 id | `%s:%#` | 源文件名:行号 |
| `%f` | 源文件名 | `%+` | 默认完整 pattern |

默认 pattern：`[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v`

## 配置化（配合 config 模块）

`LogConfig.h` 定义了 `LogSinkConfig` / `LoggerConfig` 的 JSON 序列化，并注册了全局配置项 `g_logger_config`，可在 `config` 模块加载 `conf/logs.json` 后通过 `initLogConfig()` 批量创建日志器（同步/异步、任意 Sink 组合）。

## 线程安全与性能要点

- **时间戳走后台时钟缓存**：每条日志的时间戳取自 [utils/cached_clock](../utils/README.md)（~1ms 粒度原子时间戳），规避 `clock_gettime` 在虚拟化环境下的高开销（VM 拦截实测 ~20µs/次），热路径零系统调用。代价是时间戳毫秒级粒度。
- **格式化零堆分配**：`Logger::log()` 前端用 `fmt::format_to` 直接把消息格式化进栈上内联 buffer（250B 内零 malloc），避免先落 `std::string` 再被 Sink 二次拷贝。
- **数字直写 + 按秒日期缓存**：格式项用展开的十进制直写（`append_pad2/pad4` 等）替代 `fmt::format_to` 的运行时格式串解析；默认 pattern 的日期前缀按秒缓存，同一秒内只拼一次字符串、其余调用直接追加（spdlog 同款策略）。
- **前端零锁竞争**：同步 Logger 的格式化发生在调用线程；异步 Logger 前端仅做原子操作 + 打包，真正的 I/O 都在后台线程。
- **日志事件打包**：`LogEventBuffer` 一次携带多条事件提交给线程池，摊薄任务调度开销。
- **周期 flush**：`LoggerManager::flush_every(interval)` 可配置周期落盘，避免低频日志长期滞留缓冲区。
- 日志宏自带 `LOGGER_TRY / LOGGER_CATCH` 保护：Sink 抛出的异常会被捕获并转交 error handler，不会中断业务线程。
