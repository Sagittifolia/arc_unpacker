﻿#include "dec/microsoft/exe_archive_decoder.h"
#include "algo/format.h"
#include "algo/locale.h"
#include "algo/range.h"
#include "err.h"

using namespace au;
using namespace au::dec::microsoft;

namespace
{
    struct ArchiveEntryImpl final : dec::ArchiveEntry
    {
        size_t offset;
        size_t size;
    };

    struct DosHeader final
    {
        DosHeader(io::IStream &input_stream);

        bstr magic;
        u16 e_cblp;
        u16 e_cp;
        u16 e_crlc;
        u16 e_cparhdr;
        u16 e_minalloc;
        u16 e_maxalloc;
        u16 e_ss;
        u16 e_sp;
        u16 e_csum;
        u16 e_ip;
        u16 e_cs;
        u16 e_lfarlc;
        u16 e_ovno;
        u16 e_oemid;
        u16 e_oeminfo;
        u32 e_lfanew;
    };

    struct ImageOptionalHeader final
    {
        ImageOptionalHeader(io::IStream &input_stream);

        u16 magic;
        u8 major_linker_version;
        u8 minor_linker_version;
        u32 size_of_code;
        u32 size_of_initialized_data;
        u32 size_of_uninitialized_data;
        u32 address_of_entry_point;
        u32 base_of_code;
        u32 base_of_data;
        u32 image_base;
        u32 section_alignment;
        u32 file_alignment;
        u16 major_operating_system_version;
        u16 minor_operating_system_version;
        u16 major_image_version;
        u16 minor_image_version;
        u16 major_subsystem_version;
        u16 minor_subsystem_version;
        u32 win32_version_value;
        u32 size_of_image;
        u32 size_of_headers;
        u32 checksum;
        u16 subsystem;
        u16 dll_characteristics;
        u64 size_of_stack_reserve;
        u64 size_of_stack_commit;
        u64 size_of_heap_reserve;
        u64 size_of_heap_commit;
        u32 loader_flags;
        u32 number_of_rva_and_sizes;
    };

    struct ImageFileHeader final
    {
        ImageFileHeader(io::IStream &input_stream);

        u16 machine;
        u16 number_of_sections;
        u32 timestamp;
        u32 pointer_to_symbol_table;
        u32 number_of_symbols;
        u16 size_of_optional_header;
        u16 characteristics;
    };

    struct ImageNtHeader final
    {
        ImageNtHeader(io::IStream &input_stream);

        u32 signature;
        ImageFileHeader file_header;
        ImageOptionalHeader optional_header;
    };

    struct ImageDataDir final
    {
        ImageDataDir(io::IStream &input_stream);

        u32 virtual_address;
        u32 size;
    };

    struct ImageSectionHeader final
    {
        ImageSectionHeader(io::IStream &input_stream);

        std::string name;
        u32 virtual_size;
        u32 physical_address;
        u32 virtual_address;
        u32 size_of_raw_data;
        u32 pointer_to_raw_data;
        u32 pointer_to_relocations;
        u32 pointer_to_line_numbers;
        u16 number_of_relocations;
        u16 number_of_line_numbers;
        u32 characteristics;
    };

    struct ImageResourceDir final
    {
        ImageResourceDir(io::IStream &input_stream);

        u32 characteristics;
        u32 timestamp;
        u16 major_version;
        u16 minor_version;
        u16 number_of_named_entries;
        u16 number_of_id_entries;
    };

    struct ImageResourceDirEntry final
    {
        ImageResourceDirEntry(io::IStream &input_stream);

        u32 offset_to_data;
        bool name_is_string;
        u32 name_offset;
        u32 name;
        u32 id;
        u32 data_is_dir;
    };

    struct ImageResourceDataEntry final
    {
        ImageResourceDataEntry(io::IStream &input_stream);

        u32 offset_to_data;
        u32 size;
        u32 code_page;
    };

    class RvaHelper final
    {
    public:
        RvaHelper(
            u32 file_alignment,
            u32 section_alignment,
            const std::vector<ImageSectionHeader> &sections);

        u32 rva_to_offset(u32 rva) const;

    private:
        const ImageSectionHeader &section_for_rva(u32 rva) const;
        u32 adjust_file_alignment(u32 offset) const;
        u32 adjust_section_alignment(u32 offset) const;

        u32 file_alignment;
        u32 section_alignment;
        const std::vector<ImageSectionHeader> &sections;
    };

