#include "HttpContext.h"
#include "net/Buffer.h"

namespace net::http
{

    bool HttpContext::parseRequest(Buffer *buf)
    {
        uint64_t maxBufferSize = HttpRequestParser::GetHttpRequestBufferSize();
        uint64_t bufferSize = buf->readableBytes() > maxBufferSize ? maxBufferSize : buf->readableBytes();

        if (m_state == HttpRequestParseState::kExpectHeaders)
        {
            size_t len = m_parser->execute(buf->peek(), bufferSize);
            if (m_parser->hasError() || len == maxBufferSize)
            {
                return false;
            }
            buf->retrieve(len);
            if (m_parser->isFinished())
            {
                m_state = HttpRequestParseState::kExpectBody;
            }
        }

        if (m_state == HttpRequestParseState::kExpectBody)
        {
            int64_t contentLength = m_parser->getContentLength();
            if (contentLength > 0)
            {
                if (buf->readableBytes() >= contentLength)
                {
                    m_parser->getData()->setBody(std::string(buf->peek(), contentLength));
                    buf->retrieve(contentLength);
                    m_state = HttpRequestParseState::kGotAll;
                }
            }
            else
            {
                m_state = HttpRequestParseState::kGotAll;
            }
        }
        return true;
    }
}