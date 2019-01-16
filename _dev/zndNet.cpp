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
    lastMsgTime = 0;
    latence = 0;
    sesID = -1;
    status = STATUS_DISCONNECTED;
    __idx = -1;
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


RefData::RefData(uint8_t *_data, size_t sz)
{
    data = new uint8_t[sz];
    memcpy(data, _data, sz);
    datasz = sz;

    refcnt = 0;
}

RefData::RefData(size_t sz)
{
    data = new uint8_t[sz];
    datasz = sz;

    refcnt = 0;
}

RefData::~RefData()
{
    if (data)
        delete[] data;
}


ZNDNet::ZNDNet(const std::string &servstring)
{
    mode = MODE_UNKNOWN;
    servString = servstring;
    seq = 0;

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


void ZNDNet::StartClient(const std::string &name, const IPaddress &addr)
{
    mode = MODE_CLIENT;
    sock = SDLNet_UDP_Open(0);

    cServAddress = addr;

    recvThreadEnd = false;
    recvThread = SDL_CreateThread(_RecvThread, "", this);

    sendThreadEnd = false;
    sendThread = SDL_CreateThread(_SendThread, "", this);

    updateThreadEnd = false;
    updateThread = SDL_CreateThread(_UpdateClientThread, "", this);

    RefData *rfdata = new RefData(PKT_HANDSHAKE_DATA + servString.size() + name.size());
    SendingData *dta = new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM);

    rfdata->data[0] = SYS_MSG_HANDSHAKE;
    rfdata->data[1] = servString.size();
    rfdata->data[2] = name.size();
    memcpy(&rfdata->data[3], servString.c_str(), rfdata->data[1]);
    memcpy(&rfdata->data[3 + rfdata->data[1]], name.c_str(), rfdata->data[2]);

    SDL_LockMutex(sendPktListMutex);
    sendPktList.push_back(dta);
    SDL_UnlockMutex(sendPktListMutex);
}

uint64_t ZNDNet::GenerateID()
{
    return SDL_GetPerformanceCounter();
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

            Pkt * pkt = _this->Recv_PreparePacket(ipkt);
            if (pkt)
            {

                delete pkt;
            }
        }

        for(int i = 0 ; i < _this->_activeUsersNum; i++)
        {
            NetUser *usr = _this->_activeUsers[i];
            if (usr->latence < 20)
            {
                RefData *rfdata = new RefData( (20 - usr->latence) * 10000 + 700 );

                SendingData *dta = new SendingData(usr->addr, _this->seq, rfdata, 0);
                dta->SetChannel(usr->__idx);

                SDL_LockMutex(_this->sendPktListMutex);
                _this->sendPktList.push_back(dta);
                SDL_UnlockMutex(_this->sendPktListMutex);
                printf("Sended SYNC %d %x\n", _this->seq, crc32(rfdata->data, rfdata->datasz, 0));


                for(int j = 1; j < 100; j++)
                {
                    dta = new SendingData(usr->addr, j * 20 + _this->seq, rfdata, PKT_FLAG_ASYNC * (j & 1) );
                    dta->SetChannel(usr->__idx + j);

                    SDL_LockMutex(_this->sendPktListMutex);
                    _this->sendPktList.push_back(dta);
                    SDL_UnlockMutex(_this->sendPktListMutex);
                    printf("Sended ASYNC  %d %x\n", j * 20 + _this->seq, crc32(rfdata->data, rfdata->datasz, 0));
                }




                /*dta = new SendingData(usr->addr, 20 + _this->seq, rfdata, PKT_FLAG_ASYNC);
                dta->SetChannel(usr->__idx, 1);

                SDL_LockMutex(_this->sendPktListMutex);
                _this->sendPktList.push_back(dta);
                SDL_UnlockMutex(_this->sendPktListMutex);
                printf("Sended SYNC  %d %x\n", 20 + _this->seq, crc32(rfdata->data, rfdata->datasz, 0));*/

                usr->latence++;
                _this->seq ++;
                //SDL_Delay(0);
            }
        }

        SDL_Delay(0);
    }

    return 0;
}



int ZNDNet::_UpdateClientThread(void *data)
{
    ZNDNet *_this = (ZNDNet *)data;
    int32_t pkt_recv = 0;
    while (!_this->updateThreadEnd)
    {
        uint64_t forceBrake = _this->ttime.GetTicks() + 10;
        while (_this->ttime.GetTicks() < forceBrake)
        {
            InRawPkt *ipkt = _this->Recv_PopInRaw();
            if (!ipkt)
                break; // If no more packets -> do another things

            Pkt * pkt = _this->Recv_ClientPreparePacket(ipkt);
            if (pkt)
            {
                if ((pkt->flags & PKT_FLAG_SYSTEM) == 0)
                {
                    pkt_recv++;
                    if (pkt->flags & PKT_FLAG_ASYNC)
                        printf("\t\t\tReceive ASYNC %d %x %d\n", pkt->seqid, crc32(pkt->data, pkt->datasz, 0), pkt_recv);
                    else
                        printf("\t\t\tReceive SYNC  %d %x %d \n", pkt->seqid, crc32(pkt->data, pkt->datasz, 0), pkt_recv);

                }

                delete pkt;
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
