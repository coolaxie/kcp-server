#include <string.h>

#include "kcpsession.h"
#include "kcpserver.h"

int kcp_output(const char* buf, int len, ikcpcb* kcp, void* ptr)
{
    assert(NULL != ptr);
    KCPSession* session = static_cast<KCPSession*>(ptr);
    session->Output(buf, len);
    return 0;
}

ikcpcb* NewKCP(int conv, KCPSession* session)
{
    ikcpcb* kcp = ikcp_create(conv, (void*)session);
    assert(NULL != kcp);
    ikcp_setoutput(kcp, kcp_output);
    ikcp_nodelay(kcp, 1, 10, 2, 1);
    return kcp;
}

KCPSession* NewKCPSessison(KCPServer* server, const KCPAddr& addr, int conv, 
    IUINT64 current)
{
    KCPSession* session = new KCPSession(server, addr, current);
    ikcpcb* kcp = NewKCP(conv, session);
    session->SetKCP(kcp);
    return session;
}

void KCPSession::Update(IUINT32 current)
{
    assert(NULL != kcp_);
    if (current >= ikcp_check(kcp_, current))
    {
        ikcp_update(kcp_, current);
    }
    
    static char buf[4096];
    int len = ikcp_recv(kcp_, buf, sizeof(buf));
    if (len < 0)
    {
        return;
    }

    buf[len] = '\0';
    server_->OnKCPRevc(kcp_->conv, buf, len);
    //回射服务器
    Send(buf, len);
}

int KCPSession::Send(const char* data, int len)
{
    assert(NULL != kcp_);
    return ikcp_send(kcp_, data, len);
}

IUINT64 KCPSession::LastActiveTime() const
{
    return last_active_time_;
}

void KCPSession::SetKCP(ikcpcb* kcp)
{
    kcp_ = kcp;
}

void KCPSession::KCPInput(const sockaddr_in& sockaddr, const socklen_t socklen, const char* data, 
    long sz, IUINT64 current)
{
    assert(NULL != kcp_);
    assert(NULL != data);

    if (0 != memcmp(&addr_, &sockaddr, sizeof(sockaddr_in))) //对端切换了ip或端口
    {
        int conv = kcp_->conv;
        ikcp_release(kcp_);
        kcp_ = NewKCP(conv, this);
        addr_ = KCPAddr(sockaddr, socklen);
        printf("kcp endpoint switch address or port|conv:%d\n", conv);
    }

    ikcp_input(kcp_, data, sz);
    last_active_time_ = current;
}

void KCPSession::Output(const char* buf, int len)
{
    server_->DoOutput(addr_, buf, len);
}

KCPSession::KCPSession(KCPServer* server, const KCPAddr& addr, IUINT64 current) :
    server_(server), addr_(addr), last_active_time_(current)
{
}

KCPSession::~KCPSession()
{
    if (NULL != kcp_)
    {
        ikcp_release(kcp_);
    }
}

