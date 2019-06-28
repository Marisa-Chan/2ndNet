#ifndef ZNDNET_NETUSER_H_INCLUDED
#define ZNDNET_NETUSER_H_INCLUDED

#include <deque>
#include <list>
#include <string>

namespace ZNDNet
{

struct NetUser
{
    enum
    {
        STATUS_DISCONNECTED     = 0,
        STATUS_CONNECTING       = 1,
        STATUS_CONNECTED        = 2
    };

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


    NetUser()
    {
        ID = 0;
        name = "";
        addr.host = 0;
        addr.port = 0;
        sesID = 0;
        status = STATUS_DISCONNECTED;

        pingTime = 0;
        pingSeq = 0;
        pongTime = 0;
        pongSeq = 0;

        latence = 0;
        seqid = 0;

        __idx = -1;
    };

    inline bool IsOnline()
    {
        return status == NetUser::STATUS_CONNECTED;
    };

    inline uint32_t GetSeq()
    {
        return seqid++;
    }
};

typedef std::list<NetUser *> NetUserList;
typedef std::deque<NetUser *> NetUserQueue;

};

#endif
