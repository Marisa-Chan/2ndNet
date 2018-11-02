#ifndef ZNDNET_H_INCLUDED
#define ZNDNET_H_INCLUDED

#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_net.h>
#include <list>
#include <string>
#include <map>

#define ZNDNET_INBUFF_SIZE   65535
#define ZNDNET_USER_MAX      1024
#define ZNDNET_USER_NAME_MAX 20
#define ZNDNET_MAX_MTU       1500
#define ZNDNET_PKT_UDPSZ     8
#define ZNDNET_PKT_MAXSZ     (ZNDNET_MAX_MTU - ZNDNET_PKT_UDPSZ)
#define ZNDNET_PKT_MAXDATSZ  ()

namespace ZNDNet
{
enum UIDS
{
    UID_MASK_ID       = 0x0FFFFFFF,
    UID_MASK_FLAG     = 0xF0000000,
    UID_TYPE_ALL      = 0x10000000,
    UID_TYPE_UNKNOWN  = 0x00000000
};

enum STATUS
{
    STATUS_DISCONNECTED     = 0,
    STATUS_CONNECTED        = 1,
    STATUS_JOINED           = 2
};

enum SYS_MSG
{
    SYS_MSG_HANDSHAKE    = 1,
    SYS_MSG_CONNECTED    = 2,
    SYS_MSG_ERRFULL      = 255
};

enum HDR_OFF
{
    HDR_OFF_FLAGS       = 0,

    HDR_OFF_SYS_DATA    = 1, //For system short msgs


    HDR_OFF_SEQID       = 1,

    HDR_OFF_DATA        = 5,

    HDR_OFF_PART_FSIZE  = 5,
    HDR_OFF_PART_OFFSET = 9,
    HDR_OFF_PART_DATA   = 13,

    HDR_OFF_SYS_MINSZ   = 2,
    HDR_OFF_MINSZ       = 6,
    HDR_OFF_PART_MINSZ  = 14,
};

enum PKT_FLAG
{
    PKT_FLAG_PART   = 0x1,
    PKT_FLAG_GARANT = 0x2,
    PKT_FLAG_SYSTEM = 0x80
};

enum TIMEOUT
{
    TIMEOUT_PKT = 10000
};


// Type names declaration
struct NetUser;
//

struct Tick64
{
    uint32_t lastTick;
    uint32_t lap;

    Tick64();
    uint64_t GetTicks();
    uint32_t GetSec();
};


bool IPCMP(const IPaddress &a, const IPaddress &b);
void writeU32(uint32_t u, void *dst);
uint32_t readU32(const void *src);
void writeU64(uint64_t u, void *dst);
uint64_t readU64(const void *src);


struct InRawPktHdr
{
    uint8_t   flags;
    uint32_t  fsize;    //Full size
    uint32_t  offset;   //Offset
    uint32_t  seqid;
    uint8_t  *data;
    size_t    datasz;

    InRawPktHdr();
    bool Parse(uint8_t *_data, size_t len);
};

// Raw input packets
struct InRawPkt
{
    IPaddress addr;
    uint8_t  *data;
    size_t    len;

    // Fill by parser
    InRawPktHdr hdr;

    InRawPkt(const UDPpacket &pkt);
    ~InRawPkt();
    bool Parse();
};

typedef std::list<InRawPkt *> InRawList;

// Used for packets assembly
struct AddrSeq
{
    IPaddress addr;
    uint32_t seq;

    void set(const IPaddress &_addr, uint32_t _seq);

    inline bool operator==(const AddrSeq& b){ return addr.host == b.addr.host && addr.port == b.addr.port && seq == b.seq; };
};


struct InPartedPkt
{
    AddrSeq   ipseq;
    uint64_t  timeout;
    size_t    nextOff;
    uint8_t  *data;
    size_t    len;
    uint8_t   flags;
    InRawList parts;

    InPartedPkt(const AddrSeq& _ipseq, size_t _len, uint8_t _flags);
    ~InPartedPkt();
    bool Feed(InRawPkt *pkt, uint64_t time);
    void _Insert(InRawPkt *pkt);
};

struct Pkt
{
    IPaddress addr;
    uint8_t  *_raw_data;
    size_t    _raw_len;

    uint8_t   flags;
    uint32_t  seqid;
    uint8_t  *data;
    size_t    datasz;

    NetUser  *user;

    Pkt(InRawPkt *, NetUser *);
    Pkt(InPartedPkt *, NetUser *);
    ~Pkt();
};


typedef std::list<InPartedPkt *> PartedList;

class RefData
{
protected:
    uint8_t * data;
    size_t    datasz;
    int32_t   refcnt;

    RefData(uint8_t *_data, size_t sz);
public:
    static RefData *MakePending(uint8_t *_data, size_t sz);
    ~RefData() {delete[] data;};

    uint8_t * GetData() {return data;};
    size_t    GetSize() {return datasz;};

    void Inc() {refcnt++;};
    void Dec() {refcnt--;};

    int32_t Refs() {return refcnt;};
};



struct NetUser
{
    uint64_t ID;
    std::string name;
    IPaddress addr;
    uint64_t lastMsgTime;
    uint32_t latence;
    uint64_t sesID;
    uint8_t status;

    int32_t __idx;

    NetUser();
};



class ZNDNet
{
protected:

public:
    ZNDNet(const std::string &servstring);

    void StartServer(uint16_t port);

protected:

    void PushInRaw(InRawPkt *inpkt);
    InRawPkt *PopInRaw();

    Pkt *PreparePacket(InRawPkt *pkt);

    static int _RecvThread(void *data);
    static int _UpdateThread(void *data);

    void SendRaw(const IPaddress &addr, const uint8_t *data, size_t sz);
    void SendErrFull(const IPaddress &addr);
    void SendConnected(const NetUser *usr);

//    int32_t FindUserIndexByIP(const IPaddress &addr);
    NetUser *FindUserByIP(const IPaddress &addr);
    int32_t FindFreeUser();

    NetUser *FindUserByName(const std::string &_name);

    void ActivateUser(int32_t idx);
    void DeactivateUser(int32_t idx);


    uint64_t GenerateID();
    void CorrectName(std::string &_name);

public:

protected:
    std::string servString;
    UDPsocket   sock;
    uint32_t    seq;

    // In Raw packets
    bool        recvThreadEnd;
    SDL_Thread *recvThread;

    InRawList      recvPktList;
    SDL_mutex  *recvPktListMutex;
    ////

    bool        updateThreadEnd;
    SDL_Thread *updateThread;



    NetUser     users[ZNDNET_USER_MAX];
    NetUser    *_activeUsers[ZNDNET_USER_MAX];
    int32_t     _activeUsersNum;

    PartedList  pendingPkt;

    Tick64      ttime;
};

};

#endif // ZNDNET_H_INCLUDED
