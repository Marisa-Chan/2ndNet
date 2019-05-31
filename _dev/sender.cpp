#include "zndNet.h"
#include "zndNetPkt.h"

namespace ZNDNet
{

void ZNDNet::Send_PushData(SendingData *data)
{
    if (!data)
        return;

    if (SDL_LockMutex(sendPktListMutex) == 0)
    {
        sendPktList.push_back(data);
        SDL_UnlockMutex(sendPktListMutex);
    }
}

void ZNDNet::Send_RetryData(SendingData *data, size_t from, size_t to, bool decr)
{
    if (!data)
        return;

    if (decr)
    {
        if (data->tr_cnt > 0)
            data->tr_cnt--;
    }


    if (from >= data->pdata->size() || to > data->pdata->size() || (to != 0 && to < from) )
    {
        delete data;
        return;
    }

    data->sended = from;
    data->retryUpTo = to;

    Send_PushData(data);
}

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

        if (_this->mode == MODE_CLIENT)
        {
            syncThings = new uint32_t[ZNDNET_USER_SCHNLS + 1]; // +extra channel for incorrect channels
            memset(syncThings, 0, (ZNDNET_USER_SCHNLS + 1) * sizeof(uint32_t));
        }
        else
        {
            syncThings = new uint32_t[ZNDNET_SYNC_CHANNELS + 1]; // +extra channel for incorrect channels
            memset(syncThings, 0, (ZNDNET_SYNC_CHANNELS + 1) * sizeof(uint32_t));
        }

        pkt.data = sendBuffer;
        pkt.maxlen = ZNDNET_BUFF_SIZE;
        pkt.channel = -1;

