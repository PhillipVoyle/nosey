#include <iostream>
#include <deque>
#include <cstdint>
#include <functional>
#include <vector>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include <memory>
#include <algorithm>

#ifdef _WIN32
#include <WinSock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")
typedef int socklen_t;
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

typedef int SOCKET;
const SOCKET INVALID_SOCKET = -1;
#endif

class connection;

typedef std::function<void (const uint8_t*, int)> receive_callback;
typedef std::function<void ()> connect_callback;
typedef std::function<void ()> disconnect_callback;

short listen_port = 8080;
std::string listen_address = "0.0.0.0";

short connect_port = 80;
std::string connect_address = "127.0.0.10";

bool report_ip = false;
bool report_port = false;
bool report_time = false;
int report_width = 8;
int report_repeats = 3;
bool verbose = false;

void set_nonblocking(SOCKET fd)
{
    if (fd < 0) return;

#ifdef _WIN32
   unsigned long mode = 1;
   ioctlsocket(fd, FIONBIO, &mode);
#else
   int flags = fcntl(fd, F_GETFL, 0);
   if (flags == -1) return;

   flags = (flags | O_NONBLOCK);
   fcntl(fd, F_SETFL, flags);
#endif
}

void cleanup_socket(SOCKET fd)
{
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

class connection
{
protected:
    std::deque<uint8_t> write_queue_; //TODO: circular buffer?
    receive_callback on_recv_;
    disconnect_callback on_disconnect_;
    SOCKET connection_;

    connection() = delete;
    connection(const connection&) = delete;
    connection(connection &&) = delete;
    void operator=(const connection&) = delete;
    void operator=(connection&&) = delete;

public:
    connection(
        receive_callback on_recv,
        disconnect_callback on_disconnect
    ) :
        on_recv_(on_recv),
        on_disconnect_(on_disconnect)
    {
        connection_ = INVALID_SOCKET;
    }


    ~connection() 
    {
        if (connection_ != INVALID_SOCKET)
        {
            cleanup_socket(connection_);
        }
    }

    sockaddr_in get_near_end()
    {
        socklen_t length = sizeof(sockaddr_in);
        sockaddr_in near_end = {0};
        getsockname(connection_, (sockaddr*) &near_end, &length);
        return near_end;
    }

    sockaddr_in get_far_end()
    {
        socklen_t length = sizeof(sockaddr_in);
        sockaddr_in far_end = {0};
        getpeername(connection_, (sockaddr*) &far_end, &length);
        return far_end;
    }

    void cleanup()
    {
        if (connection_ != INVALID_SOCKET)
            cleanup_socket(connection_);
        connection_ = INVALID_SOCKET;
        write_queue_.clear();
    }

    void send(const uint8_t* data, int length)
    {
        for(int i = 0; i < length; i++)
        {
            write_queue_.push_back(data[i]);
        }
    }

    void disconnect()
    {
        cleanup();
    }

    void prepare_for_poll(std::vector<pollfd>& events)
    {
        if (connection_ == INVALID_SOCKET)
            return;

        pollfd selector;
        selector.fd = connection_;
        selector.events = POLLIN | POLLERR | POLLHUP;
#ifndef _WIN32
        selector.events |= POLLRDHUP
#endif
        selector.revents = 0;
        
        if (!write_queue_.empty())
            selector.events |= POLLOUT;
        
        events.push_back(selector);
    }

    void poll()
    {
        if (connection_ == INVALID_SOCKET)
            return;

        //todo: receive always ready?
        uint8_t io_buffer[256];
        int nr = recv(connection_, (char*)io_buffer, sizeof(io_buffer), 0);
        if (nr > 0)
        {
            on_recv_(io_buffer, nr);
        }
        else if (nr == 0)
        {
            //TODO: handle far end calling shutdown, graceful one way close
            cleanup();
            on_disconnect_();
            return;
        }
#ifdef _WIN32
        else if (WSAGetLastError() != WSAEWOULDBLOCK)
#else
        else if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
#endif
        {
            cleanup();
            on_disconnect_();
            return;
        }

        if (!write_queue_.empty()) {
            int n = 0;
            for (n = 0; n < write_queue_.size() && n < sizeof(io_buffer); n++)
            {
                io_buffer[n] = write_queue_[n];
            }
            int ns = ::send(connection_, (char*) io_buffer, n, 0);
            if (ns > 0)
            {
                write_queue_.erase(
                    write_queue_.begin(),
                    write_queue_.begin() + ns);
            }
#ifdef _WIN32
            else if (WSAGetLastError() != WSAEWOULDBLOCK)
#else
            else if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
#endif
            {
                cleanup();
                on_disconnect_();
                return;
            }
        }
    }
};

class server_connection :
    public connection
{
protected:
    connect_callback on_accept_;
public:
    server_connection (
        connect_callback on_accept,
        receive_callback on_recv,
        disconnect_callback on_disconnect
    ) :
        connection(on_recv, on_disconnect),
        on_accept_(on_accept)
    {
    }

    void poll(SOCKET l)
    {
        if (connection_ == INVALID_SOCKET)
        {
            sockaddr_in far_end = {0};
            socklen_t addr_len = sizeof(sockaddr_in);
            connection_ = accept(l, (sockaddr*) &far_end, &addr_len);

            if (connection_ != INVALID_SOCKET)
            {
                set_nonblocking(connection_);

                on_accept_();
            }
        }

        if (connection_ != INVALID_SOCKET)
        {
            connection::poll();
        }
    }
};

class client_connection : public connection
{
    bool connecting_;
    bool enabled_;
    connect_callback on_connect_;

public:
    client_connection(
        connect_callback on_connect,
        receive_callback on_recv,
        disconnect_callback on_disconnect
    ) :
        connection(on_recv, on_disconnect),
        connecting_(false),
        enabled_ (false),
        on_connect_(on_connect)
    {
    }
    
    void disconnect()
    {
        cleanup();
        enabled_ = false;
        connecting_ = false;
    }

    void connect()
    {
        enabled_ = true;
        write_queue_.clear();
    }

    void prepare_for_poll(std::vector<pollfd>& events)
    {
        if (connection_ == INVALID_SOCKET)
            return;

        pollfd selector;
        selector.fd = connection_;
        selector.events = POLLIN | POLLERR | POLLHUP;
#ifndef _WIN32
        selector.events |= POLLRDHUP
#endif 
        selector.revents = 0;
        
        if (!write_queue_.empty() || connecting_)
            selector.events |= POLLOUT;
        
        events.push_back(selector);
    }

    void poll(const sockaddr_in& far_end)
    {
        if (enabled_)
        {
            if (connection_ == INVALID_SOCKET)
            {
                connecting_ = true;
                connection_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                set_nonblocking(connection_);
            }

            if (connecting_)
            {
                int r = ::connect(connection_, (sockaddr*) &far_end, sizeof(sockaddr_in));
#ifdef _WIN32
                if (WSAGetLastError() == WSAEISCONN)
#else
                if ((r == 0) || (errno == EISCONN))
#endif
                {
                    connecting_ = false;
                    on_connect_();
                }
#ifdef _WIN32
                else if (WSAGetLastError() == WSAEWOULDBLOCK)
#else
                else if ((errno == EAGAIN) || (errno == EALREADY) || (errno == EINPROGRESS) || (errno == EWOULDBLOCK))
#endif
                {
                    return;
                }
                else
                {
                    cleanup_socket(connection_);
                    connection_ = INVALID_SOCKET;
                }
            }

            if (!connecting_)
            {
                connection::poll();
            }
        }
    }
};

std::string get_log_prefix(const sockaddr_in& addr, bool dir_in)
{
    std::stringstream ss;
    if (report_time)
    {
        auto time = std::time(nullptr);
        ss << std::put_time(std::localtime(&time), "%F %T%z");
    }
    ss << (dir_in ? ">" : "<");
    if (report_ip)
        ss << inet_ntoa(addr.sin_addr);

    if (report_ip && report_port)
        ss << ":";

    if (report_port)
        ss << std::dec << ntohs(addr.sin_port);

    return ss.str();
}

void log_data(
    const std::string& line_pref,
    int word_width,
    int repeats,
    const uint8_t* data,
    int length
)
{
    int l = 0;
    for(;;)
    {
        int line_offs = (l * word_width * repeats);
        if (line_offs >= length)
            break;

        std::cout << line_pref;

        for(int r = 0; r < repeats; r++)
        {
            int repeat_offs = line_offs + r * word_width;
            if (repeat_offs >= length)
                break;

            std::cout << " ";

            for (int n = 0; n < word_width; n ++)
            {
                int word_offs = repeat_offs + n;
                if (word_offs >= length)
                    std::cout << "  ";
                else
                    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int) data[word_offs];
            }

            std::cout << " ";

            for (int n = 0; n < word_width; n ++)
            {
                int word_offs = repeat_offs + n;
                if (word_offs >= length)
                    break;
                char c = (char) data[word_offs];
                if (c >= 0x20 && c < 0xFF) {
                    std::cout << c;
                }
                else
                {
                    std::cout << ".";
                }
                
            }
        }
        std::cout << std::endl;
        l ++;
    }
}

