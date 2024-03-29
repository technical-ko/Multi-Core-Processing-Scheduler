#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include "configreader.h"
#include "process.h"

// Shared data for all cores
typedef struct SchedulerData {
    std::mutex mutex;
    std::condition_variable condition;
    ScheduleAlgorithm algorithm;
    uint32_t context_switch;
    uint32_t time_slice;
    std::list<Process*> ready_queue;
    std::vector<Process*> terminated;
    std::vector<Process*> io_q;
    bool all_terminated;
} SchedulerData;

void coreRunProcesses(uint8_t core_id, SchedulerData *data);
int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex);
void clearOutput(int num_lines);
uint32_t currentTime();
std::string processStateToString(Process::State state);

int main(int argc, char **argv)
{
    // ensure user entered a command line parameter for configuration file name
    uint32_t programStartTime = currentTime();
    if (argc < 2)
    {
        std::cerr << "Error: must specify configuration file" << std::endl;
        exit(1);
    }

    // declare variables used throughout main
    int i;
    SchedulerData *shared_data;
    std::vector<Process*> processes;

    // read configuration file for scheduling simulation
    SchedulerConfig *config = readConfigFile(argv[1]);

    // store configuration parameters in shared data object
    uint8_t num_cores = config->cores;
    shared_data = new SchedulerData();
    shared_data->algorithm = config->algorithm;
    shared_data->context_switch = config->context_switch;
    shared_data->time_slice = config->time_slice;
    shared_data->all_terminated = false;

    // create processes
    uint32_t start = currentTime();
    for (i = 0; i < config->num_processes; i++)
    {
        Process *p = new Process(config->processes[i], start);
        processes.push_back(p);
        if (p->getState() == Process::State::Ready)
        {
            shared_data->ready_queue.push_back(p);
            p->setIntoQueueTime(currentTime());
        }
    }

    // free configuration data from memory
    deleteConfig(config);

    // launch 1 scheduling thread per cpu core
    std::thread *schedule_threads = new std::thread[num_cores];
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i] = std::thread(coreRunProcesses, i, shared_data);
    }

    // main thread work goes here:
    int num_lines = 0;
    int num_terminated = 0;
    uint32_t end_time = 0;
    uint32_t half_time = 0;
    while (!(shared_data->all_terminated))
    {
        // clear output from previous iteration
        clearOutput(num_lines);

        // start new processes at their appropriate start time <-locked ready q
        {//LOCK   
            std::lock_guard<std::mutex> lock(shared_data->mutex);
            uint32_t currTime = currentTime();
            for(int i = 0; i < processes.size(); i++)
            {
                bool preEmpt = false;
                Process::State state = processes[i]->getState();
                if(state == Process::State::NotStarted)
                {
                    //check if it should be started
                    if(processes[i]->getStartTime() <= (currTime - programStartTime))
                    {    
                        processes[i]->setState(Process::State::Ready, currTime);
                        shared_data->ready_queue.push_back(processes[i]);
                        processes[i]->setIntoQueueTime(currentTime());
                    }
                }
                if (state == Process::State::Ready){
                    processes[i]->updateProcess(currentTime());
                }
                if (state == Process::State::IO){
                    processes[i]->updateProcess(currentTime());
                    if (processes[i]->getBurstTimeElapsed() >= processes[i]->getCurrentBurstTime()){
                        processes[i]->updateCurrentBurst();
                        processes[i]->setState(Process::State::Ready, currentTime());
                        shared_data->ready_queue.push_back(processes[i]);
                        processes[i]->setIntoQueueTime(currentTime());
                    }
                }
            }

            // sort the ready queue (if needed - based on scheduling algorithm)
            //check algorithm and relevant info of each item in ready q
            if(shared_data->algorithm == ScheduleAlgorithm::SJF)
            {
                shared_data->ready_queue.sort(SjfComparator());
            }
            if(shared_data->algorithm == ScheduleAlgorithm::PP)
            {
                shared_data->ready_queue.sort(PpComparator());
            }

            //check for half done and all done
            if(shared_data->terminated.size() >= processes.size()/2 && half_time == 0)
            {
                half_time = currentTime();
            }
            if(processes.size() == shared_data->terminated.size())
            {
                shared_data->all_terminated = true;
                end_time = currentTime();
            }
        }//UNLOCK

       
        // determine if all processes are in the terminated state <- release lock on ready q

        // output process status table
        num_lines = printProcessOutput(processes, shared_data->mutex);

        // sleep 1/60th of a second
        usleep(16667);
    }


    // wait for threads to finish
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i].join();
    }


    // print final statistics
    //  - CPU utilization
    //  - Throughput
    //     - Average for first 50% of processes finished
    //     - Average for second 50% of processes finished
    //     - Overall average
    //  - Average turnaround time
    //  - Average waiting time


    double cpu_total = 0;
    double turn_total = 0;
    double wait_total = 0;
    for(int i = 0; i < processes.size(); i++)
    {
        cpu_total += processes[i]->getCpuTime();
        turn_total += processes[i]->getTurnaroundTime();
        wait_total += processes[i]->getWaitTime();
    }

    double prog_runtime = (end_time - start)/1000.0;
    double first_runtime = (half_time - start)/1000.0;
    double second_runtime = (end_time - half_time)/1000.0;
    double cpu_percent = (cpu_total/prog_runtime)*100.0;
    double overall_throughput = processes.size()/prog_runtime;
    double first_throughput = (processes.size()/2)/first_runtime;
    double second_throughput = (processes.size()/2)/second_runtime;
    double turn_avg = turn_total/processes.size(); 
    double wait_avg = wait_total/processes.size(); 

    std::cout << "CPU Utilization: " << cpu_percent << "%" << std::endl;
    std::cout << "Throughput - Overall Average: " << overall_throughput << std::endl;
    std::cout << "Throughput - 1st Half Average: " << first_throughput << std::endl;
    std::cout << "Throughput - 2nd Half Average: " << second_throughput << std::endl;
    std::cout << "Average Turnaround Time: " << turn_avg << std::endl;
    std::cout << "Average Wait Time: " << wait_avg << std::endl;

    // Clean up before quitting program
    processes.clear();

    return 0;
}

