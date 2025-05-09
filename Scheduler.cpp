#include "Scheduler.h"

//#define HIGHLY_VERBOSE
#define MAX_TASKS 124

Scheduler::Scheduler() {
    tasks.reserve(MAX_TASKS);
}

void Scheduler::clearMarkedForRemoval(){
    for(auto pid : tasksToRemove){
        //find task with pid
        auto it = std::find_if(tasks.begin(), tasks.end(), [pid](const Task &t) { return t.PID == pid; });
        if (it != tasks.end()) {
            tasks.erase(it);//remove it if it exists
        }
    }
    tasksToRemove.clear();
}

PID_t Scheduler::getAndIncrementPID() {
    // this is not very efficient, but ok for now for small number of tasks
    // Safeguard: if nextPID is 0, set it to 1
    if (!nextPID) {
        nextPID = 1;
    }

    // Keep trying until we find a PID that does not collide
    bool collision;
    do {
        collision = false;
        for (const auto& t : tasks) {
            if (t.PID == nextPID) {
                // If collision, increment PID and wrap if needed
                nextPID++;
                if (!nextPID) {
                    nextPID = 1;
                }
                collision = true;
                break;  // Break out and re-check from the beginning
            }
        }
    } while (collision);

    // Now nextPID is free for sure
    return nextPID++;
}

void Scheduler::setAndStartSequentialMode(bool seq) {
    sequentialMode = seq;
    if (sequentialMode) {
        lastSequentialFinishTime = millis();
    }
}

/* 
   1) addTimedTask:
     - In parallel mode, "delayMs" is effectively "postConditionDelay" 
       with an immediately true condition, so conditionWait= 0, condition= always true.
     - If repeat in sequential => not allowed => forcibly set repeat = false.
*/
PID_t Scheduler::addTimedTask(std::function<void()> action,
                             uint32_t delayMs,
                             bool repeat,
                             uint32_t interval)
{
    if (sequentialMode && repeat) {
        Serial.println("Warning: Repeat tasks are not supported in sequential mode. Disabling repeat.");
        repeat = false;
    }
    if (taskCount() >= MAX_TASKS){
        Serial.println("Too many tasks, only 124 allowed not adding more");
        return 0;
    }

    Task t;
    t.action = action;
    t.repeat = repeat;
    t.interval = interval;

    // Condition => "always true"
    t.condition = [](){ return true; };
    t.conditionMet = false;
    
    // We do not wait for condition => conditionWait=0 => immediate
    t.conditionWait = 0;  

    // The user-supplied delay becomes the postConditionDelay
    t.postConditionDelay = delayMs;

    // We'll set "executeAt" dynamically
    t.executeAt = 0;

    t.PID = getAndIncrementPID();
    tasks.push_back(t);

#ifdef HIGHLY_VERBOSE
    Serial.print("Task added, delay: ");
    Serial.println(delayMs);
    Serial.print("PID");
    Serial.println(t.PID);
    if(repeat){
        Serial.print("Task will repeat every: ");
        Serial.println(interval);
    }
#endif
    return t.PID;
}

// 2) addConditionalTask => postConditionDelay=0 => run immediately after condition
PID_t Scheduler::addConditionalTask(std::function<void()> action,
                                   std::function<bool()> condition,
                                   uint32_t conditionWaitMs)
{
    if (taskCount() >= MAX_TASKS){
        Serial.println("Too many tasks, only 124 allowed not adding more");
        return 0;
    }
    Task t;
    t.action = action;
    t.repeat = false;
    t.interval = 0;
    t.condition = condition;
    t.conditionMet = false;
    t.conditionWait = conditionWaitMs;  // can be <= 0 => indefinite
    t.postConditionDelay = 0;           // no additional delay
    t.executeAt = 0;

    t.PID = getAndIncrementPID();
    tasks.push_back(t);
#ifdef HIGHLY_VERBOSE
    Serial.print("Task added, conditionWait: ");
    Serial.println(conditionWaitMs);
    Serial.print("PID");
    Serial.println(t.PID);
#endif
    return t.PID;
}

// 3) addConditionalTimedTask => postConditionDelay>0 => run that long after condition is true
PID_t Scheduler::addConditionalTimedTask(std::function<void()> action,
                                        std::function<bool()> condition,
                                        uint32_t postDelayMs,
                                        uint32_t conditionWaitMs)
{
    if (taskCount() >= MAX_TASKS){
        Serial.println("Too many tasks, only 124 allowed not adding more");
        return 0;
    }
    Task t;
    t.action = action;
    t.repeat = false;
    t.interval = 0;
    t.condition = condition;
    t.conditionMet = false;
    t.conditionWait = conditionWaitMs;
    t.postConditionDelay = postDelayMs;
    t.executeAt = 0;

    t.PID = getAndIncrementPID();
    tasks.push_back(t);
#ifdef HIGHLY_VERBOSE
    Serial.print("Task added, conditionWait: ");
    Serial.print(conditionWaitMs);
    Serial.print("PID");
    Serial.println(t.PID);
#endif
    return t.PID;

}

