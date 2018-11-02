#include <iostream>
#include "zndNet.h"

int main()
{
    ZNDNet::ZNDNet * serv = new ZNDNet::ZNDNet("Test\x00Server");
    serv->StartServer(61234);
    delete serv;
    return 0;
}
