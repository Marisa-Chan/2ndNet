#include "zndNet.h"
#include "zndNetPkt.h"

namespace ZNDNet
{

InRawPktHdr::InRawPktHdr()
{
    flags  = 0;
    fsize  = 0;
    offset = 0;
    seqid  = 0;
    data   = NULL;
    datasz = 0;
}

bool InRawPktHdr::Parse(uint8_t *_data, size_t len)
{
    if (!_data)
        return false; // No data

    flags = _data[HDR_OFF_FLAGS];

    if (flags & PKT_FLAG_SYSTEM)
    {
        if (flags & (~PKT_FLAG_MASK_SYSTEM)) // Only system flags must be setted
            return false;

        if (len < HDR_OFF_SYS_MINSZ)
            return false;

        data = &_data[HDR_OFF_SYS_DATA];
        datasz = len - HDR_OFF_SYS_DATA;
    }
    else
    {
        // Incorrect flags
        if ( (flags & (~PKT_FLAG_MASK_NORMAL)) )
            return false;

        if (len < HDR_OFF_SEQID + 4)
            return false;

        seqid  = readU32( &_data[HDR_OFF_SEQID] );

        if (flags & PKT_FLAG_PART)
        {
            if (len < HDR_OFF_PART_MINSZ)
                return false;

            fsize  = readU32( &_data[HDR_OFF_PART_FSIZE] );
            offset = readU32( &_data[HDR_OFF_PART_OFFSET] );
            data   = &_data[HDR_OFF_PART_DATA];
            datasz = len - HDR_OFF_PART_DATA;
        }
        else
        {
            if (len < HDR_OFF_MINSZ)
                return false;

            offset = 0;
            fsize  = len - HDR_OFF_DATA;
            data   = &_data[HDR_OFF_DATA];
            datasz = fsize;
        }

        if ( offset + datasz > fsize )
            return false;
    }

    return true;
}





InRawPkt::InRawPkt(const UDPpacket *pkt)
{
    if (pkt->len && pkt->data)
    {
        len = pkt->len;
        data = new uint8_t[len];
        memcpy(data, pkt->data, len);
        addr = pkt->address;
    }
    else
    {
        data = NULL;
        len = 0;
        addr.host = 0;
        addr.port = 0;
    }
}

InRawPkt::~InRawPkt()
{
    if (data)
        delete[] data;
}

bool InRawPkt::Parse()
{
    if (!data)
        return false; // No data

    return hdr.Parse(data, len);
}






InPartedPkt::InPartedPkt(const AddrSeq& _ipseq, size_t _len, uint8_t _flags)
{
    ipseq = _ipseq;
    timeout = 0;
    nextOff = 0;
    data = NULL;
    len = 0;
    flags = _flags;

    if (_len)
    {
        data = new uint8_t[_len];
        len = _len;
    }
}

InPartedPkt::~InPartedPkt()
{
    if (data)
        delete[] data;

    InRawList::iterator it = parts.begin();
    while(it != parts.end())
    {
        delete *it;
        it = parts.erase(it);
    }
}

void InPartedPkt::_Insert(InRawPkt *pkt)
{
    memcpy(&data[nextOff], pkt->hdr.data, pkt->hdr.datasz);
    nextOff += pkt->hdr.datasz;
}

bool InPartedPkt::Feed(InRawPkt *pkt, uint64_t timestamp)
{
    if (!pkt || !pkt->hdr.data || pkt->hdr.datasz <= 0 ||
        pkt->hdr.seqid != ipseq.seq ||
        pkt->hdr.offset < nextOff ||
        len < pkt->hdr.offset + pkt->hdr.datasz)
    {
        delete pkt;
        return false;
    }

    if (pkt->hdr.offset == nextOff)
    {
        timeout = timestamp + TIMEOUT_PKT;

        _Insert(pkt);
        delete pkt;

        if (!parts.empty())
        {
            InRawList::iterator it = parts.begin();
            while(it != parts.end())
            {
                if ( (*it)->hdr.offset < nextOff )
                {   //Wrong packet
                    delete *it;
                    it = parts.erase(it);
                }
                else if ( (*it)->hdr.offset == nextOff )
                {
                    _Insert(*it);
                    delete *it;
                    parts.erase(it);
                    it = parts.begin();
                }
                else
                    it++;

            }
        }
    }
    else
    {
        parts.push_back(pkt);
    }

    return nextOff >= len;
}







Pkt::Pkt(InRawPkt *pk, NetUser *usr)
{
    addr = pk->addr;
    _raw_data = pk->data;
    _raw_len = pk->len;
    flags = pk->hdr.flags;
    seqid = pk->hdr.seqid;
    data = pk->hdr.data;
    datasz = pk->hdr.datasz;
    user = usr;

    pk->data = NULL;
    pk->len = 0;

    delete pk;
}

Pkt::Pkt(InPartedPkt *pk, NetUser *usr)
{
    addr = pk->ipseq.addr;
    _raw_data = pk->data;
    _raw_len = pk->len;
    flags = pk->flags;
    seqid = pk->ipseq.seq;
    data = pk->data;
    datasz = pk->len;
    user = usr;

    pk->data = NULL;
    pk->len = 0;

    delete pk;
}

Pkt::~Pkt()
{
    if (_raw_data)
        delete[] _raw_data;
}




SendingData::SendingData(const AddrSeq &_addr, RefData *_data, uint8_t _flags)
{
    addr = _addr;
    pdata = _data;
    flags = _flags;

    sended = 0;
    tr_cnt = 0;
    timeout = 0;

    schnl = PKT_NO_CHANNEL;

    if (pdata)
        pdata->refcnt++;
}

SendingData::SendingData(const IPaddress &_addr, uint32_t _seq, RefData *_data, uint8_t _flags)
{
    addr.set(_addr, _seq);
    pdata = _data;
    flags = _flags;

    sended = 0;
    tr_cnt = 0;
    timeout = 0;

    schnl = PKT_NO_CHANNEL;

    if (pdata)
        pdata->refcnt++;
}

SendingData::~SendingData()
{
    if (pdata)
    {
        pdata->refcnt--;
        if (pdata->refcnt == 0)
            delete pdata;
    }
}

void SendingData::SetChannel(uint32_t userIDX, uint32_t userChnl)
{
    if (userIDX < ZNDNET_USER_MAX && userChnl < ZNDNET_USER_SCHNLS)
        schnl = userIDX * ZNDNET_USER_SCHNLS + userChnl;
    else
        schnl = PKT_NO_CHANNEL;
}


}
