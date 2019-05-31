#ifndef ZNDNET_H_INCLUDED
#define ZNDNET_H_INCLUDED

#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_net.h>
#include <list>
#include <string>
#include <deque>
#include <vector>
#include <atomic>
#include <unordered_map>

#include "errcode.h"

#define ZNDNET_BUFF_SIZE     4096
#define ZNDNET_USER_MAX      1024
#define ZNDNET_USER_NAME_MAX 20
#define ZNDNET_MAX_MTU       1400
#define ZNDNET_PKT_UDPSZ     8
#define ZNDNET_PKT_MAXSZ     (ZNDNET_MAX_MTU - ZNDNET_PKT_UDPSZ)
#define ZNDNET_PKT_MAXDATSZ  ()
#define ZNDNET_USER_SCHNLS   2          // Minimum 2 (0 - always system synced, 1+ user channels)
#define ZNDNET_SYNC_CHANNELS (ZNDNET_USER_MAX * ZNDNET_USER_SCHNLS)

#define ZNDNET_TUNE_RECV_PKTS    32     // Maximum packets that will be polled at once
#define ZNDNET_TUNE_SEND_DELAY   0      // millisecond
#define ZNDNET_TUNE_SEND_MAXDATA 128107  // how many bytes can be sent on this time delay (13107 ~bytes per 1ms on 100Mbit/s)

// High load
//#define ZNDNET_TUNE_RECV_PKTS    64
//#define ZNDNET_TUNE_SEND_DELAY   0
//#define ZNDNET_TUNE_SEND_MAXDATA 131070


namespace ZNDNet
{

enum MODE
{
    MODE_UNKNOWN,
    MODE_SERVER,
    MODE_CLIENT
};

enum UIDS
{
    UID_MASK_ID       = 0x0FFFFFFF,
    UID_MASK_FLAG     = 0xF0000000,
    UID_TYPE_ALL      = 0x10000000,
    UID_TYPE_UNKNOWN  = 0x00000000
};

enum STATUS
{
    STATUS_ONLINE_MASK      = 0xF0,
    STATUS_OFFLINE_MASK     = 0x0F,

    STATUS_DISCONNECTED     = 0x01,
    STATUS_CONNECTED        = 0x10,
    //STATUS_JOINED           = 0x20,

    STATUS_CLI_CONNECTING   = 0x03,
};

enum FLAGS
{
    FLAGS_SESSIONS_LIST_GET = 0x1,
    FLAGS_SESSIONS_LIST_UPD = 0x2,
    FLAGS_USERS_LIST_GET    = 0x4,
    FLAGS_USERS_LIST_UPD    = 0x8,
    FLAGS_SESSION_JOINED    = 0x10,
};

enum SYS_MSG //Only for user<->server internal manipulations, short messages < packet length
{
    SYS_MSG_HANDSHAKE    = 1,
    SYS_MSG_CONNECTED    = 2,
    SYS_MSG_DISCONNECT   = 3,
    SYS_MSG_PING         = 5,
    SYS_MSG_DELIVERED    = 7,
    SYS_MSG_RETRY        = 8,
    SYS_MSG_LIST_GAMES   = 0x30,
    SYS_MSG_SES_JOIN     = 0x40, //Server->User if joined (or create). User->Server for request for join
    SYS_MSG_SES_LEAVE    = 0x41, //User->Server request. Server->User kick/disconnect
    SYS_MSG_SES_LEAD     = 0x42, //Server->User
    SYS_MSG_SES_CREATE   = 0x43, //User->Server
    SYS_MSG_SES_SHOW     = 0x44, //User->Server
    SYS_MSG_SES_ERR      = 0x4F,
    SYS_MSG_CONNERR      = 0x81,
};

enum USR_MSG //For user<->user manipulations and big messages
{
    USR_MSG_DATA          = 0x10,
    USR_MSG_LIST_GAMES    = 0x30, //Server->User
    USR_MSG_SES_USERJOIN  = 0x40, //Broadcasting Server->[users]
    USR_MSG_SES_USERLEAVE = 0x41, //Broadcasting Server->[users]
    USR_MSG_SES_USERLIST  = 0x42, //Server->User (list users in session)
};

enum HDR_OFF
{
    HDR_OFF_FLAGS       = 0,

