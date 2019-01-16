#include "zndNet.h"
#include "zndNetPkt.h"

namespace ZNDNet
{



int ZNDNet::_SendThread(void *data)
{
    ZNDNet *_this = (ZNDNet *)data;

    uint8_t *sendBuffer;
    uint32_t *syncThings;
    uint32_t loop = 1;
    UDPpacket pkt;

    if (_this)
    {
        sendBuffer = new uint8_t[ZNDNET_BUFF_SIZE];
        syncThings = new uint32_t[ZNDNET_SYNC_CHANNELS + 1]; //extra channel for incorrect channels
        memset(syncThings, 0, (ZNDNET_SYNC_CHANNELS + 1) * sizeof(uint32_t));

        pkt.data = sendBuffer;
        pkt.maxlen = ZNDNET_BUFF_SIZE;
        pkt.channel = -1;

        while (!_this->sendThreadEnd)
        {
            SendingList::iterator it = _this->sendPktList.begin();
            size_t sendedBytes = 0;

            while(it != _this->sendPktList.end() && !_this->sendThreadEnd)
            {
                if (sendedBytes >= ZNDNET_TUNE_SEND_MAXDATA)
                {
                    sendedBytes = 0;
                    SDL_Delay(ZNDNET_TUNE_SEND_DELAY);
                }

                SendingData* dta = (*it);
                if (dta->schnl != PKT_NO_CHANNEL && dta->schnl > ZNDNET_SYNC_CHANNELS)
                    dta->schnl = ZNDNET_SYNC_CHANNELS;

                if (dta->flags & PKT_FLAG_SYSTEM)
                {
                    if ( dta->pdata->datasz <= (ZNDNET_PKT_MAXSZ - HDR_OFF_SYS_DATA) )
                    {
                        pkt.address = dta->addr.addr;
                        pkt.len = dta->pdata->datasz + HDR_OFF_SYS_DATA;
                        pkt.maxlen = pkt.len;
                        sendBuffer[HDR_OFF_FLAGS] = PKT_FLAG_SYSTEM;
                        memcpy(&sendBuffer[HDR_OFF_SYS_DATA], dta->pdata->data, dta->pdata->datasz);

                        //printf("SEND: System sended\n");
                        sendedBytes += pkt.len;
                        SDLNet_UDP_Send(_this->sock, -1, &pkt);
                    }

                    if ( SDL_LockMutex(_this->sendPktListMutex) == 0 )
                    {
                        delete dta;
                        it = _this->sendPktList.erase(it);
                        SDL_UnlockMutex(_this->sendPktListMutex);
                    }
                }
                else
                {
                    bool async = (dta->schnl == PKT_NO_CHANNEL) || (dta->flags & PKT_FLAG_ASYNC);

                    if ( async || syncThings[ dta->schnl ] != loop )
                    {
                        if (!async)
                            syncThings[ dta->schnl ] = loop; // Mark this channel has sent data on this loop

                        if ( dta->pdata->datasz <= (ZNDNET_PKT_MAXSZ - HDR_OFF_DATA) ) //Normal MSG by one piece
                        {
                            pkt.address = dta->addr.addr;
                            pkt.len = dta->pdata->datasz + HDR_OFF_DATA;
                            pkt.maxlen = pkt.len;

                            sendBuffer[HDR_OFF_FLAGS] = dta->flags & (PKT_FLAG_GARANT | PKT_FLAG_ASYNC);
                            writeU32(dta->addr.seq, &sendBuffer[HDR_OFF_SEQID]);

                            memcpy(&sendBuffer[HDR_OFF_DATA], dta->pdata->data, dta->pdata->datasz);

                            //printf("SEND: Normal sended\n");
                            sendedBytes += pkt.len;
                            SDLNet_UDP_Send(_this->sock, -1, &pkt);

                            if ( SDL_LockMutex(_this->sendPktListMutex) == 0 )
                            {
                                it = _this->sendPktList.erase(it);
                                SDL_UnlockMutex(_this->sendPktListMutex);
                            }

                            if (dta->flags & PKT_FLAG_GARANT)
                            {
                                dta->timeout = TIMEOUT_PKT + _this->ttime.GetTicks();

                                if ( SDL_LockMutex(_this->confirmPktListMutex)  == 0 )
                                {
                                    _this->confirmPktList.push_back(dta);
                                    SDL_UnlockMutex(_this->confirmPktListMutex);
                                }

                            }
                            else
                                delete dta;
                        }
                        else if ( dta->sended < dta->pdata->datasz ) // Multipart
                        {
                            uint32_t pktdatalen = dta->pdata->datasz - dta->sended;

                            if ( pktdatalen > (ZNDNET_PKT_MAXSZ - HDR_OFF_PART_DATA) )
                                pktdatalen = (ZNDNET_PKT_MAXSZ - HDR_OFF_PART_DATA);

                            pkt.address = dta->addr.addr;
                            pkt.len = HDR_OFF_PART_DATA + pktdatalen;
                            pkt.maxlen = pkt.len;

                            sendBuffer[HDR_OFF_FLAGS] = PKT_FLAG_PART | (dta->flags & PKT_FLAG_MASK_NORMAL);

                            writeU32(dta->addr.seq, &sendBuffer[HDR_OFF_SEQID]);

                            writeU32(dta->pdata->datasz, &sendBuffer[HDR_OFF_PART_FSIZE]);
                            writeU32(dta->sended, &sendBuffer[HDR_OFF_PART_OFFSET]);

                            memcpy(&sendBuffer[HDR_OFF_PART_DATA], &dta->pdata->data[dta->sended], pktdatalen);

                            //printf("SEND: Multipart(%d) %d/%d sended\n", dta->addr.seq, pktdatalen, (int)dta->pdata->datasz);
                            sendedBytes += pkt.len;
                            SDLNet_UDP_Send(_this->sock, -1, &pkt);

                            dta->sended += pktdatalen;

                            if (dta->sended >= dta->pdata->datasz)
                            {
                                if ( SDL_LockMutex(_this->sendPktListMutex)  == 0 )
                                {
                                    it = _this->sendPktList.erase(it);
                                    SDL_UnlockMutex(_this->sendPktListMutex);
                                }

                                if (dta->flags & PKT_FLAG_GARANT)
                                {
                                    dta->timeout = TIMEOUT_PKT + _this->ttime.GetTicks();

                                    if ( SDL_LockMutex(_this->confirmPktListMutex)  == 0 )
                                    {
                                        _this->confirmPktList.push_back(dta);
                                        SDL_UnlockMutex(_this->confirmPktListMutex);
                                    }
                                }
                                else
                                    delete dta;
                            }
                            else
                            {
                                //SDL_LockMutex(_this->sendPktListMutex);
                                it++;
                                //SDL_UnlockMutex(_this->sendPktListMutex);
                            }
                        }
                        else
                        {
                            if ( SDL_LockMutex(_this->sendPktListMutex)  == 0 )
                            {
                                it = _this->sendPktList.erase(it);
                                SDL_UnlockMutex(_this->sendPktListMutex);

                                delete dta;
                            }
                        }
                    }
                    else
                        it++;

                }
            }

            loop++;
            SDL_Delay(ZNDNET_TUNE_SEND_DELAY);
        }

        delete[] sendBuffer;
        delete[] syncThings;
    }

    return 0;
}


}
