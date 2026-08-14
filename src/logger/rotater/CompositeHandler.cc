#include "logger/rotater/RotatedFileHandler.h"

namespace logger
{

    CompositeHandler::CompositeHandler(std::vector<std::unique_ptr<RotatedFileHandler>> chain)
        : chain_(std::move(chain))
    {
    }

    void CompositeHandler::add(std::unique_ptr<RotatedFileHandler> handler)
    {
        chain_.push_back(std::move(handler));
    }

    std::string CompositeHandler::handle(const std::string &filepath)
    {
        std::string current = filepath;
        for (auto &h : chain_)
        {
            current = h->handle(current);
        }
        return current;
    }

    std::string CompositeHandler::suffix() const
    {
        std::string s;
        for (auto &h : chain_)
        {
            s += h->suffix();
        }
        return s;
    }

    std::unique_ptr<RotatedFileHandler> CompositeHandler::clone() const
    {
        auto copy = std::make_unique<CompositeHandler>();
        for (auto &h : chain_)
            copy->add(h->clone());
        return copy;
    }

} // namespace logger
