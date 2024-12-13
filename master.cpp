#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <map>
#include <algorithm>

// порт для обнаружения воркеров
const int DISCOVERY_PORT = 9001;
const int TIMEOUT_SECONDS = 10;
const char *BROADCAST_MESSAGE = "DISCOVER_SERVERS";
// секунды ожидания мастера ответа от воркера
const int WORKER_TIMEOUT = 10;

// содержит адрес и время последнего ответа воркера
struct WorkerInfo {
    sockaddr_in address;
    time_t last_response_time;
};

// Мьютекс для защиты доступа к воркерам в многопоточном окружении
std::mutex worker_mutex;
// Контейнер, где ключ - ID воркера, а значение - структура WorkerInfo
std::map<int, WorkerInfo> workers;

// ищет воркеров через UDP и обрабатывает их ответы
void discover_workers() {
    // UDP сокет
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        perror("UDP socket creation failed");
        exit(EXIT_FAILURE);
    }
    // Включается режим широковещательной передачи с помощью SO_BROADCAST
    int broadcast_enable = 1;
    if (setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        perror("Setting SO_BROADCAST failed");
        close(udp_socket);
        exit(EXIT_FAILURE);
    }

    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(DISCOVERY_PORT);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;

    if (sendto(udp_socket, BROADCAST_MESSAGE, strlen(BROADCAST_MESSAGE), 0,
               reinterpret_cast<sockaddr *>(&broadcast_addr), sizeof(broadcast_addr)) < 0) {
        perror("Broadcast failed");
        close(udp_socket);
        exit(EXIT_FAILURE);
    }
    // Передается сообщение DISCOVER_SERVERS по порту DISCOVERY_PORT в широковещательной сети
    std::cout << "Broadcast message sent: " << BROADCAST_MESSAGE << "\n";

    // Устанавливается таймаут на получение ответов.
    timeval timeout{TIMEOUT_SECONDS, 0};
    setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char buffer[256];
    sockaddr_in response_addr;
    socklen_t addr_len = sizeof(response_addr);

    while (true) {
        int bytes_received = recvfrom(udp_socket, buffer, sizeof(buffer) - 1, 0,
                                      reinterpret_cast<sockaddr *>(&response_addr), &addr_len);
        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // Timeout or success
            }
            perror("Error receiving response");
            continue;
        }

        buffer[bytes_received] = '\0';
        std::cout << "Received response: " << buffer << " from "
                  << inet_ntoa(response_addr.sin_addr) << ":" << ntohs(response_addr.sin_port) << "\n";

        // Получаются ответы от воркеров (например, SERVER_READY:9004).
        if (strncmp(buffer, "SERVER_READY:", 13) == 0) {
            int worker_port = atoi(buffer + 13);
            response_addr.sin_port = htons(worker_port);

            std::lock_guard<std::mutex> lock(worker_mutex);
            bool exists = false;

            // Проверяется уникальность воркера.
            for (const auto &[id, info] : workers) {
                if (info.address.sin_addr.s_addr == response_addr.sin_addr.s_addr &&
                    info.address.sin_port == response_addr.sin_port) {
                    exists = true;
                    break;
                }
            }

            // Если воркер новый, он добавляется в workers.
            if (!exists) {
                int new_id = workers.size() + 1;
                workers[new_id] = {response_addr, time(nullptr)};
                std::cout << "Discovered worker: " << inet_ntoa(response_addr.sin_addr) << ":"
                          << worker_port << "\n";
            }
        }
    }

    close(udp_socket);
}

