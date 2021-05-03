#ifndef video_streamer_webrtc_TYPES_HPP
#define video_streamer_webrtc_TYPES_HPP

#include <string>

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
}

#endif

