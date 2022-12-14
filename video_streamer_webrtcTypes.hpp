#ifndef video_streamer_webrtc_TYPES_HPP
#define video_streamer_webrtc_TYPES_HPP

#include <string>
#include <stdint.h>
#include <base/Float.hpp>
#include <base/Time.hpp>

namespace video_streamer_webrtc {
    enum PREDEFINED_ENCODER {
        VP8, VAAPI_VP8, H264, VAAPI_H264, CUSTOM_ENCODING
    };

    /** Definition of the underlying encoder to be used in the webrtc connections */
    struct Encoding {
        /** A predefined encoder
         *
         * Set to CUSTOM_ENCODING to not use any predefined encoder. Fields that
         * are not empty will override the fields set via a predefined encoder.
         */
        PREDEFINED_ENCODER encoder = VP8;
        /** Definition of the GStreamer element that will do the encoding.
         *
         * If 'encoder' is not CUSTOM_ENCODING, overrides the value set via 'encoder'
         */
        std::string encoder_element;
        /** Definition of the GStreamer element that will do the framing
         *
         * If 'encoder' is not CUSTOM_ENCODING, overrides the value set via 'encoder'
         */
        std::string payload_element;
        /** Definition of the encoder name as passed to the RTP definition
         *
         * If 'encoder' is not CUSTOM_ENCODER, overrides the value set via 'encoder'
         */
        std::string encoder_name;
        /** MTU of the network link */
        uint16_t mtu = 0;
    };

    /** Statistics on inbound video streams
     *
     * This is sent by our client(s) and logged here
     */
    struct InboundStreamStatistics {
        /** Bytes received so far */
        uint64_t rx_bytes = 0;
        /** How many packets have been received */
        uint64_t packets_received = 0;
        /** How many duplicate packets have been received */
        uint64_t packets_lost = 0;
        /** How many frames have been decoded */
        uint64_t frames_decoded = 0;

        /** Timestamp of the last packet received */
        base::Time last_packet_received_timestamp;

        /** How many indicators for "picture loss" were sent by the client back
         * to us */
        uint64_t picture_loss_indicators_sent = 0;
        /** How many I frame request were sent by the client back to us because
         * of frame loss */
        uint64_t full_infra_requests_sent = 0;
        /** How many NACKs have been sent by the client back to us */
        uint64_t nack_sent = 0;
    };

    /** All information related to our client */
    struct ClientStatistics {
        base::Time time;

        std::vector<InboundStreamStatistics> inbound_stream_statistics;
    };
}

#endif