        while (!_this->threadsEnd)
        {
            if (SDL_LockMutex(_this->sendModifyMutex) == 0)
            {
                SendingList::iterator it = _this->sendPktList.begin();
                size_t sendedBytes = 0;

                while(it != _this->sendPktList.end() && !_this->threadsEnd)
                {
                    if (sendedBytes >= ZNDNET_TUNE_SEND_MAXDATA)
                    {
                        sendedBytes = 0;
                        SDL_Delay(ZNDNET_TUNE_SEND_DELAY);
                    }

                    SendingData* dta = (*it);

                    if (dta->flags & PKT_FLAG_SYSTEM)
                    {
                        if ( dta->pdata->size() <= (ZNDNET_PKT_MAXSZ - HDR_OFF_SYS_DATA) )
                        {
                            pkt.address = dta->addr.addr;
                            pkt.len = dta->pdata->size() + HDR_OFF_SYS_DATA;
                            pkt.maxlen = pkt.len;
                            sendBuffer[HDR_OFF_FLAGS] = PKT_FLAG_SYSTEM;
                            dta->pdata->copy(&sendBuffer[HDR_OFF_SYS_DATA]);

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
                        uint32_t queueID;

                        if (_this->mode == MODE_CLIENT)
                        {
                            queueID = dta->uchnl;

                            if (queueID == PKT_CHNL_NOT_SET || queueID > ZNDNET_USER_SCHNLS)
                                queueID = ZNDNET_USER_SCHNLS;
                        }
                        else
                        {
                            queueID = dta->schnl;

                            if (queueID == PKT_NO_CHANNEL || queueID > ZNDNET_SYNC_CHANNELS)
                                queueID = ZNDNET_SYNC_CHANNELS;
                        }

                        bool async = (dta->schnl == PKT_NO_CHANNEL) || (dta->flags & PKT_FLAG_ASYNC);

                        if ( async || syncThings[ queueID ] != loop )
                        {
                            if (!async)
                                syncThings[ queueID ] = loop; // Mark this channel has sent data on this loop

                            if ( dta->pdata->size() <= (ZNDNET_PKT_MAXSZ - HDR_OFF_DATA) ) //Normal MSG by one piece
                            {
                                pkt.address = dta->addr.addr;
                                pkt.len = dta->pdata->size() + HDR_OFF_DATA;
                                pkt.maxlen = pkt.len;

                                sendBuffer[HDR_OFF_FLAGS] = dta->flags & (PKT_FLAG_GARANT | PKT_FLAG_ASYNC);
                                writeU32(dta->addr.seq, &sendBuffer[HDR_OFF_SEQID]);
                                sendBuffer[HDR_OFF_CHANNEL] = dta->uchnl;

                                dta->pdata->copy(&sendBuffer[HDR_OFF_DATA]);

                                sendedBytes += pkt.len;
                                SDLNet_UDP_Send(_this->sock, -1, &pkt);

                                if ( SDL_LockMutex(_this->sendPktListMutex) == 0 )
                                {
                                    it = _this->sendPktList.erase(it);
                                    SDL_UnlockMutex(_this->sendPktListMutex);
                                }

                                if (dta->flags & PKT_FLAG_GARANT)
                                {
                                    if (dta->tr_cnt)
                                    {
                                        dta->timeout = TIMEOUT_GARANT + _this->ttime.GetTicks();

                                        if ( SDL_LockMutex(_this->confirmQueueMutex) == 0 )
                                        {
                                            _this->confirmQueue.push_back(dta);
                                            SDL_UnlockMutex(_this->confirmQueueMutex);
                                        }
                                    }
                                    else
                                        delete dta;
                                }
                                else
                                    delete dta;
                            }
                            else if ( dta->sended < dta->pdata->size() ) // Multipart
                            {
                                uint32_t UpTo = dta->pdata->size();

                                if (dta->retryUpTo)
                                    UpTo = dta->retryUpTo; //Set upper border, if some part not receieved

                                uint32_t pktdatalen = UpTo - dta->sended;  //How many bytes we needed to send

                                if ( pktdatalen > (ZNDNET_PKT_MAXSZ - HDR_OFF_PART_DATA) )
                                    pktdatalen = (ZNDNET_PKT_MAXSZ - HDR_OFF_PART_DATA); //Maximum size we can handle by 1 packet

                                pkt.address = dta->addr.addr;
                                pkt.len = HDR_OFF_PART_DATA + pktdatalen;
                                pkt.maxlen = pkt.len;

                                sendBuffer[HDR_OFF_FLAGS] = PKT_FLAG_PART | (dta->flags & PKT_FLAG_MASK_NORMAL);
                                writeU32(dta->addr.seq, &sendBuffer[HDR_OFF_SEQID]);
                                sendBuffer[HDR_OFF_CHANNEL] = dta->uchnl;

                                writeU32(dta->pdata->size(), &sendBuffer[HDR_OFF_PART_FSIZE]);
                                writeU32(dta->sended, &sendBuffer[HDR_OFF_PART_OFFSET]);

                                dta->pdata->copy(&sendBuffer[HDR_OFF_PART_DATA], dta->sended, pktdatalen);

                                sendedBytes += pkt.len;
                                SDLNet_UDP_Send(_this->sock, -1, &pkt);

                                dta->sended += pktdatalen;

                                if (dta->sended >= UpTo) //We at end of data
                                {
                                    if ( SDL_LockMutex(_this->sendPktListMutex) == 0 )
                                    {
                                        it = _this->sendPktList.erase(it);
                                        SDL_UnlockMutex(_this->sendPktListMutex);
                                    }

                                    if (dta->flags & PKT_FLAG_GARANT)
                                    {
                                        if (dta->tr_cnt)
                                        {
                                            dta->timeout = TIMEOUT_GARANT_MULTIPART + _this->ttime.GetTicks();

                                            if ( SDL_LockMutex(_this->confirmQueueMutex) == 0 )
                                            {
                                                _this->confirmQueue.push_back(dta);
                                                SDL_UnlockMutex(_this->confirmQueueMutex);
                                            }
                                        }
                                        else
                                            delete dta;
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

                SDL_UnlockMutex(_this->sendModifyMutex);
            }

            loop++;
            if (_this->sendPktList.size())
                SDL_Delay(0);
            else
                SDL_Delay(5);
        }

        delete[] sendBuffer;
        delete[] syncThings;
    }

    return 0;
}


void ZNDNet::Send_Clear(const IPaddress &addr)
{
    if (SDL_LockMutex(sendModifyMutex) == 0)
    {
        for (SendingList::iterator it = sendPktList.begin(); it != sendPktList.end(); )
        {
            SendingData* dta = (*it);
            if ( IPCMP(dta->addr.addr, addr) )
            {
                delete dta;
                it = sendPktList.erase(it);
            }
            else
                it++;
        }

        SDL_UnlockMutex(sendModifyMutex);
    }
}


}
