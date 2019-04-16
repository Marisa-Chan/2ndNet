#include "zndNet.h"
#include "zndNetPkt.h"
#include "memread.h"
#include "test/crc32.h"
#include "errcode.h"

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

    NetUser *from = Srv_FindUserByIP(pkt->addr);

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
                parted = new InPartedPkt(ipseq, pkt->hdr.fsize, pkt->hdr.flags, pkt->hdr.uchnl);
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

    MemReader rd(pkt->data + 1, pkt->datasz - 1);

    if (pkt->user == NULL)
    {
        if (pkt->data[0] == SYS_MSG_HANDSHAKE && pkt->datasz >= PKT_HANDSHAKE_DATA + 2)
        {
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

            if (Srv_FindUserByName(nm))
            {
                if (nm.size() > ZNDNET_USER_NAME_MAX - 5)
                    nm.resize( ZNDNET_USER_NAME_MAX - 5 );

                nm += '.';
                std::string base = nm;

                int cnt = 0;
                do
                {
                    char bf[10];
                    sprintf(bf, "%04d", (int)(ttime.GetTicks() % 10000));
                    nm = base;
                    nm += bf;
                    cnt ++;

                    if (cnt == 100)
                    {
                        SendErrFull(pkt->addr);
                        return;
                    }
                }
                while(Srv_FindUserByName(nm));
            }

            NetUser *usr = Srv_AllocUser();

            if (usr == NULL) // No free space
            {
                SendErrFull(pkt->addr);
                return;
            }
            usr->addr = pkt->addr;
            usr->pingTime = ttime.GetTicks();
            usr->pingSeq = 0;
            usr->pongTime = usr->pingTime;
            usr->pongSeq = 0;
            usr->status = STATUS_CONNECTED;
            usr->ID = GenerateID();
            usr->name = nm;
            usr->latence = 0;
            usr->sesID = 0;

            //sLobby.users.push_back(usr);
            //usr->sesID = sLobby.ID;
            Srv_DoSessionUserJoin(usr, &sLobby);
            Srv_SendConnected(usr);
        }
    }
    else
    {
        switch(pkt->data[0])
        {
            case SYS_MSG_LIST_GAMES:
                {
                    if (rd.size() != 8)
                        return;

                    if (pkt->user->ID != rd.readU64())
                        return;

                    RefData *msg = Srv_USRDataGenGamesList();
                    SendingData *dat = new SendingData(pkt->user->addr, GetSeq(), msg, PKT_FLAG_ASYNC);
                    dat->SetChannel(pkt->user->__idx, 0);
                    Send_PushData(dat);
                }
                break;

            case SYS_MSG_SES_CREATE:
                if (rd.size() > 5)
                {
                    std::string sesname;
                    std::string pswd;

                    sesname.clear();
                    rd.readSzStr(sesname);

                    if (!SessionCheckName(sesname) || Srv_SessionFind(sesname))
                    {
                        Srv_SessionErrSend(pkt->user, ERR_SES_CREATE);
                        return;
                    }

                    uint32_t mx_pl = rd.readU32();

                    pswd.clear();
                    rd.readSzStr(pswd);
                    if (pswd.size() > 0 && !SessionCheckPswd(pswd))
                    {
                        Srv_SessionErrSend(pkt->user, ERR_SES_CREATE);
                        return;
                    }

                    NetSession *ns = new NetSession();
                    ns->Init(GenerateID(), sesname, false);
                    ns->max_players = mx_pl;
                    ns->password = pswd;
                    ns->open = true;

                    sessions[ns->ID] = ns;

                    Srv_DoSessionUserJoin(pkt->user, ns);
                }
                else
                {
                    Srv_SessionErrSend(pkt->user, ERR_SES_CREATE);
                    return;
                }
                break;

            case SYS_MSG_SES_JOIN:
                if (rd.size() == 8 )
                {
                    uint64_t SID = rd.readU64();
                    NetSession *ses = Srv_SessionFind(SID);
                    if (!ses)
                    {
                        Srv_SessionErrSend(pkt->user, ERR_SES_JOIN);
                        return;
                    }
                    Srv_DoSessionUserJoin(pkt->user, ses);
                }
                else
                {
                    Srv_SessionErrSend(pkt->user, ERR_SES_JOIN);
                    return;
                }
                break;

            case SYS_MSG_PING:
                if (rd.size() == 4)
                {
                    uint32_t seq = rd.readU32();

                    if (seq > pkt->user->pongSeq && seq <= pkt->user->pingSeq)
                    {
                        pkt->user->pongSeq = seq;
                        pkt->user->pongTime = ttime.GetTicks();


                        printf("DBG %d %d %d %d\n", pkt->user->pingSeq, seq, (int)ttime.GetTicks(), (int)pkt->user->pingTime);
                        int32_t latence = (pkt->user->pingSeq - seq) * DELAY_PING + (ttime.GetTicks() - pkt->user->pingTime);

                        if (!pkt->user->latence)
                            pkt->user->latence  = latence;
                        else
                            pkt->user->latence = latence + (pkt->user->latence - latence) / (int32_t)LATENCE_OLD_PART;


                        printf("Srv %s pong %d   latence %d\n", pkt->user->name.c_str(), seq, pkt->user->latence);
                    }
                }
                break;

            default:
                break;
        }
    }
}

