/**
 * @file filestream.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <fstream>
#include <unordered_set>
#include <string>
#include <utility>
#include <limits>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <chrono>
#include "filestream.hpp"
#include <ftl/time.hpp>
#include "packetMsgpack.hpp"

#define LOGURU_REPLACE_GLOG 1
#include <loguru.hpp>

using ftl::protocol::File;
using ftl::protocol::StreamPacket;
using ftl::protocol::DataPacket;
using ftl::protocol::Packet;
using std::get;
using ftl::protocol::Channel;
using ftl::protocol::StreamPacketMSGPACK;
using ftl::protocol::PacketMSGPACK;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;
using ftl::protocol::StreamProperty;

File::File(const std::string &uri, bool writeable) :
        Stream(),
        uri_(uri),
        ostream_(nullptr),
        istream_(nullptr),
        active_(false) {
    mode_ = (writeable) ? Mode::Write : Mode::Read;
}

File::File(std::ifstream *is) : Stream(), ostream_(nullptr), istream_(is), active_(false) {
    mode_ = Mode::Read;
}

File::File(std::ofstream *os) : Stream(), ostream_(os), istream_(nullptr), active_(false) {
    mode_ = Mode::Write;
}

File::~File() {
    end();
}

bool File::_checkFile() {
    if (!_open()) return false;

    // Read some packets to identify frame rate.
    int count = 1000;
    int64_t ts = -1000;
    int min_ts_diff = 1000;
    first_ts_ = 10000000000000ll;

    std::unordered_set<ftl::protocol::Codec> codecs_found;

    while (count > 0) {
        Packet data;
        if (!readPacket(data)) {
            break;
        }

        StreamPacket &spkt = data;
        Packet &pkt = data;

        seen(FrameID(spkt.streamID, spkt.frame_number), spkt.channel);

        // TODO(Nick): Extract metadata

        auto &fsdata = framesets_[spkt.streamID];

        codecs_found.emplace(pkt.codec);

        if (fsdata.first_ts < 0) fsdata.first_ts = spkt.timestamp;

        if (spkt.timestamp > 0 && static_cast<int>(spkt.channel) < 32) {
            if (spkt.timestamp > ts) {
                --count;
                auto d = spkt.timestamp - ts;
                if (d < min_ts_diff && d > 0) {
                    min_ts_diff = d;
                }
                ts = spkt.timestamp;
            }
        }
    }

    buffer_in_.reset();
    buffer_in_.remove_nonparsed_buffer();

    checked_ = true;

    is_video_ = count < 9;

    framerate_ = 1000 / min_ts_diff;
    if (!is_video_) {
        looping_ = false;
    }

    interval_ = min_ts_diff;
    for (auto &f : framesets_) {
        f.second.interval = interval_;
    }
    return true;
}

bool File::isValid() {
    return _checkFile();
}

bool File::post(const StreamPacket &s, const DataPacket &p) {
    if (!active_) return false;
    if (mode_ != Mode::Write) {
        // LOG(WARNING) << "Cannot write to read-only ftl file";
        return false;
    }

    // LOG(INFO) << "WRITE: " << s.timestamp << " " << (int)s.channel << " " << p.data.size();

    // Don't write dummy packets to files.
    if (p.data.size() == 0) return true;

    // Discard all data channel packets for now
    // if (!save_data_ && static_cast<int>(s.channel) >= static_cast<int>(ftl::codecs::Channel::Data)) return true;

    StreamPacket s2 = s;

    auto data = std::tie(
        *reinterpret_cast<const StreamPacketMSGPACK*>(&s2),
        *reinterpret_cast<const PacketMSGPACK*>(&p));
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, data);

    UNIQUE_LOCK(mutex_, lk);
    ostream_->write(buffer.data(), buffer.size());
    return ostream_->good();
}

bool File::readPacket(Packet &data) {
    bool partial = false;
    ftl::protocol::Packer pack;

    while ((istream_->good()) || buffer_in_.nonparsed_size() > 0u) {
        if (buffer_in_.nonparsed_size() == 0 || (partial && buffer_in_.nonparsed_size() < 10000000)) {
            buffer_in_.reserve_buffer(10000000);
            istream_->read(buffer_in_.buffer(), buffer_in_.buffer_capacity());
            // if (stream_->bad()) return false;

            int bytes = istream_->gcount();
            if (bytes == 0) return false;
            buffer_in_.buffer_consumed(bytes);
            partial = false;
        }

        msgpack::object_handle msg;
        if (!buffer_in_.next(msg)) {
            partial = true;
            continue;
        }

        msgpack::object obj = msg.get();

        try {
            // Older versions have a different SPKT structure.
            if (version_ < 5) {
                /*std::tuple<StreamPacketV4MSGPACK, PacketMSGPACK> datav4;
                obj.convert(datav4);

                auto &spkt = std::get<0>(data);
                auto &spktv4 = std::get<0>(datav4);
                spkt.version = 4;
                spkt.streamID = spktv4.streamID;
                spkt.channel = spktv4.channel;
                spkt.frame_number = spktv4.frame_number;
                spkt.timestamp = spktv4.timestamp;
                spkt.flags = 0;

                std::get<1>(data) = std::move(std::get<1>(datav4));*/
                error(ftl::protocol::Error::kBadVersion, "Version too old");
                return false;
            } else {
                pack.set(&data);
                obj.convert(pack);
            }
        } catch (std::exception &e) {
            DLOG(INFO) << "Corrupt message: " << buffer_in_.nonparsed_size() << " - " << e.what();
            // active_ = false;
            return false;
        }

        // Correct for older version differences.
        // _patchPackets(&std::get<0>(data), &std::get<1>(data));

        return true;
    }

    return false;
}