class connector
{
    sockaddr_in server_near_;
    sockaddr_in server_far_;
    sockaddr_in client_near_;
    sockaddr_in client_far_;
    std::shared_ptr<server_connection> server_;
    std::shared_ptr<client_connection> client_;
    SOCKET listener_;

public:

    void on_client_recv(const uint8_t* data, int length)
    {
        log_data(get_log_prefix(client_far_, false), report_width, report_repeats, data, length);
        server_->send(data, length);
    }

    void on_client_connect()
    {
        client_near_ = client_->get_near_end();
        client_far_ = client_->get_far_end();
        std::cout << get_log_prefix(client_far_, true) << " connected successfully" << std::endl;
    }

    void on_client_disconnect()
    {
        client_->disconnect(); //to stop it from reconnecting
        server_->disconnect();

        std::cout << get_log_prefix(client_far_, false) << " disconnect" << std::endl;
        std::cout << get_log_prefix(server_far_, false) << " disconnect" << std::endl;
    }

    void on_server_recv(const uint8_t* data, int length)
    {
        log_data(get_log_prefix(server_far_, true), report_width, report_repeats, data, length);
        client_->send(data, length);
    }

    void on_server_accept()
    {
        server_near_ = server_->get_near_end();
        server_far_ = server_->get_far_end();
        std::cout << get_log_prefix(server_far_, true) << " accepted connection" << std::endl;
        std::cout << get_log_prefix(client_far_, true) << " connecting ..." << std::endl;

        client_->connect();
    }

