#pragma once

#include <memory>
#include <string>
#include <vector>

namespace logger
{

    /// 旋转文件后处理器接口。
    /// 当 Sink 滚动日志文件时调用，对旧文件执行压缩、加密等离线处理。
    /// 实现类需保证 handle() 是线程安全的（Sink 内部已持有互斥锁，
    /// 但如果是异步实现则需自行同步）。
    class RotatedFileHandler
    {
    public:
        virtual ~RotatedFileHandler() = default;

        /// 处理旋转后的文件。完成后可删除原文件。
        /// @param filepath 待处理的文件路径
        /// @return 处理后文件的路径（含新后缀）
        virtual std::string handle(const std::string &filepath) = 0;

        /// 处理后添加的文件后缀，如 ".gz" / ".gz.enc"
        virtual std::string suffix() const = 0;

        /// 深拷贝自身，用于需要复制 handler 链的场景
        virtual std::unique_ptr<RotatedFileHandler> clone() const = 0;
    };

    /// 组合处理器：按 add() 顺序依次调用链上的 handler，线程不安全。
    /// 例如 CompressHandler → EncryptHandler，先压缩再加密。
    class CompositeHandler : public RotatedFileHandler
    {
    public:
        CompositeHandler() = default;
        CompositeHandler(std::vector<std::unique_ptr<RotatedFileHandler>> chain);
        void add(std::unique_ptr<RotatedFileHandler> handler);

        std::string handle(const std::string &filepath) override;
        std::string suffix() const override;

        std::unique_ptr<RotatedFileHandler> clone() const override;
        ~CompositeHandler() override = default;

    private:
        std::vector<std::unique_ptr<RotatedFileHandler>> chain_;
    };

} // namespace logger
