#include <iostream>
#include "zndNet.h"
#include <unistd.h>


int main()
{
    SDL_Init(0);
    ZNDNet::ZNDNet * serv = new ZNDNet::ZNDNet("TestServer");


    IPaddress server;
    SDLNet_ResolveHost(&server, "127.0.0.1", 61234);

    ZNDNet::ZNDNet * client = new ZNDNet::ZNDNet("TestServer");

    serv->StartServer(61234);
    client->StartClient("zzzz", server);


    sleep(60);


    return 0;
}