    void on_server_disconnect()
    {
        std::cout << get_log_prefix(server_far_, true) << " disconnect" << std::endl;
        std::cout << get_log_prefix(client_far_, true) << " disconnect" << std::endl;

        server_->disconnect();
        client_->disconnect();
    }

    connector() = delete;

    connector(connector&& c) = delete;

    connector(SOCKET listener, const sockaddr_in& connect_addr):
        server_near_({0}),
        server_far_({0}),
        client_near_({0}),
        client_far_({0}),
        server_(std::make_shared<server_connection>(
            [this]()
            {
                on_server_accept();
            },
            [this](const uint8_t* data, int length)
            {
                on_server_recv(data, length);
            },
            [this]()
            {
                on_server_disconnect();
            })),
        client_(std::make_shared<client_connection>(
            [this]()
            {
                on_client_connect();
            },
            [this](const uint8_t* data, int length)
            {
                on_client_recv(data, length);
            },
            [this]()
            {
                on_client_disconnect();
            })),
        listener_(INVALID_SOCKET)
    {
        client_far_= connect_addr;
        listener_ = listener;
    }

    void prepare_for_poll(std::vector<pollfd>& descriptors)
    {
        server_->prepare_for_poll(descriptors);
        client_->prepare_for_poll(descriptors);
    }

    void poll()
    {
        server_->poll(listener_);
        client_->poll(client_far_);
    }
};

void run_listener(SOCKET fd, const sockaddr_in& connect_addr)
{
    std::vector<std::shared_ptr<connector>> connections;
    connections.push_back(std::make_shared<connector>(fd, connect_addr));

    std::vector<pollfd> descriptors;
    for (;;)
    {
        descriptors.clear();
        pollfd selector;
        selector.fd = fd;
        selector.events = POLLIN | POLLERR;
        selector.revents = 0;
        descriptors.push_back(selector);

        for(auto& connection : connections)
        {
            connection->prepare_for_poll(descriptors);
        }
#ifdef _WIN32
        WSAPoll(descriptors.data(), descriptors.size(), 1000);
#else
        poll(descriptors.data(), descriptors.size(), 1000);
#endif
        for(auto& connection : connections)
        {
            connection->poll();
        }
    }
}