bool Scheduler::removeTask(PID_t pid){
    auto it = std::find_if(tasks.begin(), tasks.end(), [pid](const Task &t) { return t.PID == pid; });
    if (it == tasks.end()) return false;  
    tasksToRemove.push_back(pid);//just schedule for removal, don't remove immediately
    return true;
}

    // Adapt a task repeat interval by PID
    // Returns true if the task was found, is a repeating task, and was updated
bool Scheduler::setRepeatingTaskInterval(PID_t pid, uint32_t interval){
    if(inLoop){
        Serial.println("ERROR: Cannot modify task from within loop");
        return false;
    }
    for (Task &t : tasks) {
        if (t.PID == pid) {
            if (!t.repeat) return false; // Not a repeating task
            
            // Update interval, postConditionDelay = interval here as
            // we are modifying a repeating task, this is the most intuitive
            // way to understand it
            t.postConditionDelay = interval;
            t.interval = interval;
            //reset to be scheduled again
            t.executeAt = 0;
            return true;
        }
    }
    return false; // Task not found
}


uint32_t Scheduler::timeToNextTask() const{
    uint32_t minTime = 60000; //one minute max
    if(tasks.empty())
        return minTime; //no tasks
    uint32_t now = millis();
    for (const Task &t : tasks) {
        if (t.executeAt == 0) {
            return 0; // at least one Task needs to be initialised immediately
        }
        long timeLeft = t.executeAt - now;
        if (timeLeft < 0) {
            // Task is ready to run
            return 0;
        }
        if (timeLeft < minTime) {
            minTime = timeLeft;
        }
    }
    return minTime;
}


void Scheduler::stop(){
    will_stop = true; 
    //collect all pids that are currently in the list
    for(auto &t : tasks){
        tasksToRemove.push_back(t.PID);
    }
}


// ----------------------------------------------------
// loop() method, keep at the end always
// ----------------------------------------------------

