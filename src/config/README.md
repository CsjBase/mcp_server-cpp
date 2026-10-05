# config — 配置模块

模板化的配置项注册/查找系统，支持从 JSON 文件批量加载，并可在配置变更时触发回调。参考 sylar 的 Config 体系设计，仅依赖 nlohmann/json。

## 文件一览

| 文件 | 说明 |
|---|---|
| [ConfigVar.h](ConfigVar.h) | 配置项基类 `ConfigVarBase` 与模板子类 `ConfigVar<T>` |
| [Config.h](Config.h) | 配置项全局注册表，提供查找与 JSON 加载 |
| [ConfigException.h](ConfigException.h) | 配置异常（类型不匹配等） |

## 核心设计

- **类型安全**：`ConfigVar<T>` 对每个配置名只允许一种类型；同名不同类型查找会抛出 `ConfigException`。
- **线程安全**：注册表使用 `std::shared_mutex`，查找用共享锁、插入用独占锁，读写多线程友好。
- **变更通知**：`setValue` 时依次触发通过 `addChangeCallback` 注册的回调（回调返回唯一 id，可精确移除）。
- **懒注册**：`Config::lookup<T>(name, default, desc)` 在配置不存在时用默认值自动创建，业务方无需预先声明。

## 快速上手

```cpp
#include "config/Config.h"

// 注册（或获取）一个配置项：不存在则用默认值创建
auto port = config::Config::lookup<uint16_t>("server.port", 8080, "监听端口");

// 读取
std::cout << "port = " << port->getValue() << "\n";

// 修改（会触发变更回调）
port->setValue(9090);

// 监听变更
uint64_t cbId = port->addChangeCallback([](uint16_t oldV, uint16_t newV) {
    LOG_INFO(LOGGER_DEFAULT(), "port changed: {} -> {}", oldV, newV);
});
port->removeChangeCallback(cbId);
```

## 从 JSON 加载

```cpp
// 从文件加载：JSON 键名与配置名对应（大小写不敏感）
config::Config::loadFromFile("conf/server.json");

// 从内存中的 JSON 对象加载
nlohmann::json j = nlohmann::json::parse(R"({"server.port": 9090})");
config::Config::loadFromJson(j);

// 批量加载目录下所有 *.json（force 表示是否覆盖已有值）
config::Config::loadFromConfDir("conf", /*force=*/false);
```

`fromJson` 通过 `json.get<T>()` 做类型转换，支持任意 nlohmann/json 可反序列化的类型，包括 `std::vector`、`std::map` 等复合类型。

## 扩展自定义类型

只需让类型支持 nlohmann/json 的 `to_json` / `from_json`，即可直接作为配置项类型：

```cpp
struct ServerInfo { std::string ip; uint16_t port; };

// 实现 to_json / from_json 后：
auto info = config::Config::lookup<ServerInfo>("server.info", ServerInfo{"0.0.0.0", 8080});
```

## 线程安全说明

- `getValue()` / `setValue()` 内部以互斥锁保护当前值，可跨线程调用。
- 变更回调在 `setValue` 调用线程中同步执行，回调中请勿长时间阻塞。
- 配置名统一转为小写存储，`"Server.Port"` 与 `"server.port"` 指向同一配置项。
