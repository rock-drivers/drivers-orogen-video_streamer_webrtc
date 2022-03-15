/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "StreamerTask.hpp"

#include <base-logging/Logging.hpp>

#include <locale.h>
#include <glib.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video-info.h>
#include <gst/sdp/sdp.h>
#include <gst/app/app.h>

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

#include <libsoup/soup.h>
#include <json/json.h>
#include <string.h>

#ifndef G_SOURCE_FUNC
#define G_SOURCE_FUNC(f) ((GSourceFunc) (void (*)(void)) (f))
#endif

using namespace std;
using namespace video_streamer_webrtc;

#define RTP_PAYLOAD_TYPE "96"

Receiver *create_receiver(SoupWebsocketConnection * connection, StreamerTask& task);

GstPadProbeReturn payloader_caps_event_probe_cb (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);

void on_offer_created_cb (GstPromise * promise, gpointer user_data);
void on_negotiation_needed_cb (GstElement * webrtcbin, gpointer user_data);
void on_ice_candidate_cb (GstElement * webrtcbin, guint mline_index,
    gchar * candidate, gpointer user_data);

void soup_websocket_message_cb (SoupWebsocketConnection * connection,
    SoupWebsocketDataType data_type, GBytes * message, gpointer user_data);
void soup_websocket_closed_cb (SoupWebsocketConnection * connection,
    gpointer user_data);

void soup_http_handler (SoupServer * soup_server, SoupMessage * message,
    const char *path, GHashTable * query, SoupClientContext * client_context,
    gpointer user_data);

struct video_streamer_webrtc::Receiver
{
    StreamerTask* task;

    SoupWebsocketConnection *connection = nullptr;

    GstElement *pipeline = nullptr;
    GstElement *webrtcbin = nullptr;
    GstAppSrc *appsrc = nullptr;

    Receiver() {}
    Receiver(Receiver const&) = delete;

    ~Receiver()
    {
        if (pipeline) {
            gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
            gst_object_unref (GST_OBJECT (webrtcbin));
            gst_object_unref (GST_OBJECT (appsrc));
            gst_object_unref (GST_OBJECT (pipeline));
        }

        if (connection)
            g_object_unref (G_OBJECT (connection));
    }
};

/* This function is called when an error message is posted on the bus */
static void error_cb (GstBus *bus, GstMessage *msg, StreamerTask *task) {
    GError *err;
    gchar *debug_info;

    /* Print error details on the screen */
    gst_message_parse_error (msg, &err, &debug_info);
    g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
    g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
    g_clear_error (&err);
    g_free (debug_info);
    task->emitGstreamerError();
}

static GstVideoFormat frameModeToGSTFormat(base::samples::frame::frame_mode_t format) {
    switch(format) {
        case base::samples::frame::MODE_RGB:
            return GST_VIDEO_FORMAT_RGB;
        case base::samples::frame::MODE_BGR:
            return GST_VIDEO_FORMAT_BGR;
        case base::samples::frame::MODE_RGB32:
            return GST_VIDEO_FORMAT_RGBx;
        case base::samples::frame::MODE_GRAYSCALE:
            return GST_VIDEO_FORMAT_GRAY8;
        default:
            // Should not happen, the component validates the frame mode
            // against the accepted modes
            throw std::runtime_error("unsupported frame format received");
    }
}

