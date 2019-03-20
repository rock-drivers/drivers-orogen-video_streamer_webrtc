using_task_library 'video_streamer_webrtc'

import_types_from 'base'
class VideoGenerator < Syskit::RubyTaskContext
    output_port 'out', '/base/samples/frame/Frame'

    IMAGE_WIDTH  = 800
    IMAGE_LINE_BYTES = IMAGE_WIDTH * 3
    IMAGE_HEIGHT = 40

    LOOP_MARKER_WIDTH  = 100
    INDEX_MARKER_WIDTH = IMAGE_WIDTH - LOOP_MARKER_WIDTH

    LINE_HEIGHT = 1

    def reset_loops(pixels)
        blank_pix = "\xff\xff\xff".b.freeze
        6.times do |i|
            mark_loop(pixels, i, pixel: blank_pix)
        end
    end

    def mark_loop(pixels, index, pixel: @single_pix)
        pixels[8 + (index * 6) * IMAGE_LINE_BYTES,
            LOOP_MARKER_WIDTH * 3] = pixel * LOOP_MARKER_WIDTH
    end

    def mark_line(pixels, index)
        pixels[8 + index * IMAGE_LINE_BYTES + LOOP_MARKER_WIDTH * 3,
            INDEX_MARKER_WIDTH * 3] = @single_pix * INDEX_MARKER_WIDTH
    end

    def initialize(**args)
        super

        @sample = Types.base.samples.frame.Frame.new
        @sample.zero!
        @sample.frame_mode = :MODE_RGB
        @sample.size.width = IMAGE_WIDTH
        @sample.size.height = IMAGE_HEIGHT
        @sample.row_size = IMAGE_LINE_BYTES
        @sample.pixel_size = 3
        @sample.data_depth = 8
        @sample.frame_status = :STATUS_VALID
        @pixels = [IMAGE_LINE_BYTES * IMAGE_HEIGHT].pack('Q') + "\xff".b * (IMAGE_LINE_BYTES * IMAGE_HEIGHT)
        @sample.image.from_buffer(@pixels)
        @single_pix = "\x00\x00\x00".b.freeze

        @loop = 0
        @line = 0
    end

    poll do
        @line = (@line += 1) % IMAGE_HEIGHT
        if @line == 0
            @loop += 1
            if @loop > 5
                reset_loops(@pixels)
                @loop = 0
            else
                mark_loop(@pixels, @loop)
            end
        end
        pixels = @pixels.dup
        mark_line(pixels, @line)

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