    struct ResourceCrawlerArgs final
    {
        ResourceCrawlerArgs(
            const Logger &logger,
            const RvaHelper &rva_helper,
            const size_t base_offset,
            io::IStream &input_stream,
            dec::ArchiveMeta &meta);

        const Logger &logger;
        const RvaHelper &rva_helper;
        const size_t base_offset;
        io::IStream &input_stream;
        dec::ArchiveMeta &meta;
    };

    class ResourceCrawler final
    {
    public:
        static void crawl(const ResourceCrawlerArgs &args);

    private:
        ResourceCrawler(const ResourceCrawlerArgs &args);
        void process_entry(size_t offset, const std::string &path);
        void process_dir(size_t offset, const std::string path = "");
        std::string read_entry_name(const ImageResourceDirEntry &entry);

        const ResourceCrawlerArgs &args;
    };
}

// keep flat hierarchy for unpacked files
static const std::string path_sep = u8"／";

DosHeader::DosHeader(io::IStream &input_stream)
{
    magic      = input_stream.read(2);
    e_cblp     = input_stream.read_le<u16>();
    e_cp       = input_stream.read_le<u16>();
    e_crlc     = input_stream.read_le<u16>();
    e_cparhdr  = input_stream.read_le<u16>();
    e_minalloc = input_stream.read_le<u16>();
    e_maxalloc = input_stream.read_le<u16>();
    e_ss       = input_stream.read_le<u16>();
    e_sp       = input_stream.read_le<u16>();
    e_csum     = input_stream.read_le<u16>();
    e_ip       = input_stream.read_le<u16>();
    e_cs       = input_stream.read_le<u16>();
    e_lfarlc   = input_stream.read_le<u16>();
    e_ovno     = input_stream.read_le<u16>();
    input_stream.skip(2 * 4);
    e_oemid    = input_stream.read_le<u16>();
    e_oeminfo  = input_stream.read_le<u16>();
    input_stream.skip(2 * 10);
    e_lfanew   = input_stream.read_le<u32>();
}

ImageOptionalHeader::ImageOptionalHeader(io::IStream &input_stream)
{
    magic                          = input_stream.read_le<u16>();
    major_linker_version           = input_stream.read<u8>();
    minor_linker_version           = input_stream.read<u8>();
    size_of_code                   = input_stream.read_le<u32>();
    size_of_initialized_data       = input_stream.read_le<u32>();
    size_of_uninitialized_data     = input_stream.read_le<u32>();
    address_of_entry_point         = input_stream.read_le<u32>();
    base_of_code                   = input_stream.read_le<u32>();
    base_of_data                   = input_stream.read_le<u32>();
    image_base                     = input_stream.read_le<u32>();
    section_alignment              = input_stream.read_le<u32>();
    file_alignment                 = input_stream.read_le<u32>();
    major_operating_system_version = input_stream.read_le<u16>();
    minor_operating_system_version = input_stream.read_le<u16>();
    major_image_version            = input_stream.read_le<u16>();
    minor_image_version            = input_stream.read_le<u16>();
    major_subsystem_version        = input_stream.read_le<u16>();
    minor_subsystem_version        = input_stream.read_le<u16>();
    win32_version_value            = input_stream.read_le<u32>();
    size_of_image                  = input_stream.read_le<u32>();
    size_of_headers                = input_stream.read_le<u32>();
    checksum                       = input_stream.read_le<u32>();
    subsystem                      = input_stream.read_le<u16>();
    dll_characteristics            = input_stream.read_le<u16>();
    bool pe64 = magic == 0x20B;
    if (pe64)
    {
        size_of_stack_reserve = input_stream.read_le<u64>();
        size_of_stack_commit  = input_stream.read_le<u64>();
        size_of_heap_reserve  = input_stream.read_le<u64>();
        size_of_heap_commit   = input_stream.read_le<u64>();
    }
    else
    {
        size_of_stack_reserve = input_stream.read_le<u32>();
        size_of_stack_commit  = input_stream.read_le<u32>();
        size_of_heap_reserve  = input_stream.read_le<u32>();
        size_of_heap_commit   = input_stream.read_le<u32>();
    }
    loader_flags = input_stream.read_le<u32>();
    number_of_rva_and_sizes = input_stream.read_le<u32>();
}

ImageFileHeader::ImageFileHeader(io::IStream &input_stream)
{
    machine = input_stream.read_le<u16>();
    number_of_sections = input_stream.read_le<u16>();
    timestamp = input_stream.read_le<u32>();
    pointer_to_symbol_table = input_stream.read_le<u32>();
    number_of_symbols = input_stream.read_le<u32>();
    size_of_optional_header = input_stream.read_le<u16>();
    characteristics = input_stream.read_le<u16>();
}