    HDR_OFF_SYS_DATA    = 1, //For system short msgs

    HDR_OFF_SEQID       = 1, //SeqID
    HDR_OFF_CHANNEL     = 5,

    HDR_OFF_DATA        = 6, //Data position if not multipart

    HDR_OFF_PART_FSIZE  = 6, //Full data size
    HDR_OFF_PART_OFFSET = 10, //Offset of this chunk
    HDR_OFF_PART_DATA   = 14, //Offset of data in multipart


    HDR_OFF_SYS_MINSZ   = 2,
    HDR_OFF_MINSZ       = 7,
    HDR_OFF_PART_MINSZ  = 15,
};

enum PKT_FLAG
{
    PKT_FLAG_PART   = 0x1,
    PKT_FLAG_GARANT = 0x2,
    PKT_FLAG_ASYNC  = 0x4,
    PKT_FLAG_SYSTEM = 0x80,

    PKT_FLAG_MASK_SYSTEM = PKT_FLAG_SYSTEM,
    PKT_FLAG_MASK_NORMAL = (PKT_FLAG_PART | PKT_FLAG_GARANT | PKT_FLAG_ASYNC)
};

enum TIMEOUT
{

    TIMEOUT_SESSION = 60000,
};

enum CHANNEL
{
    CHANNEL_SYS = 0,
    CHANNEL_USR = 1,
};

enum
{
    PKT_CHNL_NOT_SET = 0xFF,
    PKT_NO_CHANNEL = 0xFFFFFFFF,
    SES_NAME_MAX = 32,

    TIMEOUT_SRV_RECV_MAX = 10,
    TIMEOUT_CLI_RECV_MAX = 3,

    TIMEOUT_PENDING = 10000,
    TIMEOUT_PENDING_GARANT = 3300,
    TIMEOUT_GARANT = 15000,
    TIMEOUT_GARANT_MULTIPART = 3300,

    TIMEOUT_CONNECT = 10000,
    TIMEOUT_USER = 15000,

    RETRY_GARANT = 2,

    DELAY_SESS_REQ = 5000,

    DELAY_PING = 5000,

    LATENCE_OLD_PART = 20,

    EVENTS_MAX = 1000,
    EVENTS_DATA_MAX = 104857600, //100 Mb for maximum data stored in events
};

enum
{
    EVENT_DISCONNECT,   //On disconnect
    EVENT_CONNECTED,    //
    EVENT_CONNERR,
    EVENT_LOBBY,        //
    EVENT_SESSION_LIST, //Sessions list receieved
    EVENT_SESSION_JOIN, //Success join
    EVENT_SESSION_FAIL, //Error join or create
    EVENT_SESSION_KICK,
    EVENT_SESSION_END,  //
    EVENT_USER_LIST,
    EVENT_USER_ADD,
    EVENT_USER_LEAVE,
    EVENT_DATA,
};


// Type names declaration
struct NetUser;
struct NetSession;
//

struct Tick64
{
    uint32_t lastTick;
    uint32_t lap;

    Tick64();
    uint64_t GetTicks();
    uint32_t GetSec();
};



void writeU32(uint32_t u, void *dst);
uint32_t readU32(const void *src);


struct InRawPktHdr
{
    uint8_t   flags;
    uint32_t  fsize;    //Full size
    uint32_t  offset;   //Offset
    uint32_t  seqid;
    uint8_t  *data;
    size_t    datasz;
    uint8_t   uchnl;

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

    InRawPkt(const UDPpacket *pkt);
    ~InRawPkt();
    bool Parse();
};

typedef std::list<InRawPkt *> InRawList;

// Used for packets assembly
struct AddrSeq
{
    IPaddress addr;
    uint32_t seq;

    AddrSeq();
    AddrSeq(const IPaddress &_addr, uint32_t _seq);

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
    uint8_t   uchnl;
    uint8_t   retry;
    InRawList parts;

    InPartedPkt(const AddrSeq& _ipseq, size_t _len, uint8_t _flags, uint8_t _channel);
    ~InPartedPkt();
    bool Feed(InRawPkt *pkt, uint64_t time);
    void _Insert(InRawPkt *pkt);
    size_t RetryUpTo();
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

