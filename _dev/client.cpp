#include "zndNet.h"
#include "zndNetPkt.h"
#include "memread.h"
#include "test/crc32.h"

namespace ZNDNet
{

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
                parted = new InPartedPkt(ipseq, pkt->hdr.fsize, pkt->hdr.flags, pkt->hdr.uchnl);
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

void ZNDNet::Cli_ProcessSystemPkt(Pkt* pkt)
{
    if (!pkt)
        return;

    if (pkt->datasz < 2 )
        return;

    MemReader rd(pkt->data + 1, pkt->datasz - 1);

    switch(pkt->data[0])
    {
        case SYS_MSG_CONNECTED:
            {
                if (cME.status == STATUS_CLI_CONNECTING)
                {
                    uint64_t ID = rd.readU64();
                    std::string nm;
                    rd.readSzStr(nm);
                    cME.name = nm;
                    cME.ID = ID;
                    cME.status = STATUS_CONNECTED;

                    printf("\t\tCONNECTED %" PRIx64 " %s\n", ID, nm.c_str() );
                    //Cli_RequestGamesList(); // DEVTEST
                }
            }
            break;

        case SYS_MSG_SES_JOIN:
            {
                cLeader = (rd.readU8() == 1);
                cME.sesID = rd.readU64();
                rd.readSzStr( cJoinedSessionName );
                eStatus |= FLAGS_SESSION_JOINED;
            }
            break;

        case SYS_MSG_PING:
            if (rd.size() == 4)
            {
                uint32_t seq = rd.readU32();
                RefDataWStream *rfdat = RefDataWStream::create();
                rfdat->writeU8(SYS_MSG_PING);
                rfdat->writeU32(seq);

                printf("Cli %s ping %d\n", cME.name.c_str(), seq);

                Send_PushData( new SendingData(cServAddress, 0, rfdat, PKT_FLAG_SYSTEM) );
            }
            break;

        default:
            printf("Client: Getted unk SYS msg %X\n", pkt->data[0]);
            break;
    }
}

void ZNDNet::Cli_ProcessRegularPkt(Pkt* pkt)
{
    if (!pkt)
        return;

    if (pkt->datasz < 2 )
        return;

    MemReader rd(pkt->data + 1, pkt->datasz - 1);

    switch(pkt->data[0])
    {
        case USR_MSG_LIST_GAMES: //New sessions list arrived -> mark it's
            {
                if (rd.size() < 4)
                    return;

                uint32_t numSessions = rd.readU32();

                eStatus &= (~FLAGS_SESSIONS_LIST_GET);

                cSessions.resize(numSessions);

                printf("Sessions %d \n", numSessions);

                for(uint32_t i = 0; i < numSessions; i++)
                {
                    SessionInfo *inf = &cSessions[i];
                    inf->ID = rd.readU64();
                    inf->players = rd.readU32();
                    inf->max_players = rd.readU32();
                    inf->pass = (rd.readU8() > 0 ? true : false);
                    rd.readSzStr(inf->name);
                }

                eStatus |= FLAGS_SESSIONS_LIST_GET;

            }
            break;

        case USR_MSG_SES_LIST:
            {
                if (rd.size() < 4)
                    return;

                uint32_t numUsers = rd.readU32();

                eStatus &= (~FLAGS_USERS_LIST_GET);

                cUsers.resize(numUsers);

                printf("Users %d \n", numUsers);

                for(uint32_t i = 0; i < numUsers; i++)
                {
                    UserInfo *inf = &cUsers[i];
                    inf->ID = rd.readU64();
                    rd.readSzStr(inf->name);
                }

                eStatus |= FLAGS_USERS_LIST_GET;

            }
            break;

        case USR_MSG_SES_JOIN:
            {
                UserInfo inf;
                inf.lead = false;

                inf.ID = rd.readU64();
                rd.readSzStr(inf.name);

                cUsers.push_back(inf);

                eStatus |= FLAGS_USERS_LIST_UPD;
                printf("User join %s %x\n", inf.name.c_str(), (int)inf.ID);
            }
            break;

        case USR_MSG_SES_LEAVE:
            {
                uint64_t UID = rd.readU64();

                for(UserInfoVect::iterator it = cUsers.begin(); it != cUsers.end(); it++)
                {
                    if (it->ID == UID)
                    {
                        printf("Leaved %s\n", it->name.c_str());
                        cUsers.erase(it);
                        eStatus |= FLAGS_USERS_LIST_UPD;
                        break;
                    }
                }
            }
            break;

        default:
            printf("Client: Getted unk USR msg %X\n", pkt->data[0]);
            break;
    }
}

int ZNDNet::_UpdateClientThread(void *data)
{
    ZNDNet *_this = (ZNDNet *)data;
    int32_t pkt_recv = 0;
    while (!_this->updateThreadEnd)
    {
        if (SDL_LockMutex(_this->eSyncMutex) == 0)
        {
            uint64_t forceBrake = _this->ttime.GetTicks() + TIMEOUT_CLI_RECV_MAX;
            while (_this->ttime.GetTicks() < forceBrake)
            {
                InRawPkt *ipkt = _this->Recv_PopInRaw();
                if (!ipkt)
                    break; // If no more packets -> do another things

                Pkt * pkt = _this->Recv_ClientPreparePacket(ipkt);
                if (pkt)
                {
                    if (pkt->flags & PKT_FLAG_SYSTEM)
                    {
                        _this->Cli_ProcessSystemPkt(pkt);
                    }
                    else
                    {
                        if (pkt->data[0] == 0xFF)
                        {
                            pkt_recv++;
                            if (pkt->flags & PKT_FLAG_ASYNC)
                                printf("\t\t\tReceive ASYNC %d %x %d chnl %d\n", pkt->seqid, crc32(pkt->data, pkt->datasz, 0), pkt_recv, pkt->uchnl);
                            else
                                printf("\t\t\tReceive SYNC %s %d %x %d chnl %d\n", _this->cME.name.c_str(), pkt->seqid, crc32(pkt->data, pkt->datasz, 0), pkt_recv, pkt->uchnl);
                        }
                        else
                            _this->Cli_ProcessRegularPkt(pkt);
                    }

                    delete pkt;
                }
            }

            SDL_UnlockMutex(_this->eSyncMutex);
        }

        SDL_Delay(0);
    }

    return 0;
}


void ZNDNet::StartClient(const std::string &name, const IPaddress &addr)
{
    mode = MODE_CLIENT;
    sock = SDLNet_UDP_Open(0);

    cServAddress = addr;
    cME.name = name;
    cME.status = STATUS_CLI_CONNECTING;

    eStatus = 0;

    cSessionsReqTimeNext = 0;

    recvThreadEnd = false;
    recvThread = SDL_CreateThread(_RecvThread, "", this);

    sendThreadEnd = false;
    sendThread = SDL_CreateThread(_SendThread, "", this);

    updateThreadEnd = false;
    updateThread = SDL_CreateThread(_UpdateClientThread, "", this);

    Cli_SendConnect();
}

void ZNDNet::Cli_SendConnect()
{
    RefDataWStream *rfdata = RefDataWStream::create();
    rfdata->writeU8(SYS_MSG_HANDSHAKE);
    rfdata->writeU8(servString.size());
    rfdata->writeU8(cME.name.size());
    rfdata->writeStr(servString);
    rfdata->writeStr(cME.name);

    Send_PushData( new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM));
}


