#include <string>
//Simple reader
namespace ZNDNet
{


class MemReader
{
public:
    uint8_t   readU8();
    uint32_t  readU32();
    uint64_t  readU64();
    size_t    readStr(std::string &str, size_t n);
    size_t    readSzStr(std::string &str);
    size_t    read(void *dst, size_t n);

    size_t    seek(size_t nbytes);
    size_t    skip(int32_t nbytes);
    size_t    tell() { return _pos; };
    size_t    size() { return _size; };

    MemReader(const void *data, size_t sz);

protected:
    size_t   _pos;
    const uint8_t * const _data;
    const size_t _size;
};

};