    uint8_t   uchnl;

    NetUser  *user;

    Pkt(InRawPkt *, NetUser *);
    Pkt(InPartedPkt *, NetUser *);
    ~Pkt();
};


typedef std::list<InPartedPkt *> PartedList;

class RefData
{
public:
    inline size_t size() {return _datasz;};
    inline int32_t unlink() {return --_refcnt;};
    inline void link() {++_refcnt;};
    inline int32_t refs() {return _refcnt;};

    void copy(void *dst) {copy(dst, 0, _datasz);};
    void copy(void *dst, size_t nbytes) {copy(dst, 0, nbytes);};

    virtual void copy(void *dst, size_t pos, size_t nbytes) = 0;
    virtual ~RefData() {};

protected:
    RefData(): _datasz(0), _refcnt(0) {};

protected:
    size_t    _datasz;
    int32_t   _refcnt;
};

class RefDataStatic: public RefData
{
public:
    inline uint8_t * get() {return _data;};

    virtual void copy(void *dst, size_t pos, size_t nbytes);

    ~RefDataStatic();

    static RefDataStatic *create(size_t sz) {return new RefDataStatic(sz);};
    static RefDataStatic *create(uint8_t *data, size_t sz) {return new RefDataStatic(data, sz);};
protected:

    RefDataStatic(uint8_t *data, size_t sz);
    RefDataStatic(size_t sz);

protected:
    uint8_t * _data;
};

class RefDataWStream: public RefData
{
public:
    void write(const void *src, size_t nbytes);
    void writeU8(uint8_t bt);
    void writeU32(uint32_t dw);
    void writeU64(uint64_t qw);
    void writeStr(const std::string &str);
    void writeSzStr(const std::string &str);

    bool seek(int32_t pos, uint8_t mode);
    size_t tell();

    virtual void copy(void *dst, size_t pos, size_t nbytes);

    ~RefDataWStream();

    static RefDataWStream *create(uint32_t blocksize = 0x4000) {return new RefDataWStream(blocksize);};
    static RefDataWStream *create(uint8_t *data, size_t sz, uint32_t blocksize = 0x4000) {return new RefDataWStream(data, sz, blocksize);};
protected:
    RefDataWStream(uint32_t blocksize);
    RefDataWStream(uint8_t *data, size_t sz, uint32_t blocksize);

    void checkfree(size_t nbytes);

protected:
    typedef std::deque<uint8_t *> _tBlockList;

    _tBlockList    _blocks;
    size_t         _pos;

    const uint32_t _blksize;
};


struct SendingData
{
    AddrSeq  addr;
    RefData *pdata;
    size_t   sended;
    size_t   retryUpTo;
    uint8_t  flags;

    uint16_t tr_cnt;
    uint64_t timeout;

    uint32_t  schnl;  //For sync sending
    uint8_t   uchnl;

    SendingData(const AddrSeq &addr, RefData *data, uint8_t flags);
    SendingData(const IPaddress &addr, uint32_t seq, RefData *data, uint8_t flags);
    ~SendingData();

    void SetChannel(uint32_t userIDX, uint32_t userChnl = 0);
};

typedef std::list<SendingData *> SendingList;


struct NetUser
{
    uint64_t ID;
    std::string name;
    IPaddress addr;
    uint64_t sesID;
    uint8_t status;


    uint64_t pingTime;
    uint32_t pingSeq;
    uint64_t pongTime;
    uint32_t pongSeq;

    int32_t latence;

    uint32_t seqid;

    int32_t __idx;

    NetUser();
    bool IsOnline();
    uint32_t GetSeq();
};

typedef std::list<NetUser *> NetUserList;
typedef std::deque<NetUser *> NetUserQueue;

struct NetSession
{
    uint64_t    ID;
    bool        lobby;
    std::string name;
    NetUserList users;
    NetUser    *lead;
    std::string password;
    bool        open;

    uint32_t     max_players;

    uint64_t  orphanedTimer;
    uint64_t  closeTimer;

    NetSession();
    void Init(uint64_t _ID, const std::string &_name, bool _lobby = false);
    void clear();
    bool HasSlot();
};

typedef std::unordered_map<uint64_t, NetSession *> NetSessionMap;


struct SessionInfo
{
    uint64_t    ID;
    std::string name;
    bool        pass;
    uint32_t    players;
    uint32_t    max_players;

