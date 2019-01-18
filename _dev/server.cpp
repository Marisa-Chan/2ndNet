#include "zndNet.h"
#include "zndNetPkt.h"
#include "memread.h"
#include "test/crc32.h"

namespace ZNDNet
{


Pkt *ZNDNet::Recv_ServerPreparePacket(InRawPkt *pkt)
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
        return new Pkt(pkt, from);
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

void ZNDNet::Srv_ProcessSystemPkt(Pkt* pkt)
{
    if (!pkt)
        return;

    if (pkt->datasz < 2 )
        return;

    if (pkt->user == NULL)
    {
        if (pkt->data[0] == SYS_MSG_HANDSHAKE && pkt->datasz >= PKT_HANDSHAKE_DATA + 2)
        {
            MemReader rd(pkt->data + 1, pkt->datasz - 1);
            uint8_t servstrSz = rd.readU8();
            uint8_t namestrSz = rd.readU8();

            printf("HANDSHAKE %d %d %d\n", servstrSz, namestrSz, (int)pkt->datasz);
                //for (int i = 0; i < pkt->hdr.datasz; i++)
                //    printf("%2.2x", pkt->hdr.data[i]);
                //putc('\n', stdout);

            if (namestrSz == 0 || servstrSz == 0 ||
               ((size_t)namestrSz + (size_t)servstrSz + PKT_HANDSHAKE_DATA) != pkt->datasz ||
                    servstrSz != servString.size())
            {
                return;
            }

            std::string srvnm;
            rd.readStr(srvnm, servstrSz);

            if (servString.compare(srvnm) != 0)
                return;

            std::string nm;
            rd.readStr(nm,  namestrSz);

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
                return;
            }

            NetUser *usr = &users[idx];
            usr->addr = pkt->addr;
            usr->lastMsgTime = ttime.GetTicks();
            usr->status = STATUS_CONNECTED;
            usr->ID = GenerateID();
            usr->name = nm;
            usr->latence = 0;

            ActivateUser(idx);

            Srv_SendConnected(usr);
        }
    }
    else
    {
        switch(pkt->data[0])
        {

            default:
                break;
        }
    }
}



int ZNDNet::_UpdateServerThread(void *data)
{
    ZNDNet *_this = (ZNDNet *)data;

    while (!_this->updateThreadEnd)
    {
        uint64_t forceBrake = _this->ttime.GetTicks() + 10;
        while (_this->ttime.GetTicks() < forceBrake)
        {
            InRawPkt *ipkt = _this->Recv_PopInRaw();
            if (!ipkt)
                break; // If no more packets -> do another things

            Pkt * pkt = _this->Recv_ServerPreparePacket(ipkt);
            if (pkt)
            {
                if (pkt->flags & PKT_FLAG_SYSTEM)
                {
                    _this->Srv_ProcessSystemPkt(pkt);
                    delete pkt;
                }
                else
                {

                }


            }
        }

        for(int i = 0 ; i < _this->_activeUsersNum; i++)
        {
            NetUser *usr = _this->_activeUsers[i];
            if (usr->latence < 10)
            {
                RefDataStatic *rfdata = RefDataStatic::create( (20 - usr->latence) * 10000 + 700 );

                SendingData *dta = new SendingData(usr->addr, _this->seq, rfdata, 0);
                dta->SetChannel(usr->__idx);

                SDL_LockMutex(_this->sendPktListMutex);
                _this->sendPktList.push_back(dta);
                SDL_UnlockMutex(_this->sendPktListMutex);
                printf("Sended SYNC %d %x\n", _this->seq, crc32(rfdata->get(), rfdata->size(), 0));

                usr->latence++;
                _this->seq ++;
                //SDL_Delay(0);
            }
        }

        SDL_Delay(0);
    }

    return 0;
}


void ZNDNet::StartServer(uint16_t port)
{
    mode = MODE_SERVER;
    sock = SDLNet_UDP_Open(port);

    recvThreadEnd = false;
    recvThread = SDL_CreateThread(_RecvThread, "", this);

    sendThreadEnd = false;
    sendThread = SDL_CreateThread(_SendThread, "", this);

    updateThreadEnd = false;
    updateThread = SDL_CreateThread(_UpdateServerThread, "", this);

}






void ZNDNet::Srv_SendConnected(const NetUser *usr)
{
    RefDataWStream *strm = RefDataWStream::create();

    strm->writeU8(SYS_MSG_CONNECTED);
    strm->writeU64(usr->ID);
    strm->writeSzStr(usr->name);

    Send_PushData( new SendingData(usr->addr, 0, strm, PKT_FLAG_SYSTEM) );
}

};
