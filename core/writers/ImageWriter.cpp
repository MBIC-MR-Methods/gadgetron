
#include <ismrmrd/meta.h>
#include <boost/optional.hpp>
#include <io/primitives.h>

#include "ImageWriter.h"

namespace {

    using namespace Gadgetron;
    using namespace Gadgetron::Core;

    template<class T>
class TypedImageWriter : public TypedWriter<ISMRMRD::ImageHeader, hoNDArray<T>, boost::optional<ISMRMRD::MetaContainer>> {
    public:
        void serialize(
                std::ostream &stream,
                const ISMRMRD::ImageHeader& header,
                const hoNDArray<T>& data,
                const optional<ISMRMRD::MetaContainer>& meta
        ) override {
            uint16_t message_id = 1022;
            IO::write(stream,message_id);
            IO::write(stream,header);

            std::string serialized_meta;

            if(meta) {
                std::stringstream meta_stream;

                ISMRMRD::serialize(*meta, meta_stream);

                serialized_meta = meta_stream.str();
            }

            uint64_t meta_size = serialized_meta.size();
            IO::write(stream,meta_size);
            stream.write(serialized_meta.c_str(), meta_size);
            IO::write(stream, data.get_data_ptr(), data.get_number_of_elements());
        }
    };

    const std::vector<std::shared_ptr<Writer>> writers {
        std::make_shared<TypedImageWriter<float>>(),
        std::make_shared<TypedImageWriter<double>>(),
        std::make_shared<TypedImageWriter<std::complex<float>>>(),
        std::make_shared<TypedImageWriter<std::complex<double>>>(),
        std::make_shared<TypedImageWriter<unsigned short>>(),
        std::make_shared<TypedImageWriter<short>>(),
        std::make_shared<TypedImageWriter<unsigned int>>(),
        std::make_shared<TypedImageWriter<int>>()
    };
}


namespace Gadgetron::Core::Writers {

    bool ImageWriter::accepts(const Message &message) {
        return std::any_of(writers.begin(), writers.end(),
                [&](auto &writer) { return writer->accepts(message); }
        );
    }

    void ImageWriter::write(std::ostream &stream, Message message) {
        for (auto &writer : writers){
            if (writer->accepts(message)) {
                writer->write(stream, std::move(message));
                return;
            }
        }
    }

    GADGETRON_WRITER_EXPORT(ImageWriter)
}