    SessionInfo& operator= (const SessionInfo& x);
};
typedef std::vector<SessionInfo> SessionInfoVect;

struct UserInfo
{
    uint64_t    ID;
    std::string name;
    bool        lead;

    UserInfo& operator= (const UserInfo& x);
};
typedef std::vector<UserInfo> UserInfoVect;

class Event
{
public:
    const uint32_t type;
    const uint32_t value;
    uint32_t size;
    uint32_t __id;

    Event(uint32_t _type, uint32_t _value);
    virtual ~Event();
};

class EventNameID: public Event
{
public:
    std::string name;
    uint64_t id;

    EventNameID(uint32_t _type, uint32_t _value, const std::string &_name, uint64_t _id);
    virtual ~EventNameID();
};

class EventData: public Event
{
public:
    uint64_t from;
    bool     cast;
    uint64_t to;
    uint8_t *data;
    uint8_t  channel;

    EventData(uint32_t _type, uint32_t _value, uint64_t _from, bool _cast, uint64_t _to, uint32_t _sz, uint8_t *_data, uint8_t _channel);
    virtual ~EventData();
};

typedef std::list<Event *> EventList;

class ZNDNet
{
// Methods
public:
    ZNDNet(const std::string &servstring);
    ~ZNDNet();

    void StartServer(uint16_t port);

    //Client methods
    void StartClient(const std::string &name, const IPaddress &addr);
    uint8_t Cli_GetStatus();

    void Cli_RequestSessions();

    bool Cli_GetSessions(SessionInfoVect &dst);
    void Cli_CreateSession(const std::string &name, const std::string &pass, uint32_t max_players);
    void Cli_JoinSession(uint64_t SID, const std::string &pass);
    void Cli_Disconnect();

    bool Cli_GetUsers(UserInfoVect &dst);

    void Cli_SendData(uint64_t to, void *data, uint32_t sz, uint8_t flags = 0, uint8_t channel = CHANNEL_USR);
    void Cli_BroadcastData(void *data, uint32_t sz, uint8_t flags = 0, uint8_t channel = CHANNEL_USR);

    void Cli_ShowSession(bool show);

    Event *Events_Pop();
    void   Events_ClearByType(uint32_t type);
    void   Events_Clear();
    Event *Events_PeekByType(uint32_t type);
    Event *Events_WaitForMsg(uint32_t type, uint32_t time = 0);
    void   Stop(); // Don't call it from threads!

protected:

    //For receive thread
    void Recv_PushInRaw(InRawPkt *inpkt);
    InRawPkt *Recv_PopInRaw();

    //For sending thread
    void Send_PushData(SendingData *data);
    void Send_RetryData(SendingData *data, size_t from = 0, size_t to = 0, bool decr = true);
    void Send_Clear(const IPaddress &addr);

    void ConfirmQueueCheck();
    void ConfirmReceive(AddrSeq _seq);
    void PendingCheck();
    void ConfirmRetry(AddrSeq _seq, uint32_t from, uint32_t to);

    void Confirm_Clear(const IPaddress &addr);
    void Pending_Clear(const IPaddress &addr);

    Pkt *Recv_ServerPreparePacket(InRawPkt *pkt);
    Pkt *Recv_ClientPreparePacket(InRawPkt *pkt);

    static int _RecvThread(void *data);
    static int _SendThread(void *data);
    static int _UpdateServerThread(void *data);
    static int _UpdateClientThread(void *data);

    void Srv_InitUsers();
    void Srv_ProcessSystemPkt(Pkt *pkt);
    void Srv_ProcessRegularPkt(Pkt *pkt);

    void Srv_InterprocessUpdate();

    void Srv_SendConnected(NetUser *usr);

    void Srv_SendLeaderStatus(NetUser *usr, bool lead);
    void Srv_SendSessionJoin(NetUser *usr, NetSession *ses, bool leader);

    void Srv_SendPing(NetUser *usr);
    void Srv_SendDisconnect(NetUser *usr);

    void Srv_DisconnectUser(NetUser *usr, bool free);