ImageNtHeader::ImageNtHeader(io::IStream &input_stream) :
    signature(input_stream.read_le<u32>()),
    file_header(input_stream),
    optional_header(input_stream)
{
}

ImageDataDir::ImageDataDir(io::IStream &input_stream)
{
    virtual_address = input_stream.read_le<u32>();
    size = input_stream.read_le<u32>();
}

ImageSectionHeader::ImageSectionHeader(io::IStream &input_stream)
{
    name                    = input_stream.read(8).str();
    virtual_size            = input_stream.read_le<u32>();
    virtual_address         = input_stream.read_le<u32>();
    size_of_raw_data        = input_stream.read_le<u32>();
    pointer_to_raw_data     = input_stream.read_le<u32>();
    pointer_to_relocations  = input_stream.read_le<u32>();
    pointer_to_line_numbers = input_stream.read_le<u32>();
    number_of_relocations   = input_stream.read_le<u16>();
    number_of_line_numbers  = input_stream.read_le<u16>();
    characteristics         = input_stream.read_le<u32>();
}

ImageResourceDir::ImageResourceDir(io::IStream &input_stream)
{
    characteristics         = input_stream.read_le<u32>();
    timestamp               = input_stream.read_le<u32>();
    major_version           = input_stream.read_le<u16>();
    minor_version           = input_stream.read_le<u16>();
    number_of_named_entries = input_stream.read_le<u16>();
    number_of_id_entries    = input_stream.read_le<u16>();
}

ImageResourceDirEntry::ImageResourceDirEntry(io::IStream &input_stream)
{
    // I am ugliness
    name = input_stream.read_le<u32>();
    offset_to_data = input_stream.read_le<u32>();
    id = name;
    name_is_string = (name >> 31) > 0;
    name_offset = name & 0x7FFFFFFF;
    data_is_dir = offset_to_data >> 31;
    offset_to_data &= 0x7FFFFFFF;
}

ImageResourceDataEntry::ImageResourceDataEntry(io::IStream &input_stream)
{
    offset_to_data = input_stream.read_le<u32>();
    size = input_stream.read_le<u32>();
    code_page = input_stream.read_le<u32>();
    input_stream.skip(4);
}

RvaHelper::RvaHelper(
    u32 file_alignment,
    u32 section_alignment,
    const std::vector<ImageSectionHeader> &sections) :
        file_alignment(file_alignment),
        section_alignment(section_alignment),
        sections(sections)
{
}

u32 RvaHelper::rva_to_offset(u32 rva) const
{
    const ImageSectionHeader &section = section_for_rva(rva);
    return rva
        + adjust_file_alignment(section.pointer_to_raw_data)
        - adjust_section_alignment(section.virtual_address);
}

const ImageSectionHeader &RvaHelper::section_for_rva(u32 rva) const
{
    for (auto &section : sections)
    {
        if (rva >= section.virtual_address
        && rva < (section.virtual_address + section.virtual_size))
        {
            return section;
        }
    }
    throw err::CorruptDataError("Section not found");
}

u32 RvaHelper::adjust_file_alignment(u32 offset) const
{
    return file_alignment < 0x200 ? offset : (offset / 0x200) * 0x200;
}

u32 RvaHelper::adjust_section_alignment(u32 offset) const
{
    u32 fixed_alignment = section_alignment < 0x1000
        ? file_alignment
        : section_alignment;
    if (fixed_alignment && (offset % fixed_alignment))
        return fixed_alignment * (offset / fixed_alignment);
    return offset;
}

ResourceCrawlerArgs::ResourceCrawlerArgs(
    const Logger &logger,
    const RvaHelper &helper,
    const size_t base_offset,
    io::IStream &input_stream,
    dec::ArchiveMeta &meta) :
        logger(logger),
        rva_helper(helper),
        base_offset(base_offset),
        input_stream(input_stream),
        meta(meta)
{
}

void ResourceCrawler::crawl(const ResourceCrawlerArgs &args)
{
    ResourceCrawler crawler(args);
    crawler.process_dir(0);
}

ResourceCrawler::ResourceCrawler(const ResourceCrawlerArgs &args) : args(args)
{
}

