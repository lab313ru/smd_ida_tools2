#pragma once

//
//
//      This is the base debmod_t class definition
//      From this class all debugger code must inherite and specialize
//
//      Some OS specific functions must be implemented:
//        bool init_subsystem();
//        bool term_subsystem();
//        debmod_t *create_debug_session();
//        int create_thread(thread_cb_t thread_cb, void *context);
//

#undef INLINE

#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <ida.hpp>
#include <idd.hpp>

//--------------------------------------------------------------------------
// Very simple class to store pending events
enum queue_pos_t {
  IN_FRONT,
  IN_BACK
};

class eventlist_t {
private:
  std::deque<debug_event_t> events_;
  std::mutex mutex_;
  std::condition_variable ready_;
  bool closed_ = false;
public:
  // save a pending event
  void enqueue(const debug_event_t &ev, queue_pos_t pos) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_)
        return;
      if (pos != IN_BACK)
        events_.push_front(ev);
      else
        events_.push_back(ev);
    }
    ready_.notify_one();
  }

  // retrieve a pending event
  gdecode_t retrieve(debug_event_t *event, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto available = [this] { return closed_ || !events_.empty(); };
    if (timeout_ms < 0)
      ready_.wait(lock, available);
    else if (timeout_ms > 0)
      ready_.wait_for(lock, std::chrono::milliseconds(timeout_ms), available);
    if (events_.empty())
      return GDE_NO_EVENT;
    *event = events_.front();
    events_.pop_front();
    return events_.empty() ? GDE_ONE_EVENT : GDE_MANY_EVENTS;
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
    closed_ = false;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
      events_.clear();
    }
    ready_.notify_all();
  }
};
