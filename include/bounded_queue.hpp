#pragma once
#include "policy.hpp"
#include <deque>
#include <mutex>
#include <condition_variable>

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity), stopped_(false) {}

    // Returns 1 if an entry was dropped, 0 otherwise.
    int push(T item, OverflowPolicy policy) {
        std::unique_lock<std::mutex> lk(mutex_);
        int undelivered = 0;

        if (data_.size() >= capacity_) {
            switch (policy) {
                case OverflowPolicy::DROP_NEWEST:
                    return 1;

                case OverflowPolicy::DROP_OLDEST:
                    data_.pop_front();   // evict oldest; it will never be dispatched
                    undelivered = 1;
                    break;

                case OverflowPolicy::BLOCK:
                    not_full_.wait(lk, [this] {
                        return data_.size() < capacity_ || stopped_;
                    });
                    if (stopped_) return 1;
                    break;
            }
        }
        // Initial code with bug
        // data_.push_back(std::move(item));
        // lk.unlock();
        // not_empty_.notify_one();
        
        // Fixed code
        bool was_empty = data_.empty();
        data_.push_back(std::move(item));                                                                                                                                                                                
        lk.unlock();                                                                                                                                                                                                     
        if (was_empty)                                                                                                                                                                                                   
            not_empty_.notify_one();   // only wake consumer when queue was empty
        return undelivered;
    }

    bool drain(std::deque<T>& out) {
        std::unique_lock<std::mutex> lk(mutex_);
        not_empty_.wait(lk, [this] {
            return !data_.empty() || stopped_;
        });

        if (data_.empty()) return false;  // stopped and empty

        out.swap(data_);
        lk.unlock();
        not_full_.notify_all();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            stopped_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return data_.empty();
    }

private:
    mutable std::mutex      mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::deque<T>           data_;
    std::size_t             capacity_;
    bool                    stopped_;
};
