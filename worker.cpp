#include <iostream>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <thread>
#include <cstdlib>
#include <ctime>

#define MIN_TCP_PORT 9002
#define MAX_TCP_PORT 9008

// Вычисляет интеграл f(x) = x^2
double calculateIntegral(double range_start, double range_end, double step_size) {
    double total = 0.0;
    for (double x = range_start; x < range_end; x += step_size) {
        total += x * x * step_size;
    }
    return total;
}

// Использует UDP-сокет для обработки широковещательных запросов мастера
void handleDiscoveryRequests(int udp_port, int tcp_port) {
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        std::cerr << "Failed to create UDP socket" << std::endl;
        return;
    }

    int reuse = 1;
    if (setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR" << std::endl;
        close(udp_socket);
        return;
    }

    struct sockaddr_in udp_address;
    udp_address.sin_family = AF_INET;
    udp_address.sin_addr.s_addr = INADDR_ANY;
    udp_address.sin_port = htons(udp_port);

    if (bind(udp_socket, reinterpret_cast<struct sockaddr*>(&udp_address), sizeof(udp_address)) < 0) {
        std::cerr << "Failed to bind UDP socket" << std::endl;
        close(udp_socket);
        return;
    }

    char buffer[256];
    while (true) {
        struct sockaddr_in sender_address;
        socklen_t address_length = sizeof(sender_address);
        std::cout << "Waiting for discovery messages..." << std::endl;
        ssize_t bytes_received = recvfrom(udp_socket, buffer, sizeof(buffer) - 1, 0,
                                          reinterpret_cast<struct sockaddr*>(&sender_address), &address_length);
        // Ожидает сообщения от мастера.
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::cout << "Received message: " << buffer << std::endl;
            if (strcmp(buffer, "DISCOVER_SERVERS") == 0) { // Ожидание сообщения от мастера
                char response[64];
                // Отвечает сообщением SERVER_READY:<TCP_PORT>
                // TCP_PORT — порт, на котором воркер ожидает TCP-соединения.
                snprintf(response, sizeof(response), "SERVER_READY:%d", tcp_port);
                sendto(udp_socket, response, strlen(response), 0,
                       reinterpret_cast<struct sockaddr*>(&sender_address), address_length);
                std::cout << "Sent response with TCP port: " << tcp_port << std::endl;
            }
        }
    }
}

// Использует TCP-сокет для приема задач от мастера
void handleTaskRequests(int tcp_port) {
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0) {
        std::cerr << "Failed to create TCP socket" << std::endl;
        return;
    }

    int reuse = 1;
    if (setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR" << std::endl;
        close(tcp_socket);
        return;
    }

    struct sockaddr_in tcp_address;
    tcp_address.sin_family = AF_INET;
    tcp_address.sin_addr.s_addr = INADDR_ANY;
    tcp_address.sin_port = htons(tcp_port);

    if (bind(tcp_socket, reinterpret_cast<struct sockaddr*>(&tcp_address), sizeof(tcp_address)) < 0) {
        std::cerr << "Failed to bind TCP socket" << std::endl;
        close(tcp_socket);
        return;
    }

    if (listen(tcp_socket, 5) < 0) {
        std::cerr << "Failed to listen on TCP socket" << std::endl;
        close(tcp_socket);
        return;
    }

    while (true) {
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);
        // Ожидает подключения от мастера через accept
        int client_socket = accept(tcp_socket, reinterpret_cast<struct sockaddr*>(&client_address), &client_address_length);
        if (client_socket < 0) {
            std::cerr << "Failed to accept connection" << std::endl;
            continue;
        }
        double task_data[3];
        // Получает массив из трех чисел [range_start, range_end, step_size].
        ssize_t bytes_received = recv(client_socket, &task_data, sizeof(task_data), 0);
        if (bytes_received == sizeof(task_data)) {
            double range_start = task_data[0];
            double range_end = task_data[1];
            double step_size = task_data[2];
            std::cout << "Received task: start=" << range_start
                      << ", end=" << range_end
                      << ", step=" << step_size << std::endl;
            // Вызывает calculateIntegral для выполнения задачи.
            double result = calculateIntegral(range_start, range_end, step_size);
            std::cout << "Computed result: " << result << std::endl;
            // Отправляет результат обратно мастеру через тот же сокет.
            send(client_socket, &result, sizeof(result), 0);
            std::cout << "Sent result to master" << std::endl;
        }
        close(client_socket);
    }
}

// Выбор случайного TCP-порта
// Генерирует случайный порт из диапазона [MIN_TCP_PORT, MAX_TCP_PORT].
// Обеспечивает, чтобы несколько воркеров на одной машине не конфликтовали из-за одинаковых портов.
int chooseRandomPort() {
    std::srand(std::time(nullptr));
    return MIN_TCP_PORT + std::rand() % (MAX_TCP_PORT - MIN_TCP_PORT + 1);
}

// Запуск воркера
void startWorker(int discovery_udp_port) {
    // Выбирает случайный TCP-порт.
    int tcp_port = chooseRandomPort();
    std::cout << "Worker started on TCP port " << tcp_port << " and UDP port " << discovery_udp_port << std::endl;
    // Запускает два потока:
    // UDP-поток для обработки запросов на обнаружение (handleDiscoveryRequests).
    // TCP-поток для обработки задач (handleTaskRequests).
    std::thread discovery_thread(handleDiscoveryRequests, discovery_udp_port, tcp_port);
    std::thread task_thread(handleTaskRequests, tcp_port);
    discovery_thread.join();
    task_thread.join();
}

// Указывает фиксированный порт 9001 для UDP-обнаружения и вызывает startWorker
int main() {
    int udp_discovery_port = 9001;
    startWorker(udp_discovery_port);
    return 0;
}