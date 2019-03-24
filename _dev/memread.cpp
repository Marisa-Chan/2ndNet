#include "memread.h"
#include <string.h>

namespace ZNDNet
{

MemReader::MemReader(const void *data, size_t sz):
    _data((const uint8_t *)data),
    _size(sz)
{
    _pos = 0;
}

size_t MemReader::read(void *dst, size_t n)
{
    if (!_data || _pos >= _size)
        return 0;

    size_t toread = _size - _pos;
    if (toread > n)
        toread = n;

    memcpy(dst, &_data[_pos], toread);
    _pos += toread;
    return toread;
}


uint8_t MemReader::readU8()
{
    if (!_data || _pos >= _size)
        return 0;

    return _data[_pos++];
}

uint32_t MemReader::readU32()
{
    if (!_data || _pos >= _size)
        return 0;

    uint32_t rt = 0;
    read(&rt, 4);

    return rt;
}

uint64_t MemReader::readU64()
{
    if (!_data || _pos >= _size)
        return 0;

    uint64_t rt = 0;
    read(&rt, 8);

    return rt;
}

size_t MemReader::readStr(std::string &str, size_t n)
{
    if (!_data || _pos >= _size)
        return 0;

    size_t toread = _size - _pos;
    if (toread > n)
        toread = n;

    str.assign((const char *)&_data[_pos], toread);

    _pos += toread;

    return toread;
}

size_t MemReader::readSzStr(std::string &str)
{
    if (!_data || _pos >= _size)
        return 0;

    uint8_t n = _data[_pos];
    _pos++;

    int32_t toread = _size - _pos;
    if (toread > n)
        toread = n;

    if (toread > 0)
        str.assign((const char *)&_data[_pos], toread);

    _pos += toread;

    return toread;
}
size_t    readSzStr(std::string &str);

size_t MemReader::seek(size_t nbytes)
{
    if (!_data)
        return 0;

    _pos = nbytes;

    if (_pos > _size)
        _pos = _size;

    return _pos;
}

size_t MemReader::skip(int32_t nbytes)
{
    if (!_data)
        return 0;

    if ( _pos + nbytes < 0 )
        _pos = 0;
    else
        _pos += nbytes;

    if (_pos > _size)
        _pos = _size;

    return _pos;
}

};
