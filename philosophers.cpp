#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <thread>
#include <iterator>
#include <string>
#include <cstdint>

#undef DEATH
namespace philosophers {

/// Fork is simple mutex
typedef std::mutex Fork;
using std::chrono::steady_clock;

class Monitor;
class Philosopher;
std::ostream&
dump(std::ostream&, Philosopher const&);
class Philosopher
{
public:
    enum class States
    {
        thinks,
        hungry,
        dines,
#ifdef DEATH
        dead
#endif
    };

    Philosopher(unsigned _id, std::shared_ptr<Fork> const& _p_left, std::shared_ptr<Fork> const& _p_right, Monitor* _p_canteen)
        : m_id(_id)
        , m_state(States::thinks)
        , m_p_left_fork(_p_left)
        , m_p_right_fork(_p_right)
        , m_p_monitor(_p_canteen)
        , m_event_wait_lock(this->m_event_mutex)
#ifdef DEATH
        , m_last_eating(steady_clock::now())
#endif
    {}

    unsigned
        id()const
    {
        return m_id;
    }
    void
        operator()()
    {
        try {
            m_thread_id = std::this_thread::get_id();
#if 0
            std::this_thread::sleep_for(std::chrono::milliseconds(id() * 100));
            dump(std::cerr, *this);
            std::this_thread::sleep_for(std::chrono::milliseconds((200 - id()) * 100 + 5000));
#endif
            for (;;) {
                thinking();
                aquire_forks();
                eating();
            }
        } catch (...) {
            std::cerr << "Catch unhandled exception in philosopher id=" << id() << std::endl;
        }
    }

    /// common thread worker
    static void
        worker(std::shared_ptr<Philosopher> const& _p_philosopfer)
    {
        (*_p_philosopfer)();
    }

    States
        state()const
    {
        return m_state;
    }

    std::thread::id
        thread_id()const
    {
        return m_thread_id;
    }

private:
    void
        thinking()
    {
        state(States::thinks);
        std::this_thread::sleep_for(random_interval());
    }

    void
        aquire_forks()
    {
        state(States::hungry);
        for (;; m_free_fork_event.wait(m_event_wait_lock)) {
            int const index = std::try_lock(*m_p_left_fork, *m_p_right_fork);
            if (-1 == index) {
                return;
            }
            check_for_death();
        }
    }
    void
        check_for_death()
    {
#ifdef DEATH
        using namespace std::chrono;
        milliseconds const time_span = duration_cast<milliseconds>(steady_clock::now() - this->m_last_eating);
        if (milliseconds(m_death_threshold * m_max_interval_ms) < time_span) {
            state(States::dead);
            for (;;) {
                std::this_thread::sleep_for(seconds(60));
            }
        }
#endif            
    }
    void
        eating()
    {
        state(States::dines);
        std::this_thread::sleep_for(random_interval());
        m_p_left_fork->unlock();
        m_p_right_fork->unlock();
        m_free_fork_event.notify_all();
#ifdef DEATH
        m_last_eating = steady_clock::now();
#endif
    }
    std::chrono::milliseconds
        random_interval()const
    {
        /// @brief mutex protected random generator
        class Random_generator
            : public std::default_random_engine
        {
            typedef std::default_random_engine Base_class;
        public:
            Random_generator()
                : Base_class(result_type(std::chrono::system_clock::now().time_since_epoch().count()))
            {}
            result_type
                operator()()
            {
                std::lock_guard<decltype(m_mutex)> guard(m_mutex);
                return Base_class::operator()();
            }

        private:
            std::mutex m_mutex;
        };
        static Random_generator g_rnd;
        static std::uniform_int_distribution<unsigned> distribution(1, m_max_interval_ms);
        return std::chrono::milliseconds(distribution(g_rnd));
    }
    inline void
        state(States _state);

    unsigned m_id;
    States m_state;
public:
    std::shared_ptr<Fork> m_p_left_fork;
    std::shared_ptr<Fork> m_p_right_fork;

private:
    Monitor* m_p_monitor;
    std::mutex m_event_mutex;
    std::unique_lock<decltype(m_event_mutex)> m_event_wait_lock;
    std::thread::id m_thread_id;

#ifdef DEATH
    steady_clock::time_point m_last_eating;
    static unsigned const m_death_threshold = 4;
#endif