void Scheduler::loop() {

    // the beginning of the loop is a safe point to clear tasks marked for removal etc.
    // in a real concurrent setting it should be guarded with a mutex
    // but here we are in a single-threaded environment
    if (tasks.empty() || onHold) return;
    //in case stop is called outside of a task
    if(will_stop){ 
        #ifdef HIGHLY_VERBOSE
        Serial.println("Stopped scheduler from outside...");
        #endif
        will_stop = false;
        clearMarkedForRemoval();
        return;
    }
    if(tasksToRemove.size() > 0){
        clearMarkedForRemoval();
    }
    // now we enter the loop, so we should not be able to modify the task list anymore
    // while tasks might change it from within etc.
    ScopedFlag guard(inLoop);
    
    unsigned long now = millis();

    if (!sequentialMode) {
        // =========================================================
        // PARALLEL MODE
        // =========================================================

        std::vector<size_t> execIndices;
        std::vector<size_t> removeIndices;

        // We'll also do the "setup" for tasks that haven't been "activated" yet
        size_t originalSize = tasks.size();
        for (size_t i = 0; i < originalSize; i++) {
            Task &t = tasks[i];

            // If t.executeAt == 0 => we haven't set up the wait 
            // for condition or postConditionDelay
            if (t.executeAt == 0) {
                #ifdef HIGHLY_VERBOSE
                Serial.println("Setting up task...");
                Serial.println(i);
                #endif
                // 1) We first check if condition is trivially true 
                //    (like "purely timed" => condition always true => 
                //    conditionWait=0 => we skip directly).
                
                if (t.indefinite()) {
                    // indefinite wait => no "deadline" for condition
                    // if condition is not met yet, we keep checking
                    // if condition is met, we set postConditionDelay
                    if (t.conditionTrue()) {
                        // condition is instantly met => set executeAt = now + postConditionDelay
                        t.conditionMet = true;
                        t.setExecutionTime(now + t.postConditionDelay);
                        #ifdef HIGHLY_VERBOSE
                        Serial.print("Condition met, setting postConditionDelay:");
                        Serial.println(t.postConditionDelay);
                        #endif
                    } 
                } 
                else {
                    // we have a finite conditionWait => set a "deadline" for condition
                    t.setExecutionTime( now + t.conditionWait);
                    // but we haven't met condition yet, so we'll check in next pass
                }
            }
        }

        // Now do a second pass to see which tasks are ready to either:
        //  - run if conditionMet and now >= postConditionDelay
        //  - or become conditionMet if condition is now true
        //  - or time out if we passed the conditionWait
        originalSize = tasks.size(); // re-check in case new tasks added
        for (size_t i = 0; i < originalSize; i++) {
            Task &t = tasks[i];
            
            if (!t.condition) {
                Serial.println("ERROR: Task has no condition!");
                // set it to trivially true
                t.condition = [](){return true;};
            }

            if (!t.conditionMet) {
                // Condition not yet met
                if (t.conditionTrue()) {
                    // Condition just became true => set conditionMet
                    t.conditionMet = true;
                    // Now we do postConditionDelay
                    // if t.executeAt was a "condition deadline," we ignore it
                    t.setExecutionTime(now + t.postConditionDelay);
                } 
                else {
                    // condition still false => check if we timed out
                    if (!t.indefinite()) {
                        if ((long)(now - t.executeAt) >= 0) {
                            // timed out => remove
                            removeIndices.push_back(i);
                        }
                    }
                }
            } 
            else {
                // conditionMet => we are waiting for "executeAt"
                if ((long)(now - t.executeAt) >= 0) {
                    #ifdef HIGHLY_VERBOSE
                    Serial.println("Scheduling execution of task...");
                    Serial.println(i);
                    #endif
                    // time to run the action
                    execIndices.push_back(i);
                }
            }
        }
        

        // Execute tasks
        for (auto idx : execIndices) {
            if (idx < tasks.size()) {
                Task &t = tasks[idx];
                #ifdef HIGHLY_VERBOSE
                Serial.println("Executing task...");
                Serial.println(idx);
                #endif
                t.action();
                if(will_stop){
                    //now, we might have added to the task list within action.
                    //we want to keep those new tasks, but mark all others for removal
                    //and don't execute them
                    will_stop = false;
                    #ifdef HIGHLY_VERBOSE
                    Serial.println("Stopping scheduler from inside...");
                    #endif
                    execIndices.clear();
                    for(const auto pid : tasksToRemove){
                        auto it = std::find_if(tasks.begin(), tasks.end(), [pid](const Task &t) { return t.PID == pid; });
                        if (it != tasks.end()) {
                            it->repeat = false;
                            execIndices.push_back(it - tasks.begin());
                        }
                    }
                    tasksToRemove.clear();
                    break; 
                }
                //stop execution if stop was called from within a task,
                //clear will happen later anyway so rest can run through
            }
        }

        // Remove or reschedule tasks
        for (auto idx : execIndices) {
            if (idx < tasks.size()) {
                Task &t = tasks[idx];
                if (t.repeat) {
                    // For repeated tasks => reset condition (?), recheck from scratch
                    t.conditionMet = false;
                    t.postConditionDelay = t.interval;//set to interval
                    t.executeAt = 0;//set it to a fresh state
                } else {
                    removeIndices.push_back(idx);
                }
            }
        }

        // now at the end of the loop we can modify the task list again.
        // Sort removeIndices descending, unique them, remove
        std::sort(removeIndices.begin(), removeIndices.end(), std::greater<size_t>());
        removeIndices.erase(std::unique(removeIndices.begin(), removeIndices.end()), removeIndices.end());
        for (auto idx : removeIndices) {
            if (idx < tasks.size()) {
                tasks.erase(tasks.begin() + idx);
            }
        }
    } // end of parallel mode, there is nothing after this
    else {
        // =========================================================
        // SEQUENTIAL MODE: only front matters
        // =========================================================
        Task &t = tasks.front();

        // If t.executeAt == 0 => not "activated" yet
        if (t.executeAt == 0) {
            // We'll set times relative to lastSequentialFinishTime
            unsigned long baseTime = lastSequentialFinishTime;

            if (!t.condition) {
                // Shouldn't happen, safety check
                t.condition = [](){return true;};
            }

            if (!t.indefinite()) {
                // We have a finite wait => set the condition wait deadline
                t.setExecutionTime(baseTime + t.conditionWait);
            }
        }

        bool removeThisTask = false;
        bool executeThisTask = false;

        if (!t.conditionMet) {
            // Condition not yet met
            if (t.conditionTrue()) {
                // Just became true => set conditionMet => schedule postConditionDelay
                t.conditionMet = true;
                t.setExecutionTime(now + t.postConditionDelay);
            } 
            else {
                // not met => check if we timed out
                if (!t.indefinite()) {
                    if ((long)(now - t.executeAt) >= 0) {
                        // timed out => remove
                        removeThisTask = true;
                    }
                }
            }
        }
        else {
            // condition was met => check if now >= t.executeAt
            if ((long)(now - t.executeAt) >= 0) {
                // run
                executeThisTask = true;
            }
        }

        if (removeThisTask) {
            tasks.erase(tasks.begin());
            lastSequentialFinishTime = now;
            return;
        }
        else if (executeThisTask) {
            t.action();
            // also here we might have called stop from within a task, but might have also added new ones
            // so we need to clear all tasks that existed before the action call
            if(will_stop){
                will_stop = false;
                #ifdef HIGHLY_VERBOSE
                Serial.println("Stopping scheduler from inside, clearing old tasks...");
                #endif
                //erase all asks in removal list
                for(const auto pid : tasksToRemove){
                    auto it = std::find_if(tasks.begin(), tasks.end(), [pid](const Task &t) { return t.PID == pid; });
                    // NEW: only remove tasks that are not the currently handled one
                    if (it != tasks.end() && it != tasks.begin()) {
                        //here we can erase directly as only this task will run in this loop
                        tasks.erase(it);
                    }
                }
                tasksToRemove.clear();
                lastSequentialFinishTime = now;
                return;
            }
            // repeating => not allowed => remove
            tasks.erase(tasks.begin());
            lastSequentialFinishTime = now;
            return;
        }
        else {
            // Not ready => do nothing
            return;
        }
    }
}