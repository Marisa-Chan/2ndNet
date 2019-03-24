#include "zndNet.h"
#include "zndNetPkt.h"
#include "test/crc32.h"

namespace ZNDNet
{

// Primes for inc seq ID
#define ZNDNET_PRIMES_CNT   96
static const int PRIMES[ZNDNET_PRIMES_CNT] =
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



NetUser::NetUser()
{
    ID = UID_TYPE_UNKNOWN;
    name = "";
    addr.host = 0;
    addr.port = 0;

    lastPingTime = 0;
    pingSeq = 0;
    pingLastSeq = 0;

    latence = 0;
    sesID = 0;
    status = STATUS_DISCONNECTED;
    __idx = -1;
}

bool NetUser::IsOnline()
{
    if (status & STATUS_ONLINE_MASK)
        return true;

    return false;
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



ZNDNet::ZNDNet(const std::string &servstring)
{
    mode = MODE_UNKNOWN;
    servString = servstring;
    seq = 0;
    seq_d = 0;

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
    confirmPktListMutex = SDL_CreateMutex();

    _activeUsersNum = 0;

    eStatus = 0;
    cSessionsReqTimeNext = 0;

    eSyncMutex = SDL_CreateMutex();
}






uint64_t ZNDNet::GenerateID()
{
    return 1 + SDL_GetPerformanceCounter();
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
    usr->__idx = idx;

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

SendingData *ZNDNet::MkSendingData(NetUser *usr, RefData *data, uint8_t flags, uint32_t chnl)
{
    SendingData *dta = new SendingData(usr->addr, GetSeq(), data, flags);
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


uint32_t ZNDNet::GetSeq()
{
    uint32_t cur = seq;
    seq += PRIMES[seq_d];
    seq_d = (seq_d + 1) % ZNDNET_PRIMES_CNT;
    return cur;
}

};