Receiver* create_receiver(SoupWebsocketConnection * connection, StreamerTask& task)
{
    unique_ptr<Receiver> receiver(new Receiver());
    receiver->connection = connection;
    g_object_ref (G_OBJECT (connection));

    g_signal_connect (G_OBJECT (connection), "message",
        G_CALLBACK (soup_websocket_message_cb), (gpointer) receiver.get());

    auto encoding = task.getEncoding();
    std::ostringstream pipelineDefinition;
    pipelineDefinition
        << "webrtcbin name=webrtcbin appsrc do-timestamp=TRUE is-live=true name=src "
        << "! videoconvert "
        << "! " << encoding.encoder_element << " "
        << "! " << encoding.payload_element << " ";
    if (encoding.mtu) {
        pipelineDefinition << "mtu=" << encoding.mtu << " ";
    }

    pipelineDefinition
        << "! application/x-rtp,media=video,encoding-name=" << encoding.encoder_name
        <<    ",payload=" << RTP_PAYLOAD_TYPE
        << "! webrtcbin.";

    std::cout << "Using pipeline: " << pipelineDefinition.str() << std::endl;

    GError* error = nullptr;
    receiver->pipeline = gst_parse_launch(pipelineDefinition.str().c_str(), &error);
    if (error) {
        g_warning ("Could not create WebRTC pipeline: %s\n", error->message);
        g_error_free (error);
        return nullptr;
    }

    receiver->webrtcbin =
        gst_bin_get_by_name (GST_BIN (receiver->pipeline), "webrtcbin");

    auto stun_server = task.getSTUNServer();
    if (!stun_server.empty()) {
        g_object_set(receiver->webrtcbin, "stun-server", stun_server.c_str(), NULL);
    }

    g_assert (receiver->webrtcbin != NULL);
    gst_object_ref(receiver->webrtcbin);
    receiver->appsrc =
        (GstAppSrc*)gst_bin_get_by_name (GST_BIN (receiver->pipeline), "src");
    g_assert (receiver->appsrc != NULL);
    gst_object_ref(receiver->appsrc);

    g_signal_connect (receiver->webrtcbin, "on-negotiation-needed",
        G_CALLBACK (on_negotiation_needed_cb), (gpointer) receiver.get());
    g_signal_connect (receiver->webrtcbin, "on-ice-candidate",
        G_CALLBACK (on_ice_candidate_cb), (gpointer) receiver.get());

    auto bus = gst_element_get_bus (receiver->pipeline);
    gst_bus_add_signal_watch (bus);
    g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb,
        receiver->task);
    gst_object_unref (bus);

    return receiver.release();
}

void
on_offer_created_cb (GstPromise * promise, gpointer user_data)
{
    Receiver *receiver = (Receiver *) user_data;
    GstStructure const* reply = gst_promise_get_reply (promise);
    GstWebRTCSessionDescription *offer = NULL;
    gst_structure_get (reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
    gst_promise_unref (promise);

    GstPromise* local_desc_promise = gst_promise_new ();
    g_signal_emit_by_name (receiver->webrtcbin, "set-local-description",
        offer, local_desc_promise);
    gst_promise_interrupt (local_desc_promise);
    gst_promise_unref (local_desc_promise);

    gchar* sdp_string = gst_sdp_message_as_text (offer->sdp);
    g_print ("Negotiation offer created:\n%s\n", sdp_string);

    Json::Value sdp_data;
    sdp_data["type"] = "offer";
    sdp_data["sdp"] = sdp_string;

    Json::Value sdp;
    sdp["type"] = "sdp";
    sdp["data"] = sdp_data;

    Json::FastWriter writer;
    string json_string = writer.write(sdp);
    soup_websocket_connection_send_text (receiver->connection, json_string.c_str());
    gst_webrtc_session_description_free (offer);
}

void on_negotiation_needed_cb (GstElement * webrtcbin, gpointer user_data)
{
    GstPromise *promise;
    Receiver *receiver = (Receiver *) user_data;

    g_print ("Creating negotiation offer\n");

    promise = gst_promise_new_with_change_func (on_offer_created_cb,
        (gpointer) receiver, NULL);
    g_signal_emit_by_name (G_OBJECT (webrtcbin), "create-offer", NULL, promise);
}


void on_ice_candidate_cb (G_GNUC_UNUSED GstElement * webrtcbin, guint mline_index,
    gchar * candidate, gpointer user_data)
{
    Receiver *receiver = (Receiver *) user_data;

    Json::Value ice_data;
    ice_data["sdpMLineIndex"] = mline_index;
    ice_data["candidate"] = candidate;

    Json::Value ice;
    ice["type"] = "ice";
    ice["data"] = ice_data;

    Json::FastWriter writer;
    string json_string = writer.write(ice);
    soup_websocket_connection_send_text (receiver->connection, json_string.c_str());
}

void handleSDPMessage(Receiver* receiver, Json::Value data);
void handleICEMessage(Receiver* receiver, Json::Value data);

void
soup_websocket_message_cb (G_GNUC_UNUSED SoupWebsocketConnection * connection,
    SoupWebsocketDataType data_type, GBytes * message, gpointer user_data)
{
    Receiver *receiver = (Receiver *) user_data;

    Json::Value json;
    switch (data_type) {
        case SOUP_WEBSOCKET_DATA_BINARY:
            LOG_ERROR_S << "Received unknown binary message, ignoring" << std::endl;
            return;

        case SOUP_WEBSOCKET_DATA_TEXT:
            {
                gsize size;
                char* gdata = (char*)g_bytes_get_data(message, &size);
                Json::CharReaderBuilder rbuilder;
                std::unique_ptr<Json::CharReader> const reader(rbuilder.newCharReader());

                string errors;
                if (!reader->parse(gdata, gdata + size, &json, &errors)) {
                    LOG_ERROR_S << "invalid document received " << errors << std::endl;
                    return;
                }
            }
            break;

        default:
            g_assert_not_reached ();
    }

    if (json["type"].isNull()) {
        LOG_ERROR_S << "signalling message without type field, ignored" << std::endl;
        return;
    }
    string type = json["type"].asString();

    if (json["data"].isNull()) {
        LOG_ERROR_S << "signalling message of type " << type
                    << " without type field, ignored" << std::endl;
        return;
    }
    Json::Value data = json["data"];

    if (type == "sdp") {
        handleSDPMessage(receiver, data);
    } else if (type == "ice") {
        handleICEMessage(receiver, data);
    } else {
        Json::FastWriter writer;
        LOG_ERROR_S
            << "Unknown JSON message received on signalling channel:\n"
            << writer.write(data) << std::endl;
    }
}

void handleSDPMessage(Receiver* receiver, Json::Value data) {
    if (data["type"].isNull()) {
        LOG_ERROR_S << "received SDP message without a 'type' field" << std::endl;
        return;
    }

    string type = data["type"].asString();
    if (type != "answer") {
        LOG_ERROR_S << "expected SDP message of type 'answer', but got " << type << std::endl;
        return;
    }

    if (data["sdp"].isNull()) {
        LOG_ERROR_S << "received SDP message without SDP field" << std::endl;
        return;
    }
    string sdp = data["sdp"].asString();
    LOG_INFO_S << "received SDP: " << sdp << std::endl;

    GstSDPMessage* sdpMsg;
    int ret = gst_sdp_message_new(&sdpMsg);
    g_assert_cmphex (ret, ==, GST_SDP_OK);
    ret = gst_sdp_message_parse_buffer ((guint8 *)sdp.c_str(), sdp.size(), sdpMsg);
    if (ret != GST_SDP_OK) {
        LOG_ERROR_S << "could not parse SDP string: " << sdp << std::endl;
        return;
    }

    GstWebRTCSessionDescription *answer =
        gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdpMsg);
    g_assert_nonnull (answer);

    GstPromise *promise = gst_promise_new();
    g_signal_emit_by_name (
        receiver->webrtcbin, "set-remote-description", answer, promise
    );
    gst_promise_interrupt (promise);
    gst_promise_unref (promise);
}