void File::_patchPackets(StreamPacket *spkt, DataPacket *pkt) {
    // Fix to clear flags for version 2.
    /*if (version_ <= 2) {
        pkt.flags = 0;
    }
    if (version_ < 4) {
        spkt.frame_number = spkt.streamID;
        spkt.streamID = 0;
        if (isFloatChannel(spkt.channel)) pkt.flags |= ftl::protocol::kFlagFloat;

        auto codec = pkt.codec;
        if (codec == ftl::codecs::codec_t::HEVC) pkt.codec = ftl::codecs::codec_t::HEVC_LOSSLESS;
    }*/

    spkt->version = ftl::protocol::kCurrentFTLVersion;

    // Fix for flags corruption
    if (pkt->data.size() == 0) {
        pkt->dataFlags = 0;
    }
}

bool File::tick(int64_t ts) {
    if (!active_) return false;
    if (mode_ != Mode::Read) {
        DLOG(ERROR) << "Cannot read from a write only file";
        return false;
    }

    // Skip if paused
    // if (value("paused", false)) return true;

    #ifdef DEBUG_MUTEX
    UNIQUE_LOCK(mutex_, lk);
    #else
    std::unique_lock<std::mutex> lk(mutex_, std::defer_lock);
    if (!lk.try_lock()) return true;
    #endif

    if (jobs_ > 0) {
        return true;
    }

    // Check buffer first for frames already read
    size_t complete_count = 0;

    for (auto i = data_.begin(); i != data_.end(); ) {
        auto &fsdata = framesets_[i->streamID];
        if (fsdata.timestamp == 0) fsdata.timestamp = i->timestamp;

        // Limit to file framerate
        if (i->timestamp > ts) {
            break;
        }

        // Is the packet too old?
        if (i->timestamp < fsdata.timestamp) {
            i = data_.erase(i);
            continue;
        }

        if (i->timestamp <= fsdata.timestamp) {
            StreamPacket &spkt = *i;

            ++jobs_;

            if (spkt.channel == Channel::kEndFrame) {
                fsdata.needs_endframe = false;
            }

            if (fsdata.needs_endframe) {
                if (spkt.frame_number < 255) {
                    Packet &pkt = *i;

                    fsdata.frame_count = std::max(
                        fsdata.frame_count,
                        static_cast<size_t>(spkt.frame_number + pkt.frame_count));
                    while (fsdata.packet_counts.size() <= spkt.frame_number) fsdata.packet_counts.push_back(0);
                    ++fsdata.packet_counts[spkt.frame_number];
                } else {
                    // Add frameset packets to frame 0 counts
                    fsdata.frame_count = std::max(fsdata.frame_count, size_t(1));
                    while (fsdata.packet_counts.size() < fsdata.frame_count) fsdata.packet_counts.push_back(0);
                    ++fsdata.packet_counts[0];
                }
            }

            auto j = i;
            ++i;

            // TODO(Nick): Probably better not to do a thread per packet
            ftl::pool.push([this, i = j](int id) {
                StreamPacket &spkt = *i;
                Packet &pkt = *i;

                spkt.localTimestamp = spkt.timestamp;

                trigger(spkt, pkt);

                UNIQUE_LOCK(data_mutex_, dlk);
                data_.erase(i);
                --jobs_;
            });
        } else {
            ++complete_count;

            if (fsdata.needs_endframe) {
                for (size_t j = 0; j < fsdata.frame_count; ++j) {
                    auto timestamp = fsdata.timestamp;
                    auto sid = i->streamID;
                    auto pcount = fsdata.packet_counts[j];

                    ftl::pool.push([this, timestamp, sid, j, pcount](int id) {
                        // Send final frame packet.
                        StreamPacket spkt;
                        spkt.timestamp = timestamp;
                        spkt.streamID = sid;
                        spkt.flags = 0;
                        spkt.channel = Channel::kEndFrame;

                        DataPacket pkt;
                        pkt.bitrate = 255;
                        pkt.codec = Codec::kInvalid;
                        pkt.packet_count = 1;
                        pkt.frame_count = 1;

                        spkt.frame_number = j;
                        pkt.packet_count = pcount+1;

                        trigger(spkt, pkt);
                    });

                    fsdata.packet_counts[j] = 0;
                }
            } else {
            }

            fsdata.timestamp = i->timestamp;
            if (complete_count == framesets_.size()) break;
        }
    }

    int64_t max_ts = std::numeric_limits<int64_t>::min();
    for (auto &fsd : framesets_) {
        max_ts = std::max(max_ts, (fsd.second.timestamp <= 0) ? timestart_ : fsd.second.timestamp);
    }
    int64_t extended_ts = max_ts + 200;  // Buffer 200ms ahead

    while (!read_error_ && ((active_ && istream_->good()) || buffer_in_.nonparsed_size() > 0u)) {
        UNIQUE_LOCK(data_mutex_, dlk);
        auto &data = data_.emplace_back();
        dlk.unlock();

        bool res = readPacket(data);
        if (!res) {
            dlk.lock();
            data_.pop_back();
            read_error_ = true;
            break;
        }

        auto &fsdata = framesets_[data.streamID];

        if (fsdata.first_ts < 0) {
            DLOG(WARNING) << "Bad first timestamp " << fsdata.first_ts << ", " << data.timestamp;
        }

        // Adjust timestamp
        // FIXME: A potential bug where multiple times are merged into one?
        data.timestamp = (((data.timestamp) - fsdata.first_ts)) + timestart_;
        data.hint_capability =
            ((is_video_) ? 0 : ftl::protocol::kStreamCap_Static) | ftl::protocol::kStreamCap_Recorded;

        if (data.timestamp > extended_ts) {
            break;
        }
    }

    // Force send end frames for static files
    if (data_.size() == 0 && !is_video_) {
        for (auto &fsix : framesets_) {
            auto &fsdata = fsix.second;
            if (fsdata.needs_endframe) {
                fsdata.needs_endframe = false;
                // Send final frame packet.
                StreamPacket spkt;
                spkt.timestamp = fsdata.timestamp;
                spkt.streamID = fsix.first;
                spkt.flags = 0;
                spkt.channel = Channel::kEndFrame;

                DataPacket pkt;
                pkt.bitrate = 255;
                pkt.codec = Codec::kInvalid;
                pkt.packet_count = 1;
                pkt.frame_count = 1;

                for (size_t i = 0; i < fsdata.frame_count; ++i) {
                    spkt.frame_number = i;
                    pkt.packet_count = fsdata.packet_counts[i]+1;
                    fsdata.packet_counts[i] = 0;

                    trigger(spkt, pkt);
                }
            }
        }
    }

    if (data_.size() == 0 && looping_) {
        buffer_in_.reset();
        buffer_in_.remove_nonparsed_buffer();
        _open();

        read_error_ = false;
        timestart_ = ftl::time::get_time();
        for (auto &fsd : framesets_) fsd.second.timestamp = 0;
        return true;
    }

    return data_.size() > 0;
}

