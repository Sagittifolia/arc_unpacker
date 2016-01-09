#include "dec/shiina_rio/warc/decompress.h"
#include "algo/pack/zlib.h"
#include "algo/range.h"
#include "err.h"
#include "io/memory_stream.h"
#include "io/msb_bit_reader.h"

using namespace au;
using namespace au::dec::shiina_rio;
using namespace au::dec::shiina_rio::warc;

namespace
{
    class CustomBitReader final : public io::BaseBitReader
    {
    public:
        CustomBitReader(const bstr &input);
        u32 get(const size_t bits) override;
    private:
        void fetch();
    };
}

CustomBitReader::CustomBitReader(const bstr &input) : io::BaseBitReader(input)
{
}

void CustomBitReader::fetch()
{
    if (input_stream->size() - input_stream->tell() >= 4)
    {
        buffer = input_stream->read_le<u32>();
        return;
    }
    while (!input_stream->eof())
    {
        buffer <<= 8;
        buffer |= input_stream->read<u8>();
    }
}

u32 CustomBitReader::get(size_t requested_bits)
{
    u32 value = 0;
    if (bits_available < requested_bits)
    {
        do
        {
            requested_bits -= bits_available;
            const u32 mask = (1ull << bits_available) - 1;
            value |= (buffer & mask) << requested_bits;
            fetch();
            bits_available = 32;
        }
        while (requested_bits > 32);
    }
    bits_available -= requested_bits;
    const u32 mask = (1ull << requested_bits) - 1;
    return value | ((buffer >> bits_available) & mask);
}

static int init_huffman(
    io::IBitReader &bit_reader, u16 nodes[2][512], int &size)
{
    if (!bit_reader.get(1))
        return bit_reader.get(8);
    const auto pos = size;
    if (pos > 511)
        return -1;
    size++;
    nodes[0][pos] = init_huffman(bit_reader, nodes, size);
    nodes[1][pos] = init_huffman(bit_reader, nodes, size);
    return pos;
}

static bstr decode_huffman(const bstr &input, const size_t size_orig)
{
    bstr output(size_orig);
    auto output_ptr = output.get<u8>();
    const auto output_end = output.end<const u8>();
    CustomBitReader bit_reader(input);
    u16 nodes[2][512];
    auto size = 256;
    auto root = init_huffman(bit_reader, nodes, size);
    while (output_ptr < output_end)
    {
        auto byte = root;
        while (byte >= 256 && byte <= 511)
            byte = nodes[bit_reader.get(1)][byte];
        *output_ptr++ = byte;
    }
    return output;
}

bstr warc::decompress_yh1(
    const bstr &input, const size_t size_orig, const bool encrypted)
{
    bstr transient(input);
    if (encrypted)
    {
        const u32 key32 = 0x6393528E;
        const u16 key16 = 0x4B4D;
        for (auto i : algo::range(transient.size() / 4))
            transient.get<u32>()[i] ^= key32 ^ key16;
    }
    return decode_huffman(transient, size_orig);
}

bstr warc::decompress_ypk(
    const bstr &input, const size_t size_orig, const bool encrypted)
{
    bstr transient(input);
    if (encrypted)
    {
        const u16 key16 = 0x4B4D;
        const u32 key32 = (key16 | (key16 << 16)) ^ 0xFFFFFFFF;
        size_t i = 0;
        while (i < transient.size() / 4)
            transient.get<u32>()[i++] ^= key32;
        i *= 4;
        while (i < transient.size())
            transient[i++] ^= key32;
    }
    return algo::pack::zlib_inflate(transient);
}

bstr warc::decompress_ylz(
    const bstr &input, const size_t size_orig, const bool encrypted)
{
    throw err::NotSupportedError("YLZ decompression not implemented");
}
