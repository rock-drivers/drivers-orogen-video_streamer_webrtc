/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "StreamerTask.hpp"

using namespace video_streamer_webrtc;

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
    return true;
}
bool StreamerTask::startHook()
{
    if (! StreamerTaskBase::startHook())
        return false;
    return true;
}
void StreamerTask::updateHook()
{
    StreamerTaskBase::updateHook();
}
void StreamerTask::errorHook()
{
    StreamerTaskBase::errorHook();
}
void StreamerTask::stopHook()
{
    StreamerTaskBase::stopHook();
}
void StreamerTask::cleanupHook()
{
    StreamerTaskBase::cleanupHook();
}
