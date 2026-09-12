#include "Servlet.h"

#include <fnmatch.h>
#include <mutex>

namespace net::http
{

    FunctionServlet::FunctionServlet(callback cb)
        : Servlet("FunctionServlet"), m_cb(cb)
    {
    }

    int32_t FunctionServlet::handle(net::http::HttpRequest::ptr request, net::http::HttpResponse::ptr response)
    {
        return m_cb(request, response);
    }

    ServletDispatch::ServletDispatch()
        : Servlet("ServletDispatch")
    {
        m_default.reset(new NotFoundServlet("myhttp/1.0"));
    }

    int32_t ServletDispatch::handle(net::http::HttpRequest::ptr request, net::http::HttpResponse::ptr response)
    {
        auto slt = getMatchedServlet(request->getPath());
        if (slt)
        {
            slt->handle(request, response);
        }
        return 0;
    }

    void ServletDispatch::addServlet(const std::string &uri, Servlet::ptr slt)
    {
        std::string uri_v = removeTrailingSlash(uri);
        std::unique_lock lock(m_mutex);
        m_datas[uri_v] = std::make_shared<HoldServletCreator>(slt);
    }

    void ServletDispatch::addServletCreator(const std::string &uri, IServletCreator::ptr creator)
    {
        std::string uri_v = removeTrailingSlash(uri);
        std::unique_lock lock(m_mutex);
        m_datas[uri_v] = creator;
    }

    void ServletDispatch::addGlobServletCreator(const std::string &uri, IServletCreator::ptr creator)
    {
        std::unique_lock lock(m_mutex);
        for (auto it = m_globs.begin();
             it != m_globs.end(); ++it)
        {
            if (it->first == uri)
            {
                it->second = creator;
                return;
            }
        }
        m_globs.push_back(std::make_pair(uri, creator));
    }

    void ServletDispatch::addServlet(const std::string &uri, FunctionServlet::callback cb)
    {
        std::string uri_v = removeTrailingSlash(uri);
        std::unique_lock lock(m_mutex);
        m_datas[uri_v] = std::make_shared<HoldServletCreator>(
            std::make_shared<FunctionServlet>(cb));
    }

    void ServletDispatch::addGlobServlet(const std::string &uri, Servlet::ptr slt)
    {
        std::unique_lock lock(m_mutex);
        for (auto it = m_globs.begin();
             it != m_globs.end(); ++it)
        {
            if (it->first == uri)
            {
                it->second = std::make_shared<HoldServletCreator>(slt);
                return;
            }
        }
        m_globs.push_back(std::make_pair(uri, std::make_shared<HoldServletCreator>(slt)));
    }

    void ServletDispatch::addGlobServlet(const std::string &uri, FunctionServlet::callback cb)
    {
        return addGlobServlet(uri, std::make_shared<FunctionServlet>(cb));
    }

    void ServletDispatch::delServlet(const std::string &uri)
    {
        std::string uri_v = removeTrailingSlash(uri);
        std::unique_lock lock(m_mutex);
        m_datas.erase(uri_v);
    }

    void ServletDispatch::delGlobServlet(const std::string &uri)
    {
        std::unique_lock lock(m_mutex);
        for (auto it = m_globs.begin();
             it != m_globs.end(); ++it)
        {
            if (it->first == uri)
            {
                m_globs.erase(it);
                break;
            }
        }
    }

    Servlet::ptr ServletDispatch::getServlet(const std::string &uri)
    {
        std::string uri_v = removeTrailingSlash(uri);
        std::shared_lock lock(m_mutex);
        auto it = m_datas.find(uri_v);
        return it == m_datas.end() ? nullptr : it->second->get();
    }

    Servlet::ptr ServletDispatch::getGlobServlet(const std::string &uri)
    {
        std::shared_lock lock(m_mutex);
        for (auto it = m_globs.begin();
             it != m_globs.end(); ++it)
        {
            if (it->first == uri)
            {
                return it->second->get();
            }
        }
        return nullptr;
    }

    Servlet::ptr ServletDispatch::getMatchedServlet(const std::string &uri)
    {
        std::string uri_v = removeTrailingSlash(uri);
        std::shared_lock lock(m_mutex);
        auto mit = m_datas.find(uri_v);
        if (mit != m_datas.end())
        {
            return mit->second->get();
        }
        for (auto it = m_globs.begin();
             it != m_globs.end(); ++it)
        {
            if (!fnmatch(it->first.c_str(), uri.c_str(), 0))
            {
                return it->second->get();
            }
        }
        return m_default;
    }

    void ServletDispatch::listAllServletCreator(std::map<std::string, IServletCreator::ptr> &infos)
    {
        std::shared_lock lock(m_mutex);
        for (auto &i : m_datas)
        {
            infos[i.first] = i.second;
        }
    }

    void ServletDispatch::listAllGlobServletCreator(std::map<std::string, IServletCreator::ptr> &infos)
    {
        std::shared_lock lock(m_mutex);
        for (auto &i : m_globs)
        {
            infos[i.first] = i.second;
        }
    }

    std::string ServletDispatch::removeTrailingSlash(const std::string &uri)
    {
        if (uri.empty())
        {
            return uri;
        }

        // 如果末尾是 '/'，则去除
        if (uri.back() == '/')
        {
            return uri.substr(0, uri.size() - 1);
        }

        return uri;
    }

    NotFoundServlet::NotFoundServlet(const std::string &name)
        : Servlet("NotFoundServlet"), m_name(name)
    {
        m_content = "<html><head><title>404 Not Found"
                    "</title></head><body><center><h1>404 Not Found</h1></center>"
                    "<hr><center>" +
                    name + "</center></body></html>";
    }

    int32_t NotFoundServlet::handle(net::http::HttpRequest::ptr request, net::http::HttpResponse::ptr response)
    {
        response->setStatus(net::http::HttpStatus::NOT_FOUND);
        response->setHeader("Server", "myhttp/1.0.0");
        response->setHeader("Content-Type", "text/html");
        response->setBody(m_content);
        return 0;
    }

}