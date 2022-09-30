/**
 * @file filestream.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <string>
#include <list>
#include <tuple>
#include <vector>
#include <unordered_map>
#include <thread>
#include <ftl/protocol/packet.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/handle.hpp>
#include <ftl/uri.hpp>
#include <msgpack.hpp>

namespace ftl {
namespace protocol {

/**
 * Provide a packet stream to/from a file. If the file already exists it is
 * opened readonly, if not it is created write only. A mode to support both
 * reading and writing (to re code it) could be supported by using a temp file
 * for writing and swapping files when finished. It must be possible to control
 * streaming rate from the file.
 */
class File : public Stream {
 public:
    explicit File(const std::string &uri, bool writeable = false);
    explicit File(std::ifstream *);
    explicit File(std::ofstream *);
    ~File();

    bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::DataPacket &) override;

    bool begin() override;
    bool end() override;
    bool active() override;

    void reset() override;
    void refresh() override;

    bool enable(FrameID id) override;
    bool enable(FrameID id, ftl::protocol::Channel c) override;
    bool enable(FrameID id, const ftl::protocol::ChannelSet &channels) override;

    void setProperty(ftl::protocol::StreamProperty opt, std::any value) override;
    std::any getProperty(ftl::protocol::StreamProperty opt) override;
    bool supportsProperty(ftl::protocol::StreamProperty opt) override;

    StreamType type() const override { return StreamType::kRecorded; }

    /**
     * Automatically tick through the frames using a timer. Threads are used.
     */
    bool run();

    /**
     * Manually tick through the frames one per call.
     */
    bool tick(int64_t);

    /**
     * Directly read a packet. Returns false if no more packets exist, true
     * otherwise. The callback is called when a packet is read.
     */
    bool readPacket(ftl::protocol::Packet &);

    enum class Mode {
        Read,
        Write,
        ReadWrite
    };

    inline void setMode(Mode m) { mode_ = m; }
    inline void setStart(int64_t ts) { timestamp_ = ts; }

    // TODO(Nick): have standalone function to for validating the file
    /// check if valid file/stream
    bool isValid();

 private:
    ftl::URI uri_;
    std::ofstream *ostream_;
    std::ifstream *istream_;
    std::thread thread_;

    bool checked_ = false;
    Mode mode_;
    msgpack::sbuffer buffer_out_;
    msgpack::unpacker buffer_in_;
    std::list<ftl::protocol::Packet> data_;
    int64_t timestart_ = 0;
    int64_t timestamp_ = 0;
    int64_t interval_ = 50;
    int64_t first_ts_ = 0;
    bool active_ = false;
    int version_ = 0;
    bool is_video_ = true;
    bool read_error_ = false;
    bool looping_ = false;
    int framerate_ = 0;
    int speed_ = 1;

    struct FramesetData {
        size_t frame_count = 0;
        bool needs_endframe = true;
        std::vector<int> packet_counts;
        int64_t timestamp = 0;
        int64_t first_ts = -1;
        int interval = 50;
    };
    std::unordered_map<int, FramesetData> framesets_;

    MUTEX mutex_;
    MUTEX data_mutex_;
    std::atomic<int> jobs_ = 0;

    bool _open();
    bool _checkFile();
    bool _validateFilename() const;

    /* Apply version patches etc... */
    void _patchPackets(ftl::protocol::StreamPacket *spkt, ftl::protocol::DataPacket *pkt);
};

}  // namespace protocol
}  // namespace ftl
