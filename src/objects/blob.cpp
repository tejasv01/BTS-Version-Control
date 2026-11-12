#include "blob.hpp"

namespace bts {

std::string Blob::serialize() const {
    return content;
}

Blob Blob::deserialize(const std::string& data) {
    return Blob{data};
}

} // namespace bts
