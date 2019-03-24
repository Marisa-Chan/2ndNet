#include <iostream>
#include "zndNet.h"
#include <unistd.h>

#if !defined(SEPARATE_TEST_CLIENT) && !defined(SEPARATE_TEST_SERVER)
int main()
{
    SDL_Init(0);
    ZNDNet::ZNDNet * serv = new ZNDNet::ZNDNet("TestServer");


    IPaddress server;
    SDLNet_ResolveHost(&server, "127.0.0.1", 61234);

    ZNDNet::ZNDNet * client = new ZNDNet::ZNDNet("TestServer");
    ZNDNet::ZNDNet * client2 = new ZNDNet::ZNDNet("TestServer");

    serv->StartServer(61234);
    client->StartClient("c1", server);
    client2->StartClient("c2", server);


    sleep(1);
    ZNDNet::SessionInfoVect sessions1;
    ZNDNet::SessionInfoVect sessions2;

    while ( (client->Cli_GetStatus() | client2->Cli_GetStatus() ) & ZNDNet::STATUS_OFFLINE_MASK )
    {
        ;
    }

    while ( !client->Cli_GetSessions(sessions1) )
    {
        ;
    }

    printf("cli 1 sessions: %d\n", (int)sessions1.size());

    client2->Cli_CreateSession("My sess", "", 0);

    while ( sessions1.size() == 0 )
    {
        client->Cli_GetSessions(sessions1);
    }

    for(int i = 0; i < (int)sessions1.size(); i++)
    {
        printf("cli 1 sessions: %s %d\t %x\n", sessions1[i].name.c_str(), sessions1[i].players, (int)sessions1[i].ID);
    }

    client->Cli_JoinSession( sessions1[0].ID );



    sleep(60);


    return 0;
}
#elif defined(SEPARATE_TEST_CLIENT)

int main(int argc, const char *argv[])
{
    SDL_Init(0);
    IPaddress server;
    SDLNet_ResolveHost(&server, argv[1], 61234);

    ZNDNet::ZNDNet * client = new ZNDNet::ZNDNet("TestServer");

    client->StartClient(argv[2], server);

    ZNDNet::SessionInfoVect sessions1;

    while ( (client->Cli_GetStatus() ) & ZNDNet::STATUS_OFFLINE_MASK )
    {
        ;
    }

    while ( !client->Cli_GetSessions(sessions1) )
    {
        ;
    }

    if (argc == 4)
    {
        client->Cli_CreateSession(argv[3], "", 0);
    }
    else if (sessions1.size() == 0)
    {
        client->Cli_CreateSession("My sess", "", 0);
    }
    else
    {
        for(int i = 0; i < (int)sessions1.size(); i++)
        {
            printf("sessions: %s \t%d \t %x\n", sessions1[i].name.c_str(), sessions1[i].players, (int)sessions1[i].ID);
        }

        client->Cli_JoinSession( sessions1[0].ID );
    }

    char cmdbuf[1024];
    while(true)
    {
        scanf("%s", cmdbuf);
        printf("%s\n", cmdbuf);
    }

    sleep(60);


    return 0;
}

#elif defined(SEPARATE_TEST_SERVER)
int main()
{
    SDL_Init(0);
    ZNDNet::ZNDNet * serv = new ZNDNet::ZNDNet("TestServer");

    serv->StartServer(61234);
    sleep(600);


    return 0;
}
#endif // SEPARATE_TEST

