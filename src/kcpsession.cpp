#include <string.h>

#include "kcpsession.h"
#include "kcpserver.h"

const int kcp_max_package_size = 4 * 1024; //4K
const int kcp_package_len_size = 4; //4B

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

KCPRingBuffer::KCPRingBuffer()
{
    Clear();
}

KCPRingBuffer::~KCPRingBuffer()
{
}

void KCPRingBuffer::Clear()
{
    read_pos_ = 0;
    write_pos_ = 0;
    is_full_ = false;
    is_empty_ = true;
}

int KCPRingBuffer::GetUsedSize() const
{
    if (is_empty_)
    {
        return 0;
    }
    else if (is_full_)
    {
        return BUFFER_SIZE;
    }

    if (write_pos_ > read_pos_)
    {
        return write_pos_ - read_pos_;
    }
    return BUFFER_SIZE - read_pos_ + write_pos_;
}

int KCPRingBuffer::GetFreeSize() const
{
    return BUFFER_SIZE - GetUsedSize();
}

int KCPRingBuffer::Write(const char* src, int len)
{
    if (len <= 0 || is_full_)
    {
        return 0;
    }

    is_empty_ = false;

    if (write_pos_ >= read_pos_)
    {
        int left_size = BUFFER_SIZE - write_pos_;
        if (left_size > len)
        {
            memcpy(buffer_ + write_pos_, src, len);
            write_pos_ += len;
            return len;
        }
        memcpy(buffer_ + write_pos_, src, left_size);
        write_pos_ = std::min(read_pos_, len - left_size);
        memcpy(buffer_, src + left_size, write_pos_);
        is_full_ = (read_pos_ == write_pos_);
        return left_size + write_pos_;
    }

    int can_write_size = std::min(GetFreeSize(), len);
    memcpy(buffer_ + write_pos_, src, can_write_size);
    write_pos_ += can_write_size;
    is_full_ = (read_pos_ == write_pos_);
    return can_write_size;
}

int KCPRingBuffer::Read(char* dst, int len)
{
    if (len <= 0 || is_empty_)
    {
        return 0;
    }

    is_full_ = false;

    if (read_pos_ >= write_pos_)
    {
        int left_size = BUFFER_SIZE - read_pos_;
        if (left_size > len)
        {
            memcpy(dst, buffer_ + read_pos_, len);
            read_pos_ += len;
            return len;
        }
        memcpy(dst, buffer_ + read_pos_, left_size);
        read_pos_ = std::min(write_pos_, len - left_size);
        memcpy(dst + left_size, buffer_, read_pos_);
        is_empty_ = (read_pos_ == write_pos_);
        return left_size + read_pos_;
    }

    int can_read_size = std::min(GetUsedSize(), len);
    memcpy(dst, buffer_ + read_pos_, can_read_size);
    read_pos_ += can_read_size;
    is_empty_ = (read_pos_ == write_pos_);
    return can_read_size;
}

bool KCPRingBuffer::ReadNoPop(char* dst, int len) const
{
    if (len <= 0 || GetUsedSize() < len)
    {
        return false;
    }

    if (read_pos_ >= write_pos_)
    {
        int left_size = BUFFER_SIZE - read_pos_;
        int first_copy_size = std::min(left_size, len);
        memcpy(dst, buffer_ + read_pos_, first_copy_size);
        if (first_copy_size < len)
        {
            memcpy(dst + first_copy_size, buffer_, len - first_copy_size);
        }
    }
    else
    {
        memcpy(dst, buffer_ + read_pos_, len);
    }

    return true;
}

int KCPRingBuffer::GetBufferSize() const
{
    return BUFFER_SIZE;
}

void KCPSession::Update(IUINT32 current)
{
    assert(NULL != kcp_);
    if (current >= ikcp_check(kcp_, current))
    {
        ikcp_update(kcp_, current);
    }

    static char buffer[kcp_max_package_size];

    do //revc kcp package 
    {
        int peek_size = ikcp_peeksize(kcp_);

        if (peek_size < 0) //no kcp package
        {
            break;
        }
        if (peek_size > kcp_max_package_size) //error£º kcp package too large
        {
            server_->DoErrorLog("kcp peek size(%d) too large", peek_size);
            break;
        }
        if (peek_size > recv_buffer_.GetFreeSize()) //buffer not enough
        {
            server_->DoErrorLog("revc buffer remain size(%d) not enough for peek size(%d)",
                recv_buffer_.GetFreeSize(), peek_size);
            break;
        }

        int len = ikcp_recv(kcp_, buffer, sizeof(buffer));
        if (len < 0) //error: kcp revc error
        {
            server_->DoErrorLog("kcp revc error");
            break;
        }

        assert(len = recv_buffer_.Write(buffer, len));
    } while (true);
    
    do
    {
        if (!recv_buffer_.ReadNoPop(buffer, 4))
        {
            break;
        }

        IUINT32 tmp_length = *((IUINT32*)(&buffer[0]));
        if (tmp_length == 0xffff) //KCP heart
        {
            assert(4 == recv_buffer_.Read(buffer, 4));
            //server_->DoErrorLog("Revc heart package");
            continue;
        }

        int package_len = (int)ntohl((u_long)tmp_length);
        if (package_len > kcp_max_package_size ||
            package_len > recv_buffer_.GetBufferSize())
        {
            //package len too large
            server_->DoErrorLog("package size(%d) too large", package_len);
            break;
        }
        if (package_len > recv_buffer_.GetUsedSize())
        {
            break;
        }

        assert(package_len == recv_buffer_.Read(buffer, package_len));
        server_->OnKCPRevc(kcp_->conv, buffer, package_len);
    } while (true);
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

    if (0 != memcmp(&addr_, &sockaddr, sizeof(sockaddr_in))) //endpoint switch address or port
    {
        server_->DoErrorLog("conv(%d) switch address(%s) port(%d) to address(%s) port(%d)",
            kcp_->conv, inet_ntoa(addr_.sockaddr.sin_addr), ntohs(addr_.sockaddr.sin_port),
            inet_ntoa(sockaddr.sin_addr), ntohs(sockaddr.sin_port));

        int conv = kcp_->conv;
        Clear();
        kcp_ = NewKCP(conv, this);
        addr_ = KCPAddr(sockaddr, socklen);
    }

    ikcp_input(kcp_, data, sz);
    last_active_time_ = current;
}

void KCPSession::Output(const char* buf, int len)
{
    server_->DoOutput(addr_, buf, len);
}

void KCPSession::Clear()
{
    if (NULL != kcp_)
    {
        ikcp_release(kcp_);
        kcp_ = NULL;
    }
    recv_buffer_.Clear();
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