    static std::condition_variable m_free_fork_event;

public:
    static unsigned m_max_interval_ms;
    friend std::ostream&
        dump(std::ostream&, Philosopher const&);
};

unsigned Philosopher::m_max_interval_ms = 10000;
std::condition_variable Philosopher::m_free_fork_event;

std::ostream&
dump(std::ostream& str, uint8_t const* const p_begin, uint8_t const* const p_end)
{
    std::for_each(p_begin, p_end, [&str](uint8_t const val) {str << std::setw(2) << std::setfill('0') << std::hex << static_cast<unsigned>(val) << ' '; });
    str << std::setw(0) << std::setfill(' ') << std::dec;
    return str;
}

#if 0
std::ostream&
dump(std::ostream& str, uint32_t const* const p_begin, uint32_t const* const p_end)
{
    std::for_each(p_begin, p_end, [&str](uint32_t const val) {str << std::setw(8) << std::setfill('0') << std::hex << val << ' '; });
    str << std::setw(0) << std::setfill(' ') << std::dec;
    return str;
}
#endif

std::ostream&
dump(std::ostream& str, Philosopher const& ph)
{
    str 
        << "Thread id=" << std::hex << ph.thread_id() << std::dec
        << " philosopher id=" << ph.id() << " state=";
    switch (ph.state()) {
    case Philosopher::States::thinks:
        std::cout << "thinks";
        break;
    case Philosopher::States::hungry:
        std::cout << "hungry";
        break;
    case Philosopher::States::dines:
        std::cout << "dines";
        break;
#ifdef DEATH
    case Philosopher::States::dead:
        std::cout << "die";
        break;
#endif
    default:
        std::cout << "?????";
        break;
    }
    std::cout << std::endl;

    return str;
}
class Monitor
{
public:
    typedef std::pair<unsigned, Philosopher::States> state_log_element_type;
    void
        log_state(Philosopher const* _p_philosopher)
    {
        if (!_p_philosopher) {
            return;
        }
        {
            std::lock_guard<decltype(m_log_queue_mutex)> locker(m_log_queue_mutex);
            m_log_queue.emplace_back(_p_philosopher->id(), _p_philosopher->state());
        }
        m_state_logged_event.notify_one();
    }
    void
        monitor_worker()
    {
        std::mutex event_mutex;
        std::unique_lock<decltype(event_mutex)> locker(event_mutex);
        decltype(m_log_queue) work_log;
        for (;;) {
            if (!m_log_queue.empty()) {
                {
                    std::lock_guard<decltype(m_log_queue_mutex)> locker(m_log_queue_mutex);
                    std::swap(work_log, m_log_queue);
                }
                events_logger(work_log);
                work_log.clear();
            } else if (std::cv_status::timeout == m_state_logged_event.wait_for(locker, std::chrono::milliseconds(10 * Philosopher::m_max_interval_ms))) {
                break;
            }
        }
        std::cerr << "m_log_queue_mutex: ";
        dump(std::cerr, reinterpret_cast<uint8_t const*>(&m_log_queue_mutex), reinterpret_cast<uint8_t const*>(&m_log_queue_mutex + 1));
        std::cerr << std::endl;
    }
    size_t
        queue_size()const
    {
        std::lock_guard<decltype(m_log_queue_mutex)> locker(m_log_queue_mutex);
        return m_log_queue.size();
    }
protected:
    typedef std::vector<state_log_element_type> log_queue_type;
    virtual void
        events_logger(log_queue_type const& work_log) = 0;

