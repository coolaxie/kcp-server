#include <stdio.h>
#include <unistd.h>

#include "kcpserver.h"

void on_kcp_revc(int conv, const char* data, int len)
{
    printf("[RECV] conv=%d data=%s\n", conv, data);
}

void on_session_kick(int conv)
{
    printf("conv:%d kicked\n", conv);
}

void isleep(unsigned long millisecond)
{
    usleep((millisecond << 10) - (millisecond << 4) - (millisecond << 3));
}



int main()
{
    KCPOptions options;
    options.recv_cb = on_kcp_revc;
    options.kick_cb = on_session_kick;
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