void handleICEMessage(Receiver* receiver, Json::Value data) {
    if (data["sdpMLineIndex"].isNull()) {
        g_error ("Received ICE message without mline index\n");
        return;
    }
    guint mline_index = data["sdpMLineIndex"].asLargestUInt();

    if (data["candidate"].isNull()) {
        g_error ("Received ICE message without ICE candidate string\n");
        return;
    }
    string candidate = data["candidate"].asString();
    g_print ("Received ICE candidate with mline index %u; candidate: %s\n",
        mline_index, candidate.c_str());

    g_signal_emit_by_name(receiver->webrtcbin, "add-ice-candidate",
        mline_index, candidate.c_str());
}

void
soup_websocket_closed_cb (SoupWebsocketConnection * connection,
    gpointer user_data)
{
    ((StreamerTask*)user_data)->deregisterReceiver(connection);
    g_print ("Closed websocket connection %p\n", (gpointer) connection);
}

void
soup_websocket_handler (G_GNUC_UNUSED SoupServer * server,
    SoupWebsocketConnection * connection, G_GNUC_UNUSED const char *path,
    G_GNUC_UNUSED SoupClientContext * client_context, gpointer _task)
{
    auto task = (StreamerTask*)_task;
    if (task->serverIsPaused()) {
        return;
    }

    g_print ("Processing new websocket connection %p", (gpointer) connection);
    g_signal_connect (G_OBJECT (connection), "closed",
        G_CALLBACK (soup_websocket_closed_cb), task);

    auto receiver = create_receiver(connection, *task);
    if (receiver) {
        receiver->task = task;
        task->registerReceiver(receiver);
    }
    else {
        task->emitGstreamerError();
    }
}

const Encoding KNOWN_ENCODERS[] = {
    { VP8, "vp8enc", "rtpvp8pay", "VP8" },
    { VAAPI_VP8, "vaapivp8enc", "rtpvp8pay", "VP8" },
    { H264, "x264enc", "rtph264pay", "H264" },
    { VAAPI_H264, "vaapih264enc ! video/x-h264,profile=constrained-baseline",
                  "rtph264pay", "H264" },
    { CUSTOM_ENCODING, "", "", "" }
};

