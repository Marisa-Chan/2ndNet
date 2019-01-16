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


        //recvBuffer = new uint8_t[ZNDNET_BUFF_SIZE];

        //inpkt.data = recvBuffer;
        //inpkt.maxlen = ZNDNET_BUFF_SIZE;
        //inpkt.channel = -1;

        while (!_this->recvThreadEnd)
        {
            //inpkt.maxlen = ZNDNET_BUFF_SIZE;
            //UDPpacket *inpkt;
            int numrecv = SDLNet_UDP_RecvV(_this->sock, inpkt);

            //if ( SDLNet_UDP_Recv(_this->sock, &inpkt) == 1 )
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
            delete[] inpkt[i];
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


Pkt *ZNDNet::Recv_PreparePacket(InRawPkt *pkt)
{
    if (!pkt->Parse())
    {
        // Incorrect packet
        delete pkt;
        return NULL;
    }

    NetUser *from = FindUserByIP(pkt->addr);

    if (pkt->hdr.flags & PKT_FLAG_SYSTEM) // System message
    {
        if (from == NULL) // Unknown user
        {
            if (pkt->hdr.data[0] == SYS_MSG_HANDSHAKE && pkt->hdr.datasz >= PKT_HANDSHAKE_DATA + 2) // It's New user?
            {
                uint8_t servstrSz = pkt->hdr.data[PKT_HANDSHAKE_SERV_SZ];
                uint8_t namestrSz = pkt->hdr.data[PKT_HANDSHAKE_NAME_SZ];

                printf("HANDSHAKE %d %d %d\n", servstrSz, namestrSz, (int)pkt->hdr.datasz);
                //for (int i = 0; i < pkt->hdr.datasz; i++)
                //    printf("%2.2x", pkt->hdr.data[i]);
                //putc('\n', stdout);

                if (namestrSz == 0 || servstrSz == 0 ||
                    ((size_t)namestrSz + (size_t)servstrSz + PKT_HANDSHAKE_DATA) != pkt->hdr.datasz ||
                    servstrSz != servString.size())
                {
                    delete pkt;
                    return NULL;
                }

                uint8_t *pdata = pkt->hdr.data + PKT_HANDSHAKE_DATA;

                if ( memcmp(pdata, servString.c_str(), servstrSz) != 0 )
                {
                    delete pkt;
                    return NULL;
                }

                pdata += servstrSz;

                std::string nm;

                nm.assign( (char *)pdata,  namestrSz );

                if (nm.size() == 0)
                {
                    delete pkt;
                    return NULL;
                }

                CorrectName(nm);

                if (FindUserByName(nm))
                {
                    if (nm.size() > ZNDNET_USER_NAME_MAX - 5)
                        nm.resize( ZNDNET_USER_NAME_MAX - 5 );

                    nm += '.';

                    char bf[10];
                    sprintf(bf, "%04d", (int)(ttime.GetTicks() % 10000));
                    nm += bf;
                }

                int32_t idx = FindFreeUser();

                if (idx == -1) // No free space
                {
                    SendErrFull(pkt->addr);
                    delete pkt;
                    return NULL;
                }

                from = &users[idx];
                from->addr = pkt->addr;
                from->lastMsgTime = ttime.GetTicks();
                from->status = STATUS_CONNECTED;
                from->ID = GenerateID();
                from->name = nm;
                from->latence = 0;

                ActivateUser(idx);

                SendConnected(from);

                delete pkt;
                return NULL;
            }
            else // Incorrect msg from unknown
            {
                delete pkt;
                return NULL;
            }
        }
        else // Known user
        {
            return new Pkt(pkt, from);
        }
    }
    else if (from) // Not system msg and known user
    {
        if (pkt->hdr.flags & PKT_FLAG_PART) // incomplete data, do assembly
        {
            AddrSeq ipseq;
            ipseq.set(pkt->addr, pkt->hdr.seqid);

            InPartedPkt *parted = NULL;

            for (PartedList::iterator it = pendingPkt.begin(); it != pendingPkt.end(); it++)
            {
                if ((*it)->ipseq == ipseq)
                {
                    parted = *it;
                    break;
                }
            }

            if (!parted)
            {
                parted = new InPartedPkt(ipseq, pkt->hdr.fsize, pkt->hdr.flags);
                pendingPkt.push_back(parted);
            }

            if ( parted->Feed(pkt, ttime.GetTicks()) ) // Is complete?
            {
                pendingPkt.remove(parted);
                return new Pkt(parted, from);
            }

            // if not complete -> return NULL
        }
        else // not parted packet -> make it
        {
            return new Pkt(pkt, from);
        }
    }
    else // Not system and unknown user -> delete packet
    {
        delete pkt;
    }

    return NULL;
}


Pkt *ZNDNet::Recv_ClientPreparePacket(InRawPkt *pkt)
{
    if (!pkt->Parse())
    {
        // Incorrect packet
        delete pkt;
        return NULL;
    }


    if (pkt->hdr.flags & PKT_FLAG_SYSTEM) // System message
    {
        return new Pkt(pkt, NULL);
    }
    else
    {
        //printf("Client: Normal parse seq %d len %d\n", pkt->hdr.seqid, (int)pkt->hdr.datasz);

        if (pkt->hdr.flags & PKT_FLAG_PART) // incomplete data, do assembly
        {
            AddrSeq ipseq;
            ipseq.set(pkt->addr, pkt->hdr.seqid);

            InPartedPkt *parted = NULL;

            for (PartedList::iterator it = pendingPkt.begin(); it != pendingPkt.end(); it++)
            {
                if ((*it)->ipseq == ipseq)
                {
                    parted = *it;
                    break;
                }
            }

            if (!parted)
            {
                parted = new InPartedPkt(ipseq, pkt->hdr.fsize, pkt->hdr.flags);
                pendingPkt.push_back(parted);
            }

            //if (pkt->hdr.seqid == 0)
            //    printf("Parted recv %d/%d\n", parted->nextOff, parted->len);

            if ( parted->Feed(pkt, ttime.GetTicks()) ) // Is complete?
            {
                pendingPkt.remove(parted);
                return new Pkt(parted, NULL);
            }

            // if not complete -> return NULL
        }
        else // not parted packet -> make it
        {
            return new Pkt(pkt, NULL);
        }
    }

    return NULL;
}

}
