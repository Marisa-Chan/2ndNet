#include <iostream>
#include "zndNet.h"

#include <unistd.h>
#include "Kbhit.h"
#include "test/crc32.h"


#if !defined(SEPARATE_TEST_CLIENT) && !defined(SEPARATE_TEST_SERVER) && !defined(SEPARATE_TEST_SINGLE)
int main()
{
    SDL_Init(0);
    ZNDNet::ZNDServer * serv = new ZNDNet::ZNDServer("TestServer");


    IPaddress server;
    SDLNet_ResolveHost(&server, "127.0.0.1", 61234);

    ZNDNet::ZNDClient * client = new ZNDNet::ZNDClient("TestServer");
    ZNDNet::ZNDClient * client2 = new ZNDNet::ZNDClient("TestServer");

    serv->Start(61234);
    //sleep(1);
    client->Start("c1", server);
    client2->Start("c2", server);


    sleep(1);
    ZNDNet::SessionInfoVect sessions1;
    ZNDNet::SessionInfoVect sessions2;

    ZNDNet::Event * tmp = client->Events_WaitForMsg(ZNDNet::EVENT_CONNECTED);
    if (tmp)
        delete tmp;

    tmp = client2->Events_WaitForMsg(ZNDNet::EVENT_CONNECTED);
    if (tmp)
        delete tmp;


    client->RequestSessions();

    tmp = client->Events_WaitForMsg(ZNDNet::EVENT_SESSION_LIST);
    if (tmp)
        delete tmp;

    client->GetSessions(sessions1);

    printf("cli 1 sessions: %d\n", (int)sessions1.size());

    client2->CreateSession("My sess", "", 0);


    while (sessions1.size() == 0)
    {
        client->RequestSessions();

        tmp = client->Events_WaitForMsg(ZNDNet::EVENT_SESSION_LIST);
        if (tmp)
            delete tmp;

        client->GetSessions(sessions1);
    }

    for(int i = 0; i < (int)sessions1.size(); i++)
    {
        printf("cli 1 sessions: %s %d\t %x\n", sessions1[i].name.c_str(), sessions1[i].players, (int)sessions1[i].ID);
    }

    client->JoinSession( sessions1[0].ID, "" );

    int cnt = 60;

    while(cnt > 0)
    {
        for(ZNDNet::Event *evt = client->Events_Pop(); evt != NULL; evt = client->Events_Pop())
        {
            delete evt;
        }

        for(ZNDNet::Event *evt = client2->Events_Pop(); evt != NULL; evt = client->Events_Pop())
        {
            delete evt;
        }

        cnt--;
        sleep(1);
    }

    return 0;
}
#elif defined(SEPARATE_TEST_CLIENT)

