#ifndef ZNDNET_SERVER_H_INCLUDED
#define ZNDNET_SERVER_H_INCLUDED

#include <vector>
#include <string>
#include <atomic>

namespace ZNDNet
{


class ZNDServer: public ZNDNet
{
public:
    ZNDServer(const std::string &servstring);
    ~ZNDServer();

    void Start(uint16_t port);

protected:
    Pkt *Recv_PreparePacket(InRawPkt *pkt);
    void ProcessSystemPkt(Pkt *pkt);
    void ProcessRegularPkt(Pkt *pkt);

    void InterprocessUpdate();

    static int _RecvThread(void *data);
    static int _SendThread(void *data);
    static int _UpdateThread(void *data);



    void InitUsers();

    void SendConnected(NetUser *usr);

    void SendLeaderStatus(NetUser *usr, bool lead);
    void SendSessionJoin(NetUser *usr, NetSession *ses, bool leader);

    void SendPing(NetUser *usr);
    void SendDisconnect(NetUser *usr);

    void DisconnectUser(NetUser *usr, bool free);

    NetSession *SessionFind(uint64_t _ID);
    NetSession *SessionFind(const std::string &name);
    void SessionDelete(uint64_t ID);
    void SessionDisconnectAllUsers(NetSession *ses, uint8_t type);
    void SessionBroadcast(NetSession *ses, RefData *dat, uint8_t flags, uint8_t chnl = 0, NetUser *from = NULL);
    void DoSessionUserJoin(NetUser *usr, NetSession *ses);
    void SessionUserLeave(NetUser *usr, uint8_t type = 0);
    void UserToLobby(NetUser *usr, uint8_t type);
    void SessionListUsers(NetUser *usr);

    RefData *SessionErr(uint8_t code);
    void SessionErrSend(NetUser *usr, uint8_t code);

    RefData *USRDataGenUserLeave(NetUser *usr, uint8_t type);
    RefData *USRDataGenUserJoin(NetUser *usr);
    RefData *USRDataGenGamesList();
    RefData *SYSDataGenSesLeave(int8_t type);

    NetUser *FindUserByIP(const IPaddress &addr);
    NetUser *FindUserByID(uint64_t ID);

    NetUser *AllocUser();
    void FreeUser(NetUser *usr);

    NetUser *FindUserByName(const std::string &_name);
    void SendConnErr(const IPaddress &addr, uint8_t type);





// Data
public:
protected:
    NetUser     *sUsers;
    NetUserList  sActiveUsers;
    NetUserQueue sFreeUsers;

    NetSession    sLobby;
};



};

#endif
