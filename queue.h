#include <stdint.h>

namespace udonyang
{
enum
{
    QUEUE_ERR_OK = 0,
    QUEUE_ERR_FULL = 1,
    QUEUE_ERR_EMPTY = 2,
    QUEUE_ERR_AGAIN = 3,
    QUEUE_ERR_LOCKED = 4,
    QUEUE_ERR_DEADLOCK = 5,
};

template<typename Data, const uint32_t size = 1<<10, const uint32_t spintime = 1<<24>
class Queue
{
public:
    Queue(): meta_(0)
    {
    }

    ~Queue()
    {
    }

    int Push(const Data& data)
    {
        int ret = 0;
        uint64_t firstmeta = meta_;
        for (int i = 0; i < spintime; i++)
        {
            uint64_t meta = meta_;
            uint32_t head = 0;
            uint32_t tail = 0;
            uint32_t lock = 0;
            Inflate(meta, &head, &tail, &lock);

            uint32_t index = tail;
            tail = tail+1 == size? 0: tail+1;
            if (tail == head)
            {
                return QUEUE_ERR_FULL;
            }

            if (lock == 1)
            {
                ret = QUEUE_ERR_LOCKED;
                continue;
            }

            uint64_t lockmeta = Deflate(head, index, 1);
            if (!__sync_bool_compare_and_swap(&meta_, meta, lockmeta))
            {
                ret = QUEUE_ERR_AGAIN;
                continue;
            }

            queue_[index] = data;

            uint64_t unlockmeta = Deflate(head, tail, 0);
            if (!__sync_bool_compare_and_swap(&meta_, lockmeta, unlockmeta))
            {
                return QUEUE_ERR_DEADLOCK;
            }

            return QUEUE_ERR_OK;
        }

        if (ret == QUEUE_ERR_LOCKED && firstmeta == meta_)
        {
            return ReleaseLock(firstmeta);
        }

        return QUEUE_ERR_AGAIN;
    }

    int Pop(Data* data)
    {
        int ret = 0;
        uint64_t firstmeta = meta_;
        for (int i = 0; i < spintime; i++)
        {
            uint64_t meta = meta_;
            uint32_t head = 0;
            uint32_t tail = 0;
            uint32_t lock = 0;
            Inflate(meta, &head, &tail, &lock);

            if (head == tail)
            {
                return QUEUE_ERR_EMPTY;
            }
            uint32_t index = head;
            head = head+1 == size? 0: head+1;

            if (lock == 1)
            {
                ret = QUEUE_ERR_LOCKED;
                continue;
            }

            uint64_t lockmeta = Deflate(index, tail, 1);
            if (!__sync_bool_compare_and_swap(&meta_, meta, lockmeta))
            {
                ret = QUEUE_ERR_AGAIN;
                continue;
            }

            *data = queue_[index];

            uint64_t unlockmeta = Deflate(head, tail, 0);
            if (!__sync_bool_compare_and_swap(&meta_, lockmeta, unlockmeta))
            {
                return QUEUE_ERR_DEADLOCK;
            }

            return QUEUE_ERR_OK;
        }

        if (ret == QUEUE_ERR_LOCKED && firstmeta == meta_)
        {
            return ReleaseLock(firstmeta);
        }

        return QUEUE_ERR_AGAIN;
    }

    uint32_t Size()
    {
        uint32_t head = 0;
        uint32_t tail = 0;
        uint32_t lock = 0;
        Inflate(meta_, &head, &tail, &lock);
        if (head <= tail)
        {
            return tail-head;
        }
        return tail+size-head;
    }

protected:
    void Inflate(uint64_t meta, uint32_t* head, uint32_t* tail, uint32_t* lock)
    {
        *lock = meta&0xff;
        *head = (meta>>8)>>28;
        *tail = (meta>>8)&0xfffffff;
        // fprintf(stderr, "inflate %lu=[%lu, %u, %u]\n", meta, *head, *tail, *lock);
    }

    uint64_t Deflate(uint64_t head, uint32_t tail, uint32_t lock)
    {
        // fprintf(stderr, "deflate [%lu, %u, %u]\n", head, tail, lock);
        return (head<<28|tail)<<8|lock;
    }

    int ReleaseLock(uint64_t meta)
    {
        uint32_t head = 0;
        uint32_t tail = 0;
        uint32_t lock = 0;
        Inflate(meta, &head, &tail, &lock);

        if (!__sync_bool_compare_and_swap(&meta_, meta, Deflate(head, tail, 0)))
        {
            return QUEUE_ERR_AGAIN;
        }

        // fprintf(stderr, "fuck\n");
        return QUEUE_ERR_OK;
    }

protected:
    uint64_t meta_;
    uint32_t head_;
    uint32_t tail_;
    Data queue_[size];
};

}
