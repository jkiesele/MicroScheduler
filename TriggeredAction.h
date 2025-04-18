#include <Arduino.h>
#include <functional>


class TriggeredAction {
    private:
        bool notified;      // True if the trigger notification has been sent (and we’re waiting for a reset)
        bool resetNotified; // True if the reset notification has been sent; used to prevent repeated resets
    
        std::function<bool()> triggerCondition;  // e.g., temperature < 5°C.
        std::function<bool()> resetCondition;    // e.g., temperature > 7°C.
        std::function<void()> notifyAction;        // Custom action for trigger (e.g., send email)
        std::function<void()> notifyResetAction;   // Custom action for reset (e.g., send "back to normal" email)
    
    public:
        TriggeredAction(std::function<bool()> trigger,
                      std::function<bool()> reset,
                      std::function<void()> notify,
                      std::function<void()> resetNotify)
            : notified(false),
              resetNotified(false),
              triggerCondition(trigger),
              resetCondition(reset),
              notifyAction(notify),
              notifyResetAction(resetNotify)
        {}

        TriggeredAction() : notified(false), resetNotified(false) {}
    
        // Called periodically by the scheduler.
        void checkAndNotify() {
            if (!notified) {
                // Not triggered yet: check if the trigger condition is met.
                if (triggerCondition()) {
                    notifyAction();
                    notified = true;
                    resetNotified = false; // clear reset flag so we can notify on reset
                }
            } else { // Already triggered—waiting for reset.
                if (resetCondition()) {
                    // Only send the reset notification once.
                    if (!resetNotified) {
                        notifyResetAction();
                        resetNotified = true;
                    }
                    // Optionally, you may clear the trigger so that a new event can be sent.
                    // For instance, if you want the next trigger to occur only after a reset,
                    // you can do:
                    notified = false;
                }
            }
        }
    };

typedef TriggeredAction EventNotifier;//compat