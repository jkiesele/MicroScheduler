#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <vector>
#include <functional>

/*
  A single unified Task struct:

   - condition: a function<bool()> 
       (if null, we treat it as always true,
        but you can store a trivial lambda for clarity)
   - conditionWait: how long we wait for the condition to become true.
       If <= 0 => indefinite wait
   - postConditionDelay: once condition is met, how long we wait until we run.
   - repeat & interval: if in parallel mode, can repeat. 
     Not supported in sequential mode => forcibly disabled.
*/
// typedef PID from uin16_t, enough for 65535 tasks, more than enough or all microcontroller purposes
// a PID is never zero (so we can use it as a "null" PID)
typedef uint16_t PID_t;


class ScopedFlag {
private:
    bool &flag;
public:
    ScopedFlag(bool &flag) : flag(flag) { flag = true; }
    ~ScopedFlag() { flag = false; }
};

class Scheduler {
private:
    struct Task {

        PID_t PID = 0;

        std::function<void()> action;
        
        // If repeat == true, we are in parallel mode only
        bool repeat = false;
        uint32_t interval = 0;

        // The condition to become true. If it's trivial "return true;" => purely timed
        std::function<bool()> condition = nullptr;
        bool conditionMet = false;

        // If conditionWait <= 0 => indefinite
        long conditionWait = 0;

        // Once condition is met => wait postConditionDelay before running
        uint32_t postConditionDelay = 0;

        // The absolute time (millis) at which the condition times out
        // or at which we run the action, depending on the stage.
        // We'll set this dynamically in the code.
        uint32_t executeAt = 0;

        // If we are waiting indefinitely for the condition, or no conditionWait set
        // this is always true for repeating tasks
        bool indefinite() const {
            return conditionWait <= 0;
        }
        bool conditionTrue() const {
            return condition && condition();
        }
        // Set a definite execution time
        void setExecutionTime(unsigned long time) {
            executeAt = (time == 0) ? 1 : time;
        }

    };

    // The container of tasks
    std::vector<Task> tasks;

    // If true => strictly one-at-a-time in order
    bool sequentialMode = false;

    // In sequential mode, tasks reference the time the previous task finished
    uint32_t lastSequentialFinishTime = 0;

    bool will_stop = false;
    std::vector<PID_t> tasksToRemove;

     // can be private now with changes to stop
    void clear() { tasks.clear(); }

    void clearMarkedForRemoval();

    PID_t nextPID = 1;
    PID_t getAndIncrementPID();

    bool onHold = false;
    bool inLoop = false;

public:
    // Constructor
    Scheduler();

    size_t taskCount() const { return tasks.size(); }

    // Switch modes
    void setAndStartSequentialMode(bool seq);
    bool isSequentialMode() const { return sequentialMode; }

    // The main update function
    void loop();

    //returns a maximum of 60s. If not all tasks are initialised, returns 0
    //otherwise returns time to next task in ms
    uint32_t timeToNextTask() const;

    void hold(){onHold = true;}
    void resume(){onHold = false;}

    // could be extended by tracking PIDs here instead of size
    void stop();
    // ----------------------------------------------------
    // Public Add Methods
    // ----------------------------------------------------

    // 1) "Purely Timed" => condition always true, no conditionWait needed
    //    user gives "delayMs" => that is your postConditionDelay
    //    If repeat is true, interval is how often it repeats (in parallel).
    PID_t addTimedTask(std::function<void()> action,
                      uint32_t delayMs,
                      bool repeat = false,
                      uint32_t interval = 0);

    // 2) "Conditional" => must become true within conditionWait
    //    postConditionDelay = 0 => run immediately after condition
    //    If conditionWait <= 0 => indefinite
    PID_t addConditionalTask(std::function<void()> action,
                            std::function<bool()> condition,
                            uint32_t conditionWaitMs = 0);

    // 3) "Conditional + Post Delay"
    //    Must become true within conditionWait; then wait postConditionDelay
    //    If conditionWait <= 0 => indefinite
    PID_t addConditionalTimedTask(std::function<void()> action,
                                 std::function<bool()> condition,
                                 uint32_t postDelayMs,
                                 uint32_t conditionWaitMs = 0);

    // ----------------------------------------------------
    // Public Task Manipulation Methods (restricted to a few)
    // These can only be executed outside of the loop
    // ----------------------------------------------------

    // Remove a task by PID (if it exists)
    // Returns true if the task was found and removed
    bool removeTask(PID_t pid);

    // Adapt a task repeat interval by PID
    // Returns true if the task was found, is a repeating task, and was updated
    bool setRepeatingTaskInterval(PID_t pid, uint32_t interval);

};

#endif