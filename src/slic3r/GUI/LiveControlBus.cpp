#include "LiveControlBus.hpp"

#include <cstring>

#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace Slic3r::GUI {

namespace {
constexpr uint32_t MAGIC = 0x544D5143; // TMQC
constexpr uint16_t VERSION = 1;

#pragma pack(push, 1)
struct WireCommand
{
    uint32_t magic;
    uint16_t version;
    uint16_t opcode;
    uint32_t value_bits;
};
#pragma pack(pop)

#ifdef __linux__
bool read_full(int fd, void* buffer, size_t size)
{
    auto* bytes = static_cast<unsigned char*>(buffer);
    size_t offset = 0;
    while (offset < size) {
        const ssize_t count = ::read(fd, bytes + offset, size - offset);
        if (count <= 0)
            return false;
        offset += static_cast<size_t>(count);
    }
    return true;
}
#endif
} // namespace

LiveControlBus::LiveControlBus(Handler handler)
    : m_handler(std::move(handler))
{
#ifdef __linux__
    m_socket_path = "/tmp/tonimcqueen-slicer-" + std::to_string(static_cast<unsigned long>(::getuid())) + ".sock";
#endif
}

LiveControlBus::~LiveControlBus()
{
    stop();
}

void LiveControlBus::start()
{
#ifdef __linux__
    if (!m_thread.joinable()) {
        m_stop = false;
        m_thread = std::thread(&LiveControlBus::run, this);
    }
#endif
}

void LiveControlBus::stop()
{
#ifdef __linux__
    m_stop = true;
    if (m_server_fd >= 0) {
        ::shutdown(m_server_fd, SHUT_RDWR);
        ::close(m_server_fd);
        m_server_fd = -1;
    }
    if (m_thread.joinable())
        m_thread.join();
    if (!m_socket_path.empty())
        ::unlink(m_socket_path.c_str());
#endif
}

void LiveControlBus::run()
{
#ifdef __linux__
    ::unlink(m_socket_path.c_str());
    m_server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_server_fd < 0)
        return;

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, m_socket_path.c_str(), sizeof(address.sun_path) - 1);
    if (::bind(m_server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || ::listen(m_server_fd, 4) < 0) {
        ::close(m_server_fd);
        m_server_fd = -1;
        return;
    }

    while (!m_stop) {
        const int client = ::accept(m_server_fd, nullptr, nullptr);
        if (client < 0) {
            if (m_stop)
                break;
            continue;
        }
        WireCommand wire{};
        if (read_full(client, &wire, sizeof(wire)) && wire.magic == MAGIC && wire.version == VERSION && m_handler)
            m_handler({wire.opcode, wire.value_bits});
        ::close(client);
    }
#endif
}

} // namespace Slic3r::GUI