void coreRunProcesses(uint8_t core_id, SchedulerData *shared_data)
{
    // Work to be done by each core idependent of the other cores
    //  - Get process at front of ready queue
    //  - Simulate the processes running until one of the following:
    //     - CPU burst time has elapsed
    //     - RR time slice has elapsed
    //     - Process preempted by higher priority process
    //  - Place the process back in the appropriate queue
    //     - I/O queue if CPU burst finished (and process not finished)
    //     - Terminated if CPU burst finished and no more bursts remain
    //     - Ready queue if time slice elapsed or process was preempted
    //  - Wait context switching time
    //  * Repeat until all processes in terminated state
    Process *p;
    p = NULL;
    bool inRobin = false;
    bool inProcess = false;
    uint16_t currentBurst = -1;
    uint32_t currentBurstTime = 0;
    uint32_t context_switch = shared_data->context_switch;
    while ((shared_data->all_terminated) != true){
        int readySize = 0;
        {//LOCK
            std::lock_guard<std::mutex> lock(shared_data->mutex);
            readySize = shared_data->ready_queue.size();
        }//UNLOCK

        //If no process on core, check readyq
        if ( readySize > 0 && p == NULL){
            {//LOCK
            std::lock_guard<std::mutex> lock(shared_data->mutex);
            readySize = shared_data->ready_queue.size();
            p = shared_data->ready_queue.front();
            shared_data->ready_queue.pop_front();
            }//UNLOCK

            readySize = readySize - 1;
            p->setState(Process::State::Running, currentTime());
            p->setCpuCore(core_id);
            if (p->isLaunched() != true){
                p->setLaunched(true);
                p->setLaunchTime(currentTime());
            }
            p->resetBurstTimeElapsed();
            if (currentBurst != p->getCurrentBurst()){
                p->setBurstStartTime(currentTime());
            }
        }
        if (shared_data->algorithm == ScheduleAlgorithm::RR && p != NULL){
            p->setRRFlag();
        }
        if (shared_data->algorithm == ScheduleAlgorithm::PP && p != NULL){
            p->setPPFlag();
        }
        //Code for First Come First Serve
        if (p != NULL && (shared_data->algorithm == ScheduleAlgorithm::FCFS || shared_data->algorithm == ScheduleAlgorithm::SJF)){
            p->updateProcess(currentTime());
            if (p->getRemainingTime() <= 0){               
                p->setState(Process::State::Terminated, currentTime());
                p->setCpuCore(-1);
                p->updateProcess(currentTime());
                {//LOCK
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                shared_data->terminated.push_back(p);
                }//UNLOCK
                p = NULL;
                uint32_t lastContextTime = currentTime();
                while (context_switch >= currentTime() - lastContextTime){};
            }
            else if (p->getBurstTimeElapsed() > p->getCurrentBurstTime()){
                p->setState(Process::State::IO, currentTime());
                p->updateCurrentBurst();
                p->setBurstStartTime(currentTime());
                p->resetBurstTimeElapsed();
                p->updateProcess(currentTime());
                p->setCpuCore(-1);
                p = NULL;
                uint32_t lastContextTime = currentTime();
                while (context_switch >= currentTime() - lastContextTime){};
            }
        }
        //Code for Round Robin
        if (p != NULL && shared_data->algorithm == ScheduleAlgorithm::RR){
            //p->updateProcess(currentTime());
            if (!inRobin){
                p->setRoundRobinStartTime(currentTime());
                inRobin = true;
                currentBurst = p->getCurrentBurst();
                currentBurstTime = p->getCurrentBurstTime();
            }
            p->updateProcess(currentTime());
            if (p->getRemainingTime() <= 0){
                p->setState(Process::State::Terminated, currentTime());
                p->setCpuCore(-1);
                p->updateProcess(currentTime());
                {//LOCK
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                shared_data->terminated.push_back(p);
                }//UNLOCK
                p = NULL;
                uint32_t lastContextTime = currentTime();
                while (context_switch >= currentTime() - lastContextTime){};
            }
            else if (p->getBurstTimeElapsed() > currentBurstTime){
                p->setState(Process::State::IO, currentTime());
                p->updateCurrentBurst();
                p->setBurstStartTime(currentTime());
                p->resetBurstTimeElapsed();
                p->setCpuCore(-1);
                p = NULL;
                inRobin = false;
                uint32_t lastContextTime = currentTime();
                while (context_switch >= currentTime() - lastContextTime){};
            }
            else if ((currentTime() - p->getRoundRobinStartTime()) >= shared_data->time_slice){
                p->setState(Process::State::Ready, currentTime());
                p->updateBurstTime(p->getCurrentBurst(), currentTime() - p->getRoundRobinStartTime());
                p->setIntoQueueTime(currentTime());
                p->setCpuCore(-1);
                {//LOCK
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                shared_data->ready_queue.push_back(p);
                }//UNLOCK
                p = NULL;
                inRobin = false;
                uint32_t lastContextTime = currentTime();
                while (context_switch >= currentTime() - lastContextTime){};
            }
        }
        //Code for PP
        if(p != NULL && shared_data->algorithm == ScheduleAlgorithm::PP){
            if (!inProcess){
                p->setPPTime(currentTime());
                inProcess = true;
            }
            p->updateProcess(currentTime());
            //LOCK
            {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                if (shared_data->ready_queue.size() != 0){
                    
                    Process *next = shared_data->ready_queue.front();
                    if (next->getPriority() < p->getPriority()){
                        next = NULL;
                        p->setState(Process::State::Ready, currentTime());
                        p->updateBurstTime(p->getCurrentBurst(), currentTime() - p->getPPTime());
                        p->setIntoQueueTime(currentTime());
                        p->setCpuCore(-1);
                        shared_data->ready_queue.push_back(p);
                        p = NULL;
                        inProcess = false;
                        uint32_t lastContextTime = currentTime();
                        while (context_switch >= currentTime() - lastContextTime){};
                    }
                }
            } //UNLOCK
            if (p != NULL){
                if (p->getRemainingTime() <= 0 ){
                    p->setState(Process::State::Terminated, currentTime());
                    p->setCpuCore(-1);
                    p->updateProcess(currentTime());

                    {//LOCK
                    std::lock_guard<std::mutex> lock(shared_data->mutex);
                    shared_data->terminated.push_back(p);
                    }//UNLOCK
                    p = NULL;
                    inProcess = false;
                    uint32_t lastContextTime = currentTime();
                    while (context_switch >= currentTime() - lastContextTime){};
                }
                else if (p->getBurstTimeElapsed() > p->getCurrentBurstTime()){
                    p->setState(Process::State::IO, currentTime());
                    p->updateCurrentBurst();
                    p->setBurstStartTime(currentTime());
                    p->resetBurstTimeElapsed();
                    p->updateProcess(currentTime());
                    p->setCpuCore(-1);
                    inProcess = false;
                    p = NULL;
                    uint32_t lastContextTime = currentTime();
                    while (context_switch >= currentTime() - lastContextTime){};
                }
            }
            

        }
        
        
        
      
    }   
}