    std::mutex mutable m_log_queue_mutex;
    std::condition_variable m_state_logged_event;
    log_queue_type m_log_queue;
};

void
Philosopher::state(States _state)
{
    m_state = _state;
    if (m_p_monitor) {
        m_p_monitor->log_state(this);
    }
}

class Canteen
{
public:
    explicit
        Canteen(Monitor& _monitor, unsigned _number_of_philosophers)
        : m_p_monitor(&_monitor)
    {
        if (_number_of_philosophers < 2) {
            throw std::invalid_argument("Invalid number of philosophers (<2)");
        }
        std::vector<std::shared_ptr<Fork> > forks;
        forks.reserve(_number_of_philosophers);
        std::generate_n(std::back_inserter(forks), _number_of_philosophers, []() {return std::make_shared<Fork>(); });
        {
            unsigned id = 0;
            std::for_each(forks.cbegin(), forks.cend(), [this,&id](std::shared_ptr<Fork> const& p_f) {
                std::cerr << "Fork #" << id++ << " @" << &*p_f << ": ";
                dump(std::cerr, reinterpret_cast<uint8_t const*>(&*p_f), reinterpret_cast<uint8_t const*>((&*p_f) + 1));
                std::cerr << std::endl;
            });
        }

        m_philosophers.reserve(_number_of_philosophers);
        for (unsigned i = 0; i < _number_of_philosophers; ++i) {
            m_philosophers.push_back(std::make_shared<Philosopher>(
                i,
                forks[i],
                forks[(i + 1) % _number_of_philosophers],
                m_p_monitor));
        }
    }
    void
        operator()()
    {
        std::vector<std::thread> threads;
        threads.reserve(m_philosophers.size());
        std::transform(m_philosophers.cbegin(), m_philosophers.cend(), std::back_inserter(threads),
                       [](std::shared_ptr<Philosopher> const& ptr) {return std::thread(Philosopher::worker, ptr); });
#if 0
        std::this_thread::sleep_for(std::chrono::milliseconds(200 * 100 + 5000));
#endif
        full_dump();
        m_p_monitor->monitor_worker();
        full_dump();
        exit(-1);
    }

private:
    void
        full_dump()
    {
        std::cerr << "queue_size = " << m_p_monitor->queue_size() << std::endl;
        for (auto const& p_ph : m_philosophers) {
            std::cerr << "Fork #" << p_ph->id() << " @" << &*p_ph->m_p_left_fork << ": ";
            dump(std::cerr, reinterpret_cast<uint8_t const*>(&*p_ph->m_p_left_fork), reinterpret_cast<uint8_t const*>((&*p_ph->m_p_left_fork) + 1));
            std::cerr << std::endl;
            dump(std::cerr, *p_ph);
        }
        std::cerr << "queue_size = " << m_p_monitor->queue_size() << std::endl;
    }

    std::vector<std::shared_ptr<Philosopher> > m_philosophers;
    Monitor *const m_p_monitor;
};

class Simple_log_monitor
    : public Monitor
{
protected:
    virtual void
        events_logger(log_queue_type const& work_log)
    {
        auto const log_event = [](log_queue_type::value_type const& el) {
            std::cout << "Philosopher #" << el.first << " ";
            switch (el.second) {
            case Philosopher::States::thinks:
                std::cout << "thinks";
                break;
            case Philosopher::States::hungry:
                std::cout << "hungry";
                break;
            case Philosopher::States::dines:
                std::cout << "dines";
                break;
#ifdef DEATH
            case Philosopher::States::dead:
                std::cout << "die";
                break;
#endif
            default:
                std::cout << "?????";
                break;
            }
            std::cout << std::endl;
        };
        std::for_each(std::begin(work_log), std::end(work_log), log_event);
    }
};

class Waterfall_monitor
    : public Monitor
{
protected:
    virtual void
        events_logger(log_queue_type const& work_log)override
    {
        auto const log_event = [this](log_queue_type::value_type const& el) {
            if (m_buffer.size() <= el.first) {
                m_buffer.append(el.first + 1 - m_buffer.size(), symb(Philosopher::States::thinks));
            }
            m_buffer[el.first] = symb(el.second);
        };
        std::for_each(std::begin(work_log), std::end(work_log), log_event);
        std::cout << m_buffer << std::endl;
    }

private:
    static char
        symb(Philosopher::States _state)
    {
        switch (_state) {
        case Philosopher::States::thinks:
            return ' ';
            break;
        case Philosopher::States::hungry:
            return '-';
            break;
        case Philosopher::States::dines:
            return '|';
            break;
#ifdef DEATH
        case Philosopher::States::dead:
            return '#';
            break;
#endif
        default:
            return '?';
            break;
        }
    }
    std::string m_buffer;
};

}  // namespace philosophers

int
main(int argc, char* argv[])
{
    try {
        using namespace philosophers;
        unsigned const num_ph = argc < 2 ? 64 : atoi(argv[1]);
        Philosopher::m_max_interval_ms = argc < 3 ? 10000 : std::max(2, atoi(argv[2]));
        Waterfall_monitor wf_monitor;
        Canteen canteen(wf_monitor, num_ph);
        canteen();
        return 0;
    } catch (std::exception const& exc) {
        std::cerr << "Unhandled std::exception: " << exc.what() << std::endl;
    } catch (...) {
        std::cerr << "Unhandled unknown exception" << std::endl;
    }
    return 1;
}
