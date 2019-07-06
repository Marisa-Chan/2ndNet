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


    void SendLeaderStatus(NetUser *usr, bool lead);

    void DisconnectUser(NetUser *usr);

    NetSession *SessionFind(uint64_t _ID);
    NetSession *SessionFind(const std::string &name);
    void SessionDelete(uint64_t ID);
    void SessionDisconnectAllUsers(NetSession *ses, uint8_t type);

    void DoSessionUserJoin(NetUser *usr, NetSession *ses);
    void SessionUserLeave(NetUser *usr, uint8_t type = 0);
    void UserToLobby(NetUser *usr, uint8_t type);
    void SessionListUsers(NetUser *usr);

    RefData *SessionErr(uint8_t code);
    void SessionErrSend(NetUser *usr, uint8_t code);


    RefData *USRDataGenGamesList();
    RefData *SYSDataGenSesLeave(int8_t type);



// Data
public:
protected:
    NetUsersPack users;

    NetSession    sLobby;
    NetSessionMap sessions;
};



};

#endif