static Encoding encoderInfo(PREDEFINED_ENCODER encoder) {
    for (Encoding const* it = KNOWN_ENCODERS; it->encoder != CUSTOM_ENCODING; ++it) {
        if (it->encoder == encoder) {
            return *it;
        }
    }
    throw std::invalid_argument("no information for given encoder");
}


StreamerTask::StreamerTask(std::string const& name)
    : StreamerTaskBase(name)
{
}

StreamerTask::StreamerTask(std::string const& name, RTT::ExecutionEngine* engine)
    : StreamerTaskBase(name, engine)
{
}

StreamerTask::~StreamerTask()
{
}

/// The following lines are template definitions for the various state machine
// hooks defined by Orocos::RTT. See StreamerTask.hpp for more detailed
// documentation about them.

bool StreamerTask::configureHook()
{
    if (! StreamerTaskBase::configureHook())
        return false;

    frameDuration = base::Time::fromSeconds(1) / _fps.get();

    auto userEncoding = _encoding.get();
    if (userEncoding.encoder != CUSTOM_ENCODING) {
        encoding = encoderInfo(userEncoding.encoder);
    }
    if (!userEncoding.encoder_element.empty()) {
        encoding.encoder_element = userEncoding.encoder_element;
    }
    if (!userEncoding.payload_element.empty()) {
        encoding.payload_element = userEncoding.payload_element;
    }
    if (!userEncoding.encoder_name.empty()) {
        encoding.encoder_name = userEncoding.encoder_name;
    }
    encoding.mtu = userEncoding.mtu;

    serverPaused = true;
    gstThread = std::thread([this](){
        maincontext = g_main_context_new();
        g_main_context_push_thread_default(maincontext);
        mainloop = g_main_loop_new (maincontext, false);
        g_assert (mainloop != NULL);

        auto soup_server =
            soup_server_new (SOUP_SERVER_SERVER_HEADER, "webrtc-soup-server", NULL);
        soup_server_add_websocket_handler (soup_server, "/ws", NULL, NULL,
            soup_websocket_handler, (gpointer) this, NULL);
        soup_server_listen_all (soup_server, _port.get(),
            (SoupServerListenOptions) 0, NULL);

        g_main_loop_run(mainloop);

        g_object_unref (G_OBJECT (soup_server));
        g_main_loop_unref(mainloop);
        g_main_context_unref(maincontext);
        mainloop = nullptr;
        maincontext = nullptr;
    });
    return true;
}
bool StreamerTask::startHook()
{
    if (! StreamerTaskBase::startHook())
        return false;
    hasFrame = false;
    hasGstreamerError = false;
    queueIdleCallback(G_SOURCE_FUNC(resumeServerCallback));
    return true;
}

string StreamerTask::getSTUNServer() const
{
    return _stun_server.get();
}

Encoding StreamerTask::getEncoding() const
{
    return encoding;
}

bool StreamerTask::serverIsPaused() const
{
    return serverPaused;
}

void StreamerTask::resumeServer()
{
    serverPaused = false;
}

void StreamerTask::pauseServer()
{
    for (auto& entry : receivers) {
        delete entry.second;
    }
    receivers.clear();
    serverPaused = true;
}

void StreamerTask::emitGstreamerError()
{
    hasGstreamerError = true;
}

void StreamerTask::queueIdleCallback(GSourceFunc callback)
{
    auto source = g_idle_source_new();
    g_source_set_callback(source, G_SOURCE_FUNC(callback), this, nullptr);
    g_source_attach(source, maincontext);
    g_source_unref(source);
}

void StreamerTask::updateHook()
{
    if (hasGstreamerError)
    {
        exception(GSTREAMER_ERROR);
    }
    else if (hasFrame)
    {
        queueIdleCallback(G_SOURCE_FUNC(pushPendingFramesCallback));
    }
    else if (waitFirstFrame())
    {
        queueIdleCallback(G_SOURCE_FUNC(startReceiversCallback));
    }

    StreamerTaskBase::updateHook();
}
void StreamerTask::errorHook()
{
    StreamerTaskBase::errorHook();
}


void StreamerTask::stopHook()
{
    clearAllReceivers();
    queueIdleCallback(G_SOURCE_FUNC(pauseServerCallback));
    StreamerTaskBase::stopHook();
}
void StreamerTask::cleanupHook()
{
    g_main_loop_quit(mainloop);
    gstThread.join();
    StreamerTaskBase::cleanupHook();
}