bool File::_open() {
    if (istream_ && istream_->is_open()) {
        istream_->clear();
        istream_->seekg(0);
    } else {
        if (!istream_) istream_ = new std::ifstream;
        istream_->open(uri_.toFilePath(), std::ifstream::in | std::ifstream::binary);

        if (!istream_->good()) {
            DLOG(ERROR) << "Could not open file: " << uri_.toFilePath();
            return false;
        }
    }

    ftl::protocol::Header h;
    (*istream_).read(reinterpret_cast<char*>(&h), sizeof(h));
    if (h.magic[0] != 'F' || h.magic[1] != 'T' || h.magic[2] != 'L' || h.magic[3] != 'F') return false;

    if (h.version >= 2) {
        ftl::protocol::IndexHeader ih;
        (*istream_).read(reinterpret_cast<char*>(&ih), sizeof(ih));
    }

    version_ = h.version;
    return true;
}

bool File::run() {
    thread_ = std::thread([this]() {
        while (active_) {
            auto now = ftl::time::get_time();
            tick(now);
            auto used = ftl::time::get_time() - now;
            int64_t spare = interval_ - used;
            // LOG(INFO) << "SLEEP = " << spare;
            sleep_for(milliseconds(std::max(int64_t(1), spare)));
        }
    });

    #ifndef WIN32
    sched_param p;
    p.sched_priority = sched_get_priority_max(SCHED_RR);
    pthread_setschedparam(thread_.native_handle(), SCHED_RR, &p);
    #endif

    // TODO(Nick): Windows thread priority

    return true;
}

