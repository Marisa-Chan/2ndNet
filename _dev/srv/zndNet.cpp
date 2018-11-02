#include "zndNet.h"
#include "zndNetPkt.h"

namespace ZNDNet
{

// Primes for inc seq ID
static const int PRIMES[96] =
{
      1,   2,   3,   5,   7,  11,  13,  17,  19,  23,  29,  31,  37,  41,  43,  47,  53,  59,  61,  67,
     71,  73,  79,  83,  89,  97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167,
    173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277,
    281, 283, 293, 307, 311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401,
    409, 419, 421, 431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499
};



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

InRawPktHdr::InRawPktHdr()
{
    flags  = 0;
    fsize  = 0;
    offset = 0;
    seqid  = 0;
    data   = NULL;
    datasz = 0;
}

bool InRawPktHdr::Parse(uint8_t *_data, size_t len)
{
    if (!_data)
        return false; // No data

    uint8_t flags = _data[HDR_OFF_FLAGS];

    if (flags & PKT_FLAG_SYSTEM)
    {
        if (flags & (~PKT_FLAG_SYSTEM)) // System flag must be only one
            return false;

        if (len < HDR_OFF_SYS_MINSZ)
            return false;

        data = &_data[HDR_OFF_SYS_DATA];
        datasz = len - HDR_OFF_SYS_DATA;
    }
    else
    {
        // Incorrect flags
        if ( (flags & (~(PKT_FLAG_PART | PKT_FLAG_GARANT))) )
            return false;

        seqid  = readU32( &data[HDR_OFF_SEQID] );

        if (flags & PKT_FLAG_PART)
        {
            if (len < HDR_OFF_PART_MINSZ)
                return false;

            fsize  = readU32( &data[HDR_OFF_PART_FSIZE] );
            offset = readU32( &data[HDR_OFF_PART_OFFSET] );
            data   = &data[HDR_OFF_PART_DATA];
            datasz = len - HDR_OFF_PART_DATA;
        }
        else
        {
            if (len < HDR_OFF_MINSZ)
                return false;

            offset = 0;
            fsize  = len - HDR_OFF_DATA;
            data   = &data[HDR_OFF_DATA];
            datasz = fsize;
        }

        if ( offset + datasz > fsize )
            return false;
    }

    return true;
}

InRawPkt::InRawPkt(const UDPpacket &pkt)
{
    if (pkt.len && pkt.data)
    {
        len = pkt.len;
        data = new uint8_t[len];
        memcpy(data, pkt.data, len);
        addr = pkt.address;
    }
    else
    {
        data = NULL;
        len = 0;
        addr.host = 0;
        addr.port = 0;
    }
}

InRawPkt::~InRawPkt()
{
    if (data)
        delete[] data;
}




bool InRawPkt::Parse()
{
    if (!data)
        return false; // No data

    return hdr.Parse(data, len);
}

InPartedPkt::InPartedPkt(const AddrSeq& _ipseq, size_t _len, uint8_t _flags)
{
    ipseq = _ipseq;
    timeout = 0;
    nextOff = 0;
    data = NULL;
    len = 0;
    flags = _flags;

    if (_len)
    {
        data = new uint8_t[_len];
        len = _len;
    }
}

InPartedPkt::~InPartedPkt()
{
    if (data)
        delete[] data;

    InRawList::iterator it = parts.begin();
    while(it != parts.end())
    {
        delete *it;
        it = parts.erase(it);
    }
}

void InPartedPkt::_Insert(InRawPkt *pkt)
{
    memcpy(&data[nextOff], pkt->hdr.data, pkt->hdr.datasz);
    nextOff += pkt->hdr.datasz;
}

bool InPartedPkt::Feed(InRawPkt *pkt, uint64_t timestamp)
{
    if (!pkt || !pkt->hdr.data || pkt->hdr.datasz <= 0 ||
        pkt->hdr.seqid != ipseq.seq ||
        pkt->hdr.offset < nextOff ||
        len < pkt->hdr.offset + pkt->hdr.datasz)
    {
        delete pkt;
        return false;
    }

    if (pkt->hdr.offset == nextOff)
    {
        timeout = timestamp + TIMEOUT_PKT;

        _Insert(pkt);
        delete pkt;

        if (!parts.empty())
        {
            InRawList::iterator it = parts.begin();
            while(it != parts.end())
            {
                if ( (*it)->hdr.offset < nextOff )
                {   //Wrong packet
                    delete *it;
                    it = parts.erase(it);
                }
                else if ( (*it)->hdr.offset == nextOff )
                {
                    _Insert(*it);
                    delete *it;
                    parts.erase(it);
                    it = parts.begin();
                }
                else
                    it++;

            }
        }
    }
    else
    {
        parts.push_back(pkt);
    }

    return nextOff >= len;
}

Pkt::Pkt(InRawPkt *pk, NetUser *usr)
{
    addr = pk->addr;
    _raw_data = pk->data;
    _raw_len = pk->len;
    flags = pk->hdr.flags;
    seqid = pk->hdr.seqid;
    data = pk->hdr.data;
    datasz = pk->hdr.datasz;
    user = usr;

    pk->data = NULL;
    pk->len = 0;

    delete pk;
}

Pkt::Pkt(InPartedPkt *pk, NetUser *usr)
{
    addr = pk->ipseq.addr;
    _raw_data = pk->data;
    _raw_len = pk->len;
    flags = pk->flags;
    seqid = pk->ipseq.seq;
    data = pk->data;
    datasz = pk->len;
    user = usr;

    pk->data = NULL;
    pk->len = 0;

    delete pk;
}

Pkt::~Pkt()
{
    if (_raw_data)
        delete[] _raw_data;
}



NetUser::NetUser()
{
    ID = UID_TYPE_UNKNOWN;
    name = "";
    addr.host = 0;
    addr.port = 0;
    lastMsgTime = 0;
    latence = 0;
    sesID = -1;
    status = STATUS_DISCONNECTED;
    __idx = -1;
}

void AddrSeq::set(const IPaddress &_addr, uint32_t _seq)
{
    addr = _addr;
    seq = _seq;
}

bool IPCMP(const IPaddress &a, const IPaddress &b)
{
    return a.host == b.host && a.port == b.port;
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

void writeU64(uint64_t u, void *dst)
{
    uint8_t *dst8 = (uint8_t *)dst;
    dst8[0] = u & 0xFF;
    dst8[1] = (u >> 8) & 0xFF;
    dst8[2] = (u >> 16) & 0xFF;
    dst8[3] = (u >> 24) & 0xFF;
    dst8[4] = (u >> 32) & 0xFF;
    dst8[5] = (u >> 40) & 0xFF;
    dst8[6] = (u >> 48) & 0xFF;
    dst8[7] = (u >> 56) & 0xFF;
}

uint64_t readU64(const void *src)
{
    uint8_t *src8 = (uint8_t *)src;
    return (uint64_t)src8[0] |
           ((uint64_t)src8[1] << 8) |
           ((uint64_t)src8[2] << 16) |
           ((uint64_t)src8[3] << 24) |
           ((uint64_t)src8[4] << 32) |
           ((uint64_t)src8[5] << 40) |
           ((uint64_t)src8[6] << 48) |
           ((uint64_t)src8[7] << 56);
}


RefData * RefData::MakePending(uint8_t *_data, size_t sz)
{
    if (_data && sz > 0)
    {
        return new RefData(_data, sz);
    }
    return NULL;
}

RefData::RefData(uint8_t *_data, size_t sz)
{
    data = new uint8_t[sz];
    memcpy(data, _data, sz);
    datasz = sz;

    refcnt = 0;
}

ZNDNet::ZNDNet(const std::string &servstring)
{
    servString = servstring;
    seq = 0;

    updateThreadEnd = true;
    updateThread = NULL;

    recvThreadEnd = true;
    recvThread = NULL;

    recvPktList.clear();
    recvPktListMutex = SDL_CreateMutex();

    _activeUsersNum = 0;
}


int ZNDNet::_RecvThread(void *data)
{
    ZNDNet *_this = (ZNDNet *)data;

    uint8_t *recvBuffer;
    UDPpacket inpkt;

    if (_this)
    {
        recvBuffer = new uint8_t[ZNDNET_INBUFF_SIZE];

        inpkt.data = recvBuffer;
        inpkt.maxlen = ZNDNET_INBUFF_SIZE;
        inpkt.channel = -1;

        while (!_this->recvThreadEnd)
        {
            if ( SDLNet_UDP_Recv(_this->sock, &inpkt) == 1 )
            {
                InRawPkt *pkt = new InRawPkt(inpkt);
                _this->PushInRaw(pkt);
            }

            SDL_Delay(0);
        }

        delete[] recvBuffer;
    }

    return 0;
}


void ZNDNet::PushInRaw(InRawPkt *inpkt)
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


InRawPkt *ZNDNet::PopInRaw()
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


void ZNDNet::StartServer(uint16_t port)
{
    sock = SDLNet_UDP_Open(port);

    recvThreadEnd = false;
    recvThread = SDL_CreateThread(_RecvThread, "", this);

    updateThreadEnd = false;
    updateThread = SDL_CreateThread(_UpdateThread, "", this);

}

uint64_t ZNDNet::GenerateID()
{
    return SDL_GetPerformanceCounter();
}


Pkt *ZNDNet::PreparePacket(InRawPkt *pkt)
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
            if (pkt->hdr.data[0] == SYS_MSG_HANDSHAKE && pkt->hdr.datasz < 5) // It's New user?
            {
                uint8_t servstrSz = pkt->hdr.data[PKT_HANDSHAKE_SERV_SZ];
                uint8_t namestrSz = pkt->hdr.data[PKT_HANDSHAKE_NAME_SZ];

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



int ZNDNet::_UpdateThread(void *data)
{
    ZNDNet *_this = (ZNDNet *)data;

    while (!_this->updateThreadEnd)
    {
        uint64_t forceBrake = _this->ttime.GetTicks() + 10;
        while (_this->ttime.GetTicks() > forceBrake)
        {
            InRawPkt *ipkt = _this->PopInRaw();
            if (!ipkt)
                break; // If no more packets -> do another things

            Pkt * pkt = _this->PreparePacket(ipkt);
            if (pkt)
            {

            }
        }

        SDL_Delay(0);
    }

    return 0;
}



//int32_t ZNDNet::FindUserIndexByIP(const IPaddress &addr)
//{
//    for(int32_t i = 0; i < _activeUsersNum; i++)
//    {
//        if ( IPCMP(_activeUsers[i]->addr, addr) )
//            return i;
//    }
//
//    return -1;
//}


NetUser *ZNDNet::FindUserByIP(const IPaddress &addr)
{
    for(int32_t i = 0; i < _activeUsersNum; i++)
    {
        if ( IPCMP(_activeUsers[i]->addr, addr) )
            return _activeUsers[i];
    }

    return NULL;
}

int32_t ZNDNet::FindFreeUser()
{
    for(int32_t i = 0; i < ZNDNET_USER_MAX; i++)
    {
        if (users[i].status == STATUS_DISCONNECTED)
            return i;
    }

    return -1;
}

NetUser *ZNDNet::FindUserByName(const std::string &name)
{
    for(int32_t i = 0; i < _activeUsersNum; i++)
    {
        if ( _activeUsers[i]->name.size() == name.size() )
        {
            if ( strcmp(_activeUsers[i]->name.c_str(), name.c_str()) == 0 )
                return _activeUsers[i];
        }
    }

    return NULL;
}


void ZNDNet::ActivateUser(int32_t idx)
{
    if (idx < 0 || idx >= ZNDNET_USER_MAX)
        return;

    NetUser *usr = &users[idx];

    for(int32_t i = 0; i < _activeUsersNum; i++)
    {
        if (_activeUsers[i] == usr)
            return; // Don't add user twice
    }

    _activeUsers[_activeUsersNum] = usr;
    _activeUsersNum++;
}


void ZNDNet::DeactivateUser(int32_t idx)
{
    if (idx < 0 || idx >= ZNDNET_USER_MAX)
        return;

    NetUser *usr = &users[idx];

    for(int32_t i = 0; i < _activeUsersNum; i++)
    {
        //Find this user
        if (_activeUsers[i] == usr)
        {
            _activeUsers[i] = _activeUsers[_activeUsersNum - 1];
            _activeUsersNum--;
            return;
        }
    }
}



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

void ZNDNet::SendErrFull(const IPaddress &addr)
{
    uint8_t buf[HDR_OFF_SYS_MINSZ];
    buf[HDR_OFF_FLAGS]    = PKT_FLAG_SYSTEM;
    buf[HDR_OFF_SYS_DATA] = SYS_MSG_ERRFULL;

    SendRaw(addr, buf, sizeof(buf));
}

void ZNDNet::SendConnected(const NetUser *usr)
{
    uint8_t buf[HDR_OFF_SYS_DATA + PKT_CONNECTED_NAME + ZNDNET_USER_NAME_MAX]; // max packet size
    buf[HDR_OFF_FLAGS]    = PKT_FLAG_SYSTEM;
    buf[HDR_OFF_SYS_DATA] = SYS_MSG_CONNECTED;

    uint8_t *pkt = &buf[HDR_OFF_SYS_DATA];
    writeU64(usr->ID, &pkt[PKT_CONNECTED_UID]);

    pkt[PKT_CONNECTED_NAME_SZ] = usr->name.size();
    memcpy(&pkt[PKT_CONNECTED_NAME], usr->name.c_str(), usr->name.size());

    size_t sz = HDR_OFF_SYS_DATA + PKT_CONNECTED_NAME + usr->name.size();

    SendRaw(usr->addr, buf, sz);
}


};