void StreamerTask::registerReceiver(Receiver* receiver)
{
    deregisterReceiver(receiver->connection);
    receivers[receiver->connection] = receiver;

    if (hasFrame)
        startReceiver(*receiver);
    else
        g_print("Waiting for first frame before starting %p\n", receiver->connection);

}
void StreamerTask::startReceivers()
{
    for (auto& receiver: receivers) {
        startReceiver(*receiver.second);
    }
}
void StreamerTask::startReceiver(Receiver& receiver)
{
    GstVideoInfo info;
    int width  = getImageWidth();
    int height = getImageHeight();
    GstVideoFormat gstFormat = frameModeToGSTFormat(getImageMode());
    gst_video_info_set_format(&info, gstFormat, width, height);

    GstCaps* caps = gst_video_info_to_caps(&info);
    g_object_set(receiver.appsrc, "caps", caps, "format", GST_FORMAT_TIME, NULL);
    gst_caps_unref(caps);
    gst_app_src_set_max_bytes(receiver.appsrc, imageByteSize * 5);

    gst_element_set_state (receiver.pipeline, GST_STATE_PLAYING);
    g_print("Started %p\n", receiver.connection);

    baseTime = nextFrameTime;
}
void StreamerTask::deregisterReceiver(SoupWebsocketConnection* connection)
{
    g_print("Removed %p\n", connection);
    auto it = receivers.find(connection);
    if (it != receivers.end()) {
        delete it->second;
        receivers.erase(it);
    }
}
void StreamerTask::clearAllReceivers() {
    while (!receivers.empty()) {
        delete receivers.begin()->second;
        receivers.erase(receivers.begin());
    }
}

int StreamerTask::getImageWidth() const
{
    return imageWidth;
}

int StreamerTask::getImageHeight() const
{
    return imageHeight;
}

base::samples::frame::frame_mode_t StreamerTask::getImageMode() const
{
    return imageMode;
}

bool StreamerTask::waitFirstFrame()
{
    RTT::extras::ReadOnlyPointer<base::samples::frame::Frame> frame_ptr;
    if (_images.read(frame_ptr) != RTT::NewData)
        return false;

    hasFrame = true;
    configureFrameParameters(*frame_ptr);
    nextFrameTime = frame_ptr->time;

    return true;
}

void StreamerTask::configureFrameParameters(base::samples::frame::Frame const& frame) {
    imageWidth = frame.getWidth();
    imageHeight = frame.getHeight();
    imageMode = frame.getFrameMode();
    imageByteSize = frame.image.size();
}
void StreamerTask::pushPendingFrames()
{
    RTT::extras::ReadOnlyPointer<base::samples::frame::Frame> frame_ptr;
    while (_images.read(frame_ptr) == RTT::NewData) {
        pushFrame(*frame_ptr);
    }
}

void StreamerTask::pushFrame(base::samples::frame::Frame const& frame)
{
    // Validate that format did not change
    if (imageMode != frame.getFrameMode() || imageWidth != frame.getWidth() ||
        imageHeight != frame.getHeight()) {
        LOG_ERROR_S << "Image parameter(s) changed while playing, "\
                       "closing connection(s)" << std::endl;
        configureFrameParameters(frame);
        clearAllReceivers();
    }

    auto time = frame.time;
    if (time < nextFrameTime) {
        return;
    }

    while (time > nextFrameTime + frameDuration) {
        nextFrameTime = nextFrameTime + frameDuration;
    }

    if (receivers.empty())
        return;

    /* Create a buffer to wrap the last received image */
    GstBuffer *buffer = gst_buffer_new_and_alloc(frame.image.size());
    gst_buffer_fill(buffer, 0, frame.image.data(), frame.image.size());

    /* Set its timestamp and duration */
    if (baseTime.isNull())
        baseTime = nextFrameTime;

    GST_BUFFER_DURATION(buffer) = frameDuration.toMicroseconds() * 1000;

    for (auto const& receiver : receivers) {
        auto& appsrc = *(receiver.second->appsrc);
        auto current = gst_app_src_get_current_level_bytes(&appsrc);
        auto max     = gst_app_src_get_max_bytes(&appsrc);
        if (current <= max) {
            gst_app_src_push_buffer(&appsrc, gst_buffer_copy(buffer));
        }
        else {
            g_print("appsrc buffer full\n");
        }
    }
    gst_buffer_unref(buffer);
}


int StreamerTask::pushPendingFramesCallback(StreamerTask* task)
{
    task->pushPendingFrames();
    return false;
}

int StreamerTask::startReceiversCallback(StreamerTask* task)
{
    task->startReceivers();
    return false;
}

int StreamerTask::resumeServerCallback(StreamerTask* task)
{
    task->resumeServer();
    return false;
}

int StreamerTask::pauseServerCallback(StreamerTask* task)
{
    task->pauseServer();
    return false;
}
