#include <ftl/lib/ctpl_stl.hpp>
#include <ftl/threads.hpp>
#include <string>

void ctpl::thread_pool::set_thread(int i) {
    std::shared_ptr<std::atomic<bool>> flag(this->flags[i]);  // a copy of the shared ptr to the flag
    auto f = [this, i, flag/* a copy of the shared ptr to the flag */]() {
        {
            const auto thread_name = "thread_pool/" + std::to_string(i);
            ftl::set_thread_name(thread_name);
        }
        std::atomic<bool> & _flag = *flag;
        std::function<void(int id)> * _f;
        bool isPop = this->q.pop(_f);
        while (true) {
            while (isPop) {  // if there is anything in the queue
                std::unique_ptr<std::function<void(int id)>> func(_f);  // at return, delete the function even if an exception occurred
                (*_f)(i);
                if (_flag)
                    return;  // the thread is wanted to stop, return even if the queue is not empty yet
                else
                    isPop = this->q.pop(_f);
            }
            // the queue is empty here, wait for the next command
            std::unique_lock<std::mutex> lock(this->mutex);
            ++this->nWaiting;
            this->cv.wait(lock, [this, &_f, &isPop, &_flag](){ isPop = this->q.pop(_f); return isPop || this->isDone || _flag; });
            --this->nWaiting;
            if (!isPop)
                return;  // if the queue is empty and this->isDone == true or *flag then return
        }
    };
    this->threads[i].reset(new std::thread(f));  // compiler may not support std::make_unique()
}
