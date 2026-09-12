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
        class HttpSession
        {
        public:
            /// 智能指针类型定义
            typedef std::shared_ptr<HttpSession> ptr;

            enum class HttpRequestParseState
            {
                kExpectInit,
                kExpectHeaders,
                kExpectBody,
                kGotAll,
            };

            HttpSession() : m_state(HttpRequestParseState::kExpectInit) {}

            bool parseRequest(Buffer *);
            bool gotAll() const
            {
                return m_state == HttpRequestParseState::kGotAll;
            }
            void reset()
            {
                m_state = HttpRequestParseState::kExpectInit;
                m_parser = nullptr;
            }

            HttpRequest::ptr getRequest() const
            {
                if (m_parser)
                {
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