void ZNDNet::Srv_ProcessRegularPkt(Pkt* pkt)
{
    if (!pkt || !pkt->user)
        return;

    if (pkt->datasz < 2 )
        return;

    MemReader rd(pkt->data + 1, pkt->datasz - 1);

    switch(pkt->data[0])
    {
        default:
            break;
    }
}



int ZNDNet::_UpdateServerThread(void *data)
{
    ZNDNet *_this = (ZNDNet *)data;

    while (!_this->updateThreadEnd)
    {
        if (SDL_LockMutex(_this->eSyncMutex) == 0)
        {
            uint64_t forceBrake = _this->ttime.GetTicks() + TIMEOUT_SRV_RECV_MAX;
            while (_this->ttime.GetTicks() < forceBrake)
            {
                InRawPkt *ipkt = _this->Recv_PopInRaw();
                if (!ipkt)
                    break; // If no more packets -> do another things

                Pkt * pkt = _this->Recv_ServerPreparePacket(ipkt);
                if (pkt)
                {
                    if (pkt->flags & PKT_FLAG_SYSTEM)
                        _this->Srv_ProcessSystemPkt(pkt);
                    else
                        _this->Srv_ProcessRegularPkt(pkt);

                    delete pkt;
                }
            }

            for(NetUserList::iterator it = _this->sActiveUsers.begin() ; it != _this->sActiveUsers.end(); it++)
            {
                NetUser *usr = *it;
                if (usr->latence == 0)
                {
                    RefDataStatic *rfdata = RefDataStatic::create( (20 - usr->latence) * 10000 + 700 );
                    rfdata->get()[0] = 0xFF;

                    SendingData *dta = new SendingData(usr->addr, _this->seq, rfdata, 0);
                    dta->SetChannel(usr->__idx, usr->latence % 2);

                    _this->Send_PushData(dta);
                    printf("Sended SYNC to user %d seq %d %x chnl %d\n", usr->__idx, _this->seq, crc32(rfdata->get(), rfdata->size(), 0), usr->latence % 2);

                    usr->latence++;
                    _this->seq ++;
                    //SDL_Delay(0);
                }
            }

            _this->Srv_InterprocessUpdate();

            SDL_UnlockMutex(_this->eSyncMutex);
        }


        SDL_Delay(0);
    }

    return 0;
}


void ZNDNet::StartServer(uint16_t port)
{
    mode = MODE_SERVER;
    sock = SDLNet_UDP_Open(port);

    Srv_InitUsers();

    recvThreadEnd = false;
    recvThread = SDL_CreateThread(_RecvThread, "", this);

    sendThreadEnd = false;
    sendThread = SDL_CreateThread(_SendThread, "", this);

    updateThreadEnd = false;
    updateThread = SDL_CreateThread(_UpdateServerThread, "", this);

    sLobby.Init( GenerateID(), "", true );
}






void ZNDNet::Srv_SendConnected(const NetUser *usr)
{
    if (!usr)
        return;

    RefDataWStream *strm = RefDataWStream::create();

    strm->writeU8(SYS_MSG_CONNECTED);
    strm->writeU64(usr->ID);
    strm->writeSzStr(usr->name);

    Send_PushData( new SendingData(usr->addr, 0, strm, PKT_FLAG_SYSTEM) );
}

void ZNDNet::Srv_SendLeaderStatus(const NetUser *usr, bool lead)
{
    if (!usr)
        return;

    RefDataWStream *dat = RefDataWStream::create();
    dat->writeU8(SYS_MSG_SES_LEAD);
    dat->writeU8(lead ? 1 : 0);

    SendingData *dta = new SendingData(usr->addr, GetSeq(), dat, PKT_FLAG_SYSTEM); // Send, NOW YOU A LEADER!
    dta->SetChannel(usr->__idx, 0);
    Send_PushData(dta);
}

void ZNDNet::Srv_SendSessionJoin(const NetUser *usr, NetSession *ses, bool leader)
{
    if (!usr || !ses)
        return;

    RefDataWStream *dat = RefDataWStream::create();
    dat->writeU8(SYS_MSG_SES_JOIN);
    dat->writeU8(leader ? 1 : 0);
    dat->writeU64(ses->ID);
    dat->writeSzStr(ses->name);

    SendingData *dta = new SendingData(usr->addr, GetSeq(), dat, PKT_FLAG_SYSTEM); // Send, NOW YOU A LEADER!
    dta->SetChannel(usr->__idx, 0);
    Send_PushData(dta);
}




RefData *ZNDNet::Srv_USRDataGenUserLeave(NetUser *usr)
{
    if (!usr)
        return NULL;

    RefDataWStream *dat = RefDataWStream::create();
    dat->writeU8(USR_MSG_SES_LEAVE);
    dat->writeU64(usr->ID);
    return dat;
}

