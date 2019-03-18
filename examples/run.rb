using_task_library 'video_streamer_webrtc'
Syskit.conf.use_deployment OroGen.video_streamer_webrtc.StreamerTask => 'streamer'

Robot.controller do
    Roby.plan.add_mission OroGen.video_streamer_webrtc.StreamerTask
end