void ZNDNet::Cli_RequestGamesList()
{
    if (cSessionsReqTimeNext < ttime.GetTicks())
    {
        printf("Cli_RequestGamesList \n");
        cSessionsReqTimeNext = ttime.GetTicks() + DELAY_SESS_REQ;

        RefDataWStream *rfdata = RefDataWStream::create();
        rfdata->writeU8(SYS_MSG_LIST_GAMES);
        rfdata->writeU64(cME.ID);

        Send_PushData( new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM));
    }
}

uint8_t ZNDNet::Cli_GetStatus()
{
    return cME.status;
}

bool ZNDNet::Cli_GetSessions(SessionInfoVect &dst)
{
    if ( !cME.IsOnline() )
        return false;

    Cli_RequestGamesList();

    if ( !(eStatus & FLAGS_SESSIONS_LIST_GET) )
        return false;

    bool res = false;

    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        if (eStatus & FLAGS_SESSIONS_LIST_GET)
        {
            eStatus &= (~FLAGS_SESSIONS_LIST_UPD);
            dst = cSessions;
            res = true;
        }
        SDL_UnlockMutex(eSyncMutex);
    }

    return res;
}


void ZNDNet::Cli_CreateSession(const std::string &name, const std::string &pass, uint32_t max_players)
{
    if ( !cME.IsOnline() )
        return;

    if ( !SessionCheckName(name) || ( pass.size() > 0 && !SessionCheckPswd(pass) ))
        return;

    RefDataWStream *rfdata = RefDataWStream::create();
    rfdata->writeU8(SYS_MSG_SES_CREATE);
    rfdata->writeSzStr(name);
    rfdata->writeU32(max_players);
    rfdata->writeSzStr(pass);

    Send_PushData( new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM));
}

void ZNDNet::Cli_JoinSession(uint64_t SID)
{
    if ( !cME.IsOnline() )
        return;

    RefDataWStream *rfdata = RefDataWStream::create();
    rfdata->writeU8(SYS_MSG_SES_JOIN);
    rfdata->writeU64(SID);

    Send_PushData( new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM));
}


bool ZNDNet::Cli_GetUsers(UserInfoVect &dst)
{
    if ( !cME.IsOnline() )
        return false;

    if ( !(eStatus & FLAGS_USERS_LIST_GET) )
        return false;

    bool res = false;

    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        if (eStatus & FLAGS_USERS_LIST_GET)
        {
            eStatus &= (~FLAGS_USERS_LIST_UPD);
            dst = cUsers;
            res = true;
        }
        SDL_UnlockMutex(eSyncMutex);
    }

    return res;
}

};
