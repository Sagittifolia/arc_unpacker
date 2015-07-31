#ifndef AU_FMT_KID_LNK_ARCHIVE_H
#define AU_FMT_KID_LNK_ARCHIVE_H
#include "fmt/archive.h"

namespace au {
namespace fmt {
namespace kid {

    class LnkArchive final : public Archive
    {
    public:
        LnkArchive();
        ~LnkArchive();
    protected:
        bool is_recognized_internal(File &) const override;
        void unpack_internal(File &, FileSaver &) const override;
    private:
        struct Priv;
        std::unique_ptr<Priv> p;
    };

} } }

#endif