void ResourceCrawler::process_dir(const size_t offset, const std::string path)
{
    args.input_stream.seek(args.base_offset + offset);
    ImageResourceDir dir(args.input_stream);
    size_t entry_count = dir.number_of_named_entries + dir.number_of_id_entries;
    for (auto i : algo::range(entry_count))
    {
        ImageResourceDirEntry entry(args.input_stream);

        try
        {
            args.input_stream.peek(args.input_stream.tell(), [&]()
            {
                std::string entry_path = read_entry_name(entry);
                if (path != "")
                    entry_path = path + path_sep + entry_path;

                if (entry.data_is_dir)
                    process_dir(entry.offset_to_data, entry_path);
                else
                    process_entry(entry.offset_to_data, entry_path);
            });
        }
        catch (const std::exception &e)
        {
            args.logger.err(
                "Can't read resource entry located at 0x%08x (%s)\n",
                args.base_offset + entry.offset_to_data,
                e.what());
        }
    }
}

void ResourceCrawler::process_entry(size_t offset, const std::string &path)
{
    args.input_stream.seek(args.base_offset + offset);
    ImageResourceDataEntry resource_entry(args.input_stream);

    auto entry = std::make_unique<ArchiveEntryImpl>();
    entry->path = path;
    entry->offset = args.rva_helper.rva_to_offset(
        resource_entry.offset_to_data);
    entry->size = resource_entry.size;
    args.meta.entries.push_back(std::move(entry));
}

std::string ResourceCrawler::read_entry_name(const ImageResourceDirEntry &entry)
{
    if (entry.name_is_string)
    {
        args.input_stream.seek(args.base_offset + entry.name_offset);
        size_t max_size = args.input_stream.read_le<u16>();
        bstr name_utf16 = args.input_stream.read(max_size * 2);
        return algo::utf16_to_utf8(name_utf16).str();
    }

    switch (entry.id)
    {
        case 1: return "CURSOR";
        case 2: return "BITMAP";
        case 3: return "ICON";
        case 4: return "MENU";
        case 5: return "DIALOG";
        case 6: return "STRING";
        case 7: return "FONT_DIRECTORY";
        case 8: return "FONT";
        case 9: return "ACCELERATOR";
        case 10: return "RC_DATA";
        case 11: return "MESSAGE_TABLE";
        case 16: return "VERSION";
        case 17: return "DLG_INCLUDE";
        case 19: return "PLUG_AND_PLAY";
        case 20: return "VXD";
        case 21: return "ANIMATED_CURSOR";
        case 22: return "ANIMATED_ICON";
        case 23: return "HTML";
        case 24: return "MANIFEST";
    }

    return algo::format("%d", entry.id);
}

bool ExeArchiveDecoder::is_recognized_impl(io::File &input_file) const
{
    DosHeader dos_header(input_file.stream);
    return dos_header.magic == "MZ"_b;
}

std::unique_ptr<dec::ArchiveMeta> ExeArchiveDecoder::read_meta_impl(
    const Logger &logger, io::File &input_file) const
{
    DosHeader dos_header(input_file.stream);
    input_file.stream.seek(dos_header.e_lfanew);
    ImageNtHeader nt_header(input_file.stream);

    size_t data_dir_count = nt_header.optional_header.number_of_rva_and_sizes;
    std::vector<ImageDataDir> data_dirs;
    data_dirs.reserve(data_dir_count);
    for (auto i : algo::range(data_dir_count))
        data_dirs.push_back(ImageDataDir(input_file.stream));

    std::vector<ImageSectionHeader> sections;
    for (auto i : algo::range(nt_header.file_header.number_of_sections))
        sections.push_back(ImageSectionHeader(input_file.stream));

    RvaHelper rva_helper(
        nt_header.optional_header.file_alignment,
        nt_header.optional_header.section_alignment,
        sections);

    auto resource_dir = data_dirs[2];
    size_t base_offset = rva_helper.rva_to_offset(resource_dir.virtual_address);
    auto meta = std::make_unique<ArchiveMeta>();
    ResourceCrawler::crawl(ResourceCrawlerArgs(
        logger, rva_helper, base_offset, input_file.stream, *meta));
    return meta;
}

std::unique_ptr<io::File> ExeArchiveDecoder::read_file_impl(
    const Logger &logger,
    io::File &input_file,
    const dec::ArchiveMeta &m,
    const dec::ArchiveEntry &e) const
{
    auto entry = static_cast<const ArchiveEntryImpl*>(&e);
    input_file.stream.seek(entry->offset);
    auto data = input_file.stream.read(entry->size);
    auto output_file = std::make_unique<io::File>(entry->path, data);
    output_file->guess_extension();
    return output_file;
}

static auto _ = dec::register_decoder<ExeArchiveDecoder>("microsoft/exe");
