# Задание 1

## Условие:

Напишите систему, вычисляющую интеграл от некоторой функции.

Мастер (клиент) находит рабочие узлы (сервера) через IP broadcast(через UDP) — рассылает стартовое сообщение по всем адресам подсети, на которое рабочие узлы, слушающие на своих TCP портах чтобы принять запрос от мастера уже после того, как он их нашёл и знает, куда стучаться отвечают. Затем каждому рабочему узлу даётся отрезок, он вычисляет на нём интеграл и отправляет ответ мастеру. Мастер складывает ответы серверов и получает итоговый результат.

## Требования:

* если после раздачи заданий сервера становятся недоступны (выключаются / происходит разрыв сети), но хотя бы один сервер доступен, программа это детектирует, раздаёт работу доступным серверам вместо отключившихся и даёт верный ответ
* если недоступный сервер снова появляется в сети и пытается послать ответ, это не приводит к ошибке, в частности, результат по соотв. отрезку не будет учтён дважды
* если недоступный сервер появился в сети, мастер должен уметь присылать на него новые задачи (например, отключился какой-то ещё сервер)

## Литература:
Стивенс У. Р. "Разработка сетевых приложений", гл. 2, 3, 4, 5, 7

# Решение

## Описание решения:

### master.cpp:

#### Этот файл реализует мастера, который:
1. Обнаруживает доступных воркеров.
2. Распределяет задания между ними.
3. Обрабатывает отказ воркеров и перезапускает задания.

#### Основные структуры данных
1. WorkerInfo:
    * Содержит sockaddr_in address — адрес воркера.
    * Содержит time_t last_response_time — время последнего успешного ответа от воркера.
2. std::map<int, WorkerInfo> workers:
    * Ассоциативный контейнер, где ключ — ID воркера, а значение — структура WorkerInfo.
3. std::mutex worker_mutex:
    * Мьютекс для защиты доступа к workers в многопоточном окружении.

#### Функция discover_workers()
Цель: обнаружение всех воркеров в сети.
* Процесс:
  1. Создается UDP-сокет.
  2. Включается режим широковещательной передачи с помощью SO_BROADCAST.
  3. Передается сообщение DISCOVER_SERVERS по порту DISCOVERY_PORT в широковещательной сети.
  4. Устанавливается таймаут на получение ответов.
  5. Получаются ответы от воркеров (например, SERVER_READY:9004).
  6. Проверяется уникальность воркера. Если воркер новый, он добавляется в workers.

#### Функция send_task_to_worker()
Цель: отправка задания на конкретный воркер и получение результата.
* Аргументы:
  - worker: информация о воркере (WorkerInfo).
  - start, end, step: параметры задания (диапазон интегрирования).
* Процесс:
  1. Создается TCP-сокет.
  2. Подключение к воркеру.
  3. Отправляется массив [start, end, step].
  4. Получение результата от воркера.
  5. В случае успеха обновляется last_response_time воркера.

#### Функция monitor_workers()
Цель: удалить из списка воркеров тех, кто долго не отвечает.
* Процесс:
  1. Запускается в отдельном потоке.
  2. Каждую секунду проверяет всех воркеров.
  3. Если воркер не отвечает более WORKER_TIMEOUT, он удаляется.

#### Основная функция main()
* Аргументы:
  - Диапазон интегрирования (start, end) и шаг (step).
* Процесс:
  1. Запускается поток мониторинга воркеров.
  2. Вызывается discover_workers() для поиска воркеров.
  3. Если воркеры не найдены, программа завершает работу.
  4. Задание разбивается на сегменты длиной step.
  5. Каждый сегмент отправляется воркеру. Если воркер недоступен, задание переназначается.

### worker.cpp:

#### Реализация интеграла:
* Функция calculateIntegral:
  * Выполняет численное интегрирование функции f(x) = x^2 на заданном интервале [range_start, range_end] с шагом step_size.
  * Использует метод прямоугольников для приближенного вычисления интеграла.

#### Обработка UDP-запросов на обнаружение
Функция handleDiscoveryRequests:
* Использует UDP-сокет для обработки широковещательных запросов мастера.
* Основной цикл:
  * Ожидает сообщения от мастера.
  * Если получает сообщение DISCOVER_SERVERS, отвечает сообщением SERVER_READY:<TCP_PORT>, где TCP_PORT — порт, на котором воркер ожидает TCP-соединения.
* Это позволяет мастеру находить доступные воркеры.

#### Обработка TCP-запросов на выполнение задач
Функция handleTaskRequests:
* Использует TCP-сокет для приема задач от мастера:
  1. Ожидает подключения от мастера через accept.
  2. Получает массив из трех чисел [range_start, range_end, step_size].
  3. Вызывает calculateIntegral для выполнения задачи.
  4. Отправляет результат обратно мастеру через тот же сокет.

#### Выбор случайного TCP-порта
Функция chooseRandomPort:
* Генерирует случайный порт из диапазона [MIN_TCP_PORT, MAX_TCP_PORT].
* Обеспечивает, чтобы несколько воркеров на одной машине не конфликтовали из-за одинаковых портов.

#### Запуск воркера
Функция startWorker:
* Выбирает случайный TCP-порт.
* Запускает два потока:
  - UDP-поток для обработки запросов на обнаружение (handleDiscoveryRequests).
  - TCP-поток для обработки задач (handleTaskRequests).

### Основные моменты и логика взаимодействия

#### 1. Мастер-воркер взаимодействие:
* Мастер отправляет UDP-запросы с содержимым DISCOVER_SERVERS на порт 9001.
* Воркеры, запущенные на этой машине, отвечают сообщением SERVER_READY:<TCP_PORT>.
* Мастер использует полученный TCP-порт для отправки задачи.

#### 2. Воркеры работают параллельно:
* Потоки для UDP и TCP обработки позволяют воркеру одновременно реагировать на запросы обнаружения и выполнять задачи.

#### 3. Устойчивость:
* Использование SO_REUSEADDR предотвращает проблемы с повторным использованием портов.

## Как запускать:

```cmd
g++ -o master master.cpp -pthread
g++ -o worker worker.cpp -pthread
```

После компиляции открываем ещё консоли (терминалы). Среди них 1 будет для работы с мастером, в остальных мы просто запустим воркеры.
Запуск воркера:
```cmd
./worker
```
После чего можем запустить мастера:
```cmd
./master <start> <end> <step>
```
