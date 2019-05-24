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
#include <json-glib/json-glib.h>
#include <string.h>

using namespace std;
using namespace video_streamer_webrtc;

#define RTP_PAYLOAD_TYPE "96"

Receiver *create_receiver (SoupWebsocketConnection * connection);

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
static gchar *get_string_from_json_object (JsonObject * object);

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

Receiver* create_receiver(SoupWebsocketConnection * connection)
{
    unique_ptr<Receiver> receiver(new Receiver());
    receiver->connection = connection;
    g_object_ref (G_OBJECT (connection));

    g_signal_connect (G_OBJECT (connection), "message",
        G_CALLBACK (soup_websocket_message_cb), (gpointer) receiver.get());

    GError* error = nullptr;
    receiver->pipeline = gst_parse_launch ("webrtcbin name=webrtcbin "
        "appsrc is-live=true name=src ! videoconvert ! vp8enc ! "
        "rtpvp8pay max-ptime=500000000 !"
        "application/x-rtp,media=video,encoding-name=VP8,payload="
        RTP_PAYLOAD_TYPE " ! webrtcbin. ", &error);
    if (error) {
        g_warning ("Could not create WebRTC pipeline: %s\n", error->message);
        g_error_free (error);
        return nullptr;
    }

    receiver->webrtcbin =
        gst_bin_get_by_name (GST_BIN (receiver->pipeline), "webrtcbin");
    g_assert (receiver->webrtcbin != NULL);
    receiver->appsrc =
        (GstAppSrc*)gst_bin_get_by_name (GST_BIN (receiver->pipeline), "src");
    g_assert (receiver->appsrc != NULL);

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
    gchar *sdp_string;
    gchar *json_string;
    JsonObject *sdp_json;
    JsonObject *sdp_data_json;
    GstStructure const *reply;
    GstPromise *local_desc_promise;
    GstWebRTCSessionDescription *offer = NULL;
    Receiver *receiver = (Receiver *) user_data;

    reply = gst_promise_get_reply (promise);
    gst_structure_get (reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
        &offer, NULL);
    gst_promise_unref (promise);

    local_desc_promise = gst_promise_new ();
    g_signal_emit_by_name (receiver->webrtcbin, "set-local-description",
        offer, local_desc_promise);
    gst_promise_interrupt (local_desc_promise);
    gst_promise_unref (local_desc_promise);

    sdp_string = gst_sdp_message_as_text (offer->sdp);
    g_print ("Negotiation offer created:\n%s\n", sdp_string);

    sdp_json = json_object_new ();
    json_object_set_string_member (sdp_json, "type", "sdp");

    sdp_data_json = json_object_new ();
    json_object_set_string_member (sdp_data_json, "type", "offer");
    json_object_set_string_member (sdp_data_json, "sdp", sdp_string);
    json_object_set_object_member (sdp_json, "data", sdp_data_json);

    json_string = get_string_from_json_object (sdp_json);
    json_object_unref (sdp_json);

    soup_websocket_connection_send_text (receiver->connection, json_string);
    g_free (json_string);

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
    JsonObject *ice_json;
    JsonObject *ice_data_json;
    gchar *json_string;
    Receiver *receiver = (Receiver *) user_data;

    ice_json = json_object_new ();
    json_object_set_string_member (ice_json, "type", "ice");

    ice_data_json = json_object_new ();
    json_object_set_int_member (ice_data_json, "sdpMLineIndex", mline_index);
    json_object_set_string_member (ice_data_json, "candidate", candidate);
    json_object_set_object_member (ice_json, "data", ice_data_json);

    json_string = get_string_from_json_object (ice_json);
    json_object_unref (ice_json);

    soup_websocket_connection_send_text (receiver->connection, json_string);
    g_free (json_string);
}