int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex)
{
    int i;
    int num_lines = 2;
    std::lock_guard<std::mutex> lock(mutex);
    printf("|   PID | Priority |      State | Core | Turn Time | Wait Time | CPU Time | Remain Time |\n");
    printf("+-------+----------+------------+------+-----------+-----------+----------+-------------+\n");
    for (i = 0; i < processes.size(); i++)
    {
        if (processes[i]->getState() != Process::State::NotStarted)
        {
            uint16_t pid = processes[i]->getPid();
            uint8_t priority = processes[i]->getPriority();
            std::string process_state = processStateToString(processes[i]->getState());
            int8_t core = processes[i]->getCpuCore();
            std::string cpu_core = (core >= 0) ? std::to_string(core) : "--";
            double turn_time = processes[i]->getTurnaroundTime();
            double wait_time = processes[i]->getWaitTime();
            double cpu_time = processes[i]->getCpuTime();
            double remain_time = processes[i]->getRemainingTime();
            printf("| %5u | %8u | %10s | %4s | %9.1lf | %9.1lf | %8.1lf | %11.1lf |\n", 
                   pid, priority, process_state.c_str(), cpu_core.c_str(), turn_time, 
                   wait_time, cpu_time, remain_time);
            num_lines++;
        }
    }
    return num_lines;
}

void clearOutput(int num_lines)
{
    int i;
    for (i = 0; i < num_lines; i++)
    {
        fputs("\033[A\033[2K", stdout);
    }
    rewind(stdout);
    fflush(stdout);
}

uint32_t currentTime()
{
    uint32_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    return ms;
}

std::string processStateToString(Process::State state)
{
    std::string str;
    switch (state)
    {
        case Process::State::NotStarted:
            str = "not started";
            break;
        case Process::State::Ready:
            str = "ready";
            break;
        case Process::State::Running:
            str = "running";
            break;
        case Process::State::IO:
            str = "i/o";
            break;
        case Process::State::Terminated:
            str = "terminated";
            break;
        default:
            str = "unknown";
            break;
    }
    return str;
}
