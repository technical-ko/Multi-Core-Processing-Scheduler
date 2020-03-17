#include "process.h"
#include "vector"

// Process class methods
Process::Process(ProcessDetails details, uint32_t current_time)
{
    int i;
    pid = details.pid;
    start_time = details.start_time;
    num_bursts = details.num_bursts;
    current_burst = 0;
    burst_times = new uint32_t[num_bursts];
    cpu_io_times = new uint32_t[num_bursts];
    for (i = 0; i < num_bursts; i++)
    {
        burst_times[i] = details.burst_times[i];
        cpu_io_times[i] = details.burst_times[i];
    }
    priority = details.priority;
    state = (start_time == 0) ? State::Ready : State::NotStarted;
    if (state == State::Ready)
    {
        launch_time = current_time;
    }
    core = -1;
    turn_time = 0;
    wait_time = 0;
    cpu_time = 0;
    lastCpuTime = 0;
    lastWaitTime = 0;
    remain_time = 0;
    into_queue_time = 0;
    burstStartTime = 0;
    burstTimeElapsed = 0;
    launched = false;
    fromRunningToReady = false;
    wait_times;
    waitTimeNow = 0;
    for (i = 0; i < num_bursts; i+=2)
    {
        remain_time += burst_times[i];
    }
    total_remain_time = remain_time;
}

Process::~Process()
{
    delete[] burst_times;
}

uint16_t Process::getPid() const
{
    return pid;
}

uint32_t Process::getStartTime() const
{
    return start_time;
}

// Gets the most recent time the process was placed on the core
uint32_t Process::getLastCpuTime() const
{
    return lastCpuTime;
}

// Sets the last cpu time to the current time
void Process::setLastCpuTime(uint32_t current_time)
{
    lastCpuTime = current_time;
}

// Gets the most recent time the process was placed into the ready queue
uint32_t Process::getLastWaitTime() const
{
    return lastWaitTime;
}

// Sets the last wait time to the current time
void Process::setLastWaitTime(uint32_t current_time)
{
    lastWaitTime = current_time;
}

void Process::setIntoQueueTime(uint32_t current_time){
    into_queue_time = current_time;
}

uint32_t Process::getBurstStartTime() const{
    return burstStartTime;
}

void Process::setBurstStartTime(uint32_t current_time){
    burstStartTime = current_time;
}

void Process::updateCurrentBurst(){
    current_burst++;
}

uint16_t Process::getCurrentBurst() const {
    return current_burst;
}

uint32_t Process::getCurrentBurstTime() const {
    return burst_times[current_burst];
}

uint32_t Process::getBurstTimeElapsed() const
{
    return burstTimeElapsed;
}

void Process::resetBurstTimeElapsed(){
    burstTimeElapsed = 0;
}

uint8_t Process::getPriority() const
{
    return priority;
}

Process::State Process::getState() const
{
    return state;
}

int8_t Process::getCpuCore() const
{
    return core;
}

double Process::getTurnaroundTime() const
{
    return (double)turn_time / 1000.0;
}

double Process::getWaitTime() const
{
    return (double)wait_time / 1000.0;
}

double Process::getCpuTime() const
{
    return (double)cpu_time / 1000.0;
}

double Process::getRemainingTime() const
{
    return (double)remain_time / 1000.0;
}

bool Process::isLaunched() {
    return launched;
}

void Process::setLaunched(bool set){
    launched = set;
}

void Process::setState(State new_state, uint32_t current_time)
{
    if (state == Process::State::Running && new_state == Process::State::Ready){
        fromRunningToReady = true;
    }
    if (state == Process::State::Ready && new_state == Process::State::Running){
        wait_times.push_back(waitTimeNow);
    }
    state = new_state;
}

void Process::setLaunchTime(uint32_t current_time){
    launch_time = current_time;
}

void Process::setCpuCore(int8_t core_num)
{
    core = core_num;
}

void Process::updateProcess(uint32_t current_time)
{
    // use `current_time` to update turnaround time, wait time, burst times, 
    // cpu time, and remaining time
    if (state != Process::State::Terminated && launch_time != 0){
        turn_time = current_time - launch_time;
    }
    if (state == Process::State::Running){
        uint32_t burstTimesSoFar = 0;
        if (fromRunningToReady){
            for (int i = current_burst; i >=0; i-=2){
                burstTimesSoFar = burstTimesSoFar + burst_times[i];
            }
            //fromRunningToReady = false;
        }
        else{
            for (int i = current_burst-2; i >=0; i-=2){
                burstTimesSoFar = burstTimesSoFar + cpu_io_times[i];
            }
        }
        cpu_time = burstTimesSoFar + (current_time - burstStartTime);
        remain_time = total_remain_time - cpu_time;
        burstTimeElapsed = current_time - burstStartTime;

    }
    if (state == Process::State::Ready){
        uint32_t waitSums = 0;
        waitTimeNow = 0;
        for (int i = 0; i<wait_times.size(); i++){
            waitSums = waitSums + wait_times.at(i);
        }
        waitTimeNow = (current_time - into_queue_time);
        wait_time = waitSums + waitTimeNow;
    }
    if (state == Process::State::IO){
        fromRunningToReady = false;
        burstTimeElapsed = current_time - burstStartTime;
    }

}

void Process::updateBurstTime(int burst_idx, uint32_t new_time)
{
    burst_times[burst_idx] = new_time;
}


// Comparator methods: used in std::list sort() method
// No comparator needed for FCFS or RR (ready queue never sorted)

// SJF - comparator for sorting read queue based on shortest remaining CPU time
bool SjfComparator::operator ()(const Process *p1, const Process *p2)
{
    return p1->getRemainingTime() < p2->getRemainingTime();
}

// PP - comparator for sorting read queue based on priority
bool PpComparator::operator ()(const Process *p1, const Process *p2)
{
    return p1->getPriority() < p2->getPriority();
}