void
soup_websocket_message_cb (G_GNUC_UNUSED SoupWebsocketConnection * connection,
    SoupWebsocketDataType data_type, GBytes * message, gpointer user_data)
{
    gsize size;
    gchar *data;
    gchar *data_string;
    const gchar *type_string;
    JsonNode *root_json;
    JsonObject *root_json_object;
    JsonObject *data_json_object;
    JsonParser *json_parser = NULL;
    Receiver *receiver = (Receiver *) user_data;

    switch (data_type) {
        case SOUP_WEBSOCKET_DATA_BINARY:
        g_error ("Received unknown binary message, ignoring\n");
        g_bytes_unref (message);
        return;

        case SOUP_WEBSOCKET_DATA_TEXT:
        data = (char*)g_bytes_unref_to_data (message, &size);
        /* Convert to NULL-terminated string */
        data_string = g_strndup (data, size);
        g_free (data);
        break;

        default:
        g_assert_not_reached ();
    }

    json_parser = json_parser_new ();
    if (!json_parser_load_from_data (json_parser, data_string, -1, NULL))
        goto unknown_message;

    root_json = json_parser_get_root (json_parser);
    if (!JSON_NODE_HOLDS_OBJECT (root_json))
        goto unknown_message;

    root_json_object = json_node_get_object (root_json);

    if (!json_object_has_member (root_json_object, "type")) {
        g_error ("Received message without type field\n");
        goto cleanup;
    }
    type_string = json_object_get_string_member (root_json_object, "type");

    if (!json_object_has_member (root_json_object, "data")) {
        g_error ("Received message without data field\n");
        goto cleanup;
    }
    data_json_object = json_object_get_object_member (root_json_object, "data");

    if (g_strcmp0 (type_string, "sdp") == 0) {
        const gchar *sdp_type_string;
        const gchar *sdp_string;
        GstPromise *promise;
        GstSDPMessage *sdp;
        GstWebRTCSessionDescription *answer;
        int ret;

        if (!json_object_has_member (data_json_object, "type")) {
        g_error ("Received SDP message without type field\n");
        goto cleanup;
        }
        sdp_type_string = json_object_get_string_member (data_json_object, "type");

        if (g_strcmp0 (sdp_type_string, "answer") != 0) {
        g_error ("Expected SDP message type \"answer\", got \"%s\"\n",
            sdp_type_string);
        goto cleanup;
        }

        if (!json_object_has_member (data_json_object, "sdp")) {
        g_error ("Received SDP message without SDP string\n");
        goto cleanup;
        }
        sdp_string = json_object_get_string_member (data_json_object, "sdp");

        g_print ("Received SDP:\n%s\n", sdp_string);

        ret = gst_sdp_message_new (&sdp);
        g_assert_cmphex (ret, ==, GST_SDP_OK);

        ret =
            gst_sdp_message_parse_buffer ((guint8 *) sdp_string,
            strlen (sdp_string), sdp);
        if (ret != GST_SDP_OK) {
        g_error ("Could not parse SDP string\n");
        goto cleanup;
        }

        answer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_ANSWER,
            sdp);
        g_assert_nonnull (answer);

        promise = gst_promise_new ();
        g_signal_emit_by_name (receiver->webrtcbin, "set-remote-description",
            answer, promise);
        gst_promise_interrupt (promise);
        gst_promise_unref (promise);
    } else if (g_strcmp0 (type_string, "ice") == 0) {
        guint mline_index;
        const gchar *candidate_string;

        if (!json_object_has_member (data_json_object, "sdpMLineIndex")) {
        g_error ("Received ICE message without mline index\n");
        goto cleanup;
        }
        mline_index =
            json_object_get_int_member (data_json_object, "sdpMLineIndex");

        if (!json_object_has_member (data_json_object, "candidate")) {
        g_error ("Received ICE message without ICE candidate string\n");
        goto cleanup;
        }
        candidate_string = json_object_get_string_member (data_json_object,
            "candidate");

        g_print ("Received ICE candidate with mline index %u; candidate: %s\n",
            mline_index, candidate_string);

        g_signal_emit_by_name (receiver->webrtcbin, "add-ice-candidate",
            mline_index, candidate_string);
    } else
        goto unknown_message;

cleanup:
    if (json_parser != NULL)
        g_object_unref (G_OBJECT (json_parser));
    g_free (data_string);
    return;

unknown_message:
    g_error ("Unknown message \"%s\", ignoring", data_string);
    goto cleanup;
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
    g_print ("Processing new websocket connection %p", (gpointer) connection);
    g_signal_connect (G_OBJECT (connection), "closed",
        G_CALLBACK (soup_websocket_closed_cb), task);

    auto receiver = create_receiver (connection);
    if (receiver) {
        receiver->task = task;
        task->registerReceiver(receiver);
    }
    else {
        task->emitGstreamerError();
    }
}

static gchar *
get_string_from_json_object (JsonObject * object)
{
    JsonNode *root;
    JsonGenerator *generator;
    gchar *text;

    /* Make it the root node */
    root = json_node_init_object (json_node_alloc (), object);
    generator = json_generator_new ();
    json_generator_set_root (generator, root);
    text = json_generator_to_data (generator, NULL);

    /* Release everything */
    g_object_unref (generator);
    json_node_free (root);
    return text;
}

StreamerTask::StreamerTask(std::string const& name)
    : StreamerTaskBase(name)
{
    init();
}