// Отправляет задание воркеру через TCP и получает результат
// Выводит 0 как результат в случае ошибки
double send_task_to_worker(const WorkerInfo &worker, double start, double end, double step) {
    // Создается TCP-сокет.
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0) {
        perror("TCP socket creation failed");
        return 0.0;
    }
    // Подключение к воркеру.
    if (connect(tcp_socket, reinterpret_cast<const sockaddr *>(&worker.address), sizeof(worker.address)) < 0) {
        perror("Connection to worker failed");
        close(tcp_socket);
        return 0.0;
    }
    // Отправляется массив [start, end, step].
    double task[3] = {start, end, step};
    if (send(tcp_socket, task, sizeof(task), 0) < 0) {
        perror("Failed to send task to worker");
        close(tcp_socket);
        return 0.0;
    }
    // Получение результата от воркера.
    double result;
    if (recv(tcp_socket, &result, sizeof(result), 0) < 0) {
        perror("Failed to receive result from worker");
        close(tcp_socket);
        return 0.0;
    }
    close(tcp_socket);
    std::lock_guard<std::mutex> lock(worker_mutex);
    for (auto &[id, info] : workers) {
        if (info.address.sin_addr.s_addr == worker.address.sin_addr.s_addr &&
            info.address.sin_port == worker.address.sin_port) {
            // В случае успеха обновляется last_response_time воркера.
            info.last_response_time = time(nullptr);
        }
    }

    return result;
}

// Проверяет доступность воркеров и удаляет их из списка если они не отвечают
void monitor_workers() {
    while (true) {
        // Запускается в отдельном потоке.
        // Каждую секунду проверяет всех воркеров.
        std::this_thread::sleep_for(std::chrono::seconds(1));
        time_t current_time = time(nullptr);
        std::lock_guard<std::mutex> lock(worker_mutex);
        for (auto it = workers.begin(); it != workers.end();) {
            // Если воркер не отвечает более WORKER_TIMEOUT, он удаляется.
            if (current_time - it->second.last_response_time > WORKER_TIMEOUT) {
                std::cout << "Worker timeout: " << inet_ntoa(it->second.address.sin_addr)
                          << ":" << ntohs(it->second.address.sin_port) << "\n";
                it = workers.erase(it);
            } else {
                ++it;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: ./master <start> <end> <step>\n";
        return EXIT_FAILURE;
    }

    double start = atof(argv[1]);
    double end = atof(argv[2]);
    double step = atof(argv[3]);

    // Мониторинг воркеров выполняется в отдельном потоке, чтобы не блокировать выполнение основной программы
    std::thread monitor_thread(monitor_workers);
    // Вызывается discover_workers() для поиска воркеров.
    discover_workers();
    // Проверка на доступных воркеров
    // Если воркеры не найдены, программа завершает работу.
    if (workers.empty()) {
        std::cerr << "No workers discovered. Exiting.\n";
        return EXIT_FAILURE;
    }

    double total_result = 0.0;
    // Задание разбивается на сегменты длиной step
    for (double x = start; x < end; x += step) {
        double segment_end = std::min(x + step, end);

        bool task_assigned = false;
        while (!task_assigned) {
            WorkerInfo worker;
            {
                std::lock_guard<std::mutex> lock(worker_mutex);
                if (workers.empty()) {
                    std::cerr << "No workers available to process task.\n";
                    return EXIT_FAILURE;
                }
                // Используем первый доступный воркер
                worker = workers.begin()->second;
            }
            // Каждый сегмент отправляется воркеру.
            // Если воркер недоступен, задание переназначается.
            double result = send_task_to_worker(worker, x, segment_end, step);
            if (result != 0.0) {
                total_result += result;
                task_assigned = true;
            } else {
                std::cerr << "Task reassignment needed.\n";
                {
                    std::lock_guard<std::mutex> lock(worker_mutex);
                    for (auto it = workers.begin(); it != workers.end(); ++it) {
                        if (it->second.address.sin_addr.s_addr == worker.address.sin_addr.s_addr &&
                            it->second.address.sin_port == worker.address.sin_port) {
                            workers.erase(it);
                            break;
                        }
                    }
                }
            }
        }
    }
    // Конечный результат
    std::cout << "Final integral result: " << total_result << "\n";

    monitor_thread.detach();
    return 0;
}
