#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

struct Motion {
    int x = 0;
    int y = 0;
};

[[noreturn]] void fail(std::string_view operation) {
    throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

auto parseNumber(std::string_view value, std::string_view name) -> int {
    int result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result < -32768 || result > 32767)
        throw std::runtime_error(std::string(name) + " must be between -32768 and 32767");
    return result;
}

auto parseMotion(std::string_view value) -> Motion {
    const auto comma = value.find(',');
    if (comma == std::string_view::npos)
        throw std::runtime_error("reports must look like 12,-4");
    return {
        .x = parseNumber(value.substr(0, comma), "x"),
        .y = parseNumber(value.substr(comma + 1), "y"),
    };
}

class UinputMouse {
  public:
    UinputMouse() {
        m_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (m_fd < 0)
            fail("open /dev/uinput");

        setBit(UI_SET_EVBIT, EV_REL);
        setBit(UI_SET_RELBIT, REL_X);
        setBit(UI_SET_RELBIT, REL_Y);
        setBit(UI_SET_EVBIT, EV_KEY);
        setBit(UI_SET_KEYBIT, BTN_LEFT);
        setBit(UI_SET_EVBIT, EV_SYN);

        uinput_setup setup{};
        std::strncpy(setup.name, "windows-pointer-linux test mouse", UINPUT_MAX_NAME_SIZE - 1);
        setup.id.bustype = BUS_USB;
        setup.id.vendor  = 0x1d6b;
        setup.id.product = 0x0104;
        setup.id.version = 1;

        if (ioctl(m_fd, UI_DEV_SETUP, &setup) < 0)
            fail("UI_DEV_SETUP");
        if (ioctl(m_fd, UI_DEV_CREATE) < 0)
            fail("UI_DEV_CREATE");
        m_created = true;
    }

    UinputMouse(const UinputMouse&)            = delete;
    UinputMouse& operator=(const UinputMouse&) = delete;

    ~UinputMouse() {
        if (m_created)
            ioctl(m_fd, UI_DEV_DESTROY);
        if (m_fd >= 0)
            close(m_fd);
    }

    void move(Motion motion) {
        emit(EV_REL, REL_X, motion.x);
        emit(EV_REL, REL_Y, motion.y);
        emit(EV_SYN, SYN_REPORT, 0);
    }

  private:
    void setBit(unsigned long request, int value) {
        if (ioctl(m_fd, request, value) < 0)
            fail("uinput capability");
    }

    void emit(std::uint16_t type, std::uint16_t code, std::int32_t value) {
        input_event event{};
        event.type  = type;
        event.code  = code;
        event.value = value;
        if (write(m_fd, &event, sizeof(event)) != sizeof(event))
            fail("write input report");
    }

    int  m_fd      = -1;
    bool m_created = false;
};

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2)
            throw std::runtime_error("usage: windows-pointer-uinput x,y [x,y ...]");

        UinputMouse mouse;

        // libinput and the compositor need to discover the new device before it
        // starts reporting. hardware also fails when used before it exists.
        std::this_thread::sleep_for(750ms);
        for (int index = 1; index < argc; ++index) {
            mouse.move(parseMotion(argv[index]));
            std::this_thread::sleep_for(10ms);
        }
        std::this_thread::sleep_for(100ms);
    } catch (const std::exception& error) {
        std::cerr << "windows-pointer-uinput: " << error.what() << '\n';
        return 1;
    }
}