StreamerTask::StreamerTask(std::string const& name, RTT::ExecutionEngine* engine)
    : StreamerTaskBase(name, engine)
{
    init();
}

StreamerTask::~StreamerTask()
{
    g_main_loop_unref (mainloop);
    gst_deinit ();
}

void StreamerTask::init()
{
    setlocale (LC_ALL, "");
    gst_init (&argc, (char***)&argv);
    mainloop = g_main_loop_new (NULL, FALSE);
    g_assert (mainloop != NULL);
}



/// The following lines are template definitions for the various state machine
// hooks defined by Orocos::RTT. See StreamerTask.hpp for more detailed
// documentation about them.

bool StreamerTask::configureHook()
{
    if (! StreamerTaskBase::configureHook())
        return false;

    soup_server =
        soup_server_new (SOUP_SERVER_SERVER_HEADER, "webrtc-soup-server", NULL);
    soup_server_add_websocket_handler (soup_server, "/ws", NULL, NULL,
        soup_websocket_handler, (gpointer) this, NULL);
    soup_server_listen_all (soup_server, _port.get(),
        (SoupServerListenOptions) 0, NULL);

    frameDuration = base::Time::fromSeconds(1) / _fps.get();
    return true;
}
bool StreamerTask::startHook()
{
    if (! StreamerTaskBase::startHook())
        return false;
    hasFrame = false;
    hasGstreamerError = false;
    gstThread = std::thread([this](){ g_main_loop_run(mainloop); });
    return true;
}

void StreamerTask::emitGstreamerError()
{
    hasGstreamerError = true;
}

int pushPendingFramesIdleCallback(StreamerTask* task)
{
    task->pushPendingFrames();
    return false;
}

void StreamerTask::updateHook()
{
    if (hasGstreamerError)
    {
        exception(GSTREAMER_ERROR);
    }
    else if (hasFrame)
    {
        g_idle_add((GSourceFunc)pushPendingFramesIdleCallback, this);
    }
    else if (waitFirstFrame())
    {
        for (auto& receiver: receivers) {
            startReceiver(*receiver.second);
        }
    }

    StreamerTaskBase::updateHook();
}
void StreamerTask::errorHook()
{
    StreamerTaskBase::errorHook();
}
void StreamerTask::stopHook()
{
    for (auto& entry : receivers) {
        gst_element_set_state(entry.second->pipeline, GST_STATE_PAUSED);
    }
    g_main_loop_quit(mainloop);
    gstThread.join();
    StreamerTaskBase::stopHook();
}
void StreamerTask::cleanupHook()
{
    for (auto& entry : receivers) {
        delete entry.second;
    }
    receivers.clear();
    g_object_unref (G_OBJECT (soup_server));
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
    imageWidth = frame_ptr->getWidth();
    imageHeight = frame_ptr->getHeight();
    imageMode = frame_ptr->getFrameMode();
    imageByteSize = frame_ptr->image.size();
    nextFrameTime = frame_ptr->time;

    return true;
}

void StreamerTask::pushPendingFrames()
{
    RTT::extras::ReadOnlyPointer<base::samples::frame::Frame> frame_ptr;
    while (_images.read(frame_ptr) == RTT::NewData)
    {
        g_print("Pushing received frame\n");
        pushFrame(*frame_ptr);
    }
}

void StreamerTask::pushFrame(base::samples::frame::Frame const& frame)
{
    // Validate that width/height/format did not change
    if (imageWidth != frame.getWidth()) {
        LOG_ERROR_S << "Image width changed while playing" << std::endl;
        return;
    }
    else if (imageHeight != frame.getHeight()) {
        LOG_ERROR_S << "Image height changed while playing" << std::endl;
        return;
    }
    else if (imageMode != frame.getFrameMode()) {
        LOG_ERROR_S << "Image mode changed while playing" << std::endl;
        return;
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

    GST_BUFFER_PTS(buffer) = (nextFrameTime - baseTime).toMicroseconds() * 1000;
    GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buffer) = frameDuration.toMicroseconds() * 1000;

    GstFlowReturn ret;

    for (auto receiver : receivers) {
        auto& appsrc = *(receiver.second->appsrc);
        auto current = gst_app_src_get_current_level_bytes(&appsrc);
        auto max     = gst_app_src_get_max_bytes(&appsrc);
        if (current <= max) {
            g_print("appsrc buffer size: %lu/%lu\n", current, max);
            g_print("Pushing frame with dts %lu\n: ", GST_BUFFER_PTS(buffer));
            ret = gst_app_src_push_buffer(receiver.second->appsrc, buffer);
        }
    }
}
