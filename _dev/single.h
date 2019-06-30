#ifndef ZNDNET_SINGLE_H_INCLUDED
#define ZNDNET_SINGLE_H_INCLUDED


namespace ZNDNet
{

class ZNDSingle: public ZNDNet
{
public:
    ZNDSingle(const std::string &servstring, const std::string &name, const std::string &pass, uint32_t max_players);
    ~ZNDSingle();

    void Start(uint16_t port);

    void ShowSession(bool show);
    void CloseSession(uint32_t closeTime);

    void KickUser(uint64_t _ID);

    bool GetUsers(UserInfoVect &dst);
    bool GetUser(UserInfo &dst, const char *name);
    bool GetUser(UserInfo &dst, uint64_t _ID);

    void SendData(uint64_t to, void *data, uint32_t sz, uint8_t flags = 0, uint8_t channel = CHANNEL_USR);
    void BroadcastData(void *data, uint32_t sz, uint8_t flags = 0, uint8_t channel = CHANNEL_USR);

protected:

// Data
public:

protected:
    NetUser     ME;
    NetUsersPack users;

    NetSession  session;
};

}

#endif
