#include "zndNet.h"
#include "zndNetPkt.h"

namespace ZNDNet
{

int ZNDNet::_RecvThread(void *data)
{
    ZNDNet *_this = (ZNDNet *)data;

    UDPpacket *inpkt[ZNDNET_TUNE_RECV_PKTS + 1];

    if (_this)
    {
        for (int i = 0; i < ZNDNET_TUNE_RECV_PKTS; i++)
        {
            inpkt[i] = new UDPpacket;
            inpkt[i]->data = new uint8_t[ZNDNET_BUFF_SIZE];
            inpkt[i]->maxlen = ZNDNET_BUFF_SIZE;
            inpkt[i]->channel = -1;
        }

        inpkt[ZNDNET_TUNE_RECV_PKTS] = NULL;

        while (!_this->threadsEnd)
        {
            int numrecv = SDLNet_UDP_RecvV(_this->sock, inpkt);

            if (numrecv > 0)
            {
                if (_this->mode == MODE_SERVER)
                {
                    for(int i = 0; i < numrecv; i++)
                    {
                        InRawPkt *pkt = new InRawPkt(inpkt[i]);
                        _this->Recv_PushInRaw(pkt);
                    }
                }
                else if (_this->mode == MODE_CLIENT)
                {
                    for(int i = 0; i < numrecv; i++)
                    {
                        //InRawPkt *pkt = new InRawPkt(inpkt[i]);
                        //_this->Recv_PushInRaw(pkt);
                        if ( IPCMP(inpkt[i]->address, _this->cServAddress) )
                        {
                            InRawPkt *pkt = new InRawPkt(inpkt[i]);
                            _this->Recv_PushInRaw(pkt);
                        }
                    }
                }
            }

            SDL_Delay(0);
        }

        for (int i = 0; i < ZNDNET_TUNE_RECV_PKTS; i++)
        {
            delete[] inpkt[i]->data;
            delete inpkt[i];
        }
    }

    return 0;
}


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
