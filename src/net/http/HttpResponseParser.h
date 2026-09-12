#pragma once

#include "http.h"
#include "httpclient_parser.h"
#include "HttpResponse.h"

#include <memory>

namespace net
{
    namespace http
    {
        /**
         * @brief Http响应解析结构体
         */
        class HttpResponseParser
        {
        public:
            /// 智能指针类型
            typedef std::shared_ptr<HttpResponseParser> ptr;

            /**
             * @brief 构造函数
             */
            HttpResponseParser();

            /**
             * @brief 解析HTTP响应协议
             * @param[in, out] data 协议数据内存
             * @param[in] len 协议数据内存大小
             * @param[in] chunck 是否在解析chunck
             * @return 返回实际解析的长度,并且移除已解析的数据
             */
            size_t execute(char *data, size_t len, bool chunck);

            /**
             * @brief 是否解析完成
             */
            int isFinished();

            /**
             * @brief 是否有错误
             */
            int hasError();

            /**
             * @brief 返回HttpResponse
             */
            HttpResponse::ptr getData() const { return m_data; }

            /**
             * @brief 设置错误码
             * @param[in] v 错误码
             */
            void setError(int v) { m_error = v; }

            /**
             * @brief 获取消息体长度
             */
            uint64_t getContentLength();

            /**
             * @brief 返回httpclient_parser
             */
            const httpclient_parser &getParser() const { return m_parser; }

        public:
            /**
             * @brief 返回HTTP响应解析缓存大小
             */
            static uint64_t GetHttpResponseBufferSize();

            /**
             * @brief 返回HTTP响应最大消息体大小
             */
            static uint64_t GetHttpResponseMaxBodySize();

        private:
            /// httpclient_parser
            httpclient_parser m_parser;
            /// HttpResponse
            HttpResponse::ptr m_data;
            /// 错误码
            /// 1001: invalid version
            /// 1002: invalid field
            int m_error;
        };

    }
}