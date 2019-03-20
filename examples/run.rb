using_task_library 'video_streamer_webrtc'

import_types_from 'base'
class VideoGenerator < Syskit::RubyTaskContext
    output_port 'out', '/base/samples/frame/Frame'

    WIDTH  = 800
    HEIGHT = 40

    LINE_HEIGHT = 1

    def initialize(**args)
        super

        @sample = Types.base.samples.frame.Frame.new
        @sample.zero!
        @sample.frame_mode = :MODE_RGB
        @sample.size.width = WIDTH
        @sample.size.height = HEIGHT
        @sample.row_size = WIDTH * 3
        @sample.pixel_size = 3
        @sample.data_depth = 8
        @sample.frame_status = :STATUS_VALID
        @pixels = [WIDTH * HEIGHT * 3].pack('Q') + "\xff".b * WIDTH * HEIGHT * 3
        @sample.image.from_buffer(@pixels)
        @line = 0
    end

    poll do
        @line = (@line += 1) % HEIGHT

        pixels = @pixels.dup
        single_pix = "\x00\x00\x00".b
        pixels[8 + @line * WIDTH * 3, WIDTH * 3] = single_pix * WIDTH
        @sample.time = Time.now
        @sample.image.from_buffer(pixels)
        orocos_task.out.write(@sample)
    end
end

class Connected < Syskit::Composition
    add VideoGenerator, as: 'src'
    add OroGen.video_streamer_webrtc.StreamerTask, as: 'sink'

    src_child.connect_to sink_child
end

Syskit.conf.use_deployment OroGen.video_streamer_webrtc.StreamerTask => 'streamer'#, valgrind: true
Syskit.conf.use_ruby_tasks VideoGenerator => 'generator'

Robot.controller do
    Roby.plan.add_mission_task Connected
end
