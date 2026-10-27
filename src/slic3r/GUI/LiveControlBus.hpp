#ifndef SLIC3R_GUI_LIVE_CONTROL_BUS_HPP
#define SLIC3R_GUI_LIVE_CONTROL_BUS_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace Slic3r::GUI {

struct LiveControlCommand
{
    uint16_t opcode{0};
    uint32_t value_bits{0};
};

class LiveControlBus
{
public:
    using Handler = std::function<void(LiveControlCommand)>;

    explicit LiveControlBus(Handler handler);
    ~LiveControlBus();

    LiveControlBus(const LiveControlBus&) = delete;
    LiveControlBus& operator=(const LiveControlBus&) = delete;

    void start();
    void stop();
    const std::string& socket_path() const { return m_socket_path; }

private:
    void run();

    Handler          m_handler;
    std::string      m_socket_path;
    std::atomic_bool m_stop{false};
    int              m_server_fd{-1};
    std::thread      m_thread;
};

} // namespace Slic3r::GUI

#endif
