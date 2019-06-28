#include "zndNet.h"

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

void ZNDNet::Send_Clear()
{
    if (SDL_LockMutex(sendModifyMutex) == 0)
    {
        for (SendingList::iterator it = sendPktList.begin(); it != sendPktList.end(); it = sendPktList.erase(it))
            delete *it;

        SDL_UnlockMutex(sendModifyMutex);
    }
}


}