RefData *ZNDNet::Srv_USRDataGenUserJoin(NetUser *usr)
{
    if (!usr)
        return NULL;

    RefDataWStream *dat = RefDataWStream::create();
    dat->writeU8(USR_MSG_SES_JOIN);
    dat->writeU64(usr->ID);
    dat->writeSzStr(usr->name);
    return dat;
}

RefData *ZNDNet::Srv_USRDataGenGamesList()
{
    RefDataWStream *dat = RefDataWStream::create();
    dat->writeU8(USR_MSG_LIST_GAMES);
    dat->writeU32(sessions.size());
    uint32_t opened = 0;
    for(NetSessionMap::iterator it = sessions.begin(); it != sessions.end(); it++)
    {
        if (it->second->open)
        {
            dat->writeU64( it->second->ID );
            dat->writeU32( it->second->users.size() );
            dat->writeU32( it->second->max_players );
            dat->writeU8( (it->second->password.size() > 0 ? 1 : 0 ) );
            dat->writeSzStr( it->second->name );

            opened++;
        }
    }

    dat->seek(1, 0);
    dat->writeU32(opened);

    dat->seek(0, 2);

    return dat;
}


void ZNDNet::Srv_SendPing(const NetUser *usr)
{
    if (!usr)
        return;

    RefDataWStream *strm = RefDataWStream::create();

    strm->writeU8(SYS_MSG_PING);
    strm->writeU32(usr->pingSeq);

    Send_PushData( new SendingData(usr->addr, 0, strm, PKT_FLAG_SYSTEM) );
}

void ZNDNet::Srv_SendDisconnect(const NetUser *usr)
{
    if (!usr)
        return;

    RefDataWStream *strm = RefDataWStream::create();

    strm->writeU8(SYS_MSG_DISCONNECT);

    Send_PushData( new SendingData(usr->addr, 0, strm, PKT_FLAG_SYSTEM) );
}

void ZNDNet::Srv_DisconnectUser(NetUser *usr)
{
    if (!usr)
        return;

    Srv_SendDisconnect(usr); //Send to user disconnect msg

    if (usr->sesID && usr->sesID != sLobby.ID)
        Srv_SessionUserLeave(usr);

    usr->status = STATUS_DISCONNECTED;
}


void ZNDNet::Srv_InterprocessUpdate()
{
    uint64_t curTime = ttime.GetTicks();

    for(NetUserList::iterator it = sActiveUsers.begin() ; it != sActiveUsers.end();)
    {
        NetUser *usr = *it;

        if (usr)
        {
            if (usr->status == STATUS_CONNECTED)
            {
                if (curTime > usr->pongTime + TIMEOUT_USER)
                {
                    Srv_DisconnectUser(usr);

                    it = sActiveUsers.erase(it);
                    sFreeUsers.push_back(usr);

                    //Disconnect player
                }
                else
                {
                    //Ping
                    if (curTime > usr->pingTime + DELAY_PING)
                    {
                        usr->pingTime = curTime;
                        usr->pingSeq++;
                        Srv_SendPing(usr);
                    }
                }
            }
        } // if (usr)

        it++;
    }

    ReceiveCheck(); // Delete timeout packets
    ConfirmQueueCheck();

}


void ZNDNet::Srv_InitUsers()
{
    sActiveUsers.clear();
    sFreeUsers.clear();

    if (!sUsers)
        sUsers = new NetUser[ZNDNET_USER_MAX];

    for(int32_t i = 0; i < ZNDNET_USER_MAX; i++)
    {
        NetUser *usr = &sUsers[i];
        usr->__idx = i;

        sFreeUsers.push_back(usr);
    }
}

NetUser *ZNDNet::Srv_FindUserByIP(const IPaddress &addr)
{
    for(NetUserList::iterator it = sActiveUsers.begin() ; it != sActiveUsers.end(); it++)
    {
        if ( IPCMP((*it)->addr, addr) )
            return (*it);
    }

    return NULL;
}

NetUser *ZNDNet::Srv_FindUserByName(const std::string &name)
{
    for(NetUserList::iterator it = sActiveUsers.begin() ; it != sActiveUsers.end(); it++)
    {
        if ( (*it)->name.size() == name.size() )
        {
            if ( strcmp((*it)->name.c_str(), name.c_str()) == 0 )
                return (*it);
        }
    }

    return NULL;
}


NetUser *ZNDNet::Srv_AllocUser()
{
    if (sFreeUsers.empty())
        return NULL;

    NetUser *usr = sFreeUsers.front();
    sFreeUsers.pop_front();

    sActiveUsers.push_back(usr);

    return usr;
}

void ZNDNet::Srv_FreeUser(NetUser *usr)
{
    if (!usr)
        return;

    sActiveUsers.remove(usr);
    sFreeUsers.push_back(usr);

    usr->status = STATUS_DISCONNECTED;
}

};
