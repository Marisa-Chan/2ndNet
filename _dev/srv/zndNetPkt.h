#ifndef ZNDNETPKT_H_INCLUDED
#define ZNDNETPKT_H_INCLUDED

namespace ZNDNet
{

enum PKT_HANDSHAKE
{
    PKT_HANDSHAKE_SERV_SZ   = 1,
    PKT_HANDSHAKE_NAME_SZ   = 2,
    PKT_HANDSHAKE_DATA      = 3 // Offset where data stored
};

enum PKT_CONNECTED
{
    PKT_CONNECTED_UID       = 1, // 64 bit
    PKT_CONNECTED_NAME_SZ   = 9, // 8 bit
    PKT_CONNECTED_NAME      = 10 // Offset where data stored
};

};

#endif // ZNDNETPKT_H_INCLUDED
