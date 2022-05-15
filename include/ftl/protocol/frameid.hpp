/**
 * @file frameid.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <cinttypes>

namespace ftl {
namespace protocol {
/**
 * Unique identifier for a single frame. This is stored as two 16bit numbers
 * packed into a 32bit int. Every frame has a FrameID, as does every frameset.
 * FrameID + Timestamp together will be a unique object within the system since
 * frames cannot be duplicated.
 */
struct FrameID {
    uint32_t id;

    /**
     * Frameset ID for this frame.
     */
    inline unsigned int frameset() const { return id >> 8; }

    /**
     * Frame index within the frameset. This will correspond to the vector
     * index in the frameset object.
     */
    inline unsigned int source() const { return id & 0xff; }

    /**
     * The packed int with both frameset ID and index.
     */
    operator uint32_t() const { return id; }

    inline FrameID &operator=(int v) {
        id = v;
        return *this;
    }

    /**
     * Create a frame ID using a frameset id and a source number.
     * @param fs Frameset id
     * @param s Source number inside frameset
     */
    FrameID(unsigned int fs, unsigned int s) : id((fs << 8) + (s & 0xff) ) {}
    explicit FrameID(uint32_t x) : id(x) {}
    FrameID() : id(0) {}
};

}  // namespace protocol
}  // namespace ftl

// custom specialization of std::hash can be injected in namespace std
template<>
struct std::hash<ftl::protocol::FrameID> {
    std::size_t operator()(ftl::protocol::FrameID const& s) const noexcept {
        return std::hash<unsigned int>{}(s.id);
    }
};
