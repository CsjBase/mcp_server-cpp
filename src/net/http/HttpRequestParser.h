#pragma once

#include "http.h"
#include "http11_parser.h"
#include "HttpRequest.h"

#include <memory>

namespace net
{
    namespace http
    {
        /**
         * @brief HTTP请求解析类
         */
        class HttpRequestParser
        {
        public:
            /// HTTP解析类的智能指针
            typedef std::unique_ptr<HttpRequestParser> ptr;

            /**
             * @brief 构造函数
             */
            HttpRequestParser();
            HttpRequestParser(const HttpRequestParser &) = delete;
            HttpRequestParser &operator=(const HttpRequestParser &) = delete;

            /**
             * @brief 解析协议
             * @param[in, out] data 协议文本内存
             * @param[in] len 协议文本内存长度
             * @return 返回实际解析的长度
             */
            size_t execute(const char *data, size_t len);

            /**
             * @brief 是否解析完成
             * @return 是否解析完成
             */
            int isFinished();

            /**
             * @brief 是否有错误
             * @return 是否有错误
             */
            int hasError();

            /**
             * @brief 返回HttpRequest结构体
             */
            HttpRequest::ptr getData() const { return m_data; }

            /**
             * @brief 设置错误
             * @param[in] v 错误值
             */
            void setError(int v) { m_error = v; }

            /**
             * @brief 获取消息体长度
             */
            uint64_t getContentLength();

            /**
             * @brief 获取http_parser结构体
             */
            const http_parser &getParser() const { return m_parser; }

            void reset();

        public:
            /**
             * @brief 返回HttpRequest协议解析的缓存大小
             */
            static uint64_t GetHttpRequestBufferSize();

            /**
             * @brief 返回HttpRequest协议的最大消息体大小
             */
            static uint64_t GetHttpRequestMaxBodySize();

        private:
            /// http_parser
            http_parser m_parser;
            /// HttpRequest结构
            HttpRequest::ptr m_data;
            /// 错误码
            /// 1000: invalid method
            /// 1001: invalid version
            /// 1002: invalid field
            int m_error;
        };

    }
}