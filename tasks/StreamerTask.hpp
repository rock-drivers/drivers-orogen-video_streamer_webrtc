/* Generated from orogen/lib/orogen/templates/tasks/Task.hpp */

#ifndef VIDEO_STREAMER_WEBRTC_STREAMERTASK_TASK_HPP
#define VIDEO_STREAMER_WEBRTC_STREAMERTASK_TASK_HPP

#include "video_streamer_webrtc/StreamerTaskBase.hpp"
#include <base/samples/Frame.hpp>
#include <gst/gstelement.h>
#include <libsoup/soup-types.h>
#include <thread>

struct _GMainLoop;
typedef struct _GMainLoop GMainLoop;
struct _GHashTable;
typedef struct _GHashTable GHashTable;

namespace video_streamer_webrtc {
    struct Receiver;

    /*! \class StreamerTask
     * \brief The task context provides and requires services. It uses an ExecutionEngine
     to perform its functions.
     * Essential interfaces are operations, data flow ports and properties. These
     interfaces have been defined using the oroGen specification.
     * In order to modify the interfaces you should (re)use oroGen and rely on the
     associated workflow.
     *
     * \details
     * The name of a TaskContext is primarily defined via:
     \verbatim
     deployment 'deployment_name'
         task('custom_task_name','video_streamer_webrtc::StreamerTask')
     end
     \endverbatim
     *  It can be dynamically adapted when the deployment is called with a prefix
     argument.
     */
    class StreamerTask : public StreamerTaskBase {
        friend class StreamerTaskBase;

    protected:
    public:
        /** TaskContext constructor for StreamerTask
         * \param name Name of the task. This name needs to be unique to make it
         * identifiable via nameservices. \param initial_state The initial TaskState of
         * the TaskContext. Default is Stopped state.
         */
        StreamerTask(std::string const& name = "video_streamer_webrtc::StreamerTask");

        /** TaskContext constructor for StreamerTask
         * \param name Name of the task. This name needs to be unique to make it
         * identifiable for nameservices. \param engine The RTT Execution engine to be
         * used for this task, which serialises the execution of all commands, programs,
         * state machines and incoming events for a task.
         *
         */
        StreamerTask(std::string const& name, RTT::ExecutionEngine* engine);

        /** Default deconstructor of StreamerTask
         */
        ~StreamerTask();

        /** This hook is called by Orocos when the state machine transitions
         * from PreOperational to Stopped. If it returns false, then the
         * component will stay in PreOperational. Otherwise, it goes into
         * Stopped.
         *
         * It is meaningful only if the #needs_configuration has been specified
         * in the task context definition with (for example):
         \verbatim
         task_context "TaskName" do
           needs_configuration
           ...
         end
         \endverbatim
         */
        bool configureHook();

        /** This hook is called by Orocos when the state machine transitions
         * from Stopped to Running. If it returns false, then the component will
         * stay in Stopped. Otherwise, it goes into Running and updateHook()
         * will be called.
         */
        bool startHook();

        /** This hook is called by Orocos when the component is in the Running
         * state, at each activity step. Here, the activity gives the "ticks"
         * when the hook should be called.
         *
         * The error(), exception() and fatal() calls, when called in this hook,
         * allow to get into the associated RunTimeError, Exception and
         * FatalError states.
         *
         * In the first case, updateHook() is still called, and recover() allows
         * you to go back into the Running state.  In the second case, the
         * errorHook() will be called instead of updateHook(). In Exception, the
         * component is stopped and recover() needs to be called before starting
         * it again. Finally, FatalError cannot be recovered.
         */
        void updateHook();

        /** This hook is called by Orocos when the component is in the
         * RunTimeError state, at each activity step. See the discussion in
         * updateHook() about triggering options.
         *
         * Call recover() to go back in the Runtime state.
         */
        void errorHook();

        /** This hook is called by Orocos when the state machine transitions
         * from Running to Stopped after stop() has been called.
         */
        void stopHook();

        /** This hook is called by Orocos when the state machine transitions
         * from Stopped to PreOperational, requiring the call to configureHook()
         * before calling start() again.
         */
        void cleanupHook();

        void registerReceiver(Receiver* entry);
        void deregisterReceiver(SoupWebsocketConnection* connection);

        int getImageWidth() const;
        int getImageHeight() const;
        base::samples::frame::frame_mode_t getImageMode() const;

        void emitGstreamerError();
        bool serverIsPaused() const;

        std::string getSTUNServer() const;
        Transport getTransport() const;
        Encoding getEncoding() const;

    private:
        void init();

        Encoding encoding;

        bool hasFrame = false;
        bool hasGstreamerError = false;
        bool serverPaused = true;
        int imageWidth;
        int imageHeight;
        int imageByteSize;
        base::samples::frame::frame_mode_t imageMode;
        base::Time baseTime;
        base::Time frameDuration;
        base::Time nextFrameTime;

        int argc = 0;
        const char* argv[1] = {"webrtc-streamer "};
        GMainContext* maincontext = nullptr;
        GMainLoop* mainloop = nullptr;

        typedef std::map<SoupWebsocketConnection*, Receiver*> ReceiverMap;
        ReceiverMap receivers;
        void startReceiver(Receiver& receiver);
        void configureFrameParameters(base::samples::frame::Frame const& frame);
        void clearAllReceivers();

        std::thread gstThread;
        bool waitFirstFrame();
        void pushFrame(base::samples::frame::Frame const& frame);

        void resumeServer();
        void pauseServer();
        void publishStats();
        void pushPendingFrames();
        void startReceivers();
        void reapDeadReceivers();

        static int pauseServerCallback(StreamerTask* task);
        static int resumeServerCallback(StreamerTask* task);
        static int pushPendingFramesCallback(StreamerTask* task);
        static int startReceiversCallback(StreamerTask* task);
        static int publishStatsCallback(StreamerTask* task);
        static int reapDeadReceiversCallback(StreamerTask* task);

        void queueIdleCallback(GSourceFunc callback);
    };
}

#endif
