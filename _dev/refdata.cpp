#include "zndNet.h"

namespace ZNDNet
{

RefDataStatic::RefDataStatic(uint8_t *data, size_t sz)
{
    _data = new uint8_t[sz];
    memcpy(_data, data, sz);

    _datasz = sz;
}

RefDataStatic::RefDataStatic(size_t sz)
{
    _data = new uint8_t[sz];
    _datasz = sz;
}

RefDataStatic::~RefDataStatic()
{
    if (_data)
        delete[] _data;
}

void RefDataStatic::copy(void *dst, size_t pos, size_t nbytes)
{
    if (!dst || pos + nbytes > _datasz)
        return;

    memcpy(dst, &_data[pos], nbytes);
}



RefDataWStream::RefDataWStream(uint8_t *data, size_t sz, uint32_t blocksize):
    _blksize(blocksize)
{
    write(data, sz);
}

RefDataWStream::RefDataWStream(uint32_t blocksize):
    _blksize(blocksize)
{
}

RefDataWStream::~RefDataWStream()
{
    for(_tBlockList::iterator it = _blocks.begin(); it != _blocks.end(); it++)
        delete [] *it;
}

void RefDataWStream::checkfree(size_t nbytes)
{
    size_t free = _blksize * _blocks.size() - _datasz;
    while (free < nbytes)
    {
        _blocks.push_back(new uint8_t[_blksize]);
        free += _blksize;
    }
}

void RefDataWStream::write(const void *_src, size_t nbytes)
{
    if (!_src)
        return;

    checkfree(nbytes);

    const uint8_t *src = (const uint8_t *)_src;

    while(nbytes > 0)
    {
        size_t blkid  = _datasz / _blksize;
        size_t blkpos = _datasz % _blksize;

        size_t blkfree = _blksize - blkpos;
        size_t tocopy = nbytes;

        if (tocopy > blkfree)
            tocopy = blkfree;

        uint8_t *dst = _blocks[blkid];
        memcpy(&dst[blkpos], src, tocopy);

        src += tocopy;
        _datasz += tocopy;
        nbytes -= tocopy;
    }
}


void RefDataWStream::copy(void *_dst, size_t pos, size_t nbytes)
{
    if (!_dst || pos + nbytes > _datasz)
        return;

    uint8_t *dst = (uint8_t *)_dst;

    while(nbytes > 0)
    {
        size_t blkid  = pos / _blksize;
        size_t blkpos = pos % _blksize;

        size_t blkafter = _blksize - blkpos;
        size_t tocopy = nbytes;

        if (tocopy > blkafter)
            tocopy = blkafter;

        uint8_t *src = _blocks[blkid];
        memcpy(dst, &src[blkpos], tocopy);

        dst += tocopy;
        nbytes -= tocopy;
    }
}

void RefDataWStream::writeU8(uint8_t bt)
{
    checkfree(1);

    size_t blkid  = _datasz / _blksize;
    size_t blkpos = _datasz % _blksize;

    _blocks[blkid][blkpos] = bt;

    _datasz++;
}

void RefDataWStream::writeU32(uint32_t dw)
{
    /*uint8_t bytes[4];
    bytes[0] = dw & 0xFF;
    bytes[1] = (dw >> 8) & 0xFF;
    bytes[2] = (dw >> 16) & 0xFF;
    bytes[3] = (dw >> 24) & 0xFF;

    write(bytes, 4);*/
    write(&dw, 4);
}

void RefDataWStream::writeU64(uint64_t qw)
{
    write(&qw, 8);
}

void RefDataWStream::writeStr(const std::string &str)
{
    write(str.c_str(), str.size());
}

void RefDataWStream::writeSzStr(const std::string &str)
{
    writeU8(str.size());
    write(str.c_str(), str.size());
}

};
