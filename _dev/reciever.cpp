#include "zndNet.h"

namespace ZNDNet
{

void ZNDNet::Recv_PushInRaw(InRawPkt *inpkt)
{
    if (SDL_LockMutex(recvPktListMutex) == 0)
    {
        recvPktList.push_back(inpkt);
        SDL_UnlockMutex(recvPktListMutex);
    }
    else
    {
        printf("ERROR: PushInRaw() -> SDL_LockMutex()\n");
    }
}


InRawPkt *ZNDNet::Recv_PopInRaw()
{
    InRawPkt *ret = NULL;

    if (SDL_LockMutex(recvPktListMutex) == 0)
    {
        if (!recvPktList.empty())
        {
            ret = recvPktList.front();
            recvPktList.pop_front();
        }

        SDL_UnlockMutex(recvPktListMutex);
    }
    else
    {
        printf("ERROR: PopInRaw() -> SDL_LockMutex()\n");
    }

    return ret;
}


}
