#include "zndNet.h"
#include "zndNetPkt.h"
#include "test/crc32.h"

namespace ZNDNet
{


Tick64::Tick64()
{
    lap = 0;
    lastTick = 0;
}

uint64_t Tick64::GetTicks()
{
    uint32_t tick = SDL_GetTicks();

    if (lastTick > tick)
        lap++;

    lastTick = tick;

    return ((uint64_t)lap << 32) | (uint64_t)tick;
}

uint32_t Tick64::GetSec()
{
    return GetTicks()/1000;
}

Event::Event(uint32_t _type, uint32_t _value):
    type(_type),
    value(_value)
{
    size = 0;
    __id = 0;
}

Event::~Event()
{
}

EventData::EventData(uint32_t _type, uint32_t _value, uint64_t _from, bool _cast, uint64_t _to, uint32_t _sz, uint8_t *_data, uint8_t _channel):
    Event(_type, _value)
{
    from = _from;
    cast = _cast;
    to = _to;
    channel = _channel;

    if (_sz && _data)
    {
        data = new uint8_t[_sz];
        memcpy(data, _data, _sz);
        size = _sz;
    }
    else
    {
        size = 0;
        data = NULL;
    }

}

EventData::~EventData()
{
    if (data)
        delete[] data;
}


EventNameID::EventNameID(uint32_t _type, uint32_t _value, const std::string &_name, uint64_t _id):
    Event(_type, _value)
{
    name = _name;
    id = _id;
}

EventNameID::~EventNameID()
{
}

NetUser::NetUser()
{
    ID = UID_TYPE_UNKNOWN;
    name = "";
    addr.host = 0;
    addr.port = 0;

    pingTime = 0;
    pingSeq = 0;
    pongTime = 0;
    pongSeq = 0;

    latence = 0;
    sesID = 0;
    status = STATUS_DISCONNECTED;

    seqid = 0;

    __idx = -1;
}

bool NetUser::IsOnline()
{
    if (status & STATUS_ONLINE_MASK)
        return true;

    return false;
}

uint32_t NetUser::GetSeq()
{
    return seqid++;
}


AddrSeq::AddrSeq()
{
    addr.host = 0;
    addr.port = 0;
    seq = 0;
}

AddrSeq::AddrSeq(const IPaddress &_addr, uint32_t _seq)
{
    set(_addr, _seq);
}

void AddrSeq::set(const IPaddress &_addr, uint32_t _seq)
{
    addr = _addr;
    seq = _seq;
}



void writeU32(uint32_t u, void *dst)
{
    uint8_t *dst8 = (uint8_t *)dst;
    dst8[0] = u & 0xFF;
    dst8[1] = (u >> 8) & 0xFF;
    dst8[2] = (u >> 16) & 0xFF;
    dst8[3] = (u >> 24) & 0xFF;
}

uint32_t readU32(const void *src)
{
    uint8_t *src8 = (uint8_t *)src;
    return src8[0] | (src8[1] << 8) | (src8[2] << 16) | (src8[3] << 24);
}



ZNDNet::ZNDNet(const std::string &servstring)
{
    mode = MODE_UNKNOWN;
    servString = servstring;

    updateThreadEnd = true;
    updateThread = NULL;

    recvThreadEnd = true;
    recvThread = NULL;

    //recvPktList.clear();
    recvPktListMutex = SDL_CreateMutex();


    sendThreadEnd = true;
    sendThread = NULL;

    //sendPktList.clear();
    sendPktListMutex = SDL_CreateMutex();

    //confirmPktList.clear();
    confirmQueueMutex = SDL_CreateMutex();

    sActiveUsers.clear();

    eStatus = 0;
    cSessionsReqTimeNext = 0;

    eSyncMutex = SDL_CreateMutex();

    sUsers = NULL;

    eEventMutex = SDL_CreateMutex();
    eEventDataSize = 0;
    eEventNextID = 0;
    eEventWaitLock = 0;

    sendModifyMutex = SDL_CreateMutex();
}

ZNDNet::~ZNDNet()
{

}






uint64_t ZNDNet::GenerateID()
{
    return 1 + SDL_GetPerformanceCounter();
}





//int32_t ZNDNet::FindUserIndexByIP(const IPaddress &addr)
//{
//    for(int32_t i = 0; i < sActiveUsersNum; i++)
//    {
//        if ( IPCMP(sActiveUsers[i]->addr, addr) )
//            return i;
//    }
//
//    return -1;
//}


void ZNDNet::CorrectName(std::string &name)
{
    if (name.size() > ZNDNET_USER_NAME_MAX)
        name.resize(ZNDNET_USER_NAME_MAX);

    for(size_t i = 0; i < name.size(); i++)
    {
        char chr = name[i];
        chr &= 0x7F;
        if (chr < ' ')
            chr = ' ';
        name[i] = chr;
    }
}


void ZNDNet::SendRaw(const IPaddress &addr, const uint8_t *data, size_t sz)
{
    UDPpacket outpkt;

    if (data && sz < ZNDNET_PKT_MAXSZ && sz >= 0)
    {
        outpkt.address = addr;
        outpkt.channel = -1;
        outpkt.len = sz;
        outpkt.maxlen = sz;
        outpkt.data = (uint8_t *)data;

        SDLNet_UDP_Send(sock, -1, &outpkt);

    }
}

SendingData *ZNDNet::MkSendingData(NetUser *usr, RefData *data, uint8_t flags, uint32_t chnl)
{
    SendingData *dta = new SendingData(usr->addr, usr->GetSeq(), data, flags);
    dta->SetChannel(usr->__idx, chnl);
    return dta;
}

void ZNDNet::SendErrFull(const IPaddress &addr)
{
    uint8_t buf[HDR_OFF_SYS_MINSZ];
    buf[HDR_OFF_FLAGS]    = PKT_FLAG_SYSTEM;
    buf[HDR_OFF_SYS_DATA] = SYS_MSG_ERRFULL;

    SendRaw(addr, buf, sizeof(buf));
}


void ZNDNet::ConfirmQueueCheck()
{
    uint64_t tcur = ttime.GetTicks();

    for (SendingList::iterator it = confirmQueue.begin(); it != confirmQueue.end(); )
    {
        SendingData *dta = *it;

        if (tcur >= dta->timeout)
        {
            if ( SDL_LockMutex(confirmQueueMutex) == 0 )
            {
                //printf("Confirm (%d) [%d] timeout!\n", mode,dta->addr.seq);
                it = confirmQueue.erase(it);
                SDL_UnlockMutex(confirmQueueMutex);

                Send_RetryData(dta);
            }
        }
        else
            it++;
    }
}

void ZNDNet::PendingCheck()
{
    uint64_t tcur = ttime.GetTicks();

    for (PartedList::iterator it = pendingPkt.begin(); it != pendingPkt.end();)
    {
        if (tcur >= (*it)->timeout)
        {
            InPartedPkt *pkt = *it;
            if ( (pkt->flags & PKT_FLAG_GARANT) && pkt->retry > 0 )
            {
                size_t upto = pkt->RetryUpTo();
                if (!upto)
                {
                    it = pendingPkt.erase(it);
                    //printf("Pending %d (%d)\n", pkt->ipseq.seq, (uint32_t)pkt->parts.size());
                    delete pkt;
                }
                else
                {
                    pkt->retry--;
                    pkt->timeout = tcur + TIMEOUT_PENDING_GARANT;
                    SendRetry(pkt->ipseq.seq, pkt->ipseq.addr, pkt->nextOff, upto);
                }
            }
            else
            {
                it = pendingPkt.erase(it);
                //printf("Pending! %d (%d)\n", pkt->ipseq.seq, (uint32_t)pkt->parts.size());
                delete pkt;
            }
        }
        else
            it++;
    }
}

void ZNDNet::ConfirmReceive(AddrSeq _seq)
{
    for (SendingList::iterator it = confirmQueue.begin(); it != confirmQueue.end(); )
    {
        SendingData *dta = *it;
        if (dta->addr == _seq)
        {
            if ( SDL_LockMutex(confirmQueueMutex) == 0 )
            {
                //printf("Confirm (%d) [%d] received, delete it! %d\n", mode,dta->addr.seq, (int)ttime.GetTicks());
                it = confirmQueue.erase(it);
                SDL_UnlockMutex(confirmQueueMutex);

                delete dta;
            }
            break;
        }
        else
            it++;
    }
}

void ZNDNet::ConfirmRetry(AddrSeq _seq, uint32_t from, uint32_t to)
{
    if (to <= from)
        return;

    for (SendingList::iterator it = confirmQueue.begin(); it != confirmQueue.end(); )
    {
        SendingData *dta = *it;
        if (dta->addr == _seq)
        {
            if (dta->pdata->size() > from && dta->pdata->size() >= to )
            {
                if ( SDL_LockMutex(confirmQueueMutex) == 0 )
                {
                    it = confirmQueue.erase(it);
                    SDL_UnlockMutex(confirmQueueMutex);

                    //printf("Retry %d: \t%d -> %d\n", dta->addr.seq, from, to);
                    Send_RetryData(dta, from, to, false);
                }
            }
            break;
        }
        else
            it++;
    }
}

void ZNDNet::SendDelivered(uint32_t _seqid, const IPaddress &addr)
{
    RefDataWStream *dat = RefDataWStream::create();
    dat->writeU8(SYS_MSG_DELIVERED);
    dat->writeU32(_seqid);

    Send_PushData( new SendingData(addr, 0, dat, PKT_FLAG_SYSTEM) );
}

void ZNDNet::SendRetry(uint32_t _seqid, const IPaddress &addr, uint32_t nextOff, uint32_t upto)
{
    RefDataWStream *dat = RefDataWStream::create();
    dat->writeU8(SYS_MSG_RETRY);
    dat->writeU32(_seqid);
    dat->writeU32(nextOff);
    dat->writeU32(upto);

    Send_PushData( new SendingData(addr, 0, dat, PKT_FLAG_SYSTEM) );
}




void ZNDNet::Events_Push(Event *evnt)
{
    if (!evnt)
        return;

    if (evnt->size >= EVENTS_DATA_MAX)
    {
        printf("Event (%d) msg too big\n", evnt->type);
        delete evnt;
        return;
    }

    if (SDL_LockMutex(eEventMutex) == 0)
    {
        if (eEventList.size() >= EVENTS_MAX || (eEventDataSize + evnt->size) >= EVENTS_DATA_MAX)
            printf("Events overflow num(%d) sz(%d) add(type %d)(sz %d)!\n", (int)eEventList.size(), eEventDataSize, evnt->type, evnt->size);

        while (eEventList.size() >= EVENTS_MAX || (eEventDataSize + evnt->size) >= EVENTS_DATA_MAX)
        {
            Event *tmp = eEventList.front();
            eEventList.pop_front();

            eEventDataSize -= tmp->size;
            delete tmp;
        }

        if (eEventWaitLock == 0 && eEventList.empty()) //Check for no more events in list and no wait functions
            eEventNextID = 0; //Clear ID for avoid overflow

        eEventDataSize += evnt->size;
        evnt->__id = eEventNextID;
        eEventList.push_back(evnt);

        eEventNextID++;

        SDL_UnlockMutex(eEventMutex);
    }
}

Event *ZNDNet::Events_Pop()
{
    if ( eEventList.empty() )
        return NULL;

    if (SDL_LockMutex(eEventMutex) == 0)
    {
        Event *tmp = eEventList.front();
        eEventList.pop_front();

        eEventDataSize -= tmp->size;

        SDL_UnlockMutex(eEventMutex);

        return tmp;
    }

    return NULL;
}

void ZNDNet::Events_ClearByType(uint32_t type)
{
    if (SDL_LockMutex(eEventMutex) == 0)
    {
        for(EventList::iterator it = eEventList.begin(); it != eEventList.end();)
        {
            Event *evt = *it;
            if (evt->type == type)
            {
                it = eEventList.erase(it);
                delete evt;
            }
            else
                it++;
        }
        SDL_UnlockMutex(eEventMutex);
    }
}

void ZNDNet::Events_Clear()
{
    if (SDL_LockMutex(eEventMutex) == 0)
    {
        for(EventList::iterator it = eEventList.begin(); it != eEventList.end();)
        {
            delete (*it);
            it = eEventList.erase(it);
        }
        SDL_UnlockMutex(eEventMutex);
    }
}

Event *ZNDNet::Events_PeekByType(uint32_t type)
{
    if (SDL_LockMutex(eEventMutex) == 0)
    {
        for(EventList::iterator it = eEventList.begin(); it != eEventList.end(); it++)
        {
            Event *evt = *it;
            if (evt->type == type)
            {
                eEventList.erase(it);
                SDL_UnlockMutex(eEventMutex);

                return evt;
            }
        }
        SDL_UnlockMutex(eEventMutex);
    }
    return NULL;
}

Event *ZNDNet::Events_WaitForMsg(uint32_t type, uint32_t time)
{
    uint32_t nextID = 0;

    if (SDL_LockMutex(eEventMutex) == 0)
    {
        eEventWaitLock++;

        for(EventList::iterator it = eEventList.begin(); it != eEventList.end(); it++)
        {
            Event *evt = *it;
            if (evt->type == type)
            {
                eEventList.erase(it);
                eEventWaitLock--;
                SDL_UnlockMutex(eEventMutex);
                return evt;
            }
        }

        nextID = eEventNextID;
        SDL_UnlockMutex(eEventMutex);
    }

    //Wait for new events

    uint64_t tmax = ttime.GetTicks() + time;

    while(true)
    {
        SDL_Delay(1);

        if (nextID < eEventNextID) // If new packets arrived
        {
            if (SDL_LockMutex(eEventMutex) == 0) //lock it for iterate
            {
                for(EventList::iterator it = eEventList.begin(); it != eEventList.end(); it++)
                {
                    Event *evt = *it;
                    if (evt->type == type)
                    {
                        eEventList.erase(it);
                        eEventWaitLock--;
                        SDL_UnlockMutex(eEventMutex);
                        return evt;
                    }
                }

                nextID = eEventNextID;
                SDL_UnlockMutex(eEventMutex);
            }
        }

        if (time && ttime.GetTicks() >= tmax)
            break;
    }

    eEventWaitLock--;
    return NULL;
}

bool ZNDNet::IPCMP(const IPaddress &a, const IPaddress &b)
{
    return a.host == b.host && a.port == b.port;
}

void ZNDNet::Confirm_Clear(const IPaddress &addr)
{
    for (SendingList::iterator it = confirmQueue.begin(); it != confirmQueue.end(); )
    {
        SendingData *dta = *it;
        if ( IPCMP(dta->addr.addr, addr) )
        {
            if ( SDL_LockMutex(confirmQueueMutex) == 0 )
            {
                it = confirmQueue.erase(it);
                SDL_UnlockMutex(confirmQueueMutex);

                delete dta;
            }
        }
        else
            it++;
    }
}

void ZNDNet::Pending_Clear(const IPaddress &addr)
{
    for (PartedList::iterator it = pendingPkt.begin(); it != pendingPkt.end();)
    {
        InPartedPkt *pkt = *it;
        if ( IPCMP(pkt->ipseq.addr, addr) )
        {
            it = pendingPkt.erase(it);
            delete pkt;
        }
        else
            it++;
    }
}

void ZNDNet::Cli_SendData(uint64_t to, void *data, uint32_t sz, uint8_t flags, uint8_t channel)
{
    if (!data || !sz)
        return;

    RefData *rfdat = USRDataGenData(cME.ID, false, to, data, sz);

    flags &= (PKT_FLAG_GARANT | PKT_FLAG_ASYNC);

    if (channel >= ZNDNET_USER_SCHNLS)
        channel = ZNDNET_USER_SCHNLS - 1;

    SendingData *snd = new SendingData(cServAddress, cME.GetSeq(), rfdat, flags);
    snd->SetChannel(0, channel);

    Send_PushData(snd);
}

void ZNDNet::Cli_BroadcastData(void *data, uint32_t sz, uint8_t flags, uint8_t channel)
{
    if (!data || !sz)
        return;

    RefData *rfdat = USRDataGenData(cME.ID, true, cME.sesID, data, sz);

    flags &= (PKT_FLAG_GARANT | PKT_FLAG_ASYNC);

    if (channel >= ZNDNET_USER_SCHNLS)
        channel = ZNDNET_USER_SCHNLS - 1;

    SendingData *snd = new SendingData(cServAddress, cME.GetSeq(), rfdat, flags);
    snd->SetChannel(0, channel);

    Send_PushData(snd);
}


RefData *ZNDNet::USRDataGenData(uint64_t from, bool cast, uint64_t to, void *data, uint32_t sz)
{
    if (!data || !sz)
        return NULL;

    RefDataWStream *rfdat = RefDataWStream::create(32 + sz);
    rfdat->writeU8(USR_MSG_DATA);
    rfdat->writeU64(from);
    rfdat->writeU8(cast ? 1 : 0);
    rfdat->writeU64(to);
    rfdat->writeU32(sz);
    rfdat->write(data, sz);
    return rfdat;
}

};
