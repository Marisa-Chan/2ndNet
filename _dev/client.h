#ifndef ZNDNET_CLIENT_H_INCLUDED
#define ZNDNET_CLIENT_H_INCLUDED

#include <vector>
#include <string>
#include <atomic>

namespace ZNDNet
{

struct SessionInfo
{
    uint64_t    ID;
    std::string name;
    bool        pass;
    uint32_t    players;
    uint32_t    max_players;

    SessionInfo& operator= (const SessionInfo& x)
    {
        ID = x.ID;
        name = x.name;
        pass = x.pass;
        players = x.players;
        max_players = x.max_players;
        return *this;
    };
};
typedef std::vector<SessionInfo> SessionInfoVect;


struct UserInfo
{
    uint64_t    ID;
    std::string name;
    bool        lead;

    UserInfo& operator= (const UserInfo& x)
    {
        ID = x.ID;
        name = x.name;
        lead = x.lead;
        return *this;
    };
};
typedef std::vector<UserInfo> UserInfoVect;






class ZNDClient: public ZNDNet
{
public:
    ZNDClient(const std::string &servstring);
    ~ZNDClient();

    //Client methods
    void Start(const std::string &name, const IPaddress &addr);
    uint8_t GetStatus();

    void RequestSessions();

    bool GetSessions(SessionInfoVect &dst);
    void CreateSession(const std::string &name, const std::string &pass, uint32_t max_players);
    void JoinSession(uint64_t SID, const std::string &pass);
    void SendDisconnect();

    void LeaveSession();

    void CloseSession(uint32_t closeTime);
    void KickUser(uint64_t _ID);

    bool GetUsers(UserInfoVect &dst);
    bool GetUser(UserInfo &dst, const char *name);
    bool GetUser(UserInfo &dst, uint64_t _ID);

    void SendData(uint64_t to, void *data, uint32_t sz, uint8_t flags = 0, uint8_t channel = CHANNEL_USR);
    void BroadcastData(void *data, uint32_t sz, uint8_t flags = 0, uint8_t channel = CHANNEL_USR);

    void ShowSession(bool show);

    /*Event *Events_Pop();
    void   Events_ClearByType(uint32_t type);
    void   Events_Clear();
    Event *Events_PeekByType(uint32_t type);
    Event *Events_WaitForMsg(uint32_t type, uint32_t time = 0);*/
protected:

    Pkt *Recv_PreparePacket(InRawPkt *pkt);

    void ProcessSystemPkt(Pkt *pkt);
    void ProcessRegularPkt(Pkt *pkt);

    void SendConnect();
    void RequestGamesList();

    void InterprocessUpdate();

    static int _RecvThread(void *data);
    static int _SendThread(void *data);
    static int _UpdateThread(void *data);
// Data
public:

protected:

    std::atomic_uint_fast32_t eStatus;

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
};

};

#endif
