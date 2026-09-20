#pragma once

#include "net/http/http.h"
#include "net/http/HttpRequest.h"
#include "net/http/HttpResponse.h"
#include "HttpRequestParser.h"
#include "net/TcpConnection.h"

namespace net
{
    class Buffer;
    namespace http
    {
        class HttpContext
        {
        public:
            /// 智能指针类型定义
            typedef std::shared_ptr<HttpContext> ptr;

            enum class HttpRequestParseState
            {
                kExpectHeaders,
                kExpectBody,
                kGotAll,
            };

            HttpContext()
                : m_state(HttpRequestParseState::kExpectHeaders),
                  m_parser(new HttpRequestParser)
            {
            }

            HttpContext(const HttpContext &) = delete;
            HttpContext &operator=(const HttpContext &) = delete;

            bool parseRequest(Buffer *);
            bool gotAll() const
            {
                return m_state == HttpRequestParseState::kGotAll;
            }
            void reset()
            {
                m_state = HttpRequestParseState::kExpectHeaders;
                m_parser->reset();
            }

            HttpRequest::ptr getRequest()
            {
                if (!m_parser->hasError() && m_parser->isFinished())
                {
                    m_parser->getData()->init();
                    return m_parser->getData();
                }
                return nullptr;
            }

        private:
            HttpRequestParseState m_state;
            HttpRequestParser::ptr m_parser;
        };
    }

}