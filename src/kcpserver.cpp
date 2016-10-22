#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>

#include "kcpserver.h"

const IUINT32 KCP_HEAD_LENGTH = 24;

KCPOptions::KCPOptions()
{
    port = 9527;
    keep_session_time = 5 * 1000; //5s //5000ms
    recv_cb = NULL;
    kick_cb = NULL;
    error_reporter = NULL;
}

KCPServer::KCPServer(const KCPOptions& options) :
    options_(options), fd_(0), current_clock_(0)
{
}

KCPServer::~KCPServer()
{
    Clear();
}

bool KCPServer::Start()
{
    bool ret = false;
    do 
    {
        if (!UDPBind())
        {
            break;
        }

        ret = true;
    } while (false);

    if (!ret)
    {
        Clear();
    }

    return ret;
}

void KCPServer::Update()
{
    current_clock_ = iclock();
    UDPRead();
    SessionUpdate();
}

bool KCPServer::Send(int conv, const char* data, int len)
{
    KCPSession* session = GetSession(conv);
    if (NULL == session)
    {
        DoErrorLog("no session(%d) find", conv);
        return false;
    }

    if (session->Send(data, len) < 0)
    {
        DoErrorLog("session(%d) send data failed", conv);
        return false;
    }

    return true;
}

void KCPServer::KickSession(int conv)
{
    auto it = sessions_.find(conv);
    if (it == sessions_.end())
    {
        return;
    }

    delete it->second;
    sessions_.erase(it);
}

bool KCPServer::SessionExist(int conv) const
{
    return sessions_.find(conv) != sessions_.end();
}

bool KCPServer::UDPBind()
{
    sockaddr_in server_addr;
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0)
    {
        DoErrorLog("call socket error:%s", strerror(errno));
        return false;
    }

    int flag = fcntl(fd_, F_GETFL, 0);
    flag |= O_NONBLOCK;
    if (-1 == fcntl(fd_, F_SETFL, flag))
    {
        DoErrorLog("set socket non block error:%s", strerror(errno));
        return false;
    }

    int opt = 1;
    if (0 != setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        DoErrorLog("set socket reuse addr error:%s", strerror(errno));
        return false;
    }

    int val = 10 * 1024 * 1024; //10M
    if (0 != setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)))
    {
        DoErrorLog("set socket recv buf error:%s", strerror(errno));
        return false;
    }

    if (0 != setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val)))
    {
        DoErrorLog("set socket send buf error:%s", strerror(errno));
        return false;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(options_.port);
    if (0 != bind(fd_, (const sockaddr*)&server_addr, sizeof(server_addr)))
    {
        DoErrorLog("call bind error:%s", strerror(errno));
        return false;
    }

    return true;
}


void KCPServer::Clear()
{
    fd_ = 0;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it)
    {
        delete it->second;
    }
    sessions_.clear();
}

KCPSession* KCPServer::GetSession(int conv)
{
    auto it = sessions_.find(conv);
    if (it != sessions_.end())
    {
        return it->second;
    }
    return NULL;
}

void KCPServer::DoOutput(const KCPAddr& addr, const char* data, int len)
{
    assert(fd_ > 0);
    if (-1 == sendto(fd_, data, len, 0, (sockaddr*)&addr.sockaddr, addr.sock_len))
    {
        DoErrorLog("udp send data size(%d) to address(%s) port(%d) error:%s",
            len, inet_ntoa(addr.sockaddr.sin_addr), ntohs(addr.sockaddr.sin_port),
            strerror(errno));
        return;
    }
}

void KCPServer::UDPRead()
{
    assert(fd_ > 0);

    static char buf[64 * 1024];
    do
    {
        sockaddr_in cliaddr;
        socklen_t len = sizeof(cliaddr);
        memset(&cliaddr, 0, sizeof(cliaddr));
        ssize_t n = recvfrom(fd_, buf, sizeof(buf), 0, (sockaddr*)&cliaddr, &len);
        if (n < 0) 
        {
            if (EAGAIN != errno) //system call error
            {
                DoErrorLog("call recv from error:%s", strerror(errno));
            }
            break;
        }

        if (n < KCP_HEAD_LENGTH)
        {
            DoErrorLog("kcp package len(%d) invalid", n);
            break;
        }

        int conv = ikcp_getconv(buf);
        KCPSession* session = GetSession(conv);
        if (NULL == session)
        {
            session = NewKCPSessison(this, KCPAddr(cliaddr, len), conv, current_clock_);
            sessions_[conv] = session;
        }
        assert(NULL != session);
        session->KCPInput(cliaddr, len, buf, n, current_clock_);
    } while (true);
}

void KCPServer::SessionUpdate()
{
    IUINT32 current = current_clock_ & 0xfffffffflu;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it)
    {
        KCPSession* session = it->second;

        if (options_.keep_session_time > 0 && 
            current_clock_ > session->LastActiveTime() + options_.keep_session_time)
        {
            DoErrorLog("conv(%d) timeout, kick it", it->first);
            if (NULL != options_.kick_cb)
            {
                options_.kick_cb(it->first);
            }
            delete session;
            sessions_.erase(it);
            continue;
        }
        session->Update(current);
    }
}

void KCPServer::OnKCPRevc(int conv, const char* data, int len)
{
    if (NULL != options_.recv_cb)
    {
        options_.recv_cb(conv, data, len);
    }
}

void KCPServer::DoErrorLog(const char *fmt, ...)
{
    if (NULL == options_.error_reporter)
    {
        return;
    }

    static char buffer[1024];
    va_list argptr;
    va_start(argptr, fmt);
    vsprintf(buffer, fmt, argptr);
    va_end(argptr);
    options_.error_reporter(buffer);
}

