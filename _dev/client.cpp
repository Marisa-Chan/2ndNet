#include "zndNet.h"
#include "zndNetPkt.h"
#include "memread.h"
#include "test/crc32.h"

namespace ZNDNet
{


void ZNDNet::Cli_ProcessSystemPkt(Pkt* pkt)
{
    if (!pkt)
        return;

    if (pkt->datasz < 2 )
        return;

    switch(pkt->data[0])
    {
        case SYS_MSG_CONNECTED:
            {
                if (cME.status == STATUS_CLI_CONNECTING)
                {
                    MemReader rdr(pkt->data, pkt->datasz);
                    rdr.seek(1);
                    uint64_t ID = rdr.readU64();
                    std::string nm;
                    rdr.readSzStr(nm);
                    cME.name = nm;
                    cME.ID = ID;
                    cME.status = STATUS_CONNECTED;

                    printf("\t\tCONNECTED %" PRIx64 " %s\n", ID, nm.c_str() );
                }
            }
            break;
        default:
            break;
    }
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
                if (pkt->flags & PKT_FLAG_SYSTEM)
                {
                    _this->Cli_ProcessSystemPkt(pkt);
                }
                else
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


void ZNDNet::StartClient(const std::string &name, const IPaddress &addr)
{
    mode = MODE_CLIENT;
    sock = SDLNet_UDP_Open(0);

    cServAddress = addr;
    cME.name = name;
    cME.status = STATUS_CLI_CONNECTING;

    recvThreadEnd = false;
    recvThread = SDL_CreateThread(_RecvThread, "", this);

    sendThreadEnd = false;
    sendThread = SDL_CreateThread(_SendThread, "", this);

    updateThreadEnd = false;
    updateThread = SDL_CreateThread(_UpdateClientThread, "", this);

    Cli_SendConnect();
}

void ZNDNet::Cli_SendConnect()
{
    RefDataWStream *rfdata = RefDataWStream::create();
    rfdata->writeU8(SYS_MSG_HANDSHAKE);
    rfdata->writeU8(servString.size());
    rfdata->writeU8(cME.name.size());
    rfdata->writeStr(servString);
    rfdata->writeStr(cME.name);

    Send_PushData( new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM));
}

};
