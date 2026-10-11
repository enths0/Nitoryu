// A fixed queue: push at the back, pop at the front, no allocation. A full queue refuses the new item and
// counts it. Not thread-safe by itself: the owner holds its lock around every call.
#pragma once

namespace core
{
    template <typename T, int N>
    class Ring
    {
    public:
        Ring() : head_(0), count_(0), dropped_(0) {}

        bool push(const T& v)
        {
            if (count_ == N)
            {
                ++dropped_;
                return false;
            }
            items_[(head_ + count_) % N] = v;
            ++count_;
            return true;
        }

        bool pop(T& out)
        {
            if (count_ == 0)
                return false;
            out = items_[head_];
            head_ = (head_ + 1) % N;
            --count_;
            return true;
        }

        int size() const { return count_; }
        // Items refused since the last call.
        unsigned takeDropped()
        {
            unsigned d = dropped_;
            dropped_ = 0;
            return d;
        }

    private:
        T items_[N];
        int head_, count_;
        unsigned dropped_;
    };
}