    NetSession *Srv_SessionFind(uint64_t _ID);
    NetSession *Srv_SessionFind(const std::string &name);
    void Srv_SessionDelete(uint64_t ID);
    void Srv_SessionDisconnectAllUsers(NetSession *ses, uint8_t type);
    void Srv_SessionBroadcast(NetSession *ses, RefData *dat, uint8_t flags, uint8_t chnl = 0, NetUser *from = NULL);
    void Srv_DoSessionUserJoin(NetUser *usr, NetSession *ses);
    void Srv_SessionUserLeave(NetUser *usr);
    void Srv_SessionListUsers(NetUser *usr);

    RefData *Srv_SessionErr(uint8_t code);
    void Srv_SessionErrSend(NetUser *usr, uint8_t code);

    RefData *Srv_USRDataGenUserLeave(NetUser *usr);
    RefData *Srv_USRDataGenUserJoin(NetUser *usr);
    RefData *Srv_USRDataGenGamesList();
    RefData *Srv_SYSDataGenSesLeave(int8_t type);


    RefData *USRDataGenData(uint64_t from, bool cast, uint64_t to, void *data, uint32_t sz);



    void Cli_ProcessSystemPkt(Pkt *pkt);
    void Cli_ProcessRegularPkt(Pkt *pkt);

    void Cli_SendConnect();
    void Cli_RequestGamesList();

    void Cli_InterprocessUpdate();

    bool SessionCheckName(const std::string &name);
    bool SessionCheckPswd(const std::string &pswd);

    void SendDelivered(uint32_t _seqid, const IPaddress &addr);
    void SendRetry(uint32_t _seqid, const IPaddress &addr, uint32_t nextOff, uint32_t upto);



//    int32_t FindUserIndexByIP(const IPaddress &addr);
    NetUser *Srv_FindUserByIP(const IPaddress &addr);
    NetUser *Srv_FindUserByID(uint64_t ID);

    NetUser *Srv_AllocUser();
    void Srv_FreeUser(NetUser *usr);

    NetUser *Srv_FindUserByName(const std::string &_name);
    void Srv_SendConnErr(const IPaddress &addr, uint8_t type);

    uint64_t GenerateID();
    //uint32_t GetSeq();
    void CorrectName(std::string &_name);
    SendingData *MkSendingData(NetUser *usr, RefData *data, uint8_t flags, uint32_t chnl = 0);

// Events
    void Events_Push(Event *evnt);


// Utils
    static bool IPCMP(const IPaddress &a, const IPaddress &b);

// Data
public:

protected:
    int         mode;
    std::string servString;
    UDPsocket   sock;
    //uint32_t    seq;
    //uint16_t    seq_d;

    volatile bool threadsEnd; //Run threads
    // In Raw packets
    SDL_Thread   *recvThread;

    InRawList   recvPktList;
    SDL_mutex  *recvPktListMutex;
    ////

    // Sending packets
    SDL_Thread   *sendThread;

    SendingList   sendPktList;
    SDL_mutex  *sendPktListMutex; //Only for pushing to the end

    SendingList   confirmQueue;
    SDL_mutex  *confirmQueueMutex;

    SDL_mutex  *sendModifyMutex; //Massive list changes, stop send iterations.

    ////

    SDL_Thread *updateThread;

    NetUser     *sUsers;
    NetUserList  sActiveUsers;
    NetUserQueue sFreeUsers;

    PartedList  pendingPkt;

    Tick64      ttime;
    NetSessionMap sessions;

// Server parts
    NetSession    sLobby;

// Client parts
    IPaddress   cServAddress;
    bool        cServHasLobby;
    NetUser     cME;
    bool        cLeader;
    std::string cJoinedSessionName;
    uint64_t    cTimeOut;

    SessionInfoVect cSessions;
    uint64_t        cSessionsReqTimeNext;
    std::atomic_bool cSessionsMakeRequest;

    UserInfoVect    cUsers;

// External sync
    std::atomic_uint_fast32_t eStatus;
    SDL_mutex  *eSyncMutex;


    EventList eEventList;
    int32_t eEventDataSize;
    SDL_mutex  *eEventMutex;
    std::atomic_uint_fast32_t eEventWaitLock;
    std::atomic_uint_fast32_t eEventNextID;

};

};

#endif // ZNDNET_H_INCLUDED