bool parse_args(int argc, char** argv)
{
    bool help = false;

    std::string argument_to_parse = "";
    for(int a = 1; a < argc; a++)
    {
        std::string arg = argv[a];
        if (argument_to_parse == "")
        {
            if ((arg == "-p") || (arg == "--listen-port"))
            {
                argument_to_parse = "listen-port";
            }
            else if ((arg == "-d") || (arg == "--destination-port"))
            {
                argument_to_parse = "destination-port";
            }
            else if ((arg == "-a") || (arg == "--destination-addr"))
            {
                argument_to_parse = "destination-addr";
            }
            else if ((arg == "-l") || (arg == "--listen-addr"))
            {
                argument_to_parse = "listen-addr";
            }
            else if ((arg == "-w") || (arg == "--report-width"))
            {
                argument_to_parse = "report-width";
            }
            else if ((arg == "-r") || (arg == "--report-repeats"))
            {
                argument_to_parse = "report-repeats";
            }
            else if ((arg == "-t") || (arg == "--report-time"))
            {
                report_time = true;
            }
            else if ((arg == "-i") || (arg == "--report-ip"))
            {
                report_ip = true;
            }
            else if ((arg == "-n") || (arg == "--report-port"))
            {
                report_port = true;
            }
            else if ((arg == "-v") || (arg == "--verbose"))
            {
                verbose = true;
                report_ip = true;
                report_port = true;
                report_time = true;
            }
            else if ((arg == "-?") || (arg == "--help"))
            {
                help = true;
            }
            else
            {
                break;
                help = true;
            }
        }
        else
        {
            if (argument_to_parse == "listen-port")
            {
                listen_port = atoi(arg.c_str());
            }
            else if (argument_to_parse == "destination-port")
            {
                connect_port = atoi(arg.c_str());
            }
            else if (argument_to_parse == "destination-addr")
            {
                connect_address = arg; //todo: validation?
            }
            else if (argument_to_parse == "listen-addr")
            {
                listen_address = arg; //todo: validation?
            }
            else if (argument_to_parse == "report-width")
            {
                report_width = atoi(arg.c_str());
            }
            else if (argument_to_parse == "report-repeats")
            {
                report_repeats = atoi(arg.c_str());
            }
            else
            {
                help = true;
                std::cerr << "internal error" << std::endl;
            }
            argument_to_parse = "";
        }
    }

    if (argument_to_parse != "")
    {
        std::cerr << "expected argument for " << argument_to_parse << std::endl;
    }

    if (help)
    {
        std::cerr << "usage: " << std::endl;
        std::cerr << "\tnosey [options]" << std::endl << std::endl;
        std::cerr << "option listing:" << std::endl;
        std::cerr << "\t-l/--listen-addr, " << listen_address << std::endl;
        std::cerr << "\t-p/--listen-port, " << listen_port << std::endl;
        std::cerr << "\t-a/--destination-addr, " << connect_address << std::endl;
        std::cerr << "\t-d/--destination-port, " << connect_port << std::endl;
        std::cerr << "\t-w/--report-width, " << report_width << std::endl;
        std::cerr << "\t-r/--report-repeats, " << report_repeats << std::endl;
        std::cerr << "\t-t/--report-time" << std::endl;
        std::cerr << "\t-i/--report-ip" << std::endl;
        std::cerr << "\t-n/--report-port" << std::endl;
        std::cerr << "\t-v/--verbose" << std::endl;
        std::cerr << "\t-?/--help" << std::endl;
        std::cerr << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char** argv) 
{
    if (!parse_args(argc, argv))
    {
        return -1;
    }

#ifdef _WIN32
    WORD wVersionRequested;
    WSADATA wsaData;
    wVersionRequested = MAKEWORD(2, 2);

    int err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        printf("WSAStartup failed with error: %d\n", err);
        return 1;
    }
#endif

    sockaddr_in listen_addr = { 0 }, connect_addr = { 0 };
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(listen_port);
    connect_addr.sin_family = AF_INET;
    connect_addr.sin_port = htons(connect_port);

#ifdef _WIN32
    int r = InetPtonA(AF_INET, listen_address.c_str(), &listen_addr.sin_addr);
#else
    int r = inet_aton(listen_address.c_str(), &listen_addr.sin_addr);
#endif

    if (r == 0)
        std::cerr << "invalid address: " << listen_address << std::endl;

#ifdef _WIN32
    r = InetPtonA(AF_INET, connect_address.c_str(), &connect_addr.sin_addr);
#else
    r = inet_aton(connect_address.c_str(), &connect_addr.sin_addr);
#endif
    if (r == 0)
        std::cerr << "invalid address: " << connect_address << std::endl;

    if (verbose)
        std::cout << "configured far end: " << inet_ntoa(connect_addr.sin_addr) << ":" << ntohs(connect_addr.sin_port) << std::endl;

    std::cout << get_log_prefix(listen_addr, true) << " listening" << std::endl;
    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    set_nonblocking(fd);

    r = bind(fd, (sockaddr*) &listen_addr, sizeof(sockaddr_in));
    if (r == 0)
    {
        if(listen(fd, 10) == 0)
        {
            run_listener(fd, connect_addr);
        }
        else
        {
            std::cout << "failure to listen" << std::endl;
        }
    }
    else
    {
        std::cerr << "could not bind to port" << std::endl;
    }

    cleanup_socket(fd);
    WSACleanup();
    return 0;
}