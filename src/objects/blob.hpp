#pragma once

#include <string>

namespace bts {

/**
 * @brief Represents a Blob object holding raw file content (Appendix A).
 */
struct Blob {
    std::string content;

    /**
     * @brief Serializes the Blob payload.
     * @return Raw content bytes.
     */
    std::string serialize() const;

    /**
     * @brief Deserializes a Blob from its payload.
     * @param data Raw payload bytes.
     * @return Deserialized Blob.
     */
    static Blob deserialize(const std::string& data);
};

} // namespace bts