int main(int argc, const char *argv[])
{

    SDL_Init(0);
    IPaddress server;
    SDLNet_ResolveHost(&server, argv[1], 61234);

    ZNDNet::ZNDClient * client = new ZNDNet::ZNDClient("TestServer");

    client->Start(argv[2], server);

    ZNDNet::SessionInfoVect sessions1;

    bool run = true;

    char cmdbuf[1024];
    int32_t ps = 0;
    bool ses1 = true;
    while(run)
    {
        for(ZNDNet::Event *evt = client->Events_Pop(); evt != NULL; evt = client->Events_Pop())
        {
            switch( evt->type )
            {
                case ZNDNet::EVENT_CONNECTED:
                    {
                        ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;

                        printf("Connected as %s ID(%x) lobby(%d)\n", dat->name.c_str(), (uint32_t)dat->id, dat->value );
                        client->RequestSessions();
                    }
                    break;

                case ZNDNet::EVENT_CONNERR:
                    {
                        printf("Err connecting %d\n", evt->value);
                        client->Stop();
                        run = false;
                    }
                    break;

                case ZNDNet::EVENT_DISCONNECT:
                    {
                        printf("Err disconnect %d\n", evt->value);
                        client->Stop();
                        run = false;
                    }
                    break;

                case ZNDNet::EVENT_SESSION_LIST:
                    client->GetSessions(sessions1);

                    for(int i = 0; i < (int)sessions1.size(); i++)
                    {
                        printf("sessions: %s \t%d \t %x\n", sessions1[i].name.c_str(), sessions1[i].players, (int)sessions1[i].ID);
                    }

                    if (ses1)
                    {
                        if (argc == 4)
                        {
                            client->CreateSession(argv[3], "", 2);
                        }
                        else if (sessions1.size() == 0)
                        {
                            client->CreateSession("My sess", "", 0);
                        }
                        else
                        {
                            client->JoinSession( sessions1[0].ID, "" );
                        }

                        ses1 = false;
                    }
                    break;

                case ZNDNet::EVENT_SESSION_JOIN:
                    {
                        ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;
                        printf("Joined to %s (%x)\n", dat->name.c_str(), (uint32_t)dat->id );
                    }
                    break;

                case ZNDNet::EVENT_DATA:
                    {
                        ZNDNet::EventData *dat = (ZNDNet::EventData *)evt;
                        printf("Recieve data sz %d from %x, crc %x\n", dat->size, (uint32_t)dat->from, crc32(dat->data, dat->size, 0) );

                    }
                    break;

                case ZNDNet::EVENT_USER_ADD:
                    {
                        ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;
                        printf("User join %s (%" PRIx64 ")\n", dat->name.c_str(), dat->id );
                    }
                    break;

                case ZNDNet::EVENT_USER_LEAVE:
                    {
                        ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;
                        printf("User leave %s (%x)\n", dat->name.c_str(), (uint32_t)dat->id );
                    }
                    break;

                case ZNDNet::EVENT_USER_LIST:
                    {
                        printf("User list\n");
                    }
                    break;

                default:
                    printf("Msg %d %d\n", evt->type, evt->value);
                    break;
            }
            delete evt;
        }

        if (kbhit())
        {
            int c = getch();

            if (c != 0)
            {
                if (c == '\n')
                {
                    printf("ENTER\n");
                    cmdbuf[ps] = 0;

                    if (strncasecmp(cmdbuf, "users", 5) == 0)
                    {
                        ZNDNet::UserInfoVect usrs;
                        client->GetUsers(usrs);

                        for(int i = 0; i < usrs.size(); i++)
                        {
                            printf("\tUser list: %s\t\t%" PRIx64 "\n", usrs[i].name.c_str(), usrs[i].ID);
                        }
                    }
                    else if (strncasecmp(cmdbuf, "sessions", 8) == 0)
                    {
                        client->RequestSessions();
                    }
                    else if (strncasecmp(cmdbuf, "show", 4) == 0)
                    {
                        client->ShowSession(true);
                    }
                    else if (strncasecmp(cmdbuf, "hide", 4) == 0)
                    {
                        client->ShowSession(false);
                    }
                    else if (strncasecmp(cmdbuf, "close", 5) == 0)
                    {
                        std::string str = cmdbuf + 5;
                        client->CloseSession(atoi(str.c_str()));
                    }
                    else if (strncasecmp(cmdbuf, "leave", 5) == 0)
                    {
                        client->LeaveSession();
                    }
                    else if (strncasecmp(cmdbuf, "kick", 4) == 0)
                    {
                        std::string str = cmdbuf + 5;
                        while (str.back() == '\r' || str.back() == '\n')
                            str.pop_back();

                        ZNDNet::UserInfo inf;

                        if (client->GetUser(inf, str.c_str()))
                        {
                            client->KickUser(inf.ID);
                        }
                    }
                    else if (strncasecmp(cmdbuf, "create", 6) == 0)
                    {
                        std::string str = cmdbuf + 7;
                        while (str.back() == '\r' || str.back() == '\n')
                            str.pop_back();

                        client->CreateSession(str.c_str(), "", 0);
                    }
                    else if (strncasecmp(cmdbuf, "join", 4) == 0)
                    {
                        std::string str = cmdbuf + 5;
                        while (str.back() == '\r' || str.back() == '\n')
                            str.pop_back();

                        for(int i = 0; i < (int)sessions1.size(); i++)
                        {
                            if (strcasecmp(sessions1[i].name.c_str(), str.c_str()) == 0)
                            {
                                client->JoinSession(sessions1[i].ID, "");
                                break;
                            }
                        }
                    }
                    else if (strncasecmp(cmdbuf, "quit", 4) == 0)
                    {
                        run = false;
                    }
                    else if (strncasecmp(cmdbuf, "cast", 4) == 0)
                    {
                        uint8_t *tmp = new uint8_t[10240];
                        for (int32_t i = 0; i < 10240; i++)
                            tmp[i] = rand() % 0xFF;

                        client->BroadcastData(tmp, 10240);
                        printf("Sended data %x\n", crc32(tmp, 10240, 0) );

                        delete[] tmp;
                    }
                    else if (strncasecmp(cmdbuf, "uni", 3) == 0)
                    {
                        std::string str = cmdbuf + 4;
                        while (str.back() == '\r' || str.back() == '\n')
                            str.pop_back();

                        ZNDNet::UserInfoVect usrs;
                        client->GetUsers(usrs);

                        for(int i = 0; i < usrs.size(); i++)
                        {
                            if ( strcasecmp( usrs[i].name.c_str(), str.c_str()) == 0 )
                            {
                                uint8_t *tmp = new uint8_t[1024];
                                for (int32_t i = 0; i < 1024; i++)
                                    tmp[i] = rand() % 0xFF;

                                client->SendData(usrs[i].ID, tmp, 1024);
                                printf("Sended data to %s crc %x\n", usrs[i].name.c_str(), crc32(tmp, 1024, 0) );

                                delete[] tmp;
                                break;
                            }
                        }
                    }

                    ps = 0;
                }
                else
                {
                    cmdbuf[ps++] = c;
                }
            }
        }


        SDL_Delay(10);

    }

    client->SendDisconnect();
    client->Stop();

    sleep(4);


    return 0;
}

