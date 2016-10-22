#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "kcpserver.h"

void on_kcp_revc(int conv, const char* data, int len)
{
    assert(len >= 4);
    char buffer[1024];
    memcpy(buffer, data, len);
    buffer[len] = 0;
    printf("[RECV] conv=%d data=%s len(%d)\n", conv, &buffer[4], len - 4);
}

void on_session_kick(int conv)
{
    printf("conv:%d kicked\n", conv);
}

void on_error_report(const char* data)
{
    printf("kcp error:%s\n", data);
}

void isleep(unsigned long millisecond)
{
    usleep((millisecond << 10) - (millisecond << 4) - (millisecond << 3));
}

void test_ring_buffer()
{
    const char* s = "0123456789";
    char buf[15];
    int loop = 100;
    KCPRingBuffer q;
    do
    {
        assert(q.GetUsedSize() == 0);
        for (int i = 0; i < 5; i++)
        {
            assert(9 == q.Write(s, 9));
        }
        //assert(q.GetUsedSize() == KCPRingBuffer::BUFFER_SIZE);
        for (int i = 0; i < 5; i++)
        {
            assert(q.Read(buf, 9) == 9);
            buf[9] = 0;
            printf("read from q:%s\n", buf);
            assert(memcmp(s, buf, 9) == 0);
        }
        assert(q.GetUsedSize() == 0);
    } while (loop--);
}

int main()
{
    //test_ring_buffer();

    
    KCPOptions options;
    options.recv_cb = on_kcp_revc;
    options.kick_cb = on_session_kick;
    options.error_reporter = on_error_report;
    options.port = 9528;
    KCPServer* server = new KCPServer(options);
    if (!server->Start())
    {
        printf("server start error");
        exit(0);
    }

    printf("kcp server start...\n");

    while (true)
    {
        isleep(1);
        server->Update();
    }
    

    return 0;
}