bool File::_validateFilename() const {
    std::filesystem::path file = std::filesystem::u8path(uri_.toFilePath());
    if (!std::filesystem::exists(file)) return true;
    if (std::string(file.extension().u8string().c_str()) == ".ftl") return true;
    // TODO(Nick): Could also check directory path
    return false;
}

bool File::begin() {
    if (active_) return true;
    if (mode_ == Mode::Read) {
        if (!checked_) {
            if (!_checkFile()) {
                DLOG(ERROR) << "Could not open file: " << uri_.toFilePath();
                return false;
            }
        }
        _open();

        // Capture current time to adjust timestamps
        timestart_ = ftl::time::get_time();
        active_ = true;
        read_error_ = false;

        tick(timestart_);  // Do some now!
        run();
    } else if (mode_ == Mode::Write) {
        if (!ostream_) ostream_ = new std::ofstream;
        if (!_validateFilename()) return false;
        ostream_->open(uri_.toFilePath(), std::ifstream::out | std::ifstream::binary);

        if (!ostream_->good()) {
            DLOG(ERROR) << "Could not open file: '" << uri_.toFilePath() << "'";
            return false;
        }

        ftl::protocol::Header h;
        (*ostream_).write((const char*)&h, sizeof(h));

        ftl::protocol::IndexHeader ih;
        ih.reserved[0] = -1;
        (*ostream_).write((const char*)&ih, sizeof(ih));

        // Capture current time to adjust timestamps
        timestart_ = ftl::time::get_time();
        active_ = true;
        interval_ = 50;  // TODO(Nick): Where to get this from?
    }

    return true;
}

bool File::end() {
    if (!active_) return false;
    active_ = false;

    if (thread_.joinable()) thread_.join();

    UNIQUE_LOCK(mutex_, lk);

    while (jobs_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (mode_ == Mode::Read) {
        if (istream_) {
            istream_->close();
            delete istream_;
            istream_ = nullptr;
        }
    } else if (mode_ == Mode::Write) {
        if (ostream_) {
            ostream_->close();
            delete ostream_;
            ostream_ = nullptr;
        }
    }
    return true;
}

void File::reset() {
    /*UNIQUE_LOCK(mutex_, lk);

    // TODO: Find a better solution
    while (jobs_ > 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));

    data_.clear();
    buffer_in_.reset();
    buffer_in_.remove_nonparsed_buffer();
    _open();

    timestart_ = (ftl::timer::get_time() / ftl::timer::getInterval()) * ftl::timer::getInterval();
    //timestamp_ = timestart_;
    for (auto &fsd : framesets_) fsd.second.timestamp = timestart_;*/
}

bool File::active() {
    return active_;
}

void File::refresh() {}

bool File::enable(FrameID id) {
    return Stream::enable(id);
}

bool File::enable(FrameID id, ftl::protocol::Channel c) {
    return Stream::enable(id, c);
}

bool File::enable(FrameID id, const ftl::protocol::ChannelSet &channels) {
    return Stream::enable(id, channels);
}

void File::setProperty(StreamProperty opt, std::any value) {
    switch (opt) {
    case StreamProperty::kFrameRate         :
    case StreamProperty::kURI               :   throw FTL_Error("Readonly property");
    case StreamProperty::kLooping           :   looping_ = std::any_cast<bool>(value); break;
    case StreamProperty::kSpeed             :   speed_ = std::any_cast<int>(value); break;
    default                                 :   throw FTL_Error("Property not supported");
    }
}

std::any File::getProperty(StreamProperty opt) {
    switch (opt) {
    case StreamProperty::kSpeed             :   return speed_;
    case StreamProperty::kFrameRate         :   return framerate_;
    case StreamProperty::kLooping           :   return looping_;
    case StreamProperty::kURI               :   return uri_.getBaseURI();
    default                                 :   throw FTL_Error("Property not supported");
    }
}

bool File::supportsProperty(StreamProperty opt) {
    switch (opt) {
    case StreamProperty::kSpeed             :
    case StreamProperty::kFrameRate         :
    case StreamProperty::kLooping           :
    case StreamProperty::kURI               :   return true;
    default                                 :   return false;
    }
}