#elif defined(SEPARATE_TEST_SERVER)
int main()
{
    SDL_Init(0);
    ZNDNet::ZNDServer * serv = new ZNDNet::ZNDServer("TestServer");

    serv->Start(61234);
    sleep(600);


    return 0;
}
#elif defined(SEPARATE_TEST_SINGLE)
int main()
{
    SDL_Init(0);
    ZNDNet::ZNDSingle * serv = new ZNDNet::ZNDSingle("TestServer");

    serv->Start("JOJO", 61234, "My mega Server", 0);
    bool run = true;

    int32_t ps = 0;
    char cmdbuf[1024];

    while(run)
    {
        for(ZNDNet::Event *evt = serv->Events_Pop(); evt != NULL; evt = serv->Events_Pop())
        {
            switch( evt->type )
            {
                case ZNDNet::EVENT_DATA:
                    {
                        ZNDNet::EventData *dat = (ZNDNet::EventData *)evt;
                        printf("Recieve data sz %d from %x, crc %x\n", dat->size, (uint32_t)dat->from, crc32(dat->data, dat->size, 0) );

                    }
                    break;

                case ZNDNet::EVENT_USER_ADD:
                    {
                        ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;
                        printf("User join %s (%" PRIx64 ")\n", dat->name.c_str(), dat->id );
                    }
                    break;

                case ZNDNet::EVENT_USER_LEAVE:
                    {
                        ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;
                        printf("User leave %s (%x)\n", dat->name.c_str(), (uint32_t)dat->id );
                    }
                    break;

                case ZNDNet::EVENT_USER_LIST:
                    {
                        printf("User list\n");
                    }
                    break;

                case ZNDNet::EVENT_DISCONNECT:
                    {
                        printf("Disconnect %d\n", evt->value);
                        serv->Stop();
                        run = false;
                    }
                    break;

                default:
                    printf("Msg %d %d\n", evt->type, evt->value);
                    break;
            }
            delete evt;
        }

        if (kbhit())
        {
            int c = getch();

            if (c != 0)
            {
                if (c == '\n')
                {
                    printf("ENTER\n");
                    cmdbuf[ps] = 0;

                    if (strncasecmp(cmdbuf, "users", 5) == 0)
                    {
                        ZNDNet::UserInfoVect usrs;
                        serv->GetUsers(usrs);

                        for(int i = 0; i < usrs.size(); i++)
                        {
                            printf("\tUser list: %s\t\t%" PRIx64 "\n", usrs[i].name.c_str(), usrs[i].ID);
                        }
                    }
                    else if (strncasecmp(cmdbuf, "show", 4) == 0)
                    {
                        serv->ShowSession(true);
                    }
                    else if (strncasecmp(cmdbuf, "hide", 4) == 0)
                    {
                        serv->ShowSession(false);
                    }
                    else if (strncasecmp(cmdbuf, "close", 5) == 0)
                    {
                        std::string str = cmdbuf + 5;
                        serv->CloseSession(atoi(str.c_str()));
                    }
                    else if (strncasecmp(cmdbuf, "kick", 4) == 0)
                    {
                        std::string str = cmdbuf + 5;
                        while (str.back() == '\r' || str.back() == '\n')
                            str.pop_back();

                        ZNDNet::UserInfo inf;

                        if (serv->GetUser(inf, str.c_str()))
                        {
                            serv->KickUser(inf.ID);
                        }
                    }
                    else if (strncasecmp(cmdbuf, "quit", 4) == 0)
                    {
                        run = false;
                    }
                    else if (strncasecmp(cmdbuf, "cast", 4) == 0)
                    {
                        uint8_t *tmp = new uint8_t[10240];
                        for (int32_t i = 0; i < 10240; i++)
                            tmp[i] = rand() % 0xFF;

                        serv->BroadcastData(tmp, 10240);
                        printf("Sended data %x\n", crc32(tmp, 10240, 0) );

                        delete[] tmp;
                    }
                    else if (strncasecmp(cmdbuf, "uni", 3) == 0)
                    {
                        std::string str = cmdbuf + 4;
                        while (str.back() == '\r' || str.back() == '\n')
                            str.pop_back();

                        ZNDNet::UserInfoVect usrs;
                        serv->GetUsers(usrs);

                        for(int i = 0; i < usrs.size(); i++)
                        {
                            if ( strcasecmp( usrs[i].name.c_str(), str.c_str()) == 0 )
                            {
                                uint8_t *tmp = new uint8_t[1024];
                                for (int32_t i = 0; i < 1024; i++)
                                    tmp[i] = rand() % 0xFF;

                                serv->SendData(usrs[i].ID, tmp, 1024);
                                printf("Sended data to %s crc %x\n", usrs[i].name.c_str(), crc32(tmp, 1024, 0) );

                                delete[] tmp;
                                break;
                            }
                        }
                    }

                    ps = 0;
                }
                else
                {
                    cmdbuf[ps++] = c;
                }
            }
        }


        SDL_Delay(10);
    }
    //sleep(600);


    return 0;
}
#endif // SEPARATE